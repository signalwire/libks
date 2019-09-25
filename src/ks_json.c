/*
 * Copyright (c) 2018-2019 SignalWire, Inc
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
	return cJSON_CreateArray();
}

KS_DECLARE(ks_json_t *) ks_json_create_object(void)
{
	return cJSON_CreateObject();
}

KS_DECLARE(ks_json_t *) ks_json_create_number(double number)
{
	return cJSON_CreateNumber(number);
}

KS_DECLARE(ks_json_t *) ks_json_create_string_fmt(const char *fmt, ...)
{
	va_list ap;
	char *str;
	cJSON *item;

	va_start(ap, fmt);
	str = ks_vmprintf(fmt, ap);
	va_end(ap);

	if (!str) return NULL;

	item = cJSON_CreateString(str);

	free(str);	/* Managed by glibc for the formatting allocation */

	return item;
}

KS_DECLARE(ks_json_t *) ks_json_create_string(const char *string)
{
	return cJSON_CreateString(string);
}

KS_DECLARE(ks_json_t *) ks_json_create_false(void)
{
	return cJSON_CreateFalse();
}

KS_DECLARE(ks_json_t *) ks_json_create_true(void)
{
	return cJSON_CreateTrue();
}

KS_DECLARE(ks_json_t *) ks_json_create_null(void)
{
	return cJSON_CreateNull();
}

KS_DECLARE(ks_json_t *) ks_json_create_bool(ks_bool_t value)
{
	return cJSON_CreateBool(value == KS_TRUE);
}

// Parse apis
KS_DECLARE(ks_json_t *) ks_json_parse(const char *value)
{
	return cJSON_Parse(value);
}

// Add apis
KS_DECLARE(void) ks_json_add_item_to_array(ks_json_t *array, ks_json_t *item)
{
	cJSON_AddItemToArray(array, item);
}

KS_DECLARE(void) ks_json_add_string_to_array(ks_json_t * const array, const char * const string)
{
	ks_json_add_item_to_array(array, ks_json_create_string(string));
}

KS_DECLARE(void) ks_json_add_number_to_array(ks_json_t * const array, const double number)
{
	ks_json_add_item_to_array(array, ks_json_create_number(number));
}

KS_DECLARE(void) ks_json_add_true_to_array(ks_json_t *const array)
{
	ks_json_add_item_to_array(array, ks_json_create_true());
}

KS_DECLARE(void) ks_json_add_false_to_array(ks_json_t * const array)
{
	ks_json_add_item_to_array(array, ks_json_create_false());
}

KS_DECLARE(void) ks_json_add_bool_to_array(ks_json_t * const array, ks_bool_t value)
{
	if (value == KS_TRUE) {
		ks_json_add_true_to_array(array);
	} else {
		ks_json_add_false_to_array(array);
	}
}

KS_DECLARE(void) ks_json_add_item_to_object(ks_json_t *object, const char * const string, ks_json_t *item)
{
	// TODO check if item parent is NULL
	cJSON_AddItemToObject(object, string, item);
}

KS_DECLARE(void) ks_json_add_true_to_object(ks_json_t * const object, const char * const string)
{
	ks_json_add_item_to_object(object, string, ks_json_create_true());
}

KS_DECLARE(void) ks_json_add_false_to_object(ks_json_t * const object, const char * const string)
{
	ks_json_add_item_to_object(object, string, ks_json_create_false());
}

KS_DECLARE(void) ks_json_add_bool_to_object(ks_json_t * const object, const char * const string, ks_bool_t value)
{
	if (value == KS_TRUE) {
		ks_json_add_true_to_object(object, string);
	} else {
		ks_json_add_false_to_object(object, string);
	}
}

KS_DECLARE(void) ks_json_add_number_to_object(ks_json_t * const object, const char * const name, const double number)
{
	ks_json_add_item_to_object(object, name, ks_json_create_number(number));
}

KS_DECLARE(void) ks_json_add_string_to_object(ks_json_t * const object, const char * const name, const char * const string)
{
	ks_json_add_item_to_object(object, name, ks_json_create_string(string));
}

KS_DECLARE(ks_json_t *) ks_json_duplicate(const ks_json_t * c, ks_bool_t recurse)
{
	return cJSON_Duplicate(c, recurse);
}

KS_DECLARE(void) ks_json_delete(ks_json_t **c)
{
	if (!c || !*c)
		return;

	cJSON_Delete(*c);
	*c = NULL;
}

KS_DECLARE(void) ks_json_delete_item_from_array(ks_json_t *array, int index)
{
	cJSON_DeleteItemFromArray(array, index);
}

KS_DECLARE(void) ks_json_delete_item_from_object(ks_json_t *obj, const char * const key)
{
	cJSON_DeleteItemFromObject(obj, key);
}

// Get apis
KS_DECLARE(ks_json_t *) ks_json_get_array_item(const ks_json_t * const array, int index)
{
	return cJSON_GetArrayItem(array, index);
}

KS_DECLARE(ks_bool_t) ks_json_get_array_bool(const ks_json_t * const array, int index, ks_bool_t def)
{
	ks_bool_t retval = def;
	const ks_json_t *item = ks_json_get_array_item(array, index);
	ks_json_value_bool(item, &retval);
	return retval;
}

KS_DECLARE(const char * const) ks_json_get_array_string(const ks_json_t * const array, int index, const char* def)
{
	const char *retval = def;
	const ks_json_t *item = ks_json_get_array_item(array, index);
	ks_json_value_string(item, &retval);
	return retval;
}

KS_DECLARE(int) ks_json_get_array_number_int(const ks_json_t * const array, int index, int def)
{
	int retval = def;
	const ks_json_t *item = ks_json_get_array_item(array, index);
	ks_json_value_number_int(item, &retval);
	return retval;
}

KS_DECLARE(double) ks_json_get_array_number_double(const ks_json_t * const array, int index, double def)
{
	double retval = def;
	const ks_json_t *item = ks_json_get_array_item(array, index);
	ks_json_value_number_double(item, &retval);
	return retval;
}

KS_DECLARE(int) ks_json_get_array_size(const ks_json_t * const array)
{
	return cJSON_GetArraySize(array);
}

KS_DECLARE(ks_json_t *) ks_json_get_object_item(const ks_json_t * const object, const char * const string)
{
	return cJSON_GetObjectItemCaseSensitive(object, string);
}

KS_DECLARE(ks_bool_t) ks_json_get_object_bool_def(const ks_json_t * const object, const char * const string, ks_bool_t def)
{
	ks_bool_t retval = def;
	ks_json_value_bool(object, &retval);
	return retval;
}

KS_DECLARE(const char * const) ks_json_get_object_string(const ks_json_t * const object, const char * const key, const char * def)
{
	const char *retval = def;
	ks_json_value_string(object, &retval);
	return retval;
}

KS_DECLARE(int) ks_json_get_object_number_int(const ks_json_t * const object, const char * const key, int def)
{
	int retval = def;
	ks_json_value_number_int(object, &retval);
	return retval;
}

KS_DECLARE(double) ks_json_get_object_number_double(const ks_json_t * const object, const char * const key, double def)
{
	double retval = def;
	ks_json_value_number_double(object, &retval);
	return retval;
}

KS_DECLARE(char *) ks_json_print(const ks_json_t * const item)
{
	return cJSON_Print(item);
}

KS_DECLARE(char *) ks_json_print_unformatted(const ks_json_t * const item)
{
	return cJSON_PrintUnformatted(item);
}

KS_DECLARE(ks_json_type_t) ks_json_type_get(const ks_json_t * const item)
{
	if (!item)
		return KS_JSON_TYPE_INVALID;
	return item->type;
}

KS_DECLARE(ks_bool_t) ks_json_type_is(const ks_json_t * const item, ks_json_type_t type)
{
	return ks_json_type_get(item) == type;
}

KS_DECLARE(ks_bool_t) ks_json_type_is_array(const ks_json_t * const item)
{
	return ks_json_type_is(item, KS_JSON_TYPE_ARRAY);
}

KS_DECLARE(ks_bool_t) ks_json_type_is_string(const ks_json_t * const item)
{
	return ks_json_type_is(item, KS_JSON_TYPE_STRING);
}

KS_DECLARE(ks_bool_t) ks_json_type_is_number(const ks_json_t * const item)
{
	return ks_json_type_is(item, KS_JSON_TYPE_NUMBER);
}

KS_DECLARE(ks_bool_t) ks_json_type_is_null(const ks_json_t * const item)
{
	return ks_json_type_is(item, KS_JSON_TYPE_NULL);
}

KS_DECLARE(ks_bool_t) ks_json_type_is_object(const ks_json_t * const item)
{
	return ks_json_type_is(item, KS_JSON_TYPE_OBJECT);
}

KS_DECLARE(ks_bool_t) ks_json_type_is_false(const ks_json_t * const item)
{
	return ks_json_type_is(item, KS_JSON_TYPE_FALSE);
}

KS_DECLARE(ks_bool_t) ks_json_type_is_true(const ks_json_t * const item)
{
	return ks_json_type_is(item, KS_JSON_TYPE_TRUE);
}

KS_DECLARE(ks_bool_t) ks_json_type_is_bool(const ks_json_t * const item)
{
	return ks_json_type_is(item, KS_JSON_TYPE_FALSE) || ks_json_type_is(item, KS_JSON_TYPE_TRUE);
}

// Value apis
KS_DECLARE(ks_status_t) ks_json_value_string(const ks_json_t * const item, const char **value)
{
	if (!ks_json_type_is_string(item)) {
		return KS_STATUS_FAIL;
	}
	*value = item->valuestring;
	return KS_STATUS_SUCCESS;
}

KS_DECLARE(ks_status_t) ks_json_value_number_int(const ks_json_t * const item, int *value)
{
	if (!ks_json_type_is_number(item)) {
		return KS_STATUS_FAIL;
	}
	*value = item->valueint;
	return KS_STATUS_SUCCESS;
}

KS_DECLARE(ks_status_t) ks_json_value_number_double(const ks_json_t * const item, double *value)
{
	if (!ks_json_type_is_number(item)) {
		return KS_STATUS_FAIL;
	}
	*value = item->valuedouble;
	return KS_STATUS_SUCCESS;
}

KS_DECLARE(ks_status_t) ks_json_value_bool(const ks_json_t * const item, ks_bool_t *value)
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

