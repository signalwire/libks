/*
 * Copyright (c) 2018 SignalWire, Inc
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

static KS_THREAD_LOCAL ks_pool_t *g_tag_pool;
static KS_THREAD_LOCAL const char *g_tag_file;
static KS_THREAD_LOCAL int g_tag_line;
static KS_THREAD_LOCAL const char *g_tag_tag;

static void *__json_alloc(size_t size)
{
	void *data = __ks_pool_alloc(g_tag_pool, size, g_tag_file, g_tag_line, g_tag_tag);
	ks_assert(data);
	return data;
}

static void __json_free(void *data)
{
	ks_free(data);
}

static void * __json_realloc(void *data, size_t size)
{
#if defined(KS_DEBUG_JSON)
	void *new_data = __ks_realloc(data, size, g_tag_file, g_tag_line, g_tag_tag);
#else
	void *new_data = ks_realloc(data, size);
#endif
	ks_assert(new_data);
	return new_data;
}

// Init
KS_DECLARE(void) ks_json_deinit(void)
{
	cJSON_InitHooks(NULL);
}

KS_DECLARE(void) ks_json_init(void)
{
	cJSON_Hooks hooks = {__json_alloc, __json_free, __json_realloc};
	cJSON_InitHooks(&hooks);
}

KS_DECLARE(void) ks_json_free_ex(void **data)
{
	if (data && *data) {
		ks_free(*data);
		*data = NULL;
	}
}

// Create apis
KS_DECLARE(ks_json_t *) __ks_json_create_array(ks_pool_t *pool, const char *file, int line, const char *tag)
{
	KS_JSON_DEBUG_TAG_SET
	return cJSON_CreateArray();
}

KS_DECLARE(ks_json_t *) __ks_json_create_array_inline(ks_pool_t *pool, const char *file, int line, const char *tag, uint32_t count, ...)
{
	va_list ap;
	ks_json_t *array;

	KS_JSON_DEBUG_TAG_SET

	if (!(array = __ks_json_create_array(pool, file, line, tag)))
		return NULL;

	va_start(ap, count);

	for (uint32_t index = 0; index < count; index++) {
		if (!ks_json_add_item_to_array(array, va_arg(ap, ks_json_t *))) {
			va_end(ap);
			ks_json_delete(&array);
			return NULL;
		}
	}

	va_end(ap);
	return array;
}

KS_DECLARE(ks_json_t *) __ks_json_create_object(ks_pool_t *pool, const char *file, int line, const char *tag)
{
	KS_JSON_DEBUG_TAG_SET

	return cJSON_CreateObject();
}

KS_DECLARE(ks_json_t *) __ks_json_create_number(ks_pool_t *pool, const char *file, int line, const char *tag, double number)
{
	KS_JSON_DEBUG_TAG_SET

	return cJSON_CreateNumber(number);
}

KS_DECLARE(ks_json_t *) __ks_json_create_string_fmt(ks_pool_t *pool, const char *file, int line, const char *tag, const char *fmt, ...)
{
	va_list ap;
	char *str;
	cJSON *item;

	KS_JSON_DEBUG_TAG_SET

	va_start(ap, fmt);
	str = ks_vmprintf(fmt, ap);
	va_end(ap);

	if (!str) return NULL;

	item = cJSON_CreateString(str);

	free(str);	/* Managed by glibc for the formatting allocation */

	return item;
}

KS_DECLARE(ks_json_t *) __ks_json_create_string(ks_pool_t *pool, const char *file, int line, const char *tag, const char *string)
{
	KS_JSON_DEBUG_TAG_SET
	return cJSON_CreateString(string);
}

KS_DECLARE(ks_json_t *) __ks_json_create_false(ks_pool_t *pool, const char *file, int line, const char *tag)
{
	KS_JSON_DEBUG_TAG_SET
	return cJSON_CreateFalse();
}

KS_DECLARE(ks_json_t *) __ks_json_create_true(ks_pool_t *pool, const char *file, int line, const char *tag)
{
	KS_JSON_DEBUG_TAG_SET
	return cJSON_CreateTrue();
}

KS_DECLARE(ks_json_t *) __ks_json_create_null(ks_pool_t *pool, const char *file, int line, const char *tag)
{
	KS_JSON_DEBUG_TAG_SET
	return cJSON_CreateNull();
}

KS_DECLARE(ks_json_t *) __ks_json_create_bool(ks_pool_t *pool, const char *file, int line, const char *tag, ks_bool_t value)
{
	KS_JSON_DEBUG_TAG_SET
	ks_assert(value == KS_TRUE || value == KS_FALSE);
	return cJSON_CreateBool(value);
}

KS_DECLARE(ks_json_t *) __ks_json_create_uuid(ks_pool_t *pool, const char *file, int line, const char *tag, ks_uuid_t uuid)
{
	KS_JSON_DEBUG_TAG_SET
	char *uuid_str = ks_uuid_str(NULL, &uuid);
	ks_json_t *item;
	if (!uuid_str)
		return NULL;

	item = __ks_json_create_string(pool, file, line, tag, uuid_str);
	ks_pool_free(&uuid_str);
	return item;

}

// Parse apis
KS_DECLARE(ks_json_t *) __ks_json_parse(ks_pool_t *pool, const char *file, int line, const char *tag, const char *value)
{
	KS_JSON_DEBUG_TAG_SET
	ks_assert(value);
	return cJSON_Parse(value);
}

// Add apis
KS_DECLARE(ks_json_t *) ks_json_add_item_to_array(ks_json_t *array, ks_json_t *item)
{
	ks_assert(array);
	ks_assert(ks_json_type_is_array(array));
	cJSON_AddItemToArray(array, item);
	return item;
}

KS_DECLARE(ks_json_t *) __ks_json_add_uuid_to_array(ks_pool_t *pool, const char *file, int line, const char *tag, ks_json_t *object, ks_uuid_t uuid)
{
	ks_json_t *item = __ks_json_create_uuid(pool, file, line, tag, uuid);
	KS_JSON_DEBUG_TAG_SET

	if (!item)
		return NULL;
	return ks_json_add_item_to_array(object, item);
}

KS_DECLARE(ks_json_t*) __ks_json_add_string_to_array(ks_pool_t *pool, const char *file, int line, const char *tag, ks_json_t * const array, const char * const string)
{
	ks_json_t *item;
	KS_JSON_DEBUG_TAG_SET

	ks_assert(array);
	ks_assert(ks_json_type_is_array(array));
	item = cJSON_CreateString(string);
	ks_assert(item);
	cJSON_AddItemToArray(array, item);
	return item;
}

KS_DECLARE(ks_json_t*) __ks_json_add_number_to_array(ks_pool_t *pool, const char *file, int line, const char *tag, ks_json_t * const array, const double number)
{
	ks_json_t *item;
	KS_JSON_DEBUG_TAG_SET

	ks_assert(array);
	ks_assert(ks_json_type_is_array(array));
	item = cJSON_CreateNumber(number);
	ks_assert(item);
	cJSON_AddItemToArray(array, item);
	return item;
}

KS_DECLARE(ks_json_t *) __ks_json_add_true_to_array(ks_pool_t *pool, const char *file, int line, const char *tag, ks_json_t *const array)
{
	ks_json_t *item;
	KS_JSON_DEBUG_TAG_SET

	ks_assert(array);
	ks_assert(ks_json_type_is_array(array));

	item = cJSON_CreateTrue();

	cJSON_AddItemToArray(array, item);

	return item;
}

KS_DECLARE(ks_json_t *) __ks_json_add_false_to_array(ks_pool_t *pool, const char *file, int line, const char *tag, ks_json_t * const array)
{
	ks_json_t *item;
	KS_JSON_DEBUG_TAG_SET

	ks_assert(array);
	ks_assert(ks_json_type_is_array(array));

	item = cJSON_CreateFalse();

	cJSON_AddItemToArray(array, item);

	return item;
}

KS_DECLARE(ks_json_t *) __ks_json_add_bool_to_array(ks_pool_t *pool, const char *file, int line, const char *tag, ks_json_t * const object, ks_bool_t value)
{
	if (value)
		return __ks_json_add_true_to_array(pool, file, line, tag, object);
	else
		return __ks_json_add_false_to_array(pool, file, line, tag, object);
}

KS_DECLARE(ks_json_t *) ks_json_add_item_to_object(ks_json_t *object, const char * const string, ks_json_t *item)
{
	ks_assert(object);
	ks_assert(ks_json_type_is_object(object));
	cJSON_AddItemToObject(object, string, item);
	return item;
}

KS_DECLARE(ks_json_t *) __ks_json_add_uuid_to_object(ks_pool_t *pool, const char *file, int line, const char *tag, ks_json_t *object, const char * const string, ks_uuid_t uuid)
{
	ks_json_t *item = __ks_json_create_uuid(pool, file, line, tag, uuid);
	KS_JSON_DEBUG_TAG_SET

	if (!item)
		return NULL;

	return ks_json_add_item_to_object(object, string, item);
}

KS_DECLARE(ks_json_t *) __ks_json_add_true_to_object(ks_pool_t *pool, const char *file, int line, const char *tag, ks_json_t * const object, const char * const string)
{
	ks_json_t *item;
	KS_JSON_DEBUG_TAG_SET

	ks_assert(object);
	ks_assert(ks_json_type_is_object(object));

	item = cJSON_CreateTrue();

	cJSON_AddItemToObject(object, string, item);

	return item;
}

KS_DECLARE(ks_json_t *) __ks_json_add_false_to_object(ks_pool_t *pool, const char *file, int line, const char *tag, ks_json_t * const object, const char * const string)
{
	ks_json_t *item;
	KS_JSON_DEBUG_TAG_SET

	ks_assert(object);
	ks_assert(ks_json_type_is_object(object));

	item = cJSON_CreateFalse();

	cJSON_AddItemToObject(object, string, item);

	return item;
}

KS_DECLARE(ks_json_t *) __ks_json_add_bool_to_object(ks_pool_t *pool, const char *file, int line, const char *tag, ks_json_t * const object, const char * const string, ks_bool_t value)
{
	KS_JSON_DEBUG_TAG_SET
	if (value)
		return __ks_json_add_true_to_object(pool, file, line, tag, object, string);
	else
		return __ks_json_add_false_to_object(pool, file, line, tag, object, string);
}

KS_DECLARE(ks_json_t*) __ks_json_add_number_to_object(ks_pool_t *pool, const char *file, int line,
	const char *tag, ks_json_t * const object, const char * const name, const double number)
{
	KS_JSON_DEBUG_TAG_SET
	ks_assert(object);
	ks_assert(ks_json_type_is_object(object));
	return cJSON_AddNumberToObject(object, name, number);
}

KS_DECLARE(ks_json_t*) __ks_json_add_string_to_object(ks_pool_t *pool, const char *file, int line, const char *tag, ks_json_t * const object, const char * const name, const char * const string)
{
	KS_JSON_DEBUG_TAG_SET
	ks_assert(object);
	ks_assert(ks_json_type_is_object(object));
	return cJSON_AddStringToObject(object, name, string);
}


// Dupe apis
KS_DECLARE(ks_json_t *) __ks_json_duplicate(ks_pool_t *pool, const char *file, int line, const char *tag, const ks_json_t * c, ks_bool_t recurse)
{
	KS_JSON_DEBUG_TAG_SET
	ks_assert(c);
	return cJSON_Duplicate(c, recurse);
}

// Delete apis
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
	ks_json_t *item;

	ks_assert(array);
	item = cJSON_GetArrayItem(array, index);
	ks_assert(item);

	return item;
}

KS_DECLARE(ks_json_t *) ks_json_get_array_item_safe(const ks_json_t * const array, int index)
{
	return cJSON_GetArrayItem(array, index);
}

KS_DECLARE(ks_bool_t) ks_json_get_array_bool(const ks_json_t * const array, int index)
{
	const ks_json_t *item;

	ks_assert(ks_json_type_is_array(array));

	item = cJSON_GetArrayItem(array, index);

	ks_assert(item);

	return item->type == cJSON_True ? KS_TRUE : KS_FALSE;
}

KS_DECLARE(const char * const) ks_json_get_array_cstr(const ks_json_t * const array, int index)
{
	const ks_json_t *item = ks_json_get_array_item(array, index);
	ks_assert(ks_json_type_is_string(item));
	return item->valuestring;
}

KS_DECLARE(int) ks_json_get_array_number_int(const ks_json_t * const array, int index)
{
	const ks_json_t *item = ks_json_get_array_item(array, index);
	ks_assert(ks_json_type_is_number(item));
	return item->valueint;
}

KS_DECLARE(ks_uuid_t) ks_json_get_array_uuid(const ks_json_t * const array, int index)
{
	const ks_json_t *item = ks_json_get_array_item(array, index);
	ks_assert(ks_json_type_is_string(item));
	return ks_uuid_from_str(item->valuestring);
}

KS_DECLARE(double) ks_json_get_array_number_double(const ks_json_t * const array, int index)
{
	const ks_json_t *item = ks_json_get_array_item(array, index);
	ks_assert(ks_json_type_is_number(item));
	return item->valuedouble;
}

KS_DECLARE(int) ks_json_get_array_size(const ks_json_t * const array)
{
	ks_assert(array);
	return cJSON_GetArraySize(array);
}

KS_DECLARE(ks_json_t *) ks_json_get_object_item(const ks_json_t * const object, const char * const string)
{
	ks_assert(object && string);
	return cJSON_GetObjectItemCaseSensitive(object, string);
}

KS_DECLARE(ks_json_t *) ks_json_get_object_item_safe(const ks_json_t * const object, const char * const string)
{
	return cJSON_GetObjectItemCaseSensitive(object, string);
}

KS_DECLARE(ks_uuid_t) ks_json_get_object_uuid(const ks_json_t *const object, const char * const string)
{
	const ks_json_t *item = ks_json_get_object_item(object, string);
	if (!ks_json_type_is_string(item)) return ks_uuid_null();
	return ks_uuid_from_str(item->valuestring);
}

KS_DECLARE(ks_bool_t) ks_json_get_object_bool(const ks_json_t * const object, const char * const string)
{
	const ks_json_t *item = ks_json_get_object_item(object, string);
	ks_assert(ks_json_type_is_bool(item));
	if (item->type == cJSON_True) {
		return KS_TRUE;
	} else {
		return KS_FALSE;
	}
}
KS_DECLARE(ks_bool_t) ks_json_get_object_bool_def(const ks_json_t * const object, const char * const string, ks_bool_t def)
{
	const ks_json_t *item = ks_json_get_object_item_safe(object, string);
	if (item && item->type == cJSON_True) {
		return KS_TRUE;
	} else if (item && item->type == cJSON_False) {
		return KS_FALSE;
	}
	return def;
}

KS_DECLARE(const char * const) ks_json_get_object_cstr(const ks_json_t * const object, const char * const key)
{
	const ks_json_t *item = ks_json_get_object_item(object, key);
	if (item) {
		ks_assert(ks_json_type_is_string(item));
		return item->valuestring;
	}
	return NULL;
}

KS_DECLARE(const char * const) ks_json_get_object_cstr_def(const ks_json_t * const object, const char * const key, const char * def)
{
	const ks_json_t *item = ks_json_get_object_item_safe(object, key);
	if (!item || !ks_json_type_is_string(item) || !item->valuestring) {
		return def;
	}
	return item->valuestring;
}

KS_DECLARE(int) ks_json_get_object_number_int(const ks_json_t * const object, const char * const key)
{
	const ks_json_t *item = ks_json_get_object_item(object, key);
	ks_assert(ks_json_type_is_number(item));
	return item->valueint;
}

KS_DECLARE(int) ks_json_get_object_number_int_def(const ks_json_t * const object, const char * const key, int def)
{
	const ks_json_t *item = ks_json_get_object_item_safe(object, key);
	if (item && ks_json_type_is_number(item)) return item->valueint;
	return def;
}

KS_DECLARE(double) ks_json_get_object_number_double(const ks_json_t * const object, const char * const key)
{
	const ks_json_t *item = ks_json_get_object_item(object, key);
	ks_assert(ks_json_type_is_number(item));
	return item->valuedouble;
}

KS_DECLARE(double) ks_json_get_object_number_double_def(const ks_json_t * const object, const char * const key, double def)
{
	const ks_json_t *item = ks_json_get_object_item_safe(object, key);
	if (item && ks_json_type_is_number(item)) return item->valuedouble;
	return def;
}

// Lookup apis

/**
 * ks_json_lookup_item - Looks up an item by multiple objects key lookups all in one
 * function call. Each variable argument at the end of this functions signature causes a lookup
 * in the previously fetched object.
 */
KS_DECLARE(ks_json_t *) ks_json_valookup(const ks_json_t * const object, int components, va_list args)
{
	const ks_json_t *next_item = object;
	uint32_t index = 0;

	for (int index = 0; index < components && next_item; index++) {
		next_item = ks_json_get_object_item_safe(next_item, va_arg(args, const char *));
	}

	return (ks_json_t *)next_item;
}

KS_DECLARE(ks_json_t *) ks_json_lookup(const ks_json_t * const object, int components, ...)
{
	const ks_json_t *next_item;
	uint32_t index = 0;
	va_list argptr;

	va_start(argptr, components);

	next_item = ks_json_valookup(object, components, argptr);

	va_end(argptr);

	return (ks_json_t *)next_item;
}

KS_DECLARE(const char * const) ks_json_lookup_cstr(const ks_json_t * const object, int components, ...)
{
	ks_json_t *item;
	va_list argptr;

	va_start(argptr, components);

	item = ks_json_valookup(object, components, argptr);

	va_end(argptr);

	if (item)
		return ks_json_value_string(item);

	return NULL;
}

KS_DECLARE(double *) ks_json_lookup_number_doubleptr(const ks_json_t * const object, int components, ...)
{
	ks_json_t *item;
	va_list argptr;

	va_start(argptr, components);

	item = ks_json_valookup(object, components, argptr);

	va_end(argptr);

	if (item)
		return ks_json_value_number_doubleptr(item);

	return NULL;
}

KS_DECLARE(int *) ks_json_lookup_number_intptr(const ks_json_t * const object, int components, ...)
{
	ks_json_t *item;
	va_list argptr;

	va_start(argptr, components);

	item = ks_json_valookup(object, components, argptr);

	va_end(argptr);

	if (item)
		return ks_json_value_number_intptr(item);

	return NULL;
}

KS_DECLARE(ks_uuid_t) ks_json_lookup_uuid(const ks_json_t * const object, int components, ...)
{
	ks_json_t *item;
	va_list argptr;

	va_start(argptr, components);

	item = ks_json_valookup(object, components, argptr);

	va_end(argptr);

	if (item)
		return ks_json_value_uuid(item);

	return ks_uuid_null(); /* null indicates failure */
}

KS_DECLARE(ks_json_t *) ks_json_lookup_array_item(const ks_json_t * const object, int array_index, int components, ...)
{
	ks_json_t *item;
	va_list argptr;

	va_start(argptr, components);

	item = ks_json_valookup(object, components, argptr);

	va_end(argptr);

	if (item)
		return ks_json_get_array_item_safe(item, array_index);

	return NULL;
}

KS_DECLARE(char *) __ks_json_lookup_print(ks_pool_t *pool, const char *file, int line, const char *tag,
		const ks_json_t * const object, int components, ...)
{
	ks_json_t *item;
	va_list argptr;

	va_start(argptr, components);

	item = ks_json_valookup(object, components, argptr);

	va_end(argptr);

	if (item)
		return __ks_json_print(pool, file, line, tag, item);

	return NULL;
}

KS_DECLARE(char *) __ks_json_lookup_print_unformatted(ks_pool_t *pool, const char *file, int line, const char *tag,
	const ks_json_t * const object, int components, ...)
{
	ks_json_t *item;
	va_list argptr;

	va_start(argptr, components);

	item = ks_json_valookup(object, components, argptr);

	va_end(argptr);

	if (item)
		return __ks_json_print_unformatted(pool, file, line, tag, item);

	return NULL;
}

// Print apis
KS_DECLARE(char *) __ks_json_print(ks_pool_t *pool, const char *file, int line, const char *tag, const ks_json_t * const item)
{
	KS_JSON_DEBUG_TAG_SET
	ks_assert(item);
	return cJSON_Print(item);
}

KS_DECLARE(char *) __ks_json_print_unformatted(ks_pool_t *pool, const char *file, int line, const char *tag, const ks_json_t * const item)
{
	KS_JSON_DEBUG_TAG_SET
	ks_assert(item);
	return cJSON_PrintUnformatted(item);
}

// Type apis
KS_DECLARE(KS_JSON_TYPES) ks_json_type_get(const ks_json_t * const item)
{
	if (!item)
		return KS_JSON_TYPE_INVALID;
	return item->type;
}

KS_DECLARE(ks_bool_t) ks_json_type_is(const ks_json_t * const item, KS_JSON_TYPES type)
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
KS_DECLARE(const char * const) ks_json_value_string(const ks_json_t * const item)
{
	ks_assert(ks_json_type_is_string(item));
	return item->valuestring;
}

KS_DECLARE(int) ks_json_value_number_int(const ks_json_t * const item)
{
	ks_assert(ks_json_type_is_number(item));
	return item->valueint;
}

KS_DECLARE(double) ks_json_value_number_double(const ks_json_t * const item)
{
	ks_assert(ks_json_type_is_number(item));
	return item->valuedouble;
}

KS_DECLARE(int*) ks_json_value_number_intptr(ks_json_t *item)
{
	ks_assert(ks_json_type_is_number(item));
	return &item->valueint;
}

KS_DECLARE(double*) ks_json_value_number_doubleptr(ks_json_t *item)
{
	ks_assert(ks_json_type_is_number(item));
	return &item->valuedouble;
}

KS_DECLARE(ks_uuid_t) ks_json_value_uuid(const ks_json_t * const item)
{
	ks_assert(ks_json_type_is_string(item));
	return ks_uuid_from_str(item->valuestring);
}

KS_DECLARE(ks_bool_t) ks_json_value_bool(const ks_json_t * const item)
{
	ks_assert(ks_json_type_is_bool(item));
	if (item->type == KS_JSON_TYPE_FALSE) {
		return KS_FALSE;
	} else {
		return KS_TRUE;
	}
}

// Enum helpers
KS_DECLARE(ks_json_t *) ks_json_enum_child(ks_json_t *item)
{
	ks_assert(item);
	return item->child;
}

KS_DECLARE(ks_json_t *) ks_json_enum_next(ks_json_t *item)
{
	ks_assert(item);
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

