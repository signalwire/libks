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

#pragma once

KS_BEGIN_EXTERN_C

typedef struct cJSON ks_json_t;

typedef enum {
	KS_JSON_TYPE_INVALID = 0,
	KS_JSON_TYPE_FALSE   = KS_BIT_FLAG(0),
	KS_JSON_TYPE_TRUE    = KS_BIT_FLAG(1),
	KS_JSON_TYPE_NULL    = KS_BIT_FLAG(2),
	KS_JSON_TYPE_NUMBER  = KS_BIT_FLAG(3),
	KS_JSON_TYPE_STRING  = KS_BIT_FLAG(4),
	KS_JSON_TYPE_ARRAY   = KS_BIT_FLAG(5),
	KS_JSON_TYPE_OBJECT  = KS_BIT_FLAG(6),
	KS_JSON_TYPE_RAW     = KS_BIT_FLAG(7)
} KS_JSON_TYPES;

KS_DECLARE(void) ks_json_deinit(void);
KS_DECLARE(void) ks_json_init(void);

KS_DECLARE(void) ks_json_free_ex(void **data);
#define ks_json_free(jsonP) do { if (jsonP) ks_json_free_ex((void **)jsonP); } while (KS_FALSE)

#define KS_JSON_DEBUG_TAG_SET \
	g_tag_pool = pool;		\
	g_tag_file = file;		\
	g_tag_line = line;		\
	g_tag_tag = tag;

KS_DECLARE(ks_json_t *) __ks_json_create_string(ks_pool_t *pool, const char *file, int line, const char *tag, const char *string);
#define ks_json_create_string(string) __ks_json_create_string(NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, string)
#define ks_json_pcreate_string(pool, string) __ks_json_create_string(pool, __FILE__, __LINE__, __PRETTY_FUNCTION__, string)

KS_DECLARE(ks_json_t *) __ks_json_create_string_fmt(ks_pool_t *pool, const char *file, int line, const char *tag, const char *fmt, ...);
#define ks_json_create_string_fmt(fmt, ...) __ks_json_create_string_fmt(NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, fmt, __VA_ARGS__)
#define ks_json_pcreate_string_fmt(pool, fmt, ...) __ks_json_create_string_fmt(pool, __FILE__, __LINE__, __PRETTY_FUNCTION__, fmt, __VA_ARGS__)

KS_DECLARE(ks_json_t *) __ks_json_create_number(ks_pool_t *pool, const char *file, int line, const char *tag, double number);
#define ks_json_create_number(number) __ks_json_create_number(NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, number)
#define ks_json_pcreate_number(pool, number) __ks_json_create_number(pool, __FILE__, __LINE__, __PRETTY_FUNCTION__, number)

KS_DECLARE(ks_json_t *) __ks_json_create_array(ks_pool_t *pool, const char *file, int line, const char *tag);
#define ks_json_create_array() __ks_json_create_array(NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ks_json_pcreate_array(pool) __ks_json_create_array(pool, __FILE__, __LINE__, __PRETTY_FUNCTION__)

KS_DECLARE(ks_json_t *) __ks_json_create_array_inline(ks_pool_t *pool, const char *file, int line, const char *tag, uint32_t count, ...);
#define ks_json_create_array_inline(count, ...) __ks_json_create_array_inline(NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, count, __VA_ARGS__)
#define ks_json_pcreate_array_inline(pool, count, ...) __ks_json_create_array_inline(pool, __FILE__, __LINE__, __PRETTY_FUNCTION__, count, __VA_ARGS__)

KS_DECLARE(ks_json_t *) __ks_json_create_object(ks_pool_t *pool, const char *file, int line, const char *tag);
#define ks_json_create_object() __ks_json_create_object(NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ks_json_pcreate_object(pool) __ks_json_create_object(pool, __FILE__, __LINE__, __PRETTY_FUNCTION__)

KS_DECLARE(ks_json_t *) __ks_json_create_null(ks_pool_t *pool, const char *file, int line, const char *tag);
#define ks_json_create_null() __ks_json_create_null(NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ks_json_pcreate_null(pool) __ks_json_create_null(pool, __FILE__, __LINE__, __PRETTY_FUNCTION__)

KS_DECLARE(ks_json_t *) __ks_json_create_true(ks_pool_t *pool, const char *file, int line, const char *tag);
#define ks_json_create_true() __ks_json_create_true(NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ks_json_pcreate_true(pool) __ks_json_create_true(pool, __FILE__, __LINE__, __PRETTY_FUNCTION__)

KS_DECLARE(ks_json_t *) __ks_json_create_false(ks_pool_t *pool, const char *file, int line, const char *tag);
#define ks_json_create_false() __ks_json_create_false(NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ks_json_pcreate_false(pool) __ks_json_create_false(pool, __FILE__, __LINE__, __PRETTY_FUNCTION__)

KS_DECLARE(ks_json_t *) __ks_json_create_bool(ks_pool_t *pool, const char *file, int line, const char *tag, ks_bool_t value);
#define ks_json_create_bool(value) __ks_json_create_bool(NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, value)
#define ks_json_pcreate_bool(pool, value) __ks_json_create_bool(pool, __FILE__, __LINE__, __PRETTY_FUNCTION__, value)

KS_DECLARE(ks_json_t *) __ks_json_create_uuid(ks_pool_t *pool, const char *file, int line, const char *tag, ks_uuid_t uuid);
#define ks_json_create_uuid(uuid) __ks_json_create_uuid(NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, uuid)
#define ks_json_pcreate_uuid(pool, uuid) __ks_json_create_uuid(pool, __FILE__, __LINE__, __PRETTY_FUNCTION__, uuid)

KS_DECLARE(ks_json_t *) __ks_json_parse(ks_pool_t *pool, const char *file, int line, const char *tag, const char *value);
#define ks_json_parse(value) __ks_json_parse(NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, value)
#define ks_json_pparse(pool, value) __ks_json_parse(pool, __FILE__, __LINE__, __PRETTY_FUNCTION__, value)

KS_DECLARE(ks_json_t *) ks_json_add_item_to_array(ks_json_t *array, ks_json_t *item);

KS_DECLARE(ks_json_t *) __ks_json_add_uuid_to_array(ks_pool_t *pool, const char *file, int line, const char *tag, ks_json_t *array, ks_uuid_t uuid);
#define ks_json_add_uuid_to_array(array, uuid)	__ks_json_add_uuid_to_array(NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, array, uuid)
#define ks_json_padd_uuid_to_array(pool, array, uuid)	__ks_json_add_uuid_to_array(pool, __FILE__, __LINE__, __PRETTY_FUNCTION__, array, uuid)

KS_DECLARE(ks_json_t *) __ks_json_add_number_to_array(ks_pool_t *pool, const char *file, int line, const char *tag, ks_json_t * const array, const double number);
#define ks_json_add_number_to_array(array, number)	__ks_json_add_number_to_array(NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, array, number)
#define ks_json_padd_number_to_array(pool, array, number)	__ks_json_add_number_to_array(pool, __FILE__, __LINE__, __PRETTY_FUNCTION__, array, number)

KS_DECLARE(ks_json_t *) __ks_json_add_string_to_array(ks_pool_t *pool, const char *file, int line, const char *tag, ks_json_t * const array, const char * const string);
#define ks_json_add_string_to_array(array, string)	__ks_json_add_string_to_array(NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, array, string)
#define ks_json_padd_string_to_array(pool, array, string)	__ks_json_add_string_to_array(pool, __FILE__, __LINE__, __PRETTY_FUNCTION__, array, string)

KS_DECLARE(ks_json_t *) __ks_json_add_true_to_array(ks_pool_t *pool, const char *file, int line, const char *tag, ks_json_t * const array);
#define ks_json_add_true_to_array(array)	__ks_json_add_true_to_array(NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, array)
#define ks_json_padd_true_to_array(pool, array)	__ks_json_add_true_to_array(pool, __FILE__, __LINE__, __PRETTY_FUNCTION__, array)

KS_DECLARE(ks_json_t *) __ks_json_add_false_to_array(ks_pool_t *pool, const char *file, int line, const char *tag, ks_json_t * const array);
#define ks_json_add_false_to_array(array)	__ks_json_add_false_to_array(NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, array)
#define ks_json_padd_false_to_array(pool, array)	__ks_json_add_false_to_array(pool, __FILE__, __LINE__, __PRETTY_FUNCTION__, array)

KS_DECLARE(ks_json_t *) __ks_json_add_bool_to_array(ks_pool_t *pool, const char *file, int line, const char *tag, ks_json_t * const array, ks_bool_t value);
#define ks_json_add_bool_to_array(array, value)		__ke_json_add_bool_to_array(NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, array, value)
#define ks_json_padd_bool_to_array(pool, array, value)		__ke_json_add_bool_to_array(pool, __FILE__, __LINE__, __PRETTY_FUNCTION__, array, value)

KS_DECLARE(ks_json_t *) ks_json_add_item_to_object(ks_json_t *object, const char * const string, ks_json_t *item);

KS_DECLARE(ks_json_t *) __ks_json_add_uuid_to_object(ks_pool_t *pool, const char *file, int line, const char *tag, ks_json_t *object, const char * const string, ks_uuid_t uuid);
#define ks_json_add_uuid_to_object(object, string, uuid)		__ks_json_add_uuid_to_object(NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, object, string, uuid)
#define ks_json_padd_uuid_to_object(pool, object, string, uuid)		__ks_json_add_uuid_to_object(pool, __FILE__, __LINE__, __PRETTY_FUNCTION__, object, string, uuid)

KS_DECLARE(ks_json_t *) __ks_json_add_number_to_object(ks_pool_t *pool, const char *file, int line, const char *tag, ks_json_t * const object, const char * const name, const double number);
#define ks_json_add_number_to_object(object, name, number)	__ks_json_add_number_to_object(NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, object, name, number)
#define ks_json_padd_number_to_object(pool, object, name, number)	__ks_json_add_number_to_object(pool, __FILE__, __LINE__, __PRETTY_FUNCTION__, object, name, number)

KS_DECLARE(ks_json_t *) __ks_json_add_string_to_object(ks_pool_t *pool, const char *file, int line, const char *tag, ks_json_t * const object, const char * const name, const char * const string);
#define ks_json_add_string_to_object(object, name, string) __ks_json_add_string_to_object(NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, object, name, string)
#define ks_json_padd_string_to_object(pool, object, name, string) __ks_json_add_string_to_object(pool, __FILE__, __LINE__, __PRETTY_FUNCTION__, object, name, string)

KS_DECLARE(ks_json_t *) __ks_json_add_false_to_object(ks_pool_t *pool, const char *file, int line, const char *tag, ks_json_t * const object, const char * const name);
#define ks_json_add_false_to_object(object, name)	__ks_json_add_false_to_object(NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, object, name)
#define ks_json_padd_false_to_object(pool, object, name)	__ks_json_add_false_to_object(pool, __FILE__, __LINE__, __PRETTY_FUNCTION__, object, name)

KS_DECLARE(ks_json_t *) __ks_json_add_true_to_object(ks_pool_t *pool, const char *file, int line, const char *tag, ks_json_t * const object, const char * const name);
#define ks_json_add_true_to_object(object, name)	__ks_json_add_true_to_object(NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, object, name)
#define ks_json_padd_true_to_object(pool, object, name)	__ks_json_add_true_to_object(pool, __FILE__, __LINE__, __PRETTY_FUNCTION__, object, name)

KS_DECLARE(ks_json_t *) __ks_json_add_bool_to_object(ks_pool_t *pool, const char *file, int line, const char *tag, ks_json_t * const array, const char * const name, ks_bool_t value);
#define ks_json_add_bool_to_object(array, name, value) __ks_json_add_bool_to_object(NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, array, name, value)
#define ks_json_padd_bool_to_object(pool, array, name, value) __ks_json_add_bool_to_object(pool, __FILE__, __LINE__, __PRETTY_FUNCTION__, array, name, value)

KS_DECLARE(ks_json_t *) __ks_json_add_false_to_object(ks_pool_t *pool, const char *file, int line, const char *tag, ks_json_t * const object, const char * const name);
#define ks_json_add_false_to_object(object, name) __ks_json_add_false_to_object(NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, object, name)
#define ks_json_padd_false_to_object(pool, object, name) __ks_json_add_false_to_object(pool, __FILE__, __LINE__, __PRETTY_FUNCTION__, object, name)

KS_DECLARE(ks_json_t *) __ks_json_duplicate(ks_pool_t *pool, const char *file, int line, const char *tag, const ks_json_t *item, ks_bool_t recurse);
#define ks_json_duplicate(item, recurse)	__ks_json_duplicate(NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, item, recurse)
#define ks_json_pduplicate(pool, item, recurse)	__ks_json_duplicate(pool, __FILE__, __LINE__, __PRETTY_FUNCTION__, item, recurse)

KS_DECLARE(void) ks_json_delete(ks_json_t **c);

KS_DECLARE(void) ks_json_delete_item_from_array(ks_json_t *array, int index);
KS_DECLARE(void) ks_json_delete_item_from_object(ks_json_t *object, const char * const key);

KS_DECLARE(ks_json_t *) ks_json_get_object_item(const ks_json_t * const object, const char * const string);
KS_DECLARE(ks_json_t *) ks_json_get_object_item_safe(const ks_json_t * const object, const char * const string);
KS_DECLARE(ks_bool_t) ks_json_get_object_bool(const ks_json_t * const object, const char * const string);
KS_DECLARE(ks_bool_t) ks_json_get_object_bool_def(const ks_json_t * const object, const char * const string, ks_bool_t def);
KS_DECLARE(const char * const) ks_json_get_object_cstr(const ks_json_t * const object, const char * const key);
KS_DECLARE(const char * const) ks_json_get_object_cstr_def(const ks_json_t * const object, const char * const key, const char * def);
KS_DECLARE(int) ks_json_get_object_number_int(const ks_json_t * const object, const char * const key);
KS_DECLARE(int) ks_json_get_object_number_int_def(const ks_json_t * const object, const char * const key, int def);
KS_DECLARE(double) ks_json_get_object_number_double(const ks_json_t * const object, const char * const key);
KS_DECLARE(double) ks_json_get_object_number_double_def(const ks_json_t * const object, const char * const key, double def);
KS_DECLARE(ks_uuid_t) ks_json_get_object_uuid(const ks_json_t * const object, const char * const key);

KS_DECLARE(ks_json_t *) ks_json_get_array_item(const ks_json_t * const array, int index);
KS_DECLARE(ks_json_t *) ks_json_get_array_item_safe(const ks_json_t * const array, int index);
KS_DECLARE(ks_bool_t) ks_json_get_array_bool(const ks_json_t * const array, int index);
KS_DECLARE(int) ks_json_get_array_size(const ks_json_t * const array);
KS_DECLARE(const char * const) ks_json_get_array_cstr(const ks_json_t * const array, int index);
KS_DECLARE(int) ks_json_get_array_number_int(const ks_json_t * const array, int index);
KS_DECLARE(double) ks_json_get_array_number_double(const ks_json_t * const array, int index);
KS_DECLARE(ks_uuid_t) ks_json_get_array_uuid(const ks_json_t * const object, int index);

KS_DECLARE(char *) __ks_json_print(ks_pool_t *pool, const char *file, int line, const char *tag, const ks_json_t * const item);
#define ks_json_print(item)	__ks_json_print(NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, item)
#define ks_json_pprint(pool, item)	__ks_json_print(pool, __FILE__, __LINE__, __PRETTY_FUNCTION__, item)

KS_DECLARE(char *) __ks_json_print_unformatted(ks_pool_t *pool, const char *file, int line, const char *tag, const ks_json_t * const item);
#define ks_json_print_unformatted(item)	__ks_json_print_unformatted(NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, item)
#define ks_json_pprint_unformatted(pool, item)	__ks_json_print_unformatted(pool, __FILE__, __LINE__, __PRETTY_FUNCTION__, item)

KS_DECLARE(const char * const) ks_json_value_string(const ks_json_t * const item);
KS_DECLARE(int) ks_json_value_number_int(const ks_json_t * const item);
KS_DECLARE(double) ks_json_value_number_double(const ks_json_t * const item);
KS_DECLARE(int*) ks_json_value_number_intptr(ks_json_t *item);
KS_DECLARE(double*) ks_json_value_number_doubleptr(ks_json_t *item);
KS_DECLARE(ks_bool_t) ks_json_value_bool(const ks_json_t * const item);
KS_DECLARE(ks_uuid_t) ks_json_value_uuid(const ks_json_t * const item);

KS_DECLARE(KS_JSON_TYPES) ks_json_type_get(const ks_json_t * const item);
KS_DECLARE(ks_bool_t) ks_json_type_is(const ks_json_t * const item, KS_JSON_TYPES type);
KS_DECLARE(ks_bool_t) ks_json_type_is_array(const ks_json_t * const item);
KS_DECLARE(ks_bool_t) ks_json_type_is_string(const ks_json_t * const item);
KS_DECLARE(ks_bool_t) ks_json_type_is_number(const ks_json_t * const item);
KS_DECLARE(ks_bool_t) ks_json_type_is_null(const ks_json_t * const item);
KS_DECLARE(ks_bool_t) ks_json_type_is_object(const ks_json_t * const item);
KS_DECLARE(ks_bool_t) ks_json_type_is_false(const ks_json_t * const item);
KS_DECLARE(ks_bool_t) ks_json_type_is_true(const ks_json_t * const item);
KS_DECLARE(ks_bool_t) ks_json_type_is_bool(const ks_json_t * const item);

KS_DECLARE(ks_json_t *) ks_json_lookup(const ks_json_t * const object, int components, ...);
KS_DECLARE(ks_json_t *) ks_json_valookup(const ks_json_t * const object, int components, va_list argptr);
KS_DECLARE(const char * const) ks_json_lookup_cstr(const ks_json_t * const object, int components, ...);
KS_DECLARE(double *) ks_json_lookup_number_doubleptr(const ks_json_t * const object, int components, ...);
KS_DECLARE(int *) ks_json_lookup_number_intptr(const ks_json_t * const object, int components, ...);
KS_DECLARE(ks_uuid_t) ks_json_lookup_uuid(const ks_json_t * const object, int components, ...);
KS_DECLARE(ks_json_t *) ks_json_lookup_array_item(const ks_json_t * const object, int array_index, int components, ...);

KS_DECLARE(char *) __ks_json_lookup_print(ks_pool_t *pool, const char *file, int line, const char *tag, const ks_json_t * const object, int components, ...);
#define ks_json_lookup_print(object, components, ...) __ks_json_lookup_print(NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, object, components, __VA_ARGS__)
#define ks_json_plookup_print(pool, object, components, ...) __ks_json_lookup_print(pool, __FILE__, __LINE__, __PRETTY_FUNCTION__, object, components, __VA_ARGS__)

KS_DECLARE(char *) __ks_json_lookup_print_unformatted(ks_pool_t *pool, const char *file, int line, const char *tag, const ks_json_t * const object, int components, ...);
#define ks_json_lookup_print_unformatted(object, components, ...) __ks_json_lookup_print_unformatted(NULL, __FILE__, __LINE__, __PRETTY_FUNCTION__, object, components, __VA_ARGS__)
#define ks_json_plookup_print_unformatted(pool, object, components, ...) __ks_json_lookup_print_unformatted(pool, __FILE__, __LINE__, __PRETTY_FUNCTION__, object, components, __VA_ARGS__)

KS_DECLARE(ks_json_t *) ks_json_enum_child(ks_json_t *item);
KS_DECLARE(ks_json_t *) ks_json_enum_next(ks_json_t *item);

#define KS_JSON_ARRAY_FOREACH(element, array) for(element = (array != NULL) ? ks_json_enum_child((array))	\
			: NULL; element != NULL; element = ks_json_enum_next(element))

#define KS_JSON_PRINT(_h, _j) do { \
		char *_json = ks_json_print(_j); \
		ks_log(KS_LOG_INFO, "--- %s ---\n%s\n---\n", _h, _json); \
		ks_json_free(&_json); \
	} while (0)

#define KS_JSON_PPRINT(_p, _h, _j) do { \
		char *_json = ks_json_pprint(_p, _j); \
		ks_log(KS_LOG_INFO, "--- %s ---\n%s\n---\n", _h, _json); \
		ks_json_free(&_json); \
	} while (0)

KS_END_EXTERN_C

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
