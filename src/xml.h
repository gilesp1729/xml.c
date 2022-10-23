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
#ifndef HEADER_XML
#define HEADER_XML


/**
 * Includes
 */
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#undef min
#undef max
//#define _CRT_SECURE_NO_WARNINGS
#pragma warning(disable: 4996)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Opaque structure holding the parsed xml document
 */
struct xml_document;
struct xml_node;
struct xml_attribute;

/**
 * Internal character sequence representation
 */
struct xml_string;



/**
 * Tries to parse the XML fragment in buffer
 *
 * @param buffer Chunk to parse
 * @param initial_offset Where the XML proper begins in the  buffer (allows skipping <?xml.... /> )
 * @param length Size of the buffer
 *
 * @warning `buffer` will be referenced by the document, you may not free it
 *     until you free the xml_document
 * @warning You have to call xml_document_free after you finished using the
 *     document
 *
 * @return The parsed xml fragment iff parsing was successful, 0 otherwise
 */
struct xml_document* xml_parse_document(uint8_t* buffer, int initial_offset, size_t length);



/**
 * Tries to read an XML document from disk
 *
 * @param source File that will be read into an xml document. Will be closed
 *
 * @warning You have to call xml_document_free with free_buffer = true after you
 *     finished using the document
 *
 * @return The parsed xml fragment iff parsing was successful, 0 otherwise
 */
struct xml_document* xml_open_document(FILE* source);


/**
 * Write a document to disk. If the documet was read from a file containing a
 * <?xml... header, that header will be written out first.
 */
void xml_write_document(FILE* dest, struct xml_document* document);


/**
 * Frees all resources associated with the document. All xml_node and xml_string
 * references obtained through the document will be invalidated
 *
 * @param document xml_document to free
 * @param free_buffer iff true the internal buffer supplied via xml_parse_buffer
 *     will be freed with the `free` system call
 */
void xml_document_free(struct xml_document* document, bool free_buffer);


/**
 * @return xml_node representing the document root
 */
struct xml_node* xml_document_root(struct xml_document* document);



/**
 * @return The xml_node's tag name
 */
struct xml_string* xml_node_name(struct xml_node* node);



/**
 * @return The xml_node's string content (if available, otherwise NULL)
 */
struct xml_string* xml_node_content(struct xml_node* node);



/**
 * @return Number of child nodes
 */
size_t xml_node_children(struct xml_node* node);



/**
 * @return The n-th child or 0 if out of range
 */
struct xml_node* xml_node_child(struct xml_node* node, size_t child);



/**
 * @return Number of attribute nodes
 */
size_t xml_node_attributes(struct xml_node* node);



/**
 * @return the n-th attribute name or 0 if out of range
 */
struct xml_string* xml_node_attribute_name(struct xml_node* node, size_t attribute);



/**
 * @return the n-th attribute content or 0 if out of range
 */
struct xml_string* xml_node_attribute_content(struct xml_node* node, size_t attribute);


/**
 * @return the attribute number, given its name, or -1 if not found
 */
int xml_node_attribute_by_name(struct xml_node* node, uint8_t* attr);


/**
 * @return the attribute content, given its name, or 0 if not found
 */
struct xml_string* xml_node_attribute_content_by_name(struct xml_node* node, uint8_t* attr);


/**
 * Replace the content of the given attribute with a given null-terminated string
 */
void xml_node_attribute_replace_content(struct xml_node* node, size_t attribute, uint8_t *buffer);


/**
 * @return The node described by the path or 0 if child cannot be found
 * @warning Each element on the way must be unique
 * @warning Last argument must be 0
 */
struct xml_node* xml_easy_child(struct xml_node* node, uint8_t const* child, ...);



/**
 * @return 0-terminated copy of node name
 * @warning User must free the result
 */
uint8_t* xml_easy_name(struct xml_node* node);



/**
 * @return 0-terminated copy of node content
 * @warning User must free the result
 */
uint8_t* xml_easy_content(struct xml_node* node);



/**
 * @return Length of the string
 */
size_t xml_string_length(struct xml_string* string);



/**
 * Copies the string into the supplied buffer
 *
 * @warning String will not be 0-terminated
 * @warning Will write at most length bytes, even if the string is longer
 */
void xml_string_copy(struct xml_string* string, uint8_t* buffer, size_t length);


/**
 * Returns strncmp of XML string with buffer
 */
int xml_string_cmp(struct xml_string* string, uint8_t* buffer);

/**
 * Allocates new null-terminated string and copies the xml_string into it
 */
uint8_t* xml_string_clone(struct xml_string* s);

#ifdef __cplusplus
}
#endif

#endif

