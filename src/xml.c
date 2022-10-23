/**
 * Copyright (c) 2012 ooxi/xml.c
 *     https://github.com/ooxi/xml.c
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from the
 * use of this software.
 * 
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 *  1. The origin of this software must not be misrepresented; you must not
 *     claim that you wrote the original software. If you use this software in a
 *     product, an acknowledgment in the product documentation would be
 *     appreciated but is not required.
 * 
 *  2. Altered source versions must be plainly marked as such, and must not be
 *     misrepresented as being the original software.
 *
 *  3. This notice may not be removed or altered from any source distribution.
 */
#ifdef XML_PARSER_VERBOSE
#include <alloca.h>
#endif

#include <ctype.h>

#ifndef __MACH__
#include <malloc.h>
#endif

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "xml.h"





/* 
 * public domain strtok_r() by Charlie Gordon
 *
 *   from comp.lang.c  9/14/2007
 *
 *      http://groups.google.com/group/comp.lang.c/msg/2ab1ecbb86646684
 *
 *     (Declaration that it's public domain):
 *      http://groups.google.com/group/comp.lang.c/msg/7c7b39328fefab9c
 */
static char* xml_strtok_r(char *str, const char *delim, char **nextp) {
	char *ret;

	if (str == NULL) {
		str = *nextp;
	}

	str += strspn(str, delim);

	if (*str == '\0') {
		return NULL;
	}

	ret = str;

	str += strcspn(str, delim);

	if (*str) {
		*str++ = '\0';
	}

	*nextp = str;

	return ret;
}






/**
 * [OPAQUE API]
 *
 * UTF-8 text string. Buffer normally points into a larger buffer which is read-only.
 * The own_buffer flag indicates the string is in a separate buffer
 * which must be freed.
 */
struct xml_string {
	uint8_t *buffer;
	size_t length;
	int own_buffer;
};

/**
 * [OPAQUE API]
 *
 * An xml_attribute contains a name and its text content.
 */
struct xml_attribute {
	struct xml_string* name;
	struct xml_string* content;
};

/**
 * [OPAQUE API]
 *
 * An xml_node will always contain a tag name, a 0-terminated list of attributes
 * and a 0-terminated list of children. Moreover it may contain text content.
 */
struct xml_node {
	struct xml_string* name;
	struct xml_string* content;
	struct xml_attribute** attributes;
	struct xml_node** children;
};

/**
 * [OPAQUE API]
 *
 * An xml_document simply contains the root node, the underlying buffer,
 * and the offset to jump over any <?xml.. header found in a file.
 */
struct xml_document {
	struct {
		uint8_t* buffer;
		size_t length;
	} buffer;

	struct xml_node* root;
	size_t initial_offset;
};





/**
 * [PRIVATE]
 *
 * Parser context
 */
struct xml_parser {
	uint8_t* buffer;
	size_t position;
	size_t length;
};

/**
 * [PRIVATE]
 *
 * Character offsets
 */
enum xml_parser_offset {
	NO_CHARACTER = -1,
	CURRENT_CHARACTER = 0,
	NEXT_CHARACTER = 1,
};





/**
 * [PRIVATE]
 *
 * @return Number of attributes in 0-terminated array
 */
static size_t get_zero_terminated_array_attributes(struct xml_attribute** attributes) {
	size_t elements = 0;

	while (attributes[elements]) {
		++elements;
	}

	return elements;
}



/**
 * [PRIVATE]
 *
 * @return Number of nodes in 0-terminated array
 */
static size_t get_zero_terminated_array_nodes(struct xml_node** nodes) {
	size_t elements = 0;

	while (nodes[elements]) {
		++elements;
	}

	return elements;
}



/**
 * [PRIVATE]
 *
 * @warning No UTF conversions will be attempted
 *
 * @return true iff a == b
 */
static _Bool xml_string_equals(struct xml_string* a, struct xml_string* b) {

	if (a->length != b->length) {
		return false;
	}

	size_t i = 0; for (; i < a->length; ++i) {
		if (a->buffer[i] != b->buffer[i]) {
			return false;
		}
	}

	return true;
}



/**
 * [PUBLIC API]
 */
uint8_t* xml_string_clone(struct xml_string* s) {
	if (!s) {
		return 0;
	}

	uint8_t* clone = calloc(s->length + 1, sizeof(uint8_t));

	xml_string_copy(s, clone, s->length);
	clone[s->length] = 0;

	return clone;
}



/**
 * [PRIVATE]
 *
 * Frees the resources allocated by the string
 *
 * @warning `buffer` must _not_ be freed, since it is a reference to the
 *     document's buffer
 */
static void xml_string_free(struct xml_string* string) {
	if (string->own_buffer)
		free(string->buffer);
	free(string);
}



/**
 * [PRIVATE]
 *
 * Frees the resources allocated by the attribute
 */
static void xml_attribute_free(struct xml_attribute* attribute) {
	if(attribute->name) {
		xml_string_free(attribute->name);
	}
	if(attribute->content) {
		xml_string_free(attribute->content);
	}
	free(attribute);
}

/**
 * [PRIVATE]
 * 
 * Frees the resources allocated by the node
 */
static void xml_node_free(struct xml_node* node) {
	xml_string_free(node->name);

	if (node->content) {
		xml_string_free(node->content);
	}

	struct xml_attribute** at = node->attributes;
	while(*at) {
		xml_attribute_free(*at);
		++at;
	}
	free(node->attributes);

	struct xml_node** it = node->children;
	while (*it) {
		xml_node_free(*it);
		++it;
	}
	free(node->children);

	free(node);
}



/**
 * [PRIVATE]
 *
 * Echos the parsers call stack for debugging purposes
 */
#ifdef XML_PARSER_VERBOSE
static void xml_parser_info(struct xml_parser* parser, char const* message) {
	fprintf(stdout, "xml_parser_info %s\n", message);
}
#else
#define xml_parser_info(parser, message) {}
#endif



/**
 * [PRIVATE]
 *
 * Echos an error regarding the parser's source to the console
 */
static void xml_parser_error(struct xml_parser* parser, enum xml_parser_offset offset, char const* message) {
	int row = 0;
	int column = 0;

	#define min(X,Y) ((X) < (Y) ? (X) : (Y))
	#define max(X,Y) ((X) > (Y) ? (X) : (Y))
	size_t character = max(0, min(parser->length, parser->position + offset));
	#undef min
	#undef max

	size_t position = 0; for (; position < character; ++position) {
		column++;

		if ('\n' == parser->buffer[position]) {
			row++;
			column = 0;
		}
	}

	if (NO_CHARACTER != offset) {
		fprintf(stderr,	"xml_parser_error at %i:%i (is %c): %s\n",
				row + 1, column, parser->buffer[character], message
		);
	} else {
		fprintf(stderr,	"xml_parser_error at %i:%i: %s\n",
				row + 1, column, message
		);
	}
}



/**
 * [PRIVATE]
 *
 * Returns the n-th not-whitespace byte in parser and 0 if such a byte does not
 * exist
 */
static uint8_t xml_parser_peek(struct xml_parser* parser, size_t n) {
	size_t position = parser->position;

	while (position < parser->length) {
		if (!isspace(parser->buffer[position])) {
			if (n == 0) {
				return parser->buffer[position];
			} else {
				--n;
			}
		}

		position++;
	}

	return 0;
}



/**
 * [PRIVATE]
 *
 * Moves the parser's position n bytes. If the new position would be out of
 * bounds, it will be converted to the bounds itself
 */
static void xml_parser_consume(struct xml_parser* parser, size_t n) {

	/* Debug information
	 */
	#ifdef XML_PARSER_VERBOSE
	#define min(X,Y) ((X) < (Y) ? (X) : (Y))
	char* consumed = alloca((n + 1) * sizeof(char));
	memcpy(consumed, &parser->buffer[parser->position], min(n, parser->length - parser->position));
	consumed[n] = 0;
	#undef min

	size_t message_buffer_length = 512;
	char* message_buffer = alloca(512 * sizeof(char));
	snprintf(message_buffer, message_buffer_length, "Consuming %li bytes \"%s\"", (long)n, consumed);
	message_buffer[message_buffer_length - 1] = 0;

	xml_parser_info(parser, message_buffer);
	#endif


	/* Move the position forward
	 */
	parser->position += n;

	/* Don't go too far
	 *
	 * @warning Valid because parser->length must be greater than 0
	 */
	if (parser->position >= parser->length) {
		parser->position = parser->length - 1;
	}
}



/**
 * [PRIVATE]
 * 
 * Skips to the next non-whitespace character
 */
static void xml_skip_whitespace(struct xml_parser* parser) {
	xml_parser_info(parser, "whitespace");

	while (isspace(parser->buffer[parser->position])) {
		if (parser->position + 1 >= parser->length) {
			return;
		} else {
			parser->position++;
		}
	}
}



/**
 * [PRIVATE]
 *
 * Finds and creates all attributes on the given node.
 *
 * @author Blake Felt
 * @see https://github.com/Molorius
 */
static struct xml_attribute** xml_find_attributes(struct xml_parser* parser, struct xml_string* tag_open) {
	xml_parser_info(parser, "find_attributes");
	char* tmp;
	char* rest = NULL;
	char* token;
	char* str_name;
	char* str_content;
	const unsigned char* start_name;
	const unsigned char* start_content;
	size_t old_elements;
	size_t new_elements;
	struct xml_attribute* new_attribute;
	struct xml_attribute** attributes;
	int position;

	attributes = calloc(1, sizeof(struct xml_attribute*));
	attributes[0] = 0;

	tmp = (char*) xml_string_clone(tag_open);

	token = xml_strtok_r(tmp, " ", &rest); // skip the first value
	if(token == NULL) {
		goto cleanup;
	}
	tag_open->length = strlen(token);

#if 0
	for(token=xml_strtok_r(NULL," ", &rest); token!=NULL; token=xml_strtok_r(NULL," ", &rest)) {
		str_name = malloc(strlen(token)+1);
		str_content = malloc(strlen(token)+1);
		// %s=\"%s\" wasn't working for some reason, ugly hack to make it work
		if(sscanf(token, "%[^=]=\"%[^\"]", str_name, str_content) != 2) {
			if(sscanf(token, "%[^=]=\'%[^\']", str_name, str_content) != 2) {
				free(str_name);
				free(str_content);
				continue;
			}
		}
		position = token-tmp;
		start_name = &tag_open->buffer[position];
		start_content = &tag_open->buffer[position + strlen(str_name) + 2];

		new_attribute = malloc(sizeof(struct xml_attribute));
		new_attribute->name = malloc(sizeof(struct xml_string));
		new_attribute->name->buffer = (unsigned char*)start_name;
		new_attribute->name->length = strlen(str_name);
		new_attribute->name->own_buffer = 0;
		new_attribute->content = malloc(sizeof(struct xml_string));
		new_attribute->content->buffer = (unsigned char*)start_content;
		new_attribute->content->length = strlen(str_content);
		new_attribute->content->own_buffer = 0;

		old_elements = get_zero_terminated_array_attributes(attributes);
		new_elements = old_elements + 1;
		attributes = realloc(attributes, (new_elements+1)*sizeof(struct xml_attributes*));

		attributes[new_elements-1] = new_attribute;
		attributes[new_elements] = 0;

		free(str_name);
		free(str_content);
	}
#else
	while (1)
	{
		str_name = xml_strtok_r(NULL, " =", &rest);
		if (str_name == NULL)
			break;
		str_content = xml_strtok_r(NULL, "\"", &rest);		// TODO handle name="" (i.e. an empty string)
		if (str_content == NULL)
			break;

		position = str_name - tmp;
		start_name = &tag_open->buffer[str_name - tmp];
		start_content = &tag_open->buffer[str_content - tmp];

		new_attribute = malloc(sizeof(struct xml_attribute));
		new_attribute->name = malloc(sizeof(struct xml_string));
		new_attribute->name->buffer = (unsigned char*)start_name;
		new_attribute->name->length = strlen(str_name);
		new_attribute->name->own_buffer = 0;
		new_attribute->content = malloc(sizeof(struct xml_string));
		new_attribute->content->buffer = (unsigned char*)start_content;
		new_attribute->content->length = strlen(str_content);
		new_attribute->content->own_buffer = 0;

		old_elements = get_zero_terminated_array_attributes(attributes);
		new_elements = old_elements + 1;
		attributes = realloc(attributes, (new_elements + 1) * sizeof(struct xml_attributes*));

		attributes[new_elements - 1] = new_attribute;
		attributes[new_elements] = 0;
	}
#endif

cleanup:
	free(tmp);
	return attributes;
}



/**
 * [PRIVATE]
 *
 * Parses the name out of the an XML tag's ending
 *
 * ---( Example )---
 * tag_name>
 * ---
 */
static struct xml_string* xml_parse_tag_end(struct xml_parser* parser) {
	xml_parser_info(parser, "tag_end");
	size_t start = parser->position;
	size_t length = 0;

	/* Parse until '>' or a whitespace is reached
	 */
	while (start + length < parser->length) {
		uint8_t current = xml_parser_peek(parser, CURRENT_CHARACTER);

		if (('>' == current) || isspace(current)) {
			break;
		} else {
			xml_parser_consume(parser, 1);
			length++;
		}
	}

	/* Consume `>'
	 */
	if ('>' != xml_parser_peek(parser, CURRENT_CHARACTER)) {
		xml_parser_error(parser, CURRENT_CHARACTER, "xml_parse_tag_end::expected tag end");
		return 0;
	}
	xml_parser_consume(parser, 1);

	/* Return parsed tag name
	 */
	struct xml_string* name = malloc(sizeof(struct xml_string));
	name->buffer = &parser->buffer[start];
	name->length = length;
	name->own_buffer = 0;
	return name;
}



/**
 * [PRIVATE]
 *
 * Parses an opening XML tag without attributes
 *
 * ---( Example )---
 * <tag_name>
 * ---
 */
static struct xml_string* xml_parse_tag_open(struct xml_parser* parser) {
	xml_parser_info(parser, "tag_open");
	xml_skip_whitespace(parser);

	/* Consume `<'
	 */
	if ('<' != xml_parser_peek(parser, CURRENT_CHARACTER)) {
		xml_parser_error(parser, CURRENT_CHARACTER, "xml_parse_tag_open::expected opening tag");
		return 0;
	}
	xml_parser_consume(parser, 1);

	/* Consume tag name
	 */
	return xml_parse_tag_end(parser);
}



/**
 * [PRIVATE]
 *
 * Parses an closing XML tag without attributes
 *
 * ---( Example )---
 * </tag_name>
 * ---
 */
static struct xml_string* xml_parse_tag_close(struct xml_parser* parser) {
	xml_parser_info(parser, "tag_close");
	xml_skip_whitespace(parser);

	/* Consume `</'
	 */
	if (		('<' != xml_parser_peek(parser, CURRENT_CHARACTER))
		||	('/' != xml_parser_peek(parser, NEXT_CHARACTER))) {

		if ('<' != xml_parser_peek(parser, CURRENT_CHARACTER)) {
			xml_parser_error(parser, CURRENT_CHARACTER, "xml_parse_tag_close::expected closing tag `<'");
		}
		if ('/' != xml_parser_peek(parser, NEXT_CHARACTER)) {
			xml_parser_error(parser, NEXT_CHARACTER, "xml_parse_tag_close::expected closing tag `/'");
		}

		return 0;
	}
	xml_parser_consume(parser, 2);

	/* Consume tag name
	 */
	return xml_parse_tag_end(parser);
}



/**
 * [PRIVATE]
 *
 * Parses a tag's content
 *
 * ---( Example )---
 *     this is
 *   a
 *       tag {} content
 * ---
 *
 * @warning CDATA etc. is _not_ and will never be supported
 */
static struct xml_string* xml_parse_content(struct xml_parser* parser) {
	xml_parser_info(parser, "content");

	/* Whitespace will be ignored
	 */
	xml_skip_whitespace(parser);

	size_t start = parser->position;
	size_t length = 0;

	/* Consume until `<' is reached
	 */
	while (start + length < parser->length) {
		uint8_t current = xml_parser_peek(parser, CURRENT_CHARACTER);

		if ('<' == current) {
			break;
		} else {
			xml_parser_consume(parser, 1);
			length++;
		}
	}

	/* Next character must be an `<' or we have reached end of file
	 */
	if ('<' != xml_parser_peek(parser, CURRENT_CHARACTER)) {
		xml_parser_error(parser, CURRENT_CHARACTER, "xml_parse_content::expected <");
		return 0;
	}

	/* Ignore tailing whitespace
	 */
	while ((length > 0) && isspace(parser->buffer[start + length - 1])) {
		length--;
	}

	/* Return text
	 */
	struct xml_string* content = malloc(sizeof(struct xml_string));
	content->buffer = &parser->buffer[start];
	content->length = length;
	content->own_buffer = 0;
	return content;
}



/**
 * [PRIVATE]
 * 
 * Parses an XML fragment node
 *
 * ---( Example without children )---
 * <Node>Text</Node>
 * ---
 *
 * ---( Example with children )---
 * <Parent>
 *     <Child>Text</Child>
 *     <Child>Text</Child>
 *     <Test>Content</Test>
 * </Parent>
 * ---
 */
static struct xml_node* xml_parse_node(struct xml_parser* parser) {
	xml_parser_info(parser, "node");

	/* Setup variables
	 */
	struct xml_string* tag_open = 0;
	struct xml_string* tag_close = 0;
	struct xml_string* content = 0;

	size_t original_length;
	struct xml_attribute** attributes;

	struct xml_node** children = calloc(1, sizeof(struct xml_node*));
	children[0] = 0;


	/* Parse open tag
	 */
	tag_open = xml_parse_tag_open(parser);
	if (!tag_open) {
		xml_parser_error(parser, NO_CHARACTER, "xml_parse_node::tag_open");
		goto exit_failure;
	}

	original_length = tag_open->length;
	attributes = xml_find_attributes(parser, tag_open);

	/* If tag ends with `/' it's self closing, skip content lookup */
	if (tag_open->length > 0 && '/' == tag_open->buffer[original_length - 1]) {
		/* Drop `/'
		 */
		if (original_length == tag_open->length)
			tag_open->length--;
		goto node_creation;
	}

	/* If the content does not start with '<', a text content is assumed
	 */
	if ('<' != xml_parser_peek(parser, CURRENT_CHARACTER)) {
		content = xml_parse_content(parser);

		if (!content) {
			xml_parser_error(parser, 0, "xml_parse_node::content");
			goto exit_failure;
		}


	/* Otherwise children are to be expected
	 */
	} else while ('/' != xml_parser_peek(parser, NEXT_CHARACTER)) {

		/* Parse child node
		 */
		struct xml_node* child = xml_parse_node(parser);
		if (!child) {
			xml_parser_error(parser, NEXT_CHARACTER, "xml_parse_node::child");
			goto exit_failure;
		}

		/* Grow child array :)
		 */
		size_t old_elements = get_zero_terminated_array_nodes(children);
		size_t new_elements = old_elements + 1;
		children = realloc(children, (new_elements + 1) * sizeof(struct xml_node*));

		/* Save child
		 */
		children[new_elements - 1] = child;
		children[new_elements] = 0;
	}


	/* Parse close tag
	 */
	tag_close = xml_parse_tag_close(parser);
	if (!tag_close) {
		xml_parser_error(parser, NO_CHARACTER, "xml_parse_node::tag_close");
		goto exit_failure;
	}


	/* Close tag has to match open tag
	 */
	if (!xml_string_equals(tag_open, tag_close)) {
		xml_parser_error(parser, NO_CHARACTER, "xml_parse_node::tag missmatch");
		goto exit_failure;
	}


	/* Return parsed node
	 */
	xml_string_free(tag_close);

node_creation:;
	struct xml_node* node = malloc(sizeof(struct xml_node));
	node->name = tag_open;
	node->content = content;
	node->attributes = attributes;
	node->children = children;
	return node;


	/* A failure occured, so free all allocated resources
	 */
exit_failure:
	if (tag_open) {
		xml_string_free(tag_open);
	}
	if (tag_close) {
		xml_string_free(tag_close);
	}
	if (content) {
		xml_string_free(content);
	}

	struct xml_node** it = children;
	while (*it) {
		xml_node_free(*it);
		++it;
	}
	free(children);

	return 0;
}


/**
 * [PRIVATE]
 *
 * Writes an XML fragment node to file at the given indent level.
 */
void xml_write_node(FILE* dest, struct xml_node* node, int indent)
{
	int i, n_attr, n_children;

	// Some blanks to do indenting with
	static char blanks[64] = "                                                                ";

	if (indent > 64)
		indent = 64;
	fprintf(dest, "%.*s", indent, blanks);

	// Write opening tag
	fprintf(dest, "<%.*s", node->name->length, node->name->buffer);

	// Write any attributes
	n_attr = get_zero_terminated_array_attributes(node->attributes);
	for (i = 0; i < n_attr; i++)
	{
		struct xml_attribute* a = node->attributes[i];

		fprintf(dest, " %.*s=\"%.*s\"", a->name->length, a->name->buffer, a->content->length, a->content->buffer);
	}

	// If there are no contents or children, the opening tag can be closed with />
	n_children = get_zero_terminated_array_nodes(node->children);
	if (node->content == NULL && n_children == 0)
	{
		fprintf(dest, "/>\n");
	}
	else
	{
		// Write content
		if (node->content != NULL)
			fprintf(dest, ">%.*s", node->content->length, node->content->buffer);
		else
			fprintf(dest, ">");

		// Write child nodes at the next indent (on separate lines if any)
		if (n_children > 0)
		{
			fprintf(dest, "\n");
			for (i = 0; i < n_children; i++)
				xml_write_node(dest, node->children[i], indent + 3);
			fprintf(dest, "%.*s", indent, blanks);
		}

		// Write closing tag
		fprintf(dest, "</%.*s>\n", node->name->length, node->name->buffer);
	}
}



/**
 * [PUBLIC API]
 */
struct xml_document* xml_parse_document(uint8_t* buffer, int initial_offset, size_t length) {

	/* Initialize parser
	 */
	struct xml_parser parser = {
		.buffer = buffer,
		.position = initial_offset,
		.length = length
	};

	/* An empty buffer can never contain a valid document
	 */
	if (!length) {
		xml_parser_error(&parser, NO_CHARACTER, "xml_parse_document::length equals zero");
		return 0;
	}

	/* Parse the root node
	 */
	struct xml_node* root = xml_parse_node(&parser);
	if (!root) {
		xml_parser_error(&parser, NO_CHARACTER, "xml_parse_document::parsing document failed");
		return 0;
	}

	/* Return parsed document
	 */
	struct xml_document* document = malloc(sizeof(struct xml_document));
	document->buffer.buffer = buffer;
	document->buffer.length = length;
	document->initial_offset = initial_offset;
	document->root = root;

	return document;
}



/**
 * [PUBLIC API]
 */
struct xml_document* xml_open_document(FILE* source) {

	/* Prepare buffer
	 */
	size_t const read_chunk = 1; // TODO 4096;
	int offset = 0;
	size_t document_length = 0;
	size_t buffer_size = 1;	// TODO 4069
	uint8_t* buffer = malloc(buffer_size * sizeof(uint8_t));

	/* Read hole file into buffer
	 */
	while (!feof(source)) {

		/* Reallocate buffer
		 */
		if (buffer_size - document_length < read_chunk) {
			buffer = realloc(buffer, buffer_size + 2 * read_chunk);
			buffer_size += 2 * read_chunk;
		}

		size_t read = fread(
			&buffer[document_length],
			sizeof(uint8_t), read_chunk,
			source
		);

		document_length += read;
	}
	fclose(source);

	/*
	 * Skip over <?xml .... ?> at the start, if it present.
	 * Leave it in the buffer but step offset over it
	 */
	if (strncmp(buffer, "<?xml", 5) == 0)
	{
		while (strncmp(buffer + offset, "?>", 2) != 0)
			offset++;
		offset += 2;
	}

	/* Try to parse buffer
	 */
	struct xml_document* document = xml_parse_document(buffer, offset, document_length);

	if (!document) {
		free(buffer);
		return 0;
	}
	return document;
}

/**
 * [PUBLIC API]
 */
void xml_write_document(FILE* dest, struct xml_document* document)
{
	// Write any <?xml header first
	if (document->initial_offset > 0)
		fprintf(dest, "%.*s\n", document->initial_offset, document->buffer.buffer);

	xml_write_node(dest, document->root, 0);
}

/**
 * [PUBLIC API]
 */
void xml_document_free(struct xml_document* document, bool free_buffer) {
	xml_node_free(document->root);

	if (free_buffer) {
		free(document->buffer.buffer);
	}
	free(document);
}



/**
 * [PUBLIC API]
 */
struct xml_node* xml_document_root(struct xml_document* document) {
	return document->root;
}



/**
 * [PUBLIC API]
 */
struct xml_string* xml_node_name(struct xml_node* node) {
	return node->name;
}



/**
 * [PUBLIC API]
 */
struct xml_string* xml_node_content(struct xml_node* node) {
	return node->content;
}



/**
 * [PUBLIC API]
 *
 * @warning O(n)
 */
size_t xml_node_children(struct xml_node* node) {
	return get_zero_terminated_array_nodes(node->children);
}



/**
 * [PUBLIC API]
 */
struct xml_node* xml_node_child(struct xml_node* node, size_t child) {
	if (child >= xml_node_children(node)) {
		return 0;
	}

	return node->children[child];
}



/**
 * [PUBLIC API]
 */
size_t xml_node_attributes(struct xml_node* node) {
	return get_zero_terminated_array_attributes(node->attributes);
}


/**
 * [PUBLIC API]
 */
int xml_node_attribute_by_name(struct xml_node* node, uint8_t* attr) {
	unsigned int attribute;
	struct xml_string xattr = { attr, strlen(attr) };

	for (attribute = 0; attribute < xml_node_attributes(node); attribute++) {
		if (xml_string_equals(node->attributes[attribute]->name, &xattr))
			break;
	}
	if (attribute >= xml_node_attributes(node))
		return -1;
	return attribute;
}


/**
 * [PUBLIC API]
 */
struct xml_string* xml_node_attribute_name(struct xml_node* node, size_t attribute) {
	if(attribute >= xml_node_attributes(node)) {
		return 0;
	}

	return node->attributes[attribute]->name;
}



/**
 * [PUBLIC API]
 */
struct xml_string* xml_node_attribute_content(struct xml_node* node, size_t attribute) {
	if (attribute >= xml_node_attributes(node)) {
		return 0;
	}

	return node->attributes[attribute]->content;
}


/**
 * [PUBLIC API]
 */
struct xml_string* xml_node_attribute_content_by_name(struct xml_node* node, uint8_t *attr) {
	
	int attribute = xml_node_attribute_by_name(node, attr);
	if (attribute < 0)
		return 0;
	return node->attributes[attribute]->content;
}


/**
 * [PUBLIC API]
 */
void xml_node_attribute_replace_content(struct xml_node* node, size_t attribute, uint8_t* buffer)
{
	struct xml_string* content = node->attributes[attribute]->content;
	unsigned int length = strlen(buffer);

	// see if new string can be accommodated within the old location
	if (length > content->length)
	{
		// It won't fit. Allocate a new buffer for the content and mark it
		// to be later freed.
		content->buffer = malloc(length);
		content->length = length;
		content->own_buffer = 1;
	}
	memcpy(content->buffer, buffer, length);
}

/**
 * [PUBLIC API]
 */
struct xml_node* xml_easy_child(struct xml_node* node, uint8_t const* child_name, ...) {

	/* Find children, one by one
	 */
	struct xml_node* current = node;

	va_list arguments;
	va_start(arguments, child_name);


	/* Descent to current.child
	 */
	while (child_name) {

		/* Convert child_name to xml_string for easy comparison
		 */
		struct xml_string cn = {
			.buffer = (uint8_t *)child_name,
			.length = strlen(child_name),
			.own_buffer = 0
		};

		/* Interate through all children
		 */
		struct xml_node* next = 0;

		size_t i = 0; for (; i < xml_node_children(current); ++i) {
			struct xml_node* child = xml_node_child(current, i);

			if (xml_string_equals(xml_node_name(child), &cn)) {
				if (!next) {
					next = child;

				/* Two children with the same name
				 */
				} else {
					va_end(arguments);
					return 0;
				}
			}
		}

		/* No child with that name found
		 */
		if (!next) {
			va_end(arguments);
			return 0;
		}
		current = next;		
		
		/* Find name of next child
		 */
		child_name = va_arg(arguments, uint8_t const*);
	}
	va_end(arguments);


	/* Return current element
	 */
	return current;
}



/**
 * [PUBLIC API]
 */
uint8_t* xml_easy_name(struct xml_node* node) {
	if (!node) {
		return 0;
	}

	return xml_string_clone(xml_node_name(node));
}



/**
 * [PUBLIC API]
 */
uint8_t* xml_easy_content(struct xml_node* node) {
	if (!node) {
		return 0;
	}

	return xml_string_clone(xml_node_content(node));
}



/**
 * [PUBLIC API]
 */
size_t xml_string_length(struct xml_string* string) {
	if (!string) {
		return 0;
	}
	return string->length;
}



/**
 * [PUBLIC API]
 */
void xml_string_copy(struct xml_string* string, uint8_t* buffer, size_t length) {
	if (!string) {
		return;
	}

	#define min(X,Y) ((X) < (Y) ? (X) : (Y))
	length = min(length, string->length);
	#undef min

	memcpy(buffer, string->buffer, length);
}


/**
 * [PUBLIC API]
 */
int xml_string_cmp(struct xml_string* string, uint8_t* buffer) {
	return strncmp(string->buffer, buffer, string->length);
}