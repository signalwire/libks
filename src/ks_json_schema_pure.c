/*
 * Copyright (c) 2025 SignalWire, Inc
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "libks/ks.h"
#include "libks/ks_json_schema_pure.h"
#include <string.h>
#include <math.h>
#include <regex.h>
#include <ctype.h>

// Maximum path length for JSON pointers during validation
#define KS_JSON_SCHEMA_MAX_PATH_LEN 2048
#define KS_JSON_SCHEMA_MAX_ERRORS 10

//
// Utility Functions
//

static ks_json_schema_pure_error_t *create_error(const char *message, const char *path)
{
	ks_json_schema_pure_error_t *error = malloc(sizeof(ks_json_schema_pure_error_t));
	if (!error) return NULL;

	error->message = strdup(message ? message : "Unknown error");
	error->path = strdup(path ? path : "");
	error->next = NULL;

	if (!error->message || !error->path) {
		free(error->message);
		free(error->path);
		free(error);
		return NULL;
	}

	return error;
}

static void add_error_to_list(ks_json_schema_pure_error_t **error_list, ks_json_schema_pure_error_t *error)
{
	if (!error_list || !error) return;

	if (*error_list == NULL) {
		*error_list = error;
	} else {
		ks_json_schema_pure_error_t *current = *error_list;
		while (current->next) {
			current = current->next;
		}
		current->next = error;
	}
}

// Convert ks_json_type_t to schema type bitmask
static uint32_t json_type_to_schema_mask(ks_json_type_t type)
{
	switch (type) {
		case KS_JSON_TYPE_NULL:   return 1 << 0;
		case KS_JSON_TYPE_FALSE:  return 1 << 1;
		case KS_JSON_TYPE_TRUE:   return 1 << 1;  // Both true/false map to boolean
		case KS_JSON_TYPE_NUMBER: return 1 << 2;
		case KS_JSON_TYPE_STRING: return 1 << 3;
		case KS_JSON_TYPE_ARRAY:  return 1 << 4;
		case KS_JSON_TYPE_OBJECT: return 1 << 5;
		default: return 0;
	}
}

static uint32_t parse_type_constraint(ks_json_t *type_value)
{
	uint32_t type_mask = 0;

	if (ks_json_type_is_string(type_value)) {
		const char *type_str = ks_json_get_string(type_value, "");
		if (strcmp(type_str, "null") == 0) type_mask |= 1 << 0;
		else if (strcmp(type_str, "boolean") == 0) type_mask |= 1 << 1;
		else if (strcmp(type_str, "integer") == 0) type_mask |= 1 << 2;
		else if (strcmp(type_str, "number") == 0) type_mask |= 1 << 2;
		else if (strcmp(type_str, "string") == 0) type_mask |= 1 << 3;
		else if (strcmp(type_str, "array") == 0) type_mask |= 1 << 4;
		else if (strcmp(type_str, "object") == 0) type_mask |= 1 << 5;
	} else if (ks_json_type_is_array(type_value)) {
		int array_size = ks_json_get_array_size(type_value);
		for (int i = 0; i < array_size; i++) {
			ks_json_t *item = ks_json_get_array_item(type_value, i);
			type_mask |= parse_type_constraint(item);
		}
	}

	return type_mask;
}

//
// Schema Node Creation Functions
//

static ks_json_schema_node_t *create_schema_node(ks_pool_t *pool, ks_json_schema_node_type_t type)
{
	ks_json_schema_node_t *node = ks_pool_alloc(pool, sizeof(ks_json_schema_node_t));
	if (!node) return NULL;

	memset(node, 0, sizeof(ks_json_schema_node_t));
	node->type = type;

	return node;
}

static ks_json_schema_node_t *compile_boolean_schema(ks_pool_t *pool, ks_bool_t value)
{
	ks_json_schema_node_t *node = create_schema_node(pool, KS_SCHEMA_NODE_BOOLEAN);
	if (!node) return NULL;

	node->constraint.boolean_constraint.value = value;
	return node;
}

static ks_json_schema_node_t *compile_type_schema(ks_pool_t *pool, ks_json_t *schema)
{
	ks_json_schema_node_t *node = create_schema_node(pool, KS_SCHEMA_NODE_TYPE);
	if (!node) return NULL;

	ks_json_t *type_value = ks_json_get_object_item(schema, "type");
	if (type_value) {
		node->constraint.type_constraint.allowed_types = parse_type_constraint(type_value);
	} else {
		// If no type specified, allow all types
		node->constraint.type_constraint.allowed_types = 0x3F; // All 6 types
	}

	return node;
}

// Forward declaration needed for recursive compilation
static ks_json_schema_node_t *compile_schema_internal(ks_pool_t *pool, ks_json_t *schema, ks_json_t *root_schema, ks_hash_t *ref_cache, ks_json_schema_pure_error_t **errors);

static ks_json_schema_node_t *compile_object_schema(ks_pool_t *pool, ks_json_t *schema, ks_json_t *root_schema, ks_hash_t *ref_cache)
{
	ks_json_schema_node_t *node = create_schema_node(pool, KS_SCHEMA_NODE_OBJECT);
	if (!node) return NULL;

	ks_schema_object_constraint_t *constraint = &node->constraint.object_constraint;

	// Initialize constraint
	constraint->properties = NULL;
	constraint->additional_properties = NULL;
	constraint->required_properties = NULL;
	constraint->required_count = 0;
	constraint->min_properties = 0;
	constraint->max_properties = 0;
	constraint->has_min_properties = KS_FALSE;
	constraint->has_max_properties = KS_FALSE;

	// Parse minProperties
	ks_json_t *min_props = ks_json_get_object_item(schema, "minProperties");
	if (min_props && ks_json_type_is_number(min_props)) {
		constraint->min_properties = ks_json_get_number_int(min_props, 0);
		constraint->has_min_properties = KS_TRUE;
	}

	// Parse maxProperties
	ks_json_t *max_props = ks_json_get_object_item(schema, "maxProperties");
	if (max_props && ks_json_type_is_number(max_props)) {
		constraint->max_properties = ks_json_get_number_int(max_props, 0);
		constraint->has_max_properties = KS_TRUE;
	}

	// Parse required array
	ks_json_t *required = ks_json_get_object_item(schema, "required");
	if (required && ks_json_type_is_array(required)) {
		int array_size = ks_json_get_array_size(required);
		if (array_size > 0) {
			constraint->required_properties = ks_pool_alloc(pool, array_size * sizeof(char*));
			if (constraint->required_properties) {
				for (int i = 0; i < array_size; i++) {
					ks_json_t *item = ks_json_get_array_item(required, i);
					if (ks_json_type_is_string(item)) {
						const char *prop_name = ks_json_get_string(item, "");
						constraint->required_properties[constraint->required_count] = ks_pool_alloc(pool, strlen(prop_name) + 1);
						if (constraint->required_properties[constraint->required_count]) {
							strcpy(constraint->required_properties[constraint->required_count], prop_name);
							constraint->required_count++;
						}
					}
				}
			}
		}
	}

	// Parse properties
	ks_json_t *properties = ks_json_get_object_item(schema, "properties");
	if (properties && ks_json_type_is_object(properties)) {
		// Create hash table for properties
		ks_status_t hash_status = ks_hash_create(&constraint->properties, KS_HASH_MODE_CASE_INSENSITIVE, 0, pool);
		if (hash_status == KS_STATUS_SUCCESS) {
			ks_json_t *prop = ks_json_enum_child(properties);
			while (prop) {
				const char *prop_name = ks_json_get_name(prop);
				if (prop_name) {
					// Compile the property schema (this will handle $ref resolution)
					ks_json_schema_node_t *prop_node = compile_schema_internal(pool, prop, root_schema, ref_cache, NULL);
					if (prop_node) {
						// Duplicate the property name for storage in hash table
						char *prop_name_copy = ks_pool_alloc(pool, strlen(prop_name) + 1);
						if (prop_name_copy) {
							strcpy(prop_name_copy, prop_name);
							// Store in hash table
							ks_hash_insert(constraint->properties, prop_name_copy, prop_node);
						}
					}
				}
				prop = ks_json_enum_next(prop);
			}
		} else {
			// If hash table creation fails, we can't validate properties
			// Set to NULL to indicate properties validation is not available
			constraint->properties = NULL;
		}

	}

	// TODO: Parse additionalProperties

	return node;
}

static ks_json_schema_node_t *compile_array_schema(ks_pool_t *pool, ks_json_t *schema)
{
	ks_json_schema_node_t *node = create_schema_node(pool, KS_SCHEMA_NODE_ARRAY);
	if (!node) return NULL;

	ks_schema_array_constraint_t *constraint = &node->constraint.array_constraint;

	// Initialize constraint
	constraint->items = NULL;
	constraint->additional_items = NULL;
	constraint->min_items = 0;
	constraint->max_items = 0;
	constraint->unique_items = KS_FALSE;
	constraint->has_min_items = KS_FALSE;
	constraint->has_max_items = KS_FALSE;

	// Parse minItems
	ks_json_t *min_items = ks_json_get_object_item(schema, "minItems");
	if (min_items && ks_json_type_is_number(min_items)) {
		constraint->min_items = ks_json_get_number_int(min_items, 0);
		constraint->has_min_items = KS_TRUE;
	}

	// Parse maxItems
	ks_json_t *max_items = ks_json_get_object_item(schema, "maxItems");
	if (max_items && ks_json_type_is_number(max_items)) {
		constraint->max_items = ks_json_get_number_int(max_items, 0);
		constraint->has_max_items = KS_TRUE;
	}

	// Parse uniqueItems
	ks_json_t *unique = ks_json_get_object_item(schema, "uniqueItems");
	if (unique && ks_json_type_is_bool(unique)) {
		constraint->unique_items = ks_json_get_bool(unique, KS_FALSE);
	}

	// TODO: Parse items, additionalItems

	return node;
}

static ks_json_schema_node_t *compile_string_schema(ks_pool_t *pool, ks_json_t *schema)
{
	ks_json_schema_node_t *node = create_schema_node(pool, KS_SCHEMA_NODE_STRING);
	if (!node) return NULL;

	ks_schema_string_constraint_t *constraint = &node->constraint.string_constraint;

	// Initialize constraint
	constraint->min_length = 0;
	constraint->max_length = 0;
	constraint->pattern = NULL;
	constraint->format = NULL;
	constraint->has_min_length = KS_FALSE;
	constraint->has_max_length = KS_FALSE;

	// Parse minLength
	ks_json_t *min_length = ks_json_get_object_item(schema, "minLength");
	if (min_length && ks_json_type_is_number(min_length)) {
		constraint->min_length = ks_json_get_number_int(min_length, 0);
		constraint->has_min_length = KS_TRUE;
	}

	// Parse maxLength
	ks_json_t *max_length = ks_json_get_object_item(schema, "maxLength");
	if (max_length && ks_json_type_is_number(max_length)) {
		constraint->max_length = ks_json_get_number_int(max_length, 0);
		constraint->has_max_length = KS_TRUE;
	}

	// Parse pattern
	ks_json_t *pattern = ks_json_get_object_item(schema, "pattern");
	if (pattern && ks_json_type_is_string(pattern)) {
		const char *pattern_str = ks_json_get_string(pattern, "");
		constraint->pattern = ks_pool_alloc(pool, strlen(pattern_str) + 1);
		if (constraint->pattern) {
			strcpy(constraint->pattern, pattern_str);
		}
	}

	// Parse format
	ks_json_t *format = ks_json_get_object_item(schema, "format");
	if (format && ks_json_type_is_string(format)) {
		const char *format_str = ks_json_get_string(format, "");
		constraint->format = ks_pool_alloc(pool, strlen(format_str) + 1);
		if (constraint->format) {
			strcpy(constraint->format, format_str);
		}
	}

	return node;
}

static ks_json_schema_node_t *compile_number_schema(ks_pool_t *pool, ks_json_t *schema)
{
	ks_json_schema_node_t *node = create_schema_node(pool, KS_SCHEMA_NODE_NUMBER);
	if (!node) return NULL;

	ks_schema_number_constraint_t *constraint = &node->constraint.number_constraint;

	// Initialize constraint
	constraint->minimum = 0.0;
	constraint->maximum = 0.0;
	constraint->multiple_of = 0.0;
	constraint->exclusive_minimum = KS_FALSE;
	constraint->exclusive_maximum = KS_FALSE;
	constraint->has_minimum = KS_FALSE;
	constraint->has_maximum = KS_FALSE;
	constraint->has_multiple_of = KS_FALSE;

	// Parse minimum
	ks_json_t *minimum = ks_json_get_object_item(schema, "minimum");
	if (minimum && ks_json_type_is_number(minimum)) {
		constraint->minimum = ks_json_get_number_double(minimum, 0.0);
		constraint->has_minimum = KS_TRUE;
	}

	// Parse maximum
	ks_json_t *maximum = ks_json_get_object_item(schema, "maximum");
	if (maximum && ks_json_type_is_number(maximum)) {
		constraint->maximum = ks_json_get_number_double(maximum, 0.0);
		constraint->has_maximum = KS_TRUE;
	}

	// Parse exclusiveMinimum
	ks_json_t *excl_min = ks_json_get_object_item(schema, "exclusiveMinimum");
	if (excl_min) {
		if (ks_json_type_is_bool(excl_min)) {
			constraint->exclusive_minimum = ks_json_get_bool(excl_min, KS_FALSE);
		} else if (ks_json_type_is_number(excl_min)) {
			// Draft 6+ style - exclusiveMinimum is a number
			constraint->minimum = ks_json_get_number_double(excl_min, 0.0);
			constraint->exclusive_minimum = KS_TRUE;
			constraint->has_minimum = KS_TRUE;
		}
	}

	// Parse exclusiveMaximum
	ks_json_t *excl_max = ks_json_get_object_item(schema, "exclusiveMaximum");
	if (excl_max) {
		if (ks_json_type_is_bool(excl_max)) {
			constraint->exclusive_maximum = ks_json_get_bool(excl_max, KS_FALSE);
		} else if (ks_json_type_is_number(excl_max)) {
			// Draft 6+ style - exclusiveMaximum is a number
			constraint->maximum = ks_json_get_number_double(excl_max, 0.0);
			constraint->exclusive_maximum = KS_TRUE;
			constraint->has_maximum = KS_TRUE;
		}
	}

	// Parse multipleOf
	ks_json_t *multiple_of = ks_json_get_object_item(schema, "multipleOf");
	if (multiple_of && ks_json_type_is_number(multiple_of)) {
		constraint->multiple_of = ks_json_get_number_double(multiple_of, 1.0);
		constraint->has_multiple_of = KS_TRUE;
	}

	return node;
}

static ks_json_schema_node_t *compile_enum_schema(ks_pool_t *pool, ks_json_t *schema)
{
	ks_json_schema_node_t *node = create_schema_node(pool, KS_SCHEMA_NODE_ENUM);
	if (!node) return NULL;

	ks_schema_enum_constraint_t *constraint = &node->constraint.enum_constraint;

	ks_json_t *enum_array = ks_json_get_object_item(schema, "enum");
	if (!enum_array || !ks_json_type_is_array(enum_array)) {
		return NULL; // Invalid enum schema
	}

	int array_size = ks_json_get_array_size(enum_array);
	if (array_size == 0) {
		return NULL; // Empty enum is invalid
	}

	constraint->enum_values = ks_pool_alloc(pool, array_size * sizeof(ks_json_t*));
	if (!constraint->enum_values) return NULL;

	constraint->enum_count = 0;
	for (int i = 0; i < array_size; i++) {
		ks_json_t *item = ks_json_get_array_item(enum_array, i);
		if (item) {
			// Create a deep copy of the enum value in our pool
			constraint->enum_values[constraint->enum_count] = ks_json_duplicate(item, KS_TRUE);
			if (constraint->enum_values[constraint->enum_count]) {
				constraint->enum_count++;
			}
		}
	}

	return node;
}

static ks_json_schema_node_t *compile_const_schema(ks_pool_t *pool, ks_json_t *schema)
{
	ks_json_schema_node_t *node = create_schema_node(pool, KS_SCHEMA_NODE_CONST);
	if (!node) return NULL;

	ks_schema_const_constraint_t *constraint = &node->constraint.const_constraint;

	ks_json_t *const_value = ks_json_get_object_item(schema, "const");
	if (!const_value) {
		return NULL; // Invalid const schema
	}

	// Create a deep copy of the const value
	constraint->const_value = ks_json_duplicate(const_value, KS_TRUE);
	if (!constraint->const_value) {
		return NULL;
	}

	return node;
}

static ks_json_schema_node_t *compile_ref_schema(ks_pool_t *pool, ks_json_t *schema, ks_hash_t *ref_cache, ks_json_t *root_schema)
{
	ks_json_schema_node_t *node = create_schema_node(pool, KS_SCHEMA_NODE_REF);
	if (!node) return NULL;

	ks_schema_ref_constraint_t *constraint = &node->constraint.ref_constraint;

	ks_json_t *ref = ks_json_get_object_item(schema, "$ref");
	if (!ref || !ks_json_type_is_string(ref)) {
		return NULL; // Invalid $ref
	}

	const char *ref_str = ks_json_get_string(ref, "");
	if (!ref_str || strlen(ref_str) == 0) {
		return NULL; // Empty $ref
	}

	// Store the reference URI
	size_t ref_len = strlen(ref_str);
	constraint->ref_uri = ks_pool_alloc(pool, ref_len + 1);
	if (!constraint->ref_uri) return NULL;
	strcpy(constraint->ref_uri, ref_str);

	// Store the reference for later resolution
	constraint->resolved_node = NULL;

	return node;
}

static ks_json_schema_node_t *compile_allof_schema(ks_pool_t *pool, ks_json_t *schema, ks_json_t *root_schema, ks_hash_t *ref_cache)
{
	ks_json_schema_node_t *node = create_schema_node(pool, KS_SCHEMA_NODE_ALLOF);
	if (!node) return NULL;

	ks_schema_combination_constraint_t *constraint = &node->constraint.combination_constraint;

	ks_json_t *allof_array = ks_json_get_object_item(schema, "allOf");
	if (!allof_array || !ks_json_type_is_array(allof_array)) {
		return NULL; // Invalid allOf schema
	}

	int array_size = ks_json_get_array_size(allof_array);
	if (array_size == 0) {
		return NULL; // Empty allOf is invalid
	}

	constraint->schemas = ks_pool_alloc(pool, array_size * sizeof(ks_json_schema_node_t*));
	if (!constraint->schemas) return NULL;

	constraint->schema_count = 0;
	for (int i = 0; i < array_size; i++) {
		ks_json_t *sub_schema = ks_json_get_array_item(allof_array, i);
		if (sub_schema) {
			ks_json_schema_node_t *sub_node = compile_schema_internal(pool, sub_schema, root_schema, ref_cache, NULL);
			if (sub_node) {
				constraint->schemas[constraint->schema_count] = sub_node;
				constraint->schema_count++;
			}
		}
	}

	return node;
}

static ks_json_schema_node_t *compile_anyof_schema(ks_pool_t *pool, ks_json_t *schema, ks_json_t *root_schema, ks_hash_t *ref_cache)
{
	ks_json_schema_node_t *node = create_schema_node(pool, KS_SCHEMA_NODE_ANYOF);
	if (!node) return NULL;

	ks_schema_combination_constraint_t *constraint = &node->constraint.combination_constraint;

	ks_json_t *anyof_array = ks_json_get_object_item(schema, "anyOf");
	if (!anyof_array || !ks_json_type_is_array(anyof_array)) {
		return NULL; // Invalid anyOf schema
	}

	int array_size = ks_json_get_array_size(anyof_array);
	if (array_size == 0) {
		return NULL; // Empty anyOf is invalid
	}

	constraint->schemas = ks_pool_alloc(pool, array_size * sizeof(ks_json_schema_node_t*));
	if (!constraint->schemas) return NULL;

	constraint->schema_count = 0;
	for (int i = 0; i < array_size; i++) {
		ks_json_t *sub_schema = ks_json_get_array_item(anyof_array, i);
		if (sub_schema) {
			ks_json_schema_node_t *sub_node = compile_schema_internal(pool, sub_schema, root_schema, ref_cache, NULL);
			if (sub_node) {
				constraint->schemas[constraint->schema_count] = sub_node;
				constraint->schema_count++;
			}
		}
	}

	return node;
}

static ks_json_schema_node_t *compile_oneof_schema(ks_pool_t *pool, ks_json_t *schema, ks_json_t *root_schema, ks_hash_t *ref_cache)
{
	ks_json_schema_node_t *node = create_schema_node(pool, KS_SCHEMA_NODE_ONEOF);
	if (!node) return NULL;

	ks_schema_combination_constraint_t *constraint = &node->constraint.combination_constraint;

	ks_json_t *oneof_array = ks_json_get_object_item(schema, "oneOf");
	if (!oneof_array || !ks_json_type_is_array(oneof_array)) {
		return NULL; // Invalid oneOf schema
	}

	int array_size = ks_json_get_array_size(oneof_array);
	if (array_size == 0) {
		return NULL; // Empty oneOf is invalid
	}

	constraint->schemas = ks_pool_alloc(pool, array_size * sizeof(ks_json_schema_node_t*));
	if (!constraint->schemas) return NULL;

	constraint->schema_count = 0;
	for (int i = 0; i < array_size; i++) {
		ks_json_t *sub_schema = ks_json_get_array_item(oneof_array, i);
		if (sub_schema) {
			ks_json_schema_node_t *sub_node = compile_schema_internal(pool, sub_schema, root_schema, ref_cache, NULL);
			if (sub_node) {
				constraint->schemas[constraint->schema_count] = sub_node;
				constraint->schema_count++;
			}
		}
	}

	return node;
}

static ks_json_schema_node_t *compile_not_schema(ks_pool_t *pool, ks_json_t *schema, ks_json_t *root_schema, ks_hash_t *ref_cache)
{
	ks_json_schema_node_t *node = create_schema_node(pool, KS_SCHEMA_NODE_NOT);
	if (!node) return NULL;

	ks_schema_not_constraint_t *constraint = &node->constraint.not_constraint;

	ks_json_t *not_schema = ks_json_get_object_item(schema, "not");
	if (!not_schema) {
		return NULL; // Invalid not schema
	}

	constraint->schema = compile_schema_internal(pool, not_schema, root_schema, ref_cache, NULL);
	if (!constraint->schema) {
		return NULL; // Failed to compile not schema
	}

	return node;
}

static ks_json_schema_node_t *compile_conditional_schema(ks_pool_t *pool, ks_json_t *schema, ks_json_t *root_schema, ks_hash_t *ref_cache)
{
	ks_json_schema_node_t *node = create_schema_node(pool, KS_SCHEMA_NODE_IF_THEN_ELSE);
	if (!node) return NULL;

	ks_schema_conditional_constraint_t *constraint = &node->constraint.conditional_constraint;

	// Initialize all schemas to NULL
	constraint->if_schema = NULL;
	constraint->then_schema = NULL;
	constraint->else_schema = NULL;

	// Compile the if schema (required)
	ks_json_t *if_schema = ks_json_get_object_item(schema, "if");
	if (!if_schema) {
		return NULL; // Invalid conditional schema - must have "if"
	}

	constraint->if_schema = compile_schema_internal(pool, if_schema, root_schema, ref_cache, NULL);
	if (!constraint->if_schema) {
		return NULL; // Failed to compile if schema
	}

	// Compile the then schema (optional)
	ks_json_t *then_schema = ks_json_get_object_item(schema, "then");
	if (then_schema) {
		constraint->then_schema = compile_schema_internal(pool, then_schema, root_schema, ref_cache, NULL);
		// Don't fail if then schema compilation fails - just leave it NULL
	}

	// Compile the else schema (optional)
	ks_json_t *else_schema = ks_json_get_object_item(schema, "else");
	if (else_schema) {
		constraint->else_schema = compile_schema_internal(pool, else_schema, root_schema, ref_cache, NULL);
		// Don't fail if else schema compilation fails - just leave it NULL
	}

	return node;
}

// Forward declarations for recursive compilation
static ks_json_schema_node_t *compile_schema_internal(ks_pool_t *pool, ks_json_t *schema, ks_json_t *root_schema, ks_hash_t *ref_cache, ks_json_schema_pure_error_t **errors);
static ks_json_schema_node_t *resolve_reference(ks_pool_t *pool, const char *ref_uri, ks_json_t *root_schema, ks_hash_t *ref_cache);
static ks_bool_t validate_schema_node(ks_json_schema_node_t *node, ks_json_t *instance, ks_json_validation_context_t *ctx);

static ks_json_schema_node_t *compile_schema(ks_pool_t *pool, ks_json_t *schema, ks_hash_t *ref_cache, ks_json_schema_pure_error_t **errors)
{
	if (!schema) return NULL;

	// Handle boolean schemas (true/false)
	if (ks_json_type_is_bool(schema)) {
		return compile_boolean_schema(pool, ks_json_get_bool(schema, KS_FALSE));
	}

	if (!ks_json_type_is_object(schema)) {
		if (errors) {
			*errors = create_error("Schema must be an object or boolean", "");
		}
		return NULL;
	}

	return compile_schema_internal(pool, schema, schema, ref_cache, errors);
}

static ks_json_schema_node_t *compile_schema_internal(ks_pool_t *pool, ks_json_t *schema, ks_json_t *root_schema, ks_hash_t *ref_cache, ks_json_schema_pure_error_t **errors)
{
	ks_json_schema_node_t *primary_node = NULL;

	// Check for $ref first
	ks_json_t *ref = ks_json_get_object_item(schema, "$ref");
	if (ref && ks_json_type_is_string(ref)) {
		primary_node = compile_ref_schema(pool, schema, ref_cache, root_schema);
		if (!primary_node && errors) {
			*errors = create_error("Failed to resolve $ref", "");
		}
		return primary_node;
	}

	// Check for specific schema types and create appropriate node
	if (ks_json_get_object_item(schema, "allOf")) {
		primary_node = compile_allof_schema(pool, schema, root_schema, ref_cache);
	} else if (ks_json_get_object_item(schema, "anyOf")) {
		primary_node = compile_anyof_schema(pool, schema, root_schema, ref_cache);
	} else if (ks_json_get_object_item(schema, "oneOf")) {
		primary_node = compile_oneof_schema(pool, schema, root_schema, ref_cache);
	} else if (ks_json_get_object_item(schema, "not")) {
		primary_node = compile_not_schema(pool, schema, root_schema, ref_cache);
	} else if (ks_json_get_object_item(schema, "if")) {
		primary_node = compile_conditional_schema(pool, schema, root_schema, ref_cache);
	} else if (ks_json_get_object_item(schema, "enum")) {
		primary_node = compile_enum_schema(pool, schema);
	} else if (ks_json_get_object_item(schema, "const")) {
		primary_node = compile_const_schema(pool, schema);
	} else if (ks_json_get_object_item(schema, "type")) {
		ks_json_t *type_value = ks_json_get_object_item(schema, "type");
		const char *type_str = NULL;

		if (ks_json_type_is_string(type_value)) {
			type_str = ks_json_get_string(type_value, "");
		}

		if (type_str) {
			if (strcmp(type_str, "object") == 0) {
				primary_node = compile_object_schema(pool, schema, root_schema, ref_cache);
			} else if (strcmp(type_str, "array") == 0) {
				primary_node = compile_array_schema(pool, schema);
			} else if (strcmp(type_str, "string") == 0) {
				primary_node = compile_string_schema(pool, schema);
			} else if (strcmp(type_str, "number") == 0 || strcmp(type_str, "integer") == 0) {
				primary_node = compile_number_schema(pool, schema);
			} else {
				primary_node = compile_type_schema(pool, schema);
			}
		} else {
			primary_node = compile_type_schema(pool, schema);
		}
	} else {
		// No specific type constraint - check for other constraints
		if (ks_json_get_object_item(schema, "properties") ||
		    ks_json_get_object_item(schema, "required") ||
		    ks_json_get_object_item(schema, "minProperties") ||
		    ks_json_get_object_item(schema, "maxProperties")) {
			primary_node = compile_object_schema(pool, schema, root_schema, ref_cache);
		} else if (ks_json_get_object_item(schema, "items") ||
		           ks_json_get_object_item(schema, "minItems") ||
		           ks_json_get_object_item(schema, "maxItems") ||
		           ks_json_get_object_item(schema, "uniqueItems")) {
			primary_node = compile_array_schema(pool, schema);
		} else if (ks_json_get_object_item(schema, "minLength") ||
		           ks_json_get_object_item(schema, "maxLength") ||
		           ks_json_get_object_item(schema, "pattern") ||
		           ks_json_get_object_item(schema, "format")) {
			primary_node = compile_string_schema(pool, schema);
		} else if (ks_json_get_object_item(schema, "minimum") ||
		           ks_json_get_object_item(schema, "maximum") ||
		           ks_json_get_object_item(schema, "exclusiveMinimum") ||
		           ks_json_get_object_item(schema, "exclusiveMaximum") ||
		           ks_json_get_object_item(schema, "multipleOf")) {
			primary_node = compile_number_schema(pool, schema);
		} else {
			// Generic schema - allow any type
			primary_node = compile_type_schema(pool, schema);
		}
	}

	// TODO: Handle logical combinations (allOf, anyOf, oneOf, not)
	// TODO: Handle conditional schemas (if/then/else)

	return primary_node;
}

static ks_json_schema_node_t *resolve_reference(ks_pool_t *pool, const char *ref_uri, ks_json_t *root_schema, ks_hash_t *ref_cache)
{
	if (!ref_uri || !root_schema) return NULL;

	// Check cache first
	if (ref_cache) {
		ks_json_schema_node_t *cached = ks_hash_search(ref_cache, ref_uri, KS_UNLOCKED);
		if (cached) {
			return cached;
		}
	}

	ks_json_t *resolved_schema = NULL;

	// Handle JSON Pointer references (starting with #/)
	if (ref_uri[0] == '#' && ref_uri[1] == '/') {
		// Use JSON Pointer to resolve within the document
		const char *pointer = ref_uri + 1; // Skip the '#'
		resolved_schema = ks_json_pointer_get_item(root_schema, pointer);
	}
	// Handle fragment-only references (just #)
	else if (strcmp(ref_uri, "#") == 0) {
		resolved_schema = root_schema;
	}
	// TODO: Handle absolute URIs and other reference types
	else {
		// For now, unsupported reference types return NULL
		return NULL;
	}

	if (!resolved_schema) {
		return NULL; // Reference not found
	}

	// Compile the resolved schema
	ks_json_schema_node_t *resolved_node = compile_schema_internal(pool, resolved_schema, root_schema, ref_cache, NULL);

	// Cache the result
	if (resolved_node && ref_cache) {
		ks_hash_insert(ref_cache, ref_uri, resolved_node);
	}

	return resolved_node;
}

//
// Public API Implementation
//

KS_DECLARE(const char *) ks_json_schema_pure_status_string(ks_json_schema_pure_status_t status)
{
	switch (status) {
		case KS_JSON_SCHEMA_PURE_STATUS_SUCCESS:
			return "Success";
		case KS_JSON_SCHEMA_PURE_STATUS_INVALID_SCHEMA:
			return "Invalid schema";
		case KS_JSON_SCHEMA_PURE_STATUS_INVALID_JSON:
			return "Invalid JSON";
		case KS_JSON_SCHEMA_PURE_STATUS_VALIDATION_FAILED:
			return "Validation failed";
		case KS_JSON_SCHEMA_PURE_STATUS_MEMORY_ERROR:
			return "Memory error";
		case KS_JSON_SCHEMA_PURE_STATUS_INVALID_PARAM:
			return "Invalid parameter";
		default:
			return "Unknown error";
	}
}

KS_DECLARE(ks_json_schema_pure_status_t) ks_json_schema_pure_create_from_json(
	ks_json_t *schema_json,
	ks_json_schema_validator_t **validator,
	ks_json_schema_pure_error_t **errors)
{
	if (!schema_json || !validator) {
		return KS_JSON_SCHEMA_PURE_STATUS_INVALID_PARAM;
	}

	*validator = NULL;
	if (errors) *errors = NULL;

	// Create validator structure
	ks_json_schema_validator_t *val = malloc(sizeof(ks_json_schema_validator_t));
	if (!val) {
		if (errors) {
			*errors = create_error("Failed to allocate validator", "");
		}
		return KS_JSON_SCHEMA_PURE_STATUS_MEMORY_ERROR;
	}

	// Initialize validator
	val->root_node = NULL;
	val->root_schema = NULL;
	val->pool = NULL;
	val->ref_cache = NULL;
	val->format_checker = ks_json_schema_pure_default_format_checker;

	// Create memory pool for schema compilation
	if (ks_pool_open(&val->pool) != KS_STATUS_SUCCESS) {
		free(val);
		if (errors) {
			*errors = create_error("Failed to create memory pool", "");
		}
		return KS_JSON_SCHEMA_PURE_STATUS_MEMORY_ERROR;
	}

	// Create reference cache
	if (ks_hash_create(&val->ref_cache, KS_HASH_MODE_CASE_INSENSITIVE, 0, val->pool) != KS_STATUS_SUCCESS) {
		ks_pool_close(&val->pool);
		free(val);
		if (errors) {
			*errors = create_error("Failed to create reference cache", "");
		}
		return KS_JSON_SCHEMA_PURE_STATUS_MEMORY_ERROR;
	}

	// Store the root schema for $ref resolution
	val->root_schema = ks_json_duplicate(schema_json, KS_TRUE);
	if (!val->root_schema) {
		ks_json_schema_pure_destroy(&val);
		if (errors) {
			*errors = create_error("Failed to store root schema", "");
		}
		return KS_JSON_SCHEMA_PURE_STATUS_MEMORY_ERROR;
	}

	// Compile the schema
	ks_json_schema_pure_error_t *compile_errors = NULL;
	val->root_node = compile_schema(val->pool, schema_json, val->ref_cache, &compile_errors);

	if (!val->root_node) {
		ks_json_schema_pure_destroy(&val);
		if (errors) {
			*errors = compile_errors;
		} else if (compile_errors) {
			ks_json_schema_pure_error_free(&compile_errors);
		}
		return KS_JSON_SCHEMA_PURE_STATUS_INVALID_SCHEMA;
	}

	*validator = val;
	return KS_JSON_SCHEMA_PURE_STATUS_SUCCESS;
}

KS_DECLARE(ks_json_schema_pure_status_t) ks_json_schema_pure_create(
	const char *schema_json,
	ks_json_schema_validator_t **validator,
	ks_json_schema_pure_error_t **errors)
{
	if (!schema_json || !validator) {
		return KS_JSON_SCHEMA_PURE_STATUS_INVALID_PARAM;
	}

	ks_json_t *parsed_schema = ks_json_parse(schema_json);
	if (!parsed_schema) {
		if (errors) {
			*errors = create_error("Failed to parse schema JSON", "");
		}
		return KS_JSON_SCHEMA_PURE_STATUS_INVALID_SCHEMA;
	}

	ks_json_schema_pure_status_t result = ks_json_schema_pure_create_from_json(parsed_schema, validator, errors);

	ks_json_delete(&parsed_schema);
	return result;
}

KS_DECLARE(void) ks_json_schema_pure_destroy(ks_json_schema_validator_t **validator)
{
	if (!validator || !*validator) return;

	ks_json_schema_validator_t *val = *validator;

	if (val->root_schema) {
		ks_json_delete(&val->root_schema);
	}

	if (val->ref_cache) {
		ks_hash_destroy(&val->ref_cache);
	}

	if (val->pool) {
		ks_pool_close(&val->pool);
	}

	free(val);
	*validator = NULL;
}

KS_DECLARE(void) ks_json_schema_pure_error_free(ks_json_schema_pure_error_t **errors)
{
	if (!errors || !*errors) return;

	ks_json_schema_pure_error_t *current = *errors;
	while (current) {
		ks_json_schema_pure_error_t *next = current->next;
		free(current->message);
		free(current->path);
		free(current);
		current = next;
	}

	*errors = NULL;
}

KS_DECLARE(void) ks_json_schema_pure_set_format_checker(
	ks_json_schema_validator_t *validator,
	ks_json_schema_format_checker_t format_checker)
{
	if (validator) {
		validator->format_checker = format_checker ? format_checker : ks_json_schema_pure_default_format_checker;
	}
}

//
// Validation Functions
//

static ks_bool_t json_values_equal(ks_json_t *a, ks_json_t *b)
{
	if (!a && !b) return KS_TRUE;
	if (!a || !b) return KS_FALSE;

	ks_json_type_t type_a = ks_json_type_get(a);
	ks_json_type_t type_b = ks_json_type_get(b);

	if (type_a != type_b) return KS_FALSE;

	switch (type_a) {
		case KS_JSON_TYPE_NULL:
			return KS_TRUE;
		case KS_JSON_TYPE_TRUE:
		case KS_JSON_TYPE_FALSE:
			return KS_TRUE; // Both are same boolean type
		case KS_JSON_TYPE_NUMBER:
			return ks_json_get_number_double(a, 0.0) == ks_json_get_number_double(b, 0.0);
		case KS_JSON_TYPE_STRING: {
			const char *str_a = ks_json_get_string(a, "");
			const char *str_b = ks_json_get_string(b, "");
			return strcmp(str_a, str_b) == 0;
		}
		case KS_JSON_TYPE_ARRAY: {
			int size_a = ks_json_get_array_size(a);
			int size_b = ks_json_get_array_size(b);
			if (size_a != size_b) return KS_FALSE;

			for (int i = 0; i < size_a; i++) {
				ks_json_t *item_a = ks_json_get_array_item(a, i);
				ks_json_t *item_b = ks_json_get_array_item(b, i);
				if (!json_values_equal(item_a, item_b)) return KS_FALSE;
			}
			return KS_TRUE;
		}
		case KS_JSON_TYPE_OBJECT: {
			// Compare all properties in both objects
			ks_json_t *child_a = ks_json_enum_child(a);
			while (child_a) {
				const char *key = ks_json_get_name(child_a);
				ks_json_t *value_b = ks_json_get_object_item(b, key);
				if (!json_values_equal(child_a, value_b)) return KS_FALSE;
				child_a = ks_json_enum_next(child_a);
			}

			ks_json_t *child_b = ks_json_enum_child(b);
			while (child_b) {
				const char *key = ks_json_get_name(child_b);
				ks_json_t *value_a = ks_json_get_object_item(a, key);
				if (!json_values_equal(value_a, child_b)) return KS_FALSE;
				child_b = ks_json_enum_next(child_b);
			}
			return KS_TRUE;
		}
		default:
			return KS_FALSE;
	}
}

static ks_bool_t validate_type_constraint(ks_json_t *instance, ks_schema_type_constraint_t *constraint)
{
	uint32_t instance_type_mask = json_type_to_schema_mask(ks_json_type_get(instance));
	return (constraint->allowed_types & instance_type_mask) != 0;
}

static ks_bool_t validate_object_constraint(ks_json_t *instance, ks_schema_object_constraint_t *constraint, ks_json_validation_context_t *ctx)
{
	if (!ks_json_type_is_object(instance)) return KS_FALSE;

	// Count properties
	int property_count = 0;
	ks_json_t *child = ks_json_enum_child(instance);
	while (child) {
		property_count++;
		child = ks_json_enum_next(child);
	}

	// Check minProperties
	if (constraint->has_min_properties && property_count < constraint->min_properties) {
		if (ctx && ctx->errors && ctx->error_count < ctx->max_errors) {
			char message[256];
			snprintf(message, sizeof(message), "Object has %d properties, minimum is %d",
			         property_count, constraint->min_properties);
			ks_json_schema_pure_error_t *error = create_error(message, ctx->instance_path);
			if (error) {
				add_error_to_list(ctx->errors, error);
				ctx->error_count++;
			}
		}
		return KS_FALSE;
	}

	// Check maxProperties
	if (constraint->has_max_properties && property_count > constraint->max_properties) {
		if (ctx && ctx->errors && ctx->error_count < ctx->max_errors) {
			char message[256];
			snprintf(message, sizeof(message), "Object has %d properties, maximum is %d",
			         property_count, constraint->max_properties);
			ks_json_schema_pure_error_t *error = create_error(message, ctx->instance_path);
			if (error) {
				add_error_to_list(ctx->errors, error);
				ctx->error_count++;
			}
		}
		return KS_FALSE;
	}

	// Check required properties
	for (size_t i = 0; i < constraint->required_count; i++) {
		const char *required_prop = constraint->required_properties[i];
		ks_json_t *prop_value = ks_json_get_object_item(instance, required_prop);
		if (!prop_value) {
			if (ctx && ctx->errors && ctx->error_count < ctx->max_errors) {
				char message[256];
				snprintf(message, sizeof(message), "Missing required property '%s'", required_prop);
				ks_json_schema_pure_error_t *error = create_error(message, ctx->instance_path);
				if (error) {
					add_error_to_list(ctx->errors, error);
					ctx->error_count++;
				}
			}
			return KS_FALSE;
		}
	}

	// Validate properties against their schemas
	if (constraint->properties) {
		ks_json_t *prop = ks_json_enum_child(instance);
		while (prop) {
			const char *prop_name = ks_json_get_name(prop);
			if (prop_name) {
				// Look up the schema for this property
				ks_json_schema_node_t *prop_schema = ks_hash_search(constraint->properties, prop_name, KS_UNLOCKED);
				if (prop_schema) {
					// Validate the property value against its schema
					if (!validate_schema_node(prop_schema, prop, ctx)) {
						return KS_FALSE;
					}
				}
				// Note: If no schema is found for the property, we allow it (like additionalProperties: true)
			}
			prop = ks_json_enum_next(prop);
		}
	}

	return KS_TRUE;
}

static ks_bool_t validate_array_constraint(ks_json_t *instance, ks_schema_array_constraint_t *constraint, ks_json_validation_context_t *ctx)
{
	if (!ks_json_type_is_array(instance)) return KS_FALSE;

	int array_size = ks_json_get_array_size(instance);

	// Check minItems
	if (constraint->has_min_items && array_size < constraint->min_items) {
		if (ctx && ctx->errors && ctx->error_count < ctx->max_errors) {
			char message[256];
			snprintf(message, sizeof(message), "Array has %d items, minimum is %d",
			         array_size, constraint->min_items);
			ks_json_schema_pure_error_t *error = create_error(message, ctx->instance_path);
			if (error) {
				add_error_to_list(ctx->errors, error);
				ctx->error_count++;
			}
		}
		return KS_FALSE;
	}

	// Check maxItems
	if (constraint->has_max_items && array_size > constraint->max_items) {
		if (ctx && ctx->errors && ctx->error_count < ctx->max_errors) {
			char message[256];
			snprintf(message, sizeof(message), "Array has %d items, maximum is %d",
			         array_size, constraint->max_items);
			ks_json_schema_pure_error_t *error = create_error(message, ctx->instance_path);
			if (error) {
				add_error_to_list(ctx->errors, error);
				ctx->error_count++;
			}
		}
		return KS_FALSE;
	}

	// Check uniqueItems
	if (constraint->unique_items) {
		for (int i = 0; i < array_size; i++) {
			ks_json_t *item_i = ks_json_get_array_item(instance, i);
			for (int j = i + 1; j < array_size; j++) {
				ks_json_t *item_j = ks_json_get_array_item(instance, j);
				if (json_values_equal(item_i, item_j)) {
					if (ctx && ctx->errors && ctx->error_count < ctx->max_errors) {
						char message[256];
						snprintf(message, sizeof(message), "Array items at indices %d and %d are not unique", i, j);
						ks_json_schema_pure_error_t *error = create_error(message, ctx->instance_path);
						if (error) {
							add_error_to_list(ctx->errors, error);
							ctx->error_count++;
						}
					}
					return KS_FALSE;
				}
			}
		}
	}

	return KS_TRUE;
}

static ks_bool_t validate_string_constraint(ks_json_t *instance, ks_schema_string_constraint_t *constraint, ks_json_validation_context_t *ctx)
{
	if (!ks_json_type_is_string(instance)) {
		if (ctx && ctx->errors && ctx->error_count < ctx->max_errors) {
			ks_json_schema_pure_error_t *error = create_error("Value is not a string", ctx->instance_path);
			if (error) {
				add_error_to_list(ctx->errors, error);
				ctx->error_count++;
			}
		}
		return KS_FALSE;
	}

	const char *str_value = ks_json_get_string(instance, "");
	int str_length = strlen(str_value);

	// Check minLength
	if (constraint->has_min_length && str_length < constraint->min_length) {
		if (ctx && ctx->errors && ctx->error_count < ctx->max_errors) {
			char message[256];
			snprintf(message, sizeof(message), "String length %d is less than minimum %d",
			         str_length, constraint->min_length);
			ks_json_schema_pure_error_t *error = create_error(message, ctx->instance_path);
			if (error) {
				add_error_to_list(ctx->errors, error);
				ctx->error_count++;
			}
		}
		return KS_FALSE;
	}

	// Check maxLength
	if (constraint->has_max_length && str_length > constraint->max_length) {
		if (ctx && ctx->errors && ctx->error_count < ctx->max_errors) {
			char message[256];
			snprintf(message, sizeof(message), "String length %d is greater than maximum %d",
			         str_length, constraint->max_length);
			ks_json_schema_pure_error_t *error = create_error(message, ctx->instance_path);
			if (error) {
				add_error_to_list(ctx->errors, error);
				ctx->error_count++;
			}
		}
		return KS_FALSE;
	}

	// Check pattern
	if (constraint->pattern) {
		regex_t regex;
		int regex_result = regcomp(&regex, constraint->pattern, REG_EXTENDED);
		if (regex_result == 0) {
			regex_result = regexec(&regex, str_value, 0, NULL, 0);
			regfree(&regex);

			if (regex_result == REG_NOMATCH) {
				if (ctx && ctx->errors && ctx->error_count < ctx->max_errors) {
					char message[512];
					snprintf(message, sizeof(message), "String does not match pattern '%s'", constraint->pattern);
					ks_json_schema_pure_error_t *error = create_error(message, ctx->instance_path);
					if (error) {
						add_error_to_list(ctx->errors, error);
						ctx->error_count++;
					}
				}
				return KS_FALSE;
			}
		}
	}

	// Check format
	if (constraint->format && ctx && ctx->format_checker) {
		if (!ctx->format_checker(constraint->format, str_value)) {
			if (ctx->errors && ctx->error_count < ctx->max_errors) {
				char message[256];
				snprintf(message, sizeof(message), "String does not match format '%s'", constraint->format);
				ks_json_schema_pure_error_t *error = create_error(message, ctx->instance_path);
				if (error) {
					add_error_to_list(ctx->errors, error);
					ctx->error_count++;
				}
			}
			return KS_FALSE;
		}
	}

	return KS_TRUE;
}

static ks_bool_t validate_number_constraint(ks_json_t *instance, ks_schema_number_constraint_t *constraint, ks_json_validation_context_t *ctx)
{
	if (!ks_json_type_is_number(instance)) return KS_FALSE;

	double num_value = ks_json_get_number_double(instance, 0.0);

	// Check minimum
	if (constraint->has_minimum) {
		if (constraint->exclusive_minimum) {
			if (num_value <= constraint->minimum) {
				if (ctx && ctx->errors && ctx->error_count < ctx->max_errors) {
					char message[256];
					snprintf(message, sizeof(message), "Number %g is not greater than exclusive minimum %g",
					         num_value, constraint->minimum);
					ks_json_schema_pure_error_t *error = create_error(message, ctx->instance_path);
					if (error) {
						add_error_to_list(ctx->errors, error);
						ctx->error_count++;
					}
				}
				return KS_FALSE;
			}
		} else {
			if (num_value < constraint->minimum) {
				if (ctx && ctx->errors && ctx->error_count < ctx->max_errors) {
					char message[256];
					snprintf(message, sizeof(message), "Number %g is less than minimum %g",
					         num_value, constraint->minimum);
					ks_json_schema_pure_error_t *error = create_error(message, ctx->instance_path);
					if (error) {
						add_error_to_list(ctx->errors, error);
						ctx->error_count++;
					}
				}
				return KS_FALSE;
			}
		}
	}

	// Check maximum
	if (constraint->has_maximum) {
		if (constraint->exclusive_maximum) {
			if (num_value >= constraint->maximum) {
				if (ctx && ctx->errors && ctx->error_count < ctx->max_errors) {
					char message[256];
					snprintf(message, sizeof(message), "Number %g is not less than exclusive maximum %g",
					         num_value, constraint->maximum);
					ks_json_schema_pure_error_t *error = create_error(message, ctx->instance_path);
					if (error) {
						add_error_to_list(ctx->errors, error);
						ctx->error_count++;
					}
				}
				return KS_FALSE;
			}
		} else {
			if (num_value > constraint->maximum) {
				if (ctx && ctx->errors && ctx->error_count < ctx->max_errors) {
					char message[256];
					snprintf(message, sizeof(message), "Number %g is greater than maximum %g",
					         num_value, constraint->maximum);
					ks_json_schema_pure_error_t *error = create_error(message, ctx->instance_path);
					if (error) {
						add_error_to_list(ctx->errors, error);
						ctx->error_count++;
					}
				}
				return KS_FALSE;
			}
		}
	}

	// Check multipleOf
	if (constraint->has_multiple_of && constraint->multiple_of > 0.0) {
		double quotient = num_value / constraint->multiple_of;
		if (fabs(quotient - round(quotient)) > 1e-10) {
			if (ctx && ctx->errors && ctx->error_count < ctx->max_errors) {
				char message[256];
				snprintf(message, sizeof(message), "Number %g is not a multiple of %g",
				         num_value, constraint->multiple_of);
				ks_json_schema_pure_error_t *error = create_error(message, ctx->instance_path);
				if (error) {
					add_error_to_list(ctx->errors, error);
					ctx->error_count++;
				}
			}
			return KS_FALSE;
		}
	}

	return KS_TRUE;
}

static ks_bool_t validate_enum_constraint(ks_json_t *instance, ks_schema_enum_constraint_t *constraint, ks_json_validation_context_t *ctx)
{
	for (size_t i = 0; i < constraint->enum_count; i++) {
		if (json_values_equal(instance, constraint->enum_values[i])) {
			return KS_TRUE;
		}
	}

	if (ctx && ctx->errors && ctx->error_count < ctx->max_errors) {
		ks_json_schema_pure_error_t *error = create_error("Value is not in the allowed enum list", ctx->instance_path);
		if (error) {
			add_error_to_list(ctx->errors, error);
			ctx->error_count++;
		}
	}

	return KS_FALSE;
}

static ks_bool_t validate_const_constraint(ks_json_t *instance, ks_schema_const_constraint_t *constraint, ks_json_validation_context_t *ctx)
{
	if (json_values_equal(instance, constraint->const_value)) {
		return KS_TRUE;
	}

	if (ctx && ctx->errors && ctx->error_count < ctx->max_errors) {
		ks_json_schema_pure_error_t *error = create_error("Value does not match const constraint", ctx->instance_path);
		if (error) {
			add_error_to_list(ctx->errors, error);
			ctx->error_count++;
		}
	}

	return KS_FALSE;
}

static ks_bool_t validate_allof_constraint(ks_json_t *instance, ks_schema_combination_constraint_t *constraint, ks_json_validation_context_t *ctx)
{
	// All schemas must pass validation
	for (size_t i = 0; i < constraint->schema_count; i++) {
		if (!validate_schema_node(constraint->schemas[i], instance, ctx)) {
			if (ctx && ctx->errors && ctx->error_count < ctx->max_errors) {
				char message[256];
				snprintf(message, sizeof(message), "allOf validation failed on schema %zu", i);
				ks_json_schema_pure_error_t *error = create_error(message, ctx->instance_path);
				if (error) {
					add_error_to_list(ctx->errors, error);
					ctx->error_count++;
				}
			}
			return KS_FALSE;
		}
	}
	return KS_TRUE;
}

static ks_bool_t validate_anyof_constraint(ks_json_t *instance, ks_schema_combination_constraint_t *constraint, ks_json_validation_context_t *ctx)
{
	// At least one schema must pass validation
	for (size_t i = 0; i < constraint->schema_count; i++) {
		// Create a temporary context to avoid polluting errors if this schema fails
		ks_json_validation_context_t temp_ctx = *ctx;
		temp_ctx.errors = NULL;
		temp_ctx.error_count = 0;

		if (validate_schema_node(constraint->schemas[i], instance, &temp_ctx)) {
			return KS_TRUE; // Found a matching schema
		}
	}

	// None of the schemas passed
	if (ctx && ctx->errors && ctx->error_count < ctx->max_errors) {
		ks_json_schema_pure_error_t *error = create_error("anyOf validation failed - no schemas matched", ctx->instance_path);
		if (error) {
			add_error_to_list(ctx->errors, error);
			ctx->error_count++;
		}
	}
	return KS_FALSE;
}

static ks_bool_t validate_oneof_constraint(ks_json_t *instance, ks_schema_combination_constraint_t *constraint, ks_json_validation_context_t *ctx)
{
	// Exactly one schema must pass validation
	int passed_count = 0;

	for (size_t i = 0; i < constraint->schema_count; i++) {
		// Create a temporary context to avoid polluting errors
		ks_json_validation_context_t temp_ctx = *ctx;
		temp_ctx.errors = NULL;
		temp_ctx.error_count = 0;

		if (validate_schema_node(constraint->schemas[i], instance, &temp_ctx)) {
			passed_count++;
		}
	}

	if (passed_count == 1) {
		return KS_TRUE;
	}

	// Either no schemas or multiple schemas passed
	if (ctx && ctx->errors && ctx->error_count < ctx->max_errors) {
		char message[256];
		if (passed_count == 0) {
			snprintf(message, sizeof(message), "oneOf validation failed - no schemas matched");
		} else {
			snprintf(message, sizeof(message), "oneOf validation failed - %d schemas matched (expected exactly 1)", passed_count);
		}
		ks_json_schema_pure_error_t *error = create_error(message, ctx->instance_path);
		if (error) {
			add_error_to_list(ctx->errors, error);
			ctx->error_count++;
		}
	}
	return KS_FALSE;
}

static ks_bool_t validate_not_constraint(ks_json_t *instance, ks_schema_not_constraint_t *constraint, ks_json_validation_context_t *ctx)
{
	// Create a temporary context to avoid polluting errors if the schema passes
	ks_json_validation_context_t temp_ctx = *ctx;
	temp_ctx.errors = NULL;
	temp_ctx.error_count = 0;

	if (validate_schema_node(constraint->schema, instance, &temp_ctx)) {
		// Schema passed validation, so 'not' fails
		if (ctx && ctx->errors && ctx->error_count < ctx->max_errors) {
			ks_json_schema_pure_error_t *error = create_error("not validation failed - schema should not match", ctx->instance_path);
			if (error) {
				add_error_to_list(ctx->errors, error);
				ctx->error_count++;
			}
		}
		return KS_FALSE;
	}

	// Schema failed validation, so 'not' passes
	return KS_TRUE;
}

static ks_bool_t validate_conditional_constraint(ks_json_t *instance, ks_schema_conditional_constraint_t *constraint, ks_json_validation_context_t *ctx)
{
	if (!constraint->if_schema) {
		// No if schema should not happen, but handle gracefully
		return KS_TRUE;
	}

	// Create a temporary context to test the if condition without polluting errors
	ks_json_validation_context_t temp_ctx = *ctx;
	temp_ctx.errors = NULL;
	temp_ctx.error_count = 0;

	// Test the if condition
	ks_bool_t if_result = validate_schema_node(constraint->if_schema, instance, &temp_ctx);

	if (if_result) {
		// If condition passed - validate against then schema if present
		if (constraint->then_schema) {
			return validate_schema_node(constraint->then_schema, instance, ctx);
		}
		// No then schema means validation passes
		return KS_TRUE;
	} else {
		// If condition failed - validate against else schema if present
		if (constraint->else_schema) {
			return validate_schema_node(constraint->else_schema, instance, ctx);
		}
		// No else schema means validation passes
		return KS_TRUE;
	}
}

// Forward declaration for recursive validation
static ks_bool_t validate_schema_node(ks_json_schema_node_t *node, ks_json_t *instance, ks_json_validation_context_t *ctx);

static ks_bool_t validate_schema_node(ks_json_schema_node_t *node, ks_json_t *instance, ks_json_validation_context_t *ctx)
{
	if (!node || !instance) return KS_FALSE;

	switch (node->type) {
		case KS_SCHEMA_NODE_BOOLEAN:
			if (!node->constraint.boolean_constraint.value) {
				// Boolean false schema should always fail
				if (ctx && ctx->errors && ctx->error_count < ctx->max_errors) {
					ks_json_schema_pure_error_t *error = create_error("Boolean false schema rejects all values", ctx->instance_path);
					if (error) {
						add_error_to_list(ctx->errors, error);
						ctx->error_count++;
					}
				}
			}
			return node->constraint.boolean_constraint.value;

		case KS_SCHEMA_NODE_TYPE:
			return validate_type_constraint(instance, &node->constraint.type_constraint);

		case KS_SCHEMA_NODE_OBJECT:
			return validate_object_constraint(instance, &node->constraint.object_constraint, ctx);

		case KS_SCHEMA_NODE_ARRAY:
			return validate_array_constraint(instance, &node->constraint.array_constraint, ctx);

		case KS_SCHEMA_NODE_STRING:
			return validate_string_constraint(instance, &node->constraint.string_constraint, ctx);

		case KS_SCHEMA_NODE_NUMBER:
			return validate_number_constraint(instance, &node->constraint.number_constraint, ctx);

		case KS_SCHEMA_NODE_ENUM:
			return validate_enum_constraint(instance, &node->constraint.enum_constraint, ctx);

		case KS_SCHEMA_NODE_CONST:
			return validate_const_constraint(instance, &node->constraint.const_constraint, ctx);

		case KS_SCHEMA_NODE_REF:
			// Resolve reference on-demand if not already resolved
			if (!node->constraint.ref_constraint.resolved_node && ctx && ctx->validator) {
				node->constraint.ref_constraint.resolved_node = resolve_reference(
					ctx->validator->pool,
					node->constraint.ref_constraint.ref_uri,
					ctx->validator->root_schema,
					ctx->validator->ref_cache
				);
			}

			// Validate against the resolved reference
			if (node->constraint.ref_constraint.resolved_node) {
				return validate_schema_node(node->constraint.ref_constraint.resolved_node, instance, ctx);
			} else {
				// Reference could not be resolved
				if (ctx && ctx->errors && ctx->error_count < ctx->max_errors) {
					char message[512];
					snprintf(message, sizeof(message), "Could not resolve reference: %s",
					         node->constraint.ref_constraint.ref_uri ? node->constraint.ref_constraint.ref_uri : "(null)");
					ks_json_schema_pure_error_t *error = create_error(message, ctx->instance_path);
					if (error) {
						add_error_to_list(ctx->errors, error);
						ctx->error_count++;
					}
				}
				return KS_FALSE;
			}

		case KS_SCHEMA_NODE_ALLOF:
			return validate_allof_constraint(instance, &node->constraint.combination_constraint, ctx);

		case KS_SCHEMA_NODE_ANYOF:
			return validate_anyof_constraint(instance, &node->constraint.combination_constraint, ctx);

		case KS_SCHEMA_NODE_ONEOF:
			return validate_oneof_constraint(instance, &node->constraint.combination_constraint, ctx);

		case KS_SCHEMA_NODE_NOT:
			return validate_not_constraint(instance, &node->constraint.not_constraint, ctx);

		case KS_SCHEMA_NODE_IF_THEN_ELSE:
			return validate_conditional_constraint(instance, &node->constraint.conditional_constraint, ctx);

		default:
			return KS_TRUE; // Unknown types pass validation for now
	}
}

KS_DECLARE(ks_json_schema_pure_status_t) ks_json_schema_pure_validate_json(
	ks_json_schema_validator_t *validator,
	ks_json_t *json,
	ks_json_schema_pure_error_t **errors)
{
	if (!validator || !json) {
		return KS_JSON_SCHEMA_PURE_STATUS_INVALID_PARAM;
	}

	if (errors) *errors = NULL;

	// Initialize validation context
	ks_json_validation_context_t ctx;
	ctx.instance_root = json;
	ctx.current_instance = json;
	ctx.instance_path = malloc(KS_JSON_SCHEMA_MAX_PATH_LEN);
	if (!ctx.instance_path) {
		return KS_JSON_SCHEMA_PURE_STATUS_MEMORY_ERROR;
	}
	strcpy(ctx.instance_path, "");
	ctx.path_buffer_size = KS_JSON_SCHEMA_MAX_PATH_LEN;
	ctx.errors = errors;
	ctx.max_errors = KS_JSON_SCHEMA_MAX_ERRORS;
	ctx.error_count = 0;
	ctx.format_checker = validator->format_checker;
	ctx.validator = validator;

	// Perform validation
	ks_bool_t is_valid = validate_schema_node(validator->root_node, json, &ctx);

	free(ctx.instance_path);

	if (!is_valid && ctx.error_count > 0) {
		return KS_JSON_SCHEMA_PURE_STATUS_VALIDATION_FAILED;
	}

	return KS_JSON_SCHEMA_PURE_STATUS_SUCCESS;
}

KS_DECLARE(ks_json_schema_pure_status_t) ks_json_schema_pure_validate_string(
	ks_json_schema_validator_t *validator,
	const char *json_string,
	ks_json_schema_pure_error_t **errors)
{
	if (!validator || !json_string) {
		return KS_JSON_SCHEMA_PURE_STATUS_INVALID_PARAM;
	}

	ks_json_t *parsed_json = ks_json_parse(json_string);
	if (!parsed_json) {
		if (errors) {
			*errors = create_error("Failed to parse JSON", "");
		}
		return KS_JSON_SCHEMA_PURE_STATUS_INVALID_JSON;
	}

	ks_json_schema_pure_status_t result = ks_json_schema_pure_validate_json(validator, parsed_json, errors);

	ks_json_delete(&parsed_json);
	return result;
}

//
// Format validation functions
//

static ks_bool_t validate_date_time_format(const char *value)
{
	// Basic RFC 3339 date-time validation: YYYY-MM-DDTHH:MM:SSZ or YYYY-MM-DDTHH:MM:SS.sssZ
	// Simplified implementation - could be more rigorous
	if (strlen(value) < 19) return KS_FALSE; // Minimum length

	// Check basic structure: YYYY-MM-DDTHH:MM:SS
	if (value[4] != '-' || value[7] != '-' || value[10] != 'T' ||
	    value[13] != ':' || value[16] != ':') {
		return KS_FALSE;
	}

	// Validate year, month, day, hour, minute, second are digits
	for (int i = 0; i < 4; i++) if (!isdigit(value[i])) return KS_FALSE; // year
	for (int i = 5; i < 7; i++) if (!isdigit(value[i])) return KS_FALSE; // month
	for (int i = 8; i < 10; i++) if (!isdigit(value[i])) return KS_FALSE; // day
	for (int i = 11; i < 13; i++) if (!isdigit(value[i])) return KS_FALSE; // hour
	for (int i = 14; i < 16; i++) if (!isdigit(value[i])) return KS_FALSE; // minute
	for (int i = 17; i < 19; i++) if (!isdigit(value[i])) return KS_FALSE; // second

	// Check ranges
	int month = (value[5] - '0') * 10 + (value[6] - '0');
	int day = (value[8] - '0') * 10 + (value[9] - '0');
	int hour = (value[11] - '0') * 10 + (value[12] - '0');
	int minute = (value[14] - '0') * 10 + (value[15] - '0');
	int second = (value[17] - '0') * 10 + (value[18] - '0');

	if (month < 1 || month > 12) return KS_FALSE;
	if (day < 1 || day > 31) return KS_FALSE;
	if (hour > 23) return KS_FALSE;
	if (minute > 59) return KS_FALSE;
	if (second > 59) return KS_FALSE;

	return KS_TRUE;
}

static ks_bool_t validate_date_format(const char *value)
{
	// RFC 3339 full-date: YYYY-MM-DD
	if (strlen(value) != 10) return KS_FALSE;

	if (value[4] != '-' || value[7] != '-') return KS_FALSE;

	// Validate digits
	for (int i = 0; i < 4; i++) if (!isdigit(value[i])) return KS_FALSE;
	for (int i = 5; i < 7; i++) if (!isdigit(value[i])) return KS_FALSE;
	for (int i = 8; i < 10; i++) if (!isdigit(value[i])) return KS_FALSE;

	// Check ranges
	int month = (value[5] - '0') * 10 + (value[6] - '0');
	int day = (value[8] - '0') * 10 + (value[9] - '0');

	if (month < 1 || month > 12) return KS_FALSE;
	if (day < 1 || day > 31) return KS_FALSE;

	return KS_TRUE;
}

static ks_bool_t validate_time_format(const char *value)
{
	// RFC 3339 full-time: HH:MM:SS or HH:MM:SS.sss
	size_t len = strlen(value);
	if (len < 8) return KS_FALSE; // Minimum HH:MM:SS

	if (value[2] != ':' || value[5] != ':') return KS_FALSE;

	// Validate digits
	for (int i = 0; i < 2; i++) if (!isdigit(value[i])) return KS_FALSE;
	for (int i = 3; i < 5; i++) if (!isdigit(value[i])) return KS_FALSE;
	for (int i = 6; i < 8; i++) if (!isdigit(value[i])) return KS_FALSE;

	// Check ranges
	int hour = (value[0] - '0') * 10 + (value[1] - '0');
	int minute = (value[3] - '0') * 10 + (value[4] - '0');
	int second = (value[6] - '0') * 10 + (value[7] - '0');

	if (hour > 23) return KS_FALSE;
	if (minute > 59) return KS_FALSE;
	if (second > 59) return KS_FALSE;

	return KS_TRUE;
}

static ks_bool_t validate_email_format(const char *value)
{
	// Basic email validation - simplified RFC 5322
	const char *at = strchr(value, '@');
	if (!at || at == value) return KS_FALSE; // Must have @ and not start with it

	const char *last_at = strrchr(value, '@');
	if (at != last_at) return KS_FALSE; // Only one @

	// Check local part (before @)
	for (const char *p = value; p < at; p++) {
		if (!isalnum(*p) && *p != '.' && *p != '-' && *p != '_') return KS_FALSE;
	}

	// Check domain part (after @)
	const char *domain = at + 1;
	if (*domain == '\0') return KS_FALSE; // Must have domain

	const char *dot = strchr(domain, '.');
	if (!dot) return KS_FALSE; // Must have at least one dot in domain

	for (const char *p = domain; *p; p++) {
		if (!isalnum(*p) && *p != '.' && *p != '-') return KS_FALSE;
	}

	return KS_TRUE;
}

static ks_bool_t validate_ipv4_format(const char *value)
{
	// IPv4: x.x.x.x where x is 0-255
	int octets[4];
	int parsed = sscanf(value, "%d.%d.%d.%d", &octets[0], &octets[1], &octets[2], &octets[3]);

	if (parsed != 4) return KS_FALSE;

	for (int i = 0; i < 4; i++) {
		if (octets[i] < 0 || octets[i] > 255) return KS_FALSE;
	}

	// Verify no extra characters
	char rebuilt[16];
	snprintf(rebuilt, sizeof(rebuilt), "%d.%d.%d.%d", octets[0], octets[1], octets[2], octets[3]);
	return strcmp(value, rebuilt) == 0;
}

static ks_bool_t validate_hostname_format(const char *value)
{
	// Basic hostname validation per RFC 1123
	size_t len = strlen(value);
	if (len == 0 || len > 253) return KS_FALSE;

	for (size_t i = 0; i < len; i++) {
		char c = value[i];
		if (!isalnum(c) && c != '.' && c != '-') return KS_FALSE;
		if (c == '-' && (i == 0 || i == len - 1)) return KS_FALSE; // No leading/trailing hyphens
	}

	return KS_TRUE;
}

static ks_bool_t validate_uuid_format(const char *value)
{
	// UUID format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
	if (strlen(value) != 36) return KS_FALSE;

	if (value[8] != '-' || value[13] != '-' || value[18] != '-' || value[23] != '-') {
		return KS_FALSE;
	}

	// Check hex digits
	for (int i = 0; i < 36; i++) {
		if (i == 8 || i == 13 || i == 18 || i == 23) continue; // Skip hyphens
		if (!isxdigit(value[i])) return KS_FALSE;
	}

	return KS_TRUE;
}

KS_DECLARE(ks_bool_t) ks_json_schema_pure_default_format_checker(const char *format, const char *value)
{
	if (!format || !value) return KS_TRUE; // No format specified or no value

	if (strcmp(format, "date-time") == 0) {
		return validate_date_time_format(value);
	} else if (strcmp(format, "date") == 0) {
		return validate_date_format(value);
	} else if (strcmp(format, "time") == 0) {
		return validate_time_format(value);
	} else if (strcmp(format, "email") == 0) {
		return validate_email_format(value);
	} else if (strcmp(format, "ipv4") == 0) {
		return validate_ipv4_format(value);
	} else if (strcmp(format, "hostname") == 0) {
		return validate_hostname_format(value);
	} else if (strcmp(format, "uuid") == 0) {
		return validate_uuid_format(value);
	}

	// For unknown formats, return true (don't fail validation)
	return KS_TRUE;
}

/* For Emacs:
 * Local Variables:
 * mode:c
 * indent-tabs-mode:t
 * tab-width:4
 * c-basic-offset:4
 * End:
 * For VIM:
 * vim:set softtabstop=4 shiftwidth=4 tabstop=4 noet:
 */