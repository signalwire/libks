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

/*
 * Resource tracking is an issue in C and addressing this problem in
 * a generic way will save a lot of one off attempts at it while also
 * providing a global singleton that will outlive any other resource
 * making it a great way to deal with resources in general. This
 * api provides a reference counted, extendable api to associate
 * resources with each other, and generalize how they are allocated
 * and released. In addition it provides an opaque resource id
 * that abstracts the actual backing ptr. This id is a incrementing
 * index started at a random value on each startup, encoded with the
 * handle type, as well as its slot index, and a sequence id (to further
 * prevent accidental re-use of handles). The slots are basically
 * what backs a handle, its a fixed size statically allocated bucket
 * that can be allocated and released using lock free patterns.
 *
 * With this model you can associate a peice of data with 'number'
 * that can then be handed out to consumers, even over say rpc. It
 * can then be validated and 'checked out' to be used. When a handle
 * gets freed it is first set to a 'not ready' state, this blocks
 * until all references drop to zero, and while in this state no
 * new checkouts can be made. The caller then has time to delete
 * whatever meta resources are needed, then calls release which
 * frees the slot for re-use.
 *
 * Handles may be associated to children. When the parent then is
 * released its releases all of its children and subchildren. In
 * this way you can associate many resources with each other
 * and safely free them in the right order, generically.
 */

#pragma once

KS_BEGIN_EXTERN_C

/* The ks handle is a 64 bit unsigned integer containing:
 * handle_type (32bit), sequence id (16bit), handle slot index(16bit)
 */
typedef uint64_t ks_handle_t;

#define KS_HANDLE_HIGH_WORD(dword)		((uint16_t)((uint16_t)(dword >> 16)))
#define KS_HANDLE_LOW_WORD(dword)		((uint16_t)((uint16_t)dword & 0xFFFF))
#define KS_HANDLE_MAKE_DWORD(high, low)	((uint32_t)(((uint32_t)high << 16) | low))

#define KS_HANDLE_HIGH_DWORD(qword)		((uint32_t)((uint64_t)qword >> 32))
#define KS_HANDLE_LOW_DWORD(qword)		((uint32_t)((uint64_t)qword) & 0xFFFFFFFF)
#define KS_HANDLE_MAKE_QWORD(high, low)	((((uint64_t)((uint32_t)high)) << 32 ) | low)

/* Define a macro that packs in the handle group into the handle id
 * A type is a 32 bit unsigned integer with the first 16 bits
 * encodes the group, and the second 16 bits stores the id (just
 * a unique number within the group). In this way we can later
 * store various handle groups in different memory models, based
 * on their type.
 */
#define KS_HANDLE_MAKE_TYPE(group, index)		KS_HANDLE_MAKE_DWORD(KS_HANDLE_GROUP_##group, index)
#define KS_HANDLE_GROUP_FROM_TYPE(type) 		KS_HANDLE_HIGH_WORD(type)
#define KS_HANDLE_GROUP_INDEX_FROM_TYPE(type) 	KS_HANDLE_LOW_WORD(type)

#define KS_NULL_HANDLE	0

#define KS_HANDLE_MAKE_HANDLE(type, sequence, slot) \
	(KS_HANDLE_MAKE_QWORD(type, KS_HANDLE_MAKE_DWORD(sequence, slot)))

#define KS_HANDLE_SLOT_INDEX_FROM_HANDLE(handle)	(KS_HANDLE_LOW_WORD(KS_HANDLE_LOW_DWORD(handle)))
#define KS_HANDLE_SLOT_SEQUENCE_FROM_HANDLE(handle)	(KS_HANDLE_HIGH_WORD(KS_HANDLE_LOW_DWORD(handle)))
#define KS_HANDLE_GROUP_FROM_HANDLE(handle) (KS_HANDLE_HIGH_WORD(KS_HANDLE_HIGH_DWORD(handle)))
#define KS_HANDLE_TYPE_FROM_HANDLE(handle) (KS_HANDLE_HIGH_DWORD(handle))
#define KS_HANDLE_GROUP_INDEX_FROM_HANDLE(handle) (KS_HANDLE_LOW_WORD(KS_HANDLE_HIGH_DWORD(handle)))

typedef uint32_t ks_handle_type_t;

typedef void *(*ks_handle_construct_cb_t) (ks_handle_t, void *);
typedef void *(*ks_handle_destruct_cb_t) (ks_handle_t, void *);
typedef void *(*ks_handle_describe_cb_t) (void *, char *, ks_size_t);

/* Define the base structure used for all handle allocation context ptrs.
 * Any context defined as a handle must be a structure with this as its first member */
typedef struct ks_handle_base_s {
	ks_handle_t handle;		/* During allocation the handle manager will set this field */
	ks_pool_t *pool;		/* Each handle gets its own pool allocated for it. When the handle
							 * is destroyed, the pool is cleared and logged if any leaks
							 * have been detected. The intent is any resources allocated by
							 * the handle should be contained within this pool */
} ks_handle_base_t;

/* Groups are slots, and we reserve the first 10 for ks proper, user groups start here */
#define KS_HANDLE_USER_GROUP_START	10

#define KS_HANDLE_GROUP_LIBKS  0

/* Define our central handle type list
 * (When in c++ allow for strong typing of these enums) */
typedef enum {
	/* Ks throughput context handle. */
    KS_HTYPE_THROUGHPUT =  KS_HANDLE_MAKE_TYPE(LIBKS, 0),

#if KS_DEBUG_POOL
	/* When pool tracking is enabled, tracked pools get wrapped in handles */
    KS_HTYPE_DBG_POOL =  KS_HANDLE_MAKE_TYPE(LIBKS, 1),
#endif
} ks_handle_types_t;

/* No handle debugging with pool debugging due to recursive dependency */
#if KS_DEBUG_POOL
	#undef KS_DEBUG_HANDLE
#endif

/* A deinit callback is callback that gets associated with the handle slot during allocation time.
 * It allows use of ks_handle_destroy that when called without setting a handle to not ready triggers
 * an implied shutdown of the handle. */
typedef void (*ks_handle_deinit_cb_t)(void *context);

/* We define this this way so we can track resource allocations in the system */
KS_DECLARE(ks_status_t) __ks_handle_alloc_ex(ks_pool_t **pool, ks_handle_type_t type, size_t size, ks_handle_base_t **data, ks_handle_t *handle, ks_handle_describe_cb_t describe_cb, ks_handle_deinit_cb_t deinit_cb, const char *file, int line, const char *tag);
KS_DECLARE(ks_status_t) __ks_handle_get(ks_handle_type_t type, ks_handle_t handle, ks_handle_base_t **data, const char *file, int line, const char *tag);
KS_DECLARE(ks_status_t) __ks_handle_put(ks_handle_type_t type, ks_handle_base_t **data, const char *file, int line, const char *tag);
KS_DECLARE(ks_status_t) __ks_handle_set_notready(ks_handle_type_t type, ks_handle_t handle, ks_handle_base_t **data, const char *file, int line, const char *tag);
KS_DECLARE(ks_status_t) __ks_handle_destroy(ks_handle_t *handle, const char *file, int line, const char *tag);
KS_DECLARE(const char *) __ks_handle_describe_ctx(const ks_handle_base_t *ctx);

#define ks_handle_get(type, handle, data) 	__ks_handle_get(type, handle, (ks_handle_base_t **)data, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ks_handle_put(type, data) 	__ks_handle_put(type, (ks_handle_base_t **)data, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ks_handle_alloc(type, size, data, handle, deinit_cb) __ks_handle_alloc_ex(NULL, type, size, (ks_handle_base_t **)data, handle, NULL, deinit_cb, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ks_handle_alloc_ex(pool, type, size, data, handle, describe_cb, deinit_cb) __ks_handle_alloc_ex(pool, type, size, (ks_handle_base_t **)data, handle, describe_cb, deinit_cb, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ks_handle_set_notready(type, handle, data) __ks_handle_set_notready(type, handle, (ks_handle_base_t **)data, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ks_handle_destroy(handle) __ks_handle_destroy(handle, __FILE__, __LINE__, __PRETTY_FUNCTION__)
#define ks_handle_describe_ctx(ctx) __ks_handle_describe_ctx((const ks_handle_base_t *)ctx)

KS_DECLARE(ks_status_t) ks_handle_set_ready(ks_handle_t handle);
KS_DECLARE(ks_status_t) ks_handle_set_parent(ks_handle_t child, ks_handle_t parent);
KS_DECLARE(ks_status_t) ks_handle_parent(ks_handle_t child, ks_handle_t *parent);
KS_DECLARE(ks_bool_t) ks_handle_valid(ks_handle_t handle);
KS_DECLARE(const char *) ks_handle_describe(ks_handle_t handle);
KS_DECLARE(ks_status_t) ks_handle_enum_type(ks_handle_type_t type, ks_handle_t *handle);
KS_DECLARE(ks_status_t) ks_handle_enum_children(ks_handle_t parent, ks_handle_t *next_child);
KS_DECLARE(ks_status_t) ks_handle_enum(ks_handle_t *handle);
KS_DECLARE(ks_status_t) ks_handle_refcount(ks_handle_t handle, uint32_t *refcount);
KS_DECLARE(uint32_t) ks_handle_count(ks_handle_type_t type);
KS_DECLARE(ks_pool_t *) ks_handle_pool(ks_handle_t handle);

KS_DECLARE(ks_status_t) ks_handle_init(void);
KS_DECLARE(void) ks_handle_shutdown(void);

/* Create some helpers for accessing the handles and writing
 * boiler for for each method that abstracts a type with a handle.
 * KS_HANDLE_SCOPE_BEG - Creates a status and handle checkout variable
 * using the names passed into the macro. Calls handle_get, and returns status
 * if the get failed.
 */
#define KS_HANDLE_SCOPE_BEG(type, handle, ptr_type, ptr, status)					\
	ks_status_t status = KS_STATUS_SUCCESS;											\
	ptr_type *ptr = NULL;															\
	if (status = ks_handle_get(type, handle, &ptr)) {								\
		goto ks_handle_scope_end;													\
	} {

#define KS_HANDLE_SCOPE_BEG_TAG(type, handle, ptr_type, ptr, status, file, line, tag)	\
	ks_status_t status = KS_STATUS_SUCCESS;												\
	ptr_type *ptr = NULL;																\
	if (status = __ks_handle_get(type, handle, (ks_handle_base_t **)&ptr, file, line, tag)) {				\
		goto ks_handle_scope_end;														\
	} {
/*
 * KS_HANDLE_SCOPE_END - Puts the handle and returns status.
 * Note: When using these macros there is a implied jump tag
 * created to help with error handling, it is called conn_scope_end.
 */
#define KS_HANDLE_SCOPE_END(type, handle, ptr_type, ptr, status)		\
	}																	\
ks_handle_scope_end:													\
	if (ptr) {															\
		ks_handle_put(type, &ptr);										\
	}																	\
	return status;

#define KS_HANDLE_SCOPE_END_TAG(type, handle, ptr_type, ptr, status, file, line, tag)	\
	}																					\
ks_handle_scope_end:																	\
	if (ptr) {																			\
		__ks_handle_put(type, (ks_handle_base_t **)&ptr, file, line, tag);				\
	}																					\
	return status;


 /* This macro does some generic boiler plate logic for allocating a handle.
  * Note: There are S and M versions here due to the fact that if no args are passed
  * the va_args trick in the macros fails to build.
  * Note: TAG means you pass in the file/line/tag directly (useful to further locate
  * potential get leaks) */
#define KS_HANDLE_ALLOC_TEMPLATE_S_TAG(pool, file, line, tag, handle_type, handle_ptr, context_type, describe_method, deinit_method, init_method)	\
	ks_status_t status;																			\
	context_type *context;																		\
																								\
	if (status = __ks_handle_alloc_ex(pool, handle_type, sizeof(context_type),					\
				(ks_handle_base_t **)&context, handle_ptr,										\
				(ks_handle_describe_cb_t)describe_method,										\
				(ks_handle_deinit_cb_t)deinit_method,											\
				file, line, tag))											 					\
		return status;																			\
																								\
	if (status = init_method(context)) {														\
		deinit_method(context);																	\
		ks_handle_destroy(handle_ptr);															\
		return status;																			\
	}																							\
																								\
																								\
	ks_handle_set_ready(*handle_ptr);															\
	return status;

#define KS_HANDLE_ALLOC_TEMPLATE_M_TAG(pool, file, line, tag, handle_type, handle_ptr, context_type, describe_method, deinit_method, init_method, ...)		\
	ks_status_t status;																											\
	context_type *context;																										\
																																\
	if (status = __ks_handle_alloc_ex(pool, handle_type, sizeof(context_type), (ks_handle_base_t **)&context, handle_ptr,		\
				(ks_handle_describe_cb_t)describe_method, (ks_handle_deinit_cb_t)deinit_method,									\
				file, line, tag))								 																\
		return status;																											\
																																\
	if (status = init_method(context, __VA_ARGS__)) {																			\
		deinit_method(context);																									\
		ks_handle_destroy(handle_ptr);																							\
		return status;																											\
	}																															\
																																\
	ks_handle_set_ready(*handle_ptr);																							\
	return status;

#define KS_HANDLE_ALLOC_TEMPLATE_S(pool, handle_type, handle_ptr, context_type, describe_method, deinit_method, init_method)	\
	ks_status_t status;																			\
	context_type *context;																		\
																								\
	if (status = ks_handle_alloc_ex(pool, handle_type, sizeof(context_type),					\
				&context, handle_ptr,															\
				(ks_handle_describe_cb_t)describe_method,										\
				(ks_handle_deinit_cb_t)deinit_method))											\
		return status;																			\
																								\
	if (status = init_method(context)) {														\
		deinit_method(context);																	\
		ks_handle_destroy(handle_ptr);															\
		return status;																			\
	}																							\
																								\
	ks_handle_set_ready(*handle_ptr);															\
	return status;

#define KS_HANDLE_ALLOC_TEMPLATE_M(pool, handle_type, handle_ptr, context_type, describe_method, deinit_method, init_method, ...)			\
	ks_status_t status;																					\
	context_type *context;																				\
																										\
	if (status = ks_handle_alloc_ex(pool, handle_type, sizeof(context_type), &context, handle_ptr,		\
				(ks_handle_describe_cb_t)describe_method, (ks_handle_deinit_cb_t)deinit_method))		\
		return status;																					\
																										\
	if (status = init_method(context, __VA_ARGS__)) {													\
		deinit_method(context);																			\
		ks_handle_destroy(handle_ptr);																	\
		return status;																					\
	}																									\
																										\
	ks_handle_set_ready(*handle_ptr);																	\
	return status;

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
