/*
 * Copyright (c) 2023 SignalWire, Inc
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "tap.h"

#ifdef HAVE_VALIJSON

static void test_schema_creation(void)
{
	ks_json_schema_t *schema = NULL;
	const char *schema_json = "{\"type\": \"object\", \"properties\": {\"name\": {\"type\": \"string\"}}, \"required\": [\"name\"]}";
	
	ks_json_schema_status_t status = ks_json_schema_create(schema_json, &schema, NULL);
	ok(status == KS_JSON_SCHEMA_STATUS_SUCCESS, "Schema creation should succeed");
	ok(schema != NULL, "Schema should not be NULL");
	
	if (schema) {
		ks_json_schema_destroy(&schema);
		ok(schema == NULL, "Schema should be NULL after destroy");
	}
}

static void test_invalid_schema(void)
{
	ks_json_schema_t *schema = NULL;
	ks_json_schema_error_t *errors = NULL;
	const char *invalid_schema = "{\"type\": \"invalid_type\"}";
	
	ks_json_schema_status_t status = ks_json_schema_create(invalid_schema, &schema, &errors);
	ok(status == KS_JSON_SCHEMA_STATUS_INVALID_SCHEMA, "Invalid schema should fail");
	ok(schema == NULL, "Schema should be NULL for invalid schema");
	ok(errors != NULL, "Errors should be returned for invalid schema");
	
	if (errors) {
		ok(errors->message != NULL, "Error message should not be NULL");
		ks_json_schema_error_free(&errors);
		ok(errors == NULL, "Errors should be NULL after free");
	}
}

static void test_valid_json_validation(void)
{
	ks_json_schema_t *schema = NULL;
	const char *schema_json = "{\"type\": \"object\", \"properties\": {\"name\": {\"type\": \"string\"}, \"age\": {\"type\": \"number\"}}, \"required\": [\"name\"]}";
	const char *valid_json = "{\"name\": \"John\", \"age\": 30}";
	ks_json_schema_error_t *errors = NULL;
	
	ks_json_schema_status_t status = ks_json_schema_create(schema_json, &schema, NULL);
	ok(status == KS_JSON_SCHEMA_STATUS_SUCCESS, "Schema creation should succeed");
	
	if (schema) {
		status = ks_json_schema_validate_string(schema, valid_json, &errors);
		ok(status == KS_JSON_SCHEMA_STATUS_SUCCESS, "Valid JSON should pass validation");
		ok(errors == NULL, "No errors should be returned for valid JSON");
		
		ks_json_schema_destroy(&schema);
	}
}

static void test_invalid_json_validation(void)
{
	ks_json_schema_t *schema = NULL;
	const char *schema_json = "{\"type\": \"object\", \"properties\": {\"name\": {\"type\": \"string\"}, \"age\": {\"type\": \"number\"}}, \"required\": [\"name\"]}";
	const char *invalid_json = "{\"age\": 30}"; // Missing required "name" field
	ks_json_schema_error_t *errors = NULL;
	
	ks_json_schema_status_t status = ks_json_schema_create(schema_json, &schema, NULL);
	ok(status == KS_JSON_SCHEMA_STATUS_SUCCESS, "Schema creation should succeed");
	
	if (schema) {
		status = ks_json_schema_validate_string(schema, invalid_json, &errors);
		ok(status == KS_JSON_SCHEMA_STATUS_VALIDATION_FAILED, "Invalid JSON should fail validation");
		ok(errors != NULL, "Errors should be returned for invalid JSON");
		
		if (errors) {
			ok(errors->message != NULL, "Error message should not be NULL");
			ok(errors->path != NULL, "Error path should not be NULL");
			ks_json_schema_error_free(&errors);
			ok(errors == NULL, "Errors should be NULL after free");
		}
		
		ks_json_schema_destroy(&schema);
	}
}

static void test_json_object_validation(void)
{
	ks_json_schema_t *schema = NULL;
	ks_json_t *schema_json_obj = NULL;
	ks_json_t *valid_json_obj = NULL;
	ks_json_schema_error_t *errors = NULL;
	
	// Create schema JSON object
	schema_json_obj = ks_json_create_object();
	ks_json_add_string_to_object(schema_json_obj, "type", "object");
	
	ks_json_t *properties = ks_json_add_object_to_object(schema_json_obj, "properties");
	ks_json_t *name_prop = ks_json_add_object_to_object(properties, "name");
	ks_json_add_string_to_object(name_prop, "type", "string");
	
	ks_json_t *required_array = ks_json_add_array_to_object(schema_json_obj, "required");
	ks_json_add_string_to_array(required_array, "name");
	
	ks_json_schema_status_t status = ks_json_schema_create_from_json(schema_json_obj, &schema, NULL);
	ok(status == KS_JSON_SCHEMA_STATUS_SUCCESS, "Schema creation from JSON object should succeed");
	
	if (schema) {
		// Create valid JSON object
		valid_json_obj = ks_json_create_object();
		ks_json_add_string_to_object(valid_json_obj, "name", "Alice");
		
		status = ks_json_schema_validate_json(schema, valid_json_obj, &errors);
		ok(status == KS_JSON_SCHEMA_STATUS_SUCCESS, "Valid JSON object should pass validation");
		ok(errors == NULL, "No errors should be returned for valid JSON object");
		
		ks_json_delete(&valid_json_obj);
		ks_json_schema_destroy(&schema);
	}
	
	ks_json_delete(&schema_json_obj);
}

static void test_status_strings(void)
{
	const char *status_str;
	
	status_str = ks_json_schema_status_string(KS_JSON_SCHEMA_STATUS_SUCCESS);
	ok(strcmp(status_str, "Success") == 0, "Success status string should be correct");
	
	status_str = ks_json_schema_status_string(KS_JSON_SCHEMA_STATUS_INVALID_SCHEMA);
	ok(strcmp(status_str, "Invalid schema") == 0, "Invalid schema status string should be correct");
	
	status_str = ks_json_schema_status_string(KS_JSON_SCHEMA_STATUS_VALIDATION_FAILED);
	ok(strcmp(status_str, "Validation failed") == 0, "Validation failed status string should be correct");
}

#endif /* HAVE_VALIJSON */

int main(int argc, char **argv)
{
	ks_init();

#ifdef HAVE_VALIJSON
	plan(23);

	test_schema_creation();
	test_invalid_schema();
	test_valid_json_validation();
	test_invalid_json_validation();
	test_json_object_validation();
	test_status_strings();
#else
	plan(1);
	ok(1, "# SKIP valijson not available, skipping JSON schema validation tests");
#endif

	ks_shutdown();
	
	done_testing();
}