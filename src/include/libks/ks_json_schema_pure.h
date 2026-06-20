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

#pragma once

#include "ks_json.h"
#include "ks_pool.h"
#include "ks_hash.h"

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
typedef struct ks_json_schema_validator ks_json_schema_validator_t;
typedef struct ks_json_schema_node ks_json_schema_node_t;

// Schema validation status codes
typedef enum {
	KS_JSON_SCHEMA_PURE_STATUS_SUCCESS = 0,
	KS_JSON_SCHEMA_PURE_STATUS_INVALID_SCHEMA,
	KS_JSON_SCHEMA_PURE_STATUS_INVALID_JSON,
	KS_JSON_SCHEMA_PURE_STATUS_VALIDATION_FAILED,
	KS_JSON_SCHEMA_PURE_STATUS_MEMORY_ERROR,
	KS_JSON_SCHEMA_PURE_STATUS_INVALID_PARAM
} ks_json_schema_pure_status_t;

// Error structure (reuse existing structure for compatibility)
typedef struct ks_json_schema_error ks_json_schema_pure_error_t;

// Schema node types for validation tree
typedef enum {
	KS_SCHEMA_NODE_TYPE,              // type validation
	KS_SCHEMA_NODE_OBJECT,            // object validation
	KS_SCHEMA_NODE_ARRAY,             // array validation
	KS_SCHEMA_NODE_STRING,            // string validation
	KS_SCHEMA_NODE_NUMBER,            // number validation
	KS_SCHEMA_NODE_ENUM,              // enum validation
	KS_SCHEMA_NODE_CONST,             // const validation
	KS_SCHEMA_NODE_REF,               // $ref validation
	KS_SCHEMA_NODE_ALLOF,             // allOf validation
	KS_SCHEMA_NODE_ANYOF,             // anyOf validation
	KS_SCHEMA_NODE_ONEOF,             // oneOf validation
	KS_SCHEMA_NODE_NOT,               // not validation
	KS_SCHEMA_NODE_IF_THEN_ELSE,      // if/then/else validation
	KS_SCHEMA_NODE_BOOLEAN            // boolean schema (true/false)
} ks_json_schema_node_type_t;

// Type constraints for type validation
typedef struct {
	uint32_t allowed_types;           // Bitmask of allowed ks_json_type_t values
} ks_schema_type_constraint_t;

// Object constraints
typedef struct {
	ks_hash_t *properties;            // property name -> ks_json_schema_node_t*
	ks_json_schema_node_t *additional_properties;
	char **required_properties;
	size_t required_count;
	int min_properties;
	int max_properties;
	ks_bool_t has_min_properties;
	ks_bool_t has_max_properties;
} ks_schema_object_constraint_t;

// Array constraints
typedef struct {
	ks_json_schema_node_t *items;
	ks_json_schema_node_t *additional_items;
	int min_items;
	int max_items;
	ks_bool_t unique_items;
	ks_bool_t has_min_items;
	ks_bool_t has_max_items;
} ks_schema_array_constraint_t;

// String constraints
typedef struct {
	int min_length;
	int max_length;
	char *pattern;                    // Regex pattern (if any)
	char *format;                     // Format name (if any)
	ks_bool_t has_min_length;
	ks_bool_t has_max_length;
} ks_schema_string_constraint_t;

// Number constraints
typedef struct {
	double minimum;
	double maximum;
	double multiple_of;
	ks_bool_t exclusive_minimum;
	ks_bool_t exclusive_maximum;
	ks_bool_t has_minimum;
	ks_bool_t has_maximum;
	ks_bool_t has_multiple_of;
} ks_schema_number_constraint_t;

// Enum constraints
typedef struct {
	ks_json_t **enum_values;          // Array of allowed values
	size_t enum_count;
} ks_schema_enum_constraint_t;

// Const constraints
typedef struct {
	ks_json_t *const_value;
} ks_schema_const_constraint_t;

// Reference constraints
typedef struct {
	char *ref_uri;
	ks_json_schema_node_t *resolved_node;
} ks_schema_ref_constraint_t;

// Logical combination constraints (allOf, anyOf, oneOf)
typedef struct {
	ks_json_schema_node_t **schemas;
	size_t schema_count;
} ks_schema_combination_constraint_t;

// Not constraint
typedef struct {
	ks_json_schema_node_t *schema;
} ks_schema_not_constraint_t;

// If/then/else constraint
typedef struct {
	ks_json_schema_node_t *if_schema;
	ks_json_schema_node_t *then_schema;
	ks_json_schema_node_t *else_schema;
} ks_schema_conditional_constraint_t;

// Boolean schema constraint
typedef struct {
	ks_bool_t value;                  // true = always valid, false = always invalid
} ks_schema_boolean_constraint_t;

// Main schema node structure
struct ks_json_schema_node {
	ks_json_schema_node_type_t type;
	
	union {
		ks_schema_type_constraint_t type_constraint;
		ks_schema_object_constraint_t object_constraint;
		ks_schema_array_constraint_t array_constraint;
		ks_schema_string_constraint_t string_constraint;
		ks_schema_number_constraint_t number_constraint;
		ks_schema_enum_constraint_t enum_constraint;
		ks_schema_const_constraint_t const_constraint;
		ks_schema_ref_constraint_t ref_constraint;
		ks_schema_combination_constraint_t combination_constraint;
		ks_schema_not_constraint_t not_constraint;
		ks_schema_conditional_constraint_t conditional_constraint;
		ks_schema_boolean_constraint_t boolean_constraint;
	} constraint;
	
	// Additional validation nodes (for combining multiple constraints)
	ks_json_schema_node_t **additional_nodes;
	size_t additional_count;
};

// Format checker function type
typedef ks_bool_t (*ks_json_schema_format_checker_t)(const char *format, const char *value);

// Validation context for tracking state during validation
typedef struct {
	ks_json_t *instance_root;         // Root of JSON being validated
	ks_json_t *current_instance;      // Current validation point
	char *instance_path;              // JSON pointer to current location
	size_t path_buffer_size;          // Size of path buffer
	ks_json_schema_pure_error_t **errors; // Error accumulator
	int max_errors;                   // Stop after N errors
	int error_count;                  // Current error count
	ks_json_schema_format_checker_t format_checker; // Format validation function
	ks_json_schema_validator_t *validator; // Validator for $ref resolution
} ks_json_validation_context_t;

// Main validator structure
struct ks_json_schema_validator {
	ks_json_schema_node_t *root_node; // Compiled schema tree
	ks_json_t *root_schema;           // Original root schema JSON for $ref resolution
	ks_pool_t *pool;                  // Memory pool for schema nodes
	ks_hash_t *ref_cache;             // $ref resolution cache
	ks_json_schema_format_checker_t format_checker;
};

//
// Public API Functions
//

// Create a schema validator from JSON string
KS_DECLARE(ks_json_schema_pure_status_t) ks_json_schema_pure_create(
	const char *schema_json,
	ks_json_schema_validator_t **validator,
	ks_json_schema_pure_error_t **errors
);

// Create a schema validator from ks_json_t object
KS_DECLARE(ks_json_schema_pure_status_t) ks_json_schema_pure_create_from_json(
	ks_json_t *schema_json,
	ks_json_schema_validator_t **validator,
	ks_json_schema_pure_error_t **errors
);

// Validate JSON string against compiled schema
KS_DECLARE(ks_json_schema_pure_status_t) ks_json_schema_pure_validate_string(
	ks_json_schema_validator_t *validator,
	const char *json_string,
	ks_json_schema_pure_error_t **errors
);

// Validate ks_json_t object against compiled schema
KS_DECLARE(ks_json_schema_pure_status_t) ks_json_schema_pure_validate_json(
	ks_json_schema_validator_t *validator,
	ks_json_t *json,
	ks_json_schema_pure_error_t **errors
);

// Destroy schema validator and free resources
KS_DECLARE(void) ks_json_schema_pure_destroy(
	ks_json_schema_validator_t **validator
);

// Free error list
KS_DECLARE(void) ks_json_schema_pure_error_free(
	ks_json_schema_pure_error_t **errors
);

// Get string description of status code
KS_DECLARE(const char *) ks_json_schema_pure_status_string(
	ks_json_schema_pure_status_t status
);

// Set custom format checker (optional)
KS_DECLARE(void) ks_json_schema_pure_set_format_checker(
	ks_json_schema_validator_t *validator,
	ks_json_schema_format_checker_t format_checker
);

//
// Built-in format checker (validates common formats like email, uri, date-time, etc.)
//
KS_DECLARE(ks_bool_t) ks_json_schema_pure_default_format_checker(
	const char *format,
	const char *value
);

#ifdef __cplusplus
}
#endif

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