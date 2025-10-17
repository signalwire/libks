/*
 * Copyright (c) 2018-2023 SignalWire, Inc
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
#include "cJSON/cJSON.h"

// Create apis
KS_DECLARE(ks_json_t *) ks_json_create_array(void)
{
	return kJSON_CreateArray();
}

KS_DECLARE(ks_json_t *) ks_json_create_object(void)
{
	return kJSON_CreateObject();
}

KS_DECLARE(ks_json_t *) ks_json_create_number(double number)
{
	return kJSON_CreateNumber(number);
}

KS_DECLARE(ks_json_t *) ks_json_create_string_fmt(const char *fmt, ...)
{
	va_list ap;
	char *str;
	ks_json_t *item;

	va_start(ap, fmt);
	str = ks_vmprintf(fmt, ap);
	va_end(ap);

	if (!str) return NULL;

	item = kJSON_CreateString(str);

	free(str);	/* Managed by glibc for the formatting allocation */

	return item;
}

KS_DECLARE(ks_json_t *) ks_json_create_string(const char *string)
{
	return kJSON_CreateString(string);
}

KS_DECLARE(ks_json_t *) ks_json_create_false(void)
{
	return kJSON_CreateFalse();
}

KS_DECLARE(ks_json_t *) ks_json_create_true(void)
{
	return kJSON_CreateTrue();
}

KS_DECLARE(ks_json_t *) ks_json_create_null(void)
{
	return kJSON_CreateNull();
}

KS_DECLARE(ks_json_t *) ks_json_create_bool(ks_bool_t value)
{
	return kJSON_CreateBool(value == KS_TRUE);
}

// Parse apis
KS_DECLARE(ks_json_t *) ks_json_parse(const char *value)
{
	return kJSON_Parse(value);
}

// Add apis
KS_DECLARE(void) ks_json_add_item_to_array(ks_json_t *array, ks_json_t *item)
{
	kJSON_AddItemToArray(array, item);
}

KS_DECLARE(ks_json_t *) ks_json_add_array_to_array(ks_json_t *array)
{
	ks_json_t *new_array = ks_json_create_array();
	ks_json_add_item_to_array(array, new_array);
	return new_array;
}

KS_DECLARE(ks_json_t *) ks_json_add_object_to_array(ks_json_t *array)
{
	ks_json_t *new_object = ks_json_create_object();
	ks_json_add_item_to_array(array, new_object);
	return new_object;
}

KS_DECLARE(void) ks_json_add_string_to_array(ks_json_t *array, const char *string)
{
	ks_json_add_item_to_array(array, ks_json_create_string(string));
}

KS_DECLARE(void) ks_json_add_number_to_array(ks_json_t *array, double number)
{
	ks_json_add_item_to_array(array, ks_json_create_number(number));
}

KS_DECLARE(void) ks_json_add_true_to_array(ks_json_t *array)
{
	ks_json_add_item_to_array(array, ks_json_create_true());
}

KS_DECLARE(void) ks_json_add_false_to_array(ks_json_t *array)
{
	ks_json_add_item_to_array(array, ks_json_create_false());
}

KS_DECLARE(void) ks_json_add_bool_to_array(ks_json_t *array, ks_bool_t value)
{
	if (value == KS_TRUE) {
		ks_json_add_true_to_array(array);
	} else {
		ks_json_add_false_to_array(array);
	}
}

KS_DECLARE(void) ks_json_add_item_to_object(ks_json_t *object, const char *string, ks_json_t *item)
{
	// TODO check if item parent is NULL
	kJSON_AddItemToObject(object, string, item);
}

KS_DECLARE(ks_json_t *) ks_json_add_array_to_object(ks_json_t *object, const char *string)
{
	ks_json_t *new_array = ks_json_create_array();
	ks_json_add_item_to_object(object, string, new_array);
	return new_array;
}

KS_DECLARE(ks_json_t *) ks_json_add_object_to_object(ks_json_t *object, const char *string)
{
	ks_json_t *new_object = ks_json_create_object();
	ks_json_add_item_to_object(object, string, new_object);
	return new_object;
}

KS_DECLARE(void) ks_json_add_true_to_object(ks_json_t *object, const char *string)
{
	ks_json_add_item_to_object(object, string, ks_json_create_true());
}

KS_DECLARE(void) ks_json_add_false_to_object(ks_json_t *object, const char *string)
{
	ks_json_add_item_to_object(object, string, ks_json_create_false());
}

KS_DECLARE(void) ks_json_add_bool_to_object(ks_json_t *object, const char *string, ks_bool_t value)
{
	if (value == KS_TRUE) {
		ks_json_add_true_to_object(object, string);
	} else {
		ks_json_add_false_to_object(object, string);
	}
}

KS_DECLARE(void) ks_json_add_number_to_object(ks_json_t *object, const char *name, double number)
{
	ks_json_add_item_to_object(object, name, ks_json_create_number(number));
}

KS_DECLARE(void) ks_json_add_string_to_object(ks_json_t *object, const char *name, const char *string)
{
	ks_json_add_item_to_object(object, name, ks_json_create_string(string));
}

KS_DECLARE(ks_json_t *) ks_json_duplicate(ks_json_t *c, ks_bool_t recurse)
{
	return kJSON_Duplicate(c, recurse);
}

KS_DECLARE(void) ks_json_delete(ks_json_t **c)
{
	if (!c || !*c)
		return;

	kJSON_Delete(*c);
	*c = NULL;
}

KS_DECLARE(void) ks_json_delete_item_from_array(ks_json_t *array, int index)
{
	kJSON_DeleteItemFromArray(array, index);
}

KS_DECLARE(void) ks_json_delete_item_from_object(ks_json_t *obj, const char *key)
{
	kJSON_DeleteItemFromObject(obj, key);
}

// Get apis
KS_DECLARE(ks_json_t *) ks_json_get_array_item(ks_json_t *array, int index)
{
	return kJSON_GetArrayItem(array, index);
}

KS_DECLARE(ks_bool_t) ks_json_get_array_bool(ks_json_t *array, int index, ks_bool_t def)
{
	ks_bool_t retval = def;
	ks_json_t *item = ks_json_get_array_item(array, index);
	ks_json_value_bool(item, &retval);
	return retval;
}

KS_DECLARE(const char *) ks_json_get_array_string(ks_json_t *array, int index, const char* def)
{
	const char *retval = def;
	ks_json_t *item = ks_json_get_array_item(array, index);
	ks_json_value_string(item, &retval);
	return retval;
}

KS_DECLARE(int) ks_json_get_array_number_int(ks_json_t *array, int index, int def)
{
	int retval = def;
	ks_json_t *item = ks_json_get_array_item(array, index);
	ks_json_value_number_int(item, &retval);
	return retval;
}

KS_DECLARE(double) ks_json_get_array_number_double(ks_json_t *array, int index, double def)
{
	double retval = def;
	ks_json_t *item = ks_json_get_array_item(array, index);
	ks_json_value_number_double(item, &retval);
	return retval;
}

KS_DECLARE(int) ks_json_get_array_size(ks_json_t *array)
{
	return kJSON_GetArraySize(array);
}

KS_DECLARE(ks_json_t *) ks_json_get_object_item(ks_json_t *object, const char *string)
{
	return kJSON_GetObjectItemCaseSensitive(object, string);
}

KS_DECLARE(ks_bool_t) ks_json_get_object_bool(ks_json_t *object, const char *string, ks_bool_t def)
{
	ks_bool_t retval = def;
	ks_json_value_bool(ks_json_get_object_item(object, string), &retval);
	return retval;
}

KS_DECLARE(const char *) ks_json_get_object_string(ks_json_t *object, const char *key, const char *def)
{
	const char *retval = def;
	ks_json_value_string(ks_json_get_object_item(object, key), &retval);
	return retval;
}

KS_DECLARE(int) ks_json_get_object_number_int(ks_json_t *object, const char *key, int def)
{
	int retval = def;
	ks_json_value_number_int(ks_json_get_object_item(object, key), &retval);
	return retval;
}

KS_DECLARE(double) ks_json_get_object_number_double(ks_json_t *object, const char *key, double def)
{
	double retval = def;
	ks_json_value_number_double(ks_json_get_object_item(object, key), &retval);
	return retval;
}

KS_DECLARE(const char *) ks_json_get_name(ks_json_t *item)
{
	if (item) {
		return item->string;
	}
	return NULL;
}

KS_DECLARE(const char *) ks_json_get_string(ks_json_t *item, const char *def)
{
	const char *val = def;
	ks_json_value_string(item, &val);
	return val;
}

KS_DECLARE(int) ks_json_get_number_int(ks_json_t *item, int def)
{
	int val = def;
	ks_json_value_number_int(item, &val);
	return val;
}

KS_DECLARE(double) ks_json_get_number_double(ks_json_t *item, double def)
{
	double val = def;
	ks_json_value_number_double(item, &val);
	return val;
}

KS_DECLARE(ks_bool_t) ks_json_get_bool(ks_json_t *item, ks_bool_t def)
{
	ks_bool_t val = def;
	ks_json_value_bool(item, &val);
	return val;
}

KS_DECLARE(char *) ks_json_print(ks_json_t *item)
{
	return kJSON_Print(item);
}

KS_DECLARE(char *) ks_json_print_unformatted(ks_json_t *item)
{
	return kJSON_PrintUnformatted(item);
}

KS_DECLARE(ks_json_type_t) ks_json_type_get(ks_json_t *item)
{
	if (!item)
		return KS_JSON_TYPE_INVALID;
	return item->type;
}

KS_DECLARE(ks_bool_t) ks_json_type_is(ks_json_t *item, ks_json_type_t type)
{
	return ks_json_type_get(item) == type;
}

KS_DECLARE(ks_bool_t) ks_json_type_is_array(ks_json_t *item)
{
	return ks_json_type_is(item, KS_JSON_TYPE_ARRAY);
}

KS_DECLARE(ks_bool_t) ks_json_type_is_string(ks_json_t *item)
{
	return ks_json_type_is(item, KS_JSON_TYPE_STRING);
}

KS_DECLARE(ks_bool_t) ks_json_type_is_number(ks_json_t *item)
{
	return ks_json_type_is(item, KS_JSON_TYPE_NUMBER);
}

KS_DECLARE(ks_bool_t) ks_json_type_is_null(ks_json_t *item)
{
	return ks_json_type_is(item, KS_JSON_TYPE_NULL);
}

KS_DECLARE(ks_bool_t) ks_json_type_is_object(ks_json_t *item)
{
	return ks_json_type_is(item, KS_JSON_TYPE_OBJECT);
}

KS_DECLARE(ks_bool_t) ks_json_type_is_false(ks_json_t *item)
{
	return ks_json_type_is(item, KS_JSON_TYPE_FALSE);
}

KS_DECLARE(ks_bool_t) ks_json_type_is_true(ks_json_t *item)
{
	return ks_json_type_is(item, KS_JSON_TYPE_TRUE);
}

KS_DECLARE(ks_bool_t) ks_json_type_is_bool(ks_json_t *item)
{
	return ks_json_type_is(item, KS_JSON_TYPE_FALSE) || ks_json_type_is(item, KS_JSON_TYPE_TRUE);
}

KS_DECLARE(ks_status_t) ks_json_name(ks_json_t *item, const char **name)
{
	if (!item->string || !*item->string) {
		return KS_STATUS_FAIL;
	}
	*name = item->string;
	return KS_STATUS_SUCCESS;
}

// Value apis
KS_DECLARE(ks_status_t) ks_json_value_string(ks_json_t *item, const char **value)
{
	if (!ks_json_type_is_string(item)) {
		return KS_STATUS_FAIL;
	}
	*value = item->valuestring;
	return KS_STATUS_SUCCESS;
}

KS_DECLARE(ks_status_t) ks_json_value_number_int(ks_json_t *item, int *value)
{
	if (!ks_json_type_is_number(item)) {
		return KS_STATUS_FAIL;
	}
	*value = item->valueint;
	return KS_STATUS_SUCCESS;
}

KS_DECLARE(ks_status_t) ks_json_value_number_double(ks_json_t *item, double *value)
{
	if (!ks_json_type_is_number(item)) {
		return KS_STATUS_FAIL;
	}
	*value = item->valuedouble;
	return KS_STATUS_SUCCESS;
}

KS_DECLARE(ks_status_t) ks_json_value_bool(ks_json_t *item, ks_bool_t *value)
{
	if (!ks_json_type_is_bool(item)) {
		return KS_STATUS_FAIL;
	}
	if (item->type == KS_JSON_TYPE_TRUE) {
		*value = KS_TRUE;
	} else {
		*value = KS_FALSE;
	}
	return KS_STATUS_SUCCESS;
}

KS_DECLARE(ks_json_t *) ks_json_enum_child(ks_json_t *item)
{
	if (!item) {
		return NULL;
	}
	return item->child;
}

KS_DECLARE(ks_json_t *) ks_json_enum_next(ks_json_t *item)
{
	if (!item) {
		return NULL;
	}
	return item->next;
}

static char *ks_json_pointer_unescape(const char *token)
{
	char *result;
	const char *src;
	char *dst;
	size_t len;

	if (!token) return NULL;

	len = strlen(token);
	result = malloc(len + 1);
	if (!result) return NULL;

	src = token;
	dst = result;

	while (*src) {
		if (*src == '~') {
			if (*(src + 1) == '0') {
				*dst++ = '~';
				src += 2;
			} else if (*(src + 1) == '1') {
				*dst++ = '/';
				src += 2;
			} else {
				*dst++ = *src++;
			}
		} else {
			*dst++ = *src++;
		}
	}
	*dst = '\0';

	return result;
}

static char *ks_json_pointer_escape(const char *token)
{
	char *result;
	const char *src;
	char *dst;
	size_t len, escaped_len;

	if (!token) return NULL;

	len = strlen(token);
	escaped_len = len;

	for (src = token; *src; src++) {
		if (*src == '~' || *src == '/') {
			escaped_len++;
		}
	}

	result = malloc(escaped_len + 1);
	if (!result) return NULL;

	src = token;
	dst = result;

	while (*src) {
		if (*src == '~') {
			*dst++ = '~';
			*dst++ = '0';
		} else if (*src == '/') {
			*dst++ = '~';
			*dst++ = '1';
		} else {
			*dst++ = *src;
		}
		src++;
	}
	*dst = '\0';

	return result;
}

static ks_bool_t ks_json_build_pointer_path(ks_json_t *root, ks_json_t *target, char **result)
{
	ks_json_t *child;
	int index;
	char *escaped_key;
	char *child_path;
	char *temp_result;

	if (!root || !target || !result) return KS_FALSE;

	if (root == target) {
		*result = strdup("");
		return *result != NULL;
	}

	if (ks_json_type_is_object(root)) {
		child = ks_json_enum_child(root);
		while (child) {
			if (child == target) {
				escaped_key = ks_json_pointer_escape(child->string);
				if (!escaped_key) return KS_FALSE;
				*result = malloc(strlen(escaped_key) + 2);
				if (!*result) {
					free(escaped_key);
					return KS_FALSE;
				}
				sprintf(*result, "/%s", escaped_key);
				free(escaped_key);
				return KS_TRUE;
			}

			if (ks_json_build_pointer_path(child, target, &child_path)) {
				escaped_key = ks_json_pointer_escape(child->string);
				if (!escaped_key) {
					free(child_path);
					return KS_FALSE;
				}
				*result = malloc(strlen(escaped_key) + strlen(child_path) + 2);
				if (!*result) {
					free(escaped_key);
					free(child_path);
					return KS_FALSE;
				}
				sprintf(*result, "/%s%s", escaped_key, child_path);
				free(escaped_key);
				free(child_path);
				return KS_TRUE;
			}
			child = ks_json_enum_next(child);
		}
	} else if (ks_json_type_is_array(root)) {
		child = ks_json_enum_child(root);
		index = 0;
		while (child) {
			if (child == target) {
				*result = malloc(16);
				if (!*result) return KS_FALSE;
				sprintf(*result, "/%d", index);
				return KS_TRUE;
			}

			if (ks_json_build_pointer_path(child, target, &child_path)) {
				*result = malloc(strlen(child_path) + 16);
				if (!*result) {
					free(child_path);
					return KS_FALSE;
				}
				sprintf(*result, "/%d%s", index, child_path);
				free(child_path);
				return KS_TRUE;
			}
			child = ks_json_enum_next(child);
			index++;
		}
	}

	return KS_FALSE;
}

KS_DECLARE(char *) ks_json_get_json_pointer(ks_json_t *json)
{
	ks_json_t *current = json;
	char **path_parts = NULL;
	int path_count = 0;
	char *result = NULL;
	int i;

	if (!json) return NULL;

	while (current && current->parent) {
		char **new_path = realloc(path_parts, (path_count + 1) * sizeof(char *));
		if (!new_path) {
			if (path_parts) {
				for (i = 0; i < path_count; i++) {
					free(path_parts[i]);
				}
				free(path_parts);
			}
			return NULL;
		}
		path_parts = new_path;

		if (ks_json_type_is_array(current->parent)) {
			ks_json_t *sibling = ks_json_enum_child(current->parent);
			int index = 0;
			while (sibling && sibling != current) {
				sibling = ks_json_enum_next(sibling);
				index++;
			}
			
			path_parts[path_count] = malloc(16);
			if (!path_parts[path_count]) {
				for (i = 0; i < path_count; i++) {
					free(path_parts[i]);
				}
				free(path_parts);
				return NULL;
			}
			sprintf(path_parts[path_count], "%d", index);
		} else if (current->string) {
			path_parts[path_count] = ks_json_pointer_escape(current->string);
			if (!path_parts[path_count]) {
				for (i = 0; i < path_count; i++) {
					free(path_parts[i]);
				}
				free(path_parts);
				return NULL;
			}
		} else {
			for (i = 0; i < path_count; i++) {
				free(path_parts[i]);
			}
			free(path_parts);
			return NULL;
		}
		path_count++;

		current = current->parent;
	}

	if (path_count == 0) {
		result = strdup("");
	} else {
		size_t total_len = 1;
		for (i = path_count - 1; i >= 0; i--) {
			total_len += 1 + strlen(path_parts[i]);
		}

		result = malloc(total_len);
		if (result) {
			result[0] = '\0';
			for (i = path_count - 1; i >= 0; i--) {
				strcat(result, "/");
				strcat(result, path_parts[i]);
			}
		}
	}

	for (i = 0; i < path_count; i++) {
		free(path_parts[i]);
	}
	free(path_parts);

	return result;
}

KS_DECLARE(ks_json_t *) ks_json_pointer_get_item(ks_json_t *json, const char *json_pointer)
{
	ks_json_t *current = json;
	const char *ptr = json_pointer;
	char *token;
	char *unescaped_token;
	const char *next_slash;
	size_t token_len;

	if (!json || !json_pointer) return NULL;

	if (*json_pointer == '\0') {
		return json;
	}

	if (*ptr != '/') {
		return NULL;
	}

	ptr++;

	while (*ptr) {
		next_slash = strchr(ptr, '/');
		if (next_slash) {
			token_len = next_slash - ptr;
		} else {
			token_len = strlen(ptr);
		}

		token = malloc(token_len + 1);
		if (!token) return NULL;

		strncpy(token, ptr, token_len);
		token[token_len] = '\0';

		unescaped_token = ks_json_pointer_unescape(token);
		free(token);
		if (!unescaped_token) return NULL;

		if (ks_json_type_is_object(current)) {
			current = ks_json_get_object_item(current, unescaped_token);
		} else if (ks_json_type_is_array(current)) {
			if (strcmp(unescaped_token, "-") == 0) {
				free(unescaped_token);
				return NULL;
			}

			char *endptr;
			long index = strtol(unescaped_token, &endptr, 10);
			if (*endptr != '\0' || index < 0) {
				free(unescaped_token);
				return NULL;
			}

			current = ks_json_get_array_item(current, (int)index);
		} else {
			free(unescaped_token);
			return NULL;
		}

		free(unescaped_token);

		if (!current) {
			return NULL;
		}

		if (next_slash) {
			ptr = next_slash + 1;
		} else {
			break;
		}
	}

	return current;
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

