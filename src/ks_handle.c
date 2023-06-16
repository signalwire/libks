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
#include "libks/ks_atomic.h"
#include "libks/internal/ks_handle.h"

static ks_handle_group_t g_handle_groups[KS_HANDLE_MAX_GROUPS];
static ks_bool_t g_initialized;

/* Try to return most likely next allocated slot, skipping over pages and chunks that have no allocated slots  */
static uint32_t next_allocated_slot(ks_handle_group_t *group, uint32_t slot_index)
{
	ks_spinlock_acquire(&group->lock);
	// check if page has any allocated slots
	uint32_t page_index = slot_index / 1024;
	if (group->slot_pages[page_index] == 0) {
		// skip to next page
		ks_spinlock_release(&group->lock);
		return (page_index + 1) * 1024;
	}

	// check if chunk has any allocated slots
	uint32_t chunk_index = slot_index / 32;
	if (group->slot_pages[chunk_index] == 0) {
		// skip to next chunk
		ks_spinlock_release(&group->lock);
		return (chunk_index + 1) * 32;
	}
	ks_spinlock_release(&group->lock);

	// next slot
	return slot_index + 1;
}

static ks_status_t mark_allocated_slot(ks_handle_group_t *group, uint32_t slot_index)
{
	if (group && slot_index > 0 && slot_index < KS_HANDLE_MAX_SLOTS) {
		ks_spinlock_acquire(&group->lock);
		uint32_t chunk_index = slot_index / 32;
		uint32_t chunk_bit = slot_index % 32;
		group->slot_chunks[chunk_index] |= (1 << chunk_bit);
		uint32_t page_index = chunk_index / 32;
		uint32_t page_bit = chunk_index % 32;
		group->slot_pages[page_index] |= (1 << page_bit);
		ks_spinlock_release(&group->lock);
		return KS_STATUS_SUCCESS;
	}
	return KS_STATUS_INVALID_ARGUMENT;
}

static ks_status_t unmark_allocated_slot(ks_handle_group_t *group, uint32_t slot_index)
{
	if (group && slot_index > 0 && slot_index < KS_HANDLE_MAX_SLOTS) {
		ks_spinlock_acquire(&group->lock);
		uint32_t chunk_index = slot_index / 32;
		uint32_t chunk_bit = slot_index % 32;
		group->slot_chunks[chunk_index] &= ~(1 << chunk_bit);
		if (group->slot_chunks[chunk_index] == 0) {
			uint32_t page_index = chunk_index / 32;
			uint32_t page_bit = chunk_index % 32;
			group->slot_pages[page_index] &= ~(1 << page_bit);
		}
		ks_spinlock_release(&group->lock);
		return KS_STATUS_SUCCESS;
	}
	return KS_STATUS_INVALID_ARGUMENT;
}

/**
 * Lock controls for a slot. Each slot has an atomic_flag in it
 * which gets set to true whenever we want to gate access to it.
 * Think of each slot as having their own mutex.
 */
static inline ks_bool_t __try_lock_slot(ks_handle_slot_t *slot)
{
	if (g_initialized == KS_FALSE)
		return KS_TRUE;
	return ks_spinlock_try_acquire(&slot->lock);
}

static inline void __lock_slot(ks_handle_slot_t *slot)
{
	if (g_initialized == KS_FALSE)
		return;
	ks_spinlock_acquire(&slot->lock);
}

static inline void __unlock_slot(ks_handle_slot_t *slot)
{
	if (g_initialized == KS_FALSE)
		return;
	ks_spinlock_release(&slot->lock);
}

/**
 * Allocation means we lock, then attempt to be the first to set the
 * allocate flag in the flags field. On failure KS_FALSE is returned.
 */
static inline ks_bool_t __try_allocate_slot(ks_handle_slot_t *slot)
{
	/* No allocations allowed in single threaded mode */
	ks_assertd(g_initialized == KS_TRUE);

	/* First grab the lock, if busy just say fail */
	if (!__try_lock_slot(slot)) {
		return KS_FALSE;
	}

	/* Now attempt to see if its free to use (flags == 0 means un-used)*/
	if (slot->flags) {
		__unlock_slot(slot);
		return KS_FALSE;
	}

	/* Verify our assumptions are correct */
	ks_assertd(slot->size == 0);
	ks_assertd(slot->flags == 0);
	ks_assertd(slot->sequence == 0);
	ks_assertd(slot->refcount == 0);
	ks_assertd(slot->parent == 0);

#if KS_BUILD_DEBUG
	ks_assertd(slot->line == 0);
	ks_assertd(slot->file == NULL);
	ks_assertd(slot->tag == NULL);
#endif

	ks_assertd(slot->data == NULL);

	/* Ok we're free to allocate it */
	slot->flags |= KS_HANDLE_FLAG_ALLOCATED;

	/* Safe to unlock it now */
	__unlock_slot(slot);

	return KS_TRUE;
}

/**
 * Races to reserve a slot. Reserving a slot in a group amounts to one thread
 * being first to increment the allocated feild to 1.
 */
static inline ks_status_t __reserve_slot(ks_handle_group_t *group,
	   uint32_t start_index, uint16_t *found_index, ks_handle_slot_t **found_slot)
{
	/* A 0 slot is considered invalid so skip it */
	if (start_index == 0)
		start_index = 1;

	/* Allocate a slot by being the first to increment it to 1 */
	for (uint16_t slot_index = start_index; slot_index < KS_HANDLE_MAX_SLOTS; slot_index++) {
		ks_handle_slot_t *slot = &group->slots[slot_index];

		/* Attempt to allocate it */
		if (!__try_allocate_slot(slot)) {
			continue;
		}
		mark_allocated_slot(group, slot_index);

		/* Enforce 0 is invalid */
		ks_assert(slot_index != 0);

		/* Got one */
		*found_slot = slot;
		*found_index = slot_index;
		return KS_STATUS_SUCCESS;
	}

	/* Try again at zero if we couldn't find one at the hint
	 * (Always start at 1, 0 is invalid) */
	if (start_index > 1) {
		return __reserve_slot(group, 1, found_index, found_slot);
	}

	ks_log(KS_LOG_ERROR, "OUT OF SLOTS!!!");
	return KS_STATUS_HANDLE_NO_MORE_SLOTS;
}

/**
 * Releases a slot, clear its fields, after a call to this function the
 * slot is free to be re-used.
 */
static inline void __release_slot(ks_handle_slot_t *slot)
{
	if (!slot)
		return;

	ks_assertd(slot->flags & KS_HANDLE_FLAG_ALLOCATED);

	slot->flags = 0;
	slot->sequence = 0;
	slot->refcount = 0;
	slot->parent = 0;

#if KS_BUILD_DEBUG
	slot->line = 0;
	slot->file = NULL;
	slot->tag = NULL;
#endif

	slot->type = 0;
	slot->child_count = 0;

	/* Do not free on custom ptr indicated by a zero size */
	if (slot->size != 0 && slot->data) {
		ks_pool_t *pool = slot->data->pool;
		ks_pool_close(&pool);
	}

	slot->size = 0;
	slot->data = NULL;
}

static inline void __inc_ref(ks_handle_slot_t *slot)
{
	ks_assertd(ks_atomic_increment_uint32(&slot->refcount) >= 0);
}

static inline void __dec_ref(ks_handle_slot_t *slot)
{
	ks_assertd(ks_atomic_decrement_uint32(&slot->refcount) != 0);
}

static inline ks_status_t __validate_type(ks_handle_type_t type, ks_handle_group_t **group)
{
	uint16_t group_id = KS_HANDLE_GROUP_FROM_TYPE(type);

	if (group_id >= KS_HANDLE_MAX_GROUPS) {
		ks_log(KS_LOG_DEBUG, "Invalid handle type: %8.8x\n", type);
		return KS_STATUS_FAIL;
	}

	*group = &g_handle_groups[group_id];
	return KS_STATUS_SUCCESS;
}

static inline ks_status_t __validate_handle(ks_handle_type_t type,
	   ks_handle_t handle, ks_handle_group_t **group, uint16_t *sequence, uint16_t *slot_index)
{
	ks_status_t status;

	/* Parse the type first */
	if (status = __validate_type(type, group)) {
		ks_log(KS_LOG_DEBUG, "Invalid type: %8.8llx", handle);
		return status;
	}

	if (KS_HANDLE_TYPE_FROM_HANDLE(handle) != type) {
		ks_log(KS_LOG_DEBUG, "Invalid type (2): %8.8llx", handle);
		return KS_STATUS_HANDLE_TYPE_MISMATCH;
	}

	/* Now figure out if the handle references a valid slot index */
	*slot_index = KS_HANDLE_SLOT_INDEX_FROM_HANDLE(handle);

	if (*slot_index >= KS_HANDLE_MAX_SLOTS) {
		ks_log(KS_LOG_DEBUG, "Invalid handle slot: %8.8x\n", *slot_index);
		return KS_STATUS_FAIL;
	}

	/* And finally extract the sequence out of the handle */
	*sequence = KS_HANDLE_SLOT_SEQUENCE_FROM_HANDLE(handle);

	/* zero is an illegal sequence for any handle */
	if (!*sequence)
		return KS_STATUS_HANDLE_INVALID;

	return KS_STATUS_SUCCESS;
}

static ks_status_t __lookup_allocated_slot(ks_handle_type_t type, ks_handle_t handle,
	ks_bool_t lock, uint16_t rflags, ks_handle_group_t **_group, uint16_t *_slot_index, ks_handle_slot_t **_slot)
{
	uint16_t slot_index, sequence;
	ks_handle_group_t *group;
	ks_handle_slot_t *slot;
	ks_status_t status;

	/* Parse the handle */
	if (status = __validate_handle(type, handle, &group, &sequence, &slot_index)) {
		ks_log(KS_LOG_WARNING, "VALIDATION FAILED : %lu HANDLE: %16.16llx", status, handle);
		return status;
	}

	/* Reference the slot and verify its readiness */
	slot = &group->slots[slot_index];
	__lock_slot(slot);

	/* Check required flags */
	if (rflags) {
		uint16_t flags = slot->flags & rflags;
		if (!(flags)) {
			status = KS_STATUS_HANDLE_INVALID;
			ks_log(KS_LOG_ERROR, "RFLAGS INVALID: %lu", status);
			goto done;
		}
	}

	/* Also validate its sequence, this catches any re-use of stale handles */
	if (slot->sequence != sequence) {
		status = KS_STATUS_HANDLE_SEQ_MISMATCH;
			ks_log(KS_LOG_ERROR, "SEQ MISMATCH: %lu HANDLE VALUE: %16.16llx", status, handle);
		goto done;
	}

	/* Types better match */
	if (slot->type != type) {
		status = KS_STATUS_HANDLE_TYPE_MISMATCH;
			ks_log(KS_LOG_ERROR, "TYPE MISMATCH: %lu", status);
		goto done;
	}

	/* All set, return the required results */
	if (_group) {
		*_group = group;
	}

	if (_slot_index)
		*_slot_index = slot_index;

	if (_slot) {
		*_slot = slot;
	} else if (lock) {
		/* It doesn't make sense to request us to lock a slot but then also not pass in its
		 * slot arg */
		status = KS_STATUS_HANDLE_INVALID_REQUEST;
			ks_log(KS_LOG_ERROR, "INVALID REQUEST: %lu", status);
		goto done;
	}

done:
	if (status || !lock) {
		__unlock_slot(slot);
	}

	return status;
}

/**
 * Allocates a handle and allocates a chunk of memory for it. The handle will be
 * returned in a 'not ready' state, meaning no other process can use the handle
 * until the caller decides its good and ready by calling ks_handle_set_ready.
 */
KS_DECLARE(ks_status_t) __ks_handle_alloc_ex(ks_pool_t **pool, ks_handle_type_t type, size_t size,
	   ks_handle_base_t **data, ks_handle_t *handle,
	   ks_handle_describe_cb_t describe_cb, ks_handle_deinit_cb_t deinit_cb,
	   const char *file, int line, const char *tag)
{
	uint16_t group_id = KS_HANDLE_GROUP_FROM_TYPE(type);
	ks_handle_group_t *group = NULL;
	ks_handle_slot_t *slot = NULL;
	uint16_t slot_index = 0;
	ks_pool_t *_pool = NULL;
	ks_status_t status;

#if defined(KS_DEBUG_HANDLE)
	ks_log(KS_LOG_DEBUG, "ALLOC - GROUP: %lu TOTAL: %lu", KS_HANDLE_GROUP_FROM_TYPE(type), ks_handle_count(type));
#endif

	ks_assertd(size == 0 || size >= sizeof(ks_handle_base_t));

	/* Validate the encoded group */
	if (status = __validate_type(type, &group))
		return status;

	/* Custom allocations don't use the pool */
	if (size == 0) 
		ks_assertd(!pool);
	else {
		/* Attempt to allocate a pool for this handle if the caller isn't giving us a pool */
		if (!pool || !*pool) {
			if (status = ks_pool_tagged_open(&_pool, file, line, tag))
				return status;
		} else {
			/* Use theirs, and indicate ownership by setting their copy to NULL */
			_pool = *pool;
			*pool = NULL;
		}
	}

	/* Reserve a slot in this group */
	group = &g_handle_groups[group_id];
	if (status = __reserve_slot(group, group->next_free, &slot_index, &slot)) {
#if defined(KS_DEBUG_HANDLE)
		ks_log(KS_LOG_ERROR, "Out of handles for group %d %s:%lu (%s)\n", group_id, file, line, tag);
#endif
		ks_pool_close(pool);
		return status;
	}

	/* Allocate its data (in its own pool) */
	slot->size = size;

	/* Do not alloc if size is zero this indicates the caller is managing their memory */
	if (size == 0) {
		ks_assert(data && *data);
		slot->data = *data;
	} else if (!(slot->data = __ks_pool_alloc(_pool, size, file, line, tag))) {
		status = KS_STATUS_HANDLE_NOMEM;
		ks_pool_close(&_pool);
#if defined(KS_DEBUG_HANDLE)
		ks_log(KS_LOG_ERROR, "Out of memory when allocating handle of size: %zu\n", size);
#endif
		goto done;
	}

	/* Bind the current sequence with this slot */
	slot->sequence = ks_atomic_increment_uint32(&group->sequence);

	/* Mark it not ready (caller has to set it up first, then call setready) */
	slot->flags |= KS_HANDLE_FLAG_NOT_READY;

	/* Stash the type for enum */
	slot->type = type;

#if KS_BUILD_DEBUG
	slot->file = file;
	slot->line = line;
	slot->tag = tag;
#endif

	*data = slot->data;

	slot->deinit_cb = deinit_cb;
	slot->describe_cb = describe_cb;

	/* And fabricate a handle number */
	*handle = KS_HANDLE_MAKE_HANDLE(type, slot->sequence, slot_index);

	/* Stash a copy of the handle in the base structure and assign it its pool */
	slot->data->handle = *handle;
	slot->data->pool = _pool;

#if KS_DEBUG_HANDLE
	ks_log(KS_LOG_DEBUG, "ALLOC HANDLE: %16.16llx", *handle);
#endif

done:
	if (status) {
		if (group && slot_index > 0) {
			unmark_allocated_slot(group, slot_index);
		}
		__release_slot(slot);
	}

	return status;
}

KS_DECLARE(ks_status_t) __ks_handle_get(ks_handle_type_t type,
	   	ks_handle_t handle, ks_handle_base_t **data, const char *file, int line, const char *tag)
{
	ks_status_t status;
	ks_handle_slot_t *slot;

	/* Allow anonumous gets */
	if (type == 0) {
		type = KS_HANDLE_TYPE_FROM_HANDLE(handle);
	}

	/* Lookup the slot, and expect it to be ready */
	if (status = __lookup_allocated_slot(type, handle, KS_FALSE, KS_HANDLE_FLAG_READY, NULL, NULL, &slot)) {
#if KS_DEBUG_HANDLE
		ks_log(KS_LOG_ERROR, "GETHANDLE: %16.16llx %s:%lu (%s) FAILED WITH STATUS: %lu", handle, file, line, tag, status);
#endif
		return status;
	}

#if KS_DEBUG_HANDLE
	__lock_slot(slot);
	slot->last_get_file = file;
	slot->last_get_line = line;
	slot->last_get_tag = tag;
	__unlock_slot(slot);
#endif

	/* Add a reference */
	__inc_ref(slot);

	/* Return the callers ptr */
	*data = slot->data;

	return KS_STATUS_SUCCESS;
}

KS_DECLARE(ks_status_t) __ks_handle_put(ks_handle_type_t type,
	   	ks_handle_base_t **data, const char *file, int line, const char *tag)
{
	ks_status_t status;
	ks_handle_slot_t *slot;

	if (!data || !*data)
		return KS_STATUS_SUCCESS;

	/* Allow anonymous puts */
	if (type == 0)
		type = KS_HANDLE_TYPE_FROM_HANDLE((*data)->handle);

	/* Lookup the slot, and expect it to be allocated (may be not ready as we're allowed to put in that case) */
	if (status = __lookup_allocated_slot(type, (*data)->handle, KS_FALSE, 0, NULL, NULL, &slot)) {
		ks_debug_break();
		return status;
	}

	/* Dec a reference */
	__dec_ref(slot);

	/* Null the callers ptr to prevent misuse */
	*data = NULL;

	return KS_STATUS_SUCCESS;
}

KS_DECLARE(ks_status_t) __ks_handle_set_notready(
	ks_handle_type_t type,
	ks_handle_t handle,
	ks_handle_base_t **data,
	const char *file,
	int line,
	const char *tag)
{
	ks_status_t status;
	ks_handle_slot_t *slot;
	ks_time_t wait_start;
	ks_time_t sleep_amount;

	/* Lookup the slot, and expect it to be ready, and keep it locked so we can set a flag
	 * to prevent others from coming in and also entering the wait below  */
	if (status = __lookup_allocated_slot(type, handle, KS_TRUE, KS_HANDLE_FLAG_READY, NULL, NULL, &slot)) {
		return status;
	}

	/* Mark it not ready */
	slot->flags |= KS_HANDLE_FLAG_NOT_READY;

	/* Remove it ready flag */
	slot->flags &= ~KS_HANDLE_FLAG_READY;

	/* Now unlock it so when another thread that attempts to mark this handle as not ready
	 * won't hang instead it will error right away */
	__unlock_slot(slot);

	/* Now wait around while the ref count is still non zero, after a certain amount of time
	 * log some errors as this is a typical bug with checkout/checkin models */
	wait_start = ks_time_now_sec();
	sleep_amount = 500;
	while (slot->refcount) {
		ks_sleep_ms(sleep_amount);

		if ((ks_time_now_sec() - wait_start) > KS_HANDLE_MAX_NOTREADY_WAIT_MS) {
		#if defined(KS_DEBUG_HANDLE)
			ks_log(KS_LOG_ERROR, "Hangup when releasing handle (%s), last checkout location: %s:%d:%s",
				ks_handle_describe_ctx(slot->data), slot->file, slot->line, slot->file);
		#else
			ks_log(KS_LOG_ERROR, "Hangup when releasing handle (%s)", ks_handle_describe_ctx(slot->data));
		#endif
			ks_assert(!"Hangup when releasing a handle");

			/* Switch to a long poll */
			sleep_amount = 5000;
		}
	}

	/* Ok free to tear it down now */
	*data = slot->data;
	return KS_STATUS_SUCCESS;
}

static ks_status_t  __handle_destroy(ks_handle_t *handle, ks_status_t *child_status);

static ks_status_t __destroy_slot_children(ks_handle_t parent)
{
	ks_handle_t next = 0;
	ks_status_t pending_child_status = 0;
	uint32_t pending_children = 0;

	while (KS_TRUE) {
		/* Iterate all handles and find ones that have a matching parent */
		while (!ks_handle_enum_children(parent, &next)) {
			ks_status_t status;
			ks_handle_t _handle = next;
			uint32_t refcount;

			/* Now if the refcount on this child is not 0, move on */
			if (ks_handle_refcount(_handle, &refcount) || refcount > 0) {
				pending_children++;
				continue;
			}

			if (status = __handle_destroy(&_handle, &pending_child_status)) {
				ks_abort_fmt("Error releasing dependent child handle: 16.16llx (%lu)", next, status);
			}
		}

		if (pending_children) {
			return KS_STATUS_HANDLE_PENDING_CHILDREN;
		}

		break;
	}

	return KS_STATUS_SUCCESS;
}

static ks_status_t __handle_destroy(ks_handle_t *handle, ks_status_t *child_status)
{
	ks_status_t status = KS_STATUS_SUCCESS;
	uint16_t slot_index = 0;
	ks_handle_group_t *group = NULL;
	ks_handle_slot_t *slot;

	if (!handle || !*handle)
		return status;

	/* Lookup the slot */
	if (status = __lookup_allocated_slot(KS_HANDLE_TYPE_FROM_HANDLE(*handle), *handle, KS_TRUE, 0, &group, &slot_index, &slot))
		return status;

	/* prevent simultaneous destroy */
	if (slot->flags & KS_HANDLE_FLAG_DESTROY) {
		__unlock_slot(slot);
		*handle = KS_NULL_HANDLE;
		return KS_STATUS_SUCCESS;
	}
	slot->flags |= KS_HANDLE_FLAG_DESTROY;

	/* See if it is not, not ready. Illegal unless there is a deinit cb set. */
	if (!(slot->flags & KS_HANDLE_FLAG_NOT_READY)) {
		void *data;

		if (slot->deinit_cb == NULL)
			ks_abort_fmt("Cannot destroy a ready handle that does not have a deint callback set on handle: 16.16llx", *handle);

		/* Right it has one so set it not ready */
		__unlock_slot(slot);

		ks_assertd(!ks_handle_set_notready(KS_HANDLE_TYPE_FROM_HANDLE(*handle), *handle, &data));

		/* Now free all children if children were set */
		if (slot->child_count) {
			if (status = __destroy_slot_children(*handle)) {
				// have to let retry destroy
				slot->flags &= ~KS_HANDLE_FLAG_DESTROY;
				return status;
			}
		}

		/* Now call deinit on it */
		if (slot->deinit_cb)
			slot->deinit_cb(slot->data);

		/* Re-lock */
		__lock_slot(slot);

		/* Now proceed to regular destroy */
	}

	/* Release it */
	unmark_allocated_slot(group, slot_index);
	__release_slot(slot);

	/* Mark the group as next free as a hint */
	group->next_free = slot_index;

	/* Don't forget to unlock it */
	__unlock_slot(slot);

	/* Clear the callers value to avoid invalid handle use */
	*handle = KS_NULL_HANDLE;

	return status;
}

KS_DECLARE(ks_status_t) __ks_handle_destroy(ks_handle_t *handle, const char *file, int line, const char *tag)
{
	ks_status_t child_status;
	if (!handle)
		return KS_STATUS_HANDLE_INVALID;
#if KS_DEBUG_HANDLE
	ks_log(KS_LOG_DEBUG, "HANDLE DESTROY: %16.16llx %s:%lu (%s)", *handle, file, line, tag);
#endif
	return __handle_destroy(handle, &child_status);
}

KS_DECLARE(const char *) __ks_handle_describe_ctx(const ks_handle_base_t *ctx)
{
	static KS_THREAD_LOCAL char handle_description_buffer[32768] = {0};
	ks_handle_slot_t *slot;
	ks_status_t status;

	/* Lookup the ready slot */
	if (status = __lookup_allocated_slot(KS_HANDLE_TYPE_FROM_HANDLE(ctx->handle), ctx->handle, KS_FALSE, 0, NULL, NULL, &slot)) {
		snprintf(handle_description_buffer, sizeof(handle_description_buffer), "Invalid handle, get failed: %d", status);
		return handle_description_buffer;
	}

	if (slot->describe_cb)
		slot->describe_cb(slot->data, handle_description_buffer, sizeof(handle_description_buffer) - 1);

	return handle_description_buffer;
}

KS_DECLARE(const char *) ks_handle_describe(ks_handle_t handle)
{
	static KS_THREAD_LOCAL char handle_description_buffer[32768] = {0};
	ks_handle_slot_t *slot;
	ks_status_t status;

	if (!handle) {
		snprintf(handle_description_buffer, sizeof(handle_description_buffer), "{NULL HANDLE}");
		return handle_description_buffer;
	}

	/* Lookup the ready slot */
	if (status = __lookup_allocated_slot(KS_HANDLE_TYPE_FROM_HANDLE(handle), handle, KS_TRUE, KS_HANDLE_FLAG_READY, NULL, NULL, &slot)) {
		snprintf(handle_description_buffer, sizeof(handle_description_buffer), "Invalid handle, get failed: %d", status);
		return handle_description_buffer;
	}

	/* Grab a ref while we render */
	__inc_ref(slot);
	__unlock_slot(slot);

	if (slot->describe_cb)
		slot->describe_cb(slot->data, handle_description_buffer, sizeof(handle_description_buffer) - 1);

	/* Done with it, dec and done */
	__dec_ref(slot);

	return handle_description_buffer;
}

KS_DECLARE(ks_status_t) ks_handle_set_ready(ks_handle_t handle)
{
	ks_status_t status;
	ks_handle_slot_t *slot;

#if KS_DEBUG_HANDLE
	ks_log(KS_LOG_DEBUG, "READY HANDLE: %16.16llx", handle);
#endif

	/* Lookup the not ready slot */
	if (status = __lookup_allocated_slot(KS_HANDLE_TYPE_FROM_HANDLE(handle), handle, KS_TRUE,
			KS_HANDLE_FLAG_NOT_READY, NULL, NULL, &slot)) {
#if KS_DEBUG_HANDLE
		ks_log(KS_LOG_ERROR, "READYHANDLE: %16.16llx FAILED: %lu", handle, status);
#endif
		return status;
	}

	/* Flag it as ready under the lock */
	slot->flags |= KS_HANDLE_FLAG_READY;

	/* And remove its not ready flag */
	slot->flags &= ~KS_HANDLE_FLAG_NOT_READY;

	/* Unlock it and we're done */
	__unlock_slot(slot);

#if KS_DEBUG_HANDLE
	ks_log(KS_LOG_DEBUG, "READYHANDLE: %16.16llx SUCCESS", handle);
#endif

	return KS_STATUS_SUCCESS;
}

KS_DECLARE(ks_bool_t) ks_handle_valid(ks_handle_t handle)
{
	uint32_t refcount;
	return ks_handle_refcount(handle, &refcount) == KS_STATUS_SUCCESS;
}

KS_DECLARE(ks_status_t) ks_handle_parent(ks_handle_t child, ks_handle_t *parent)
{
	ks_handle_slot_t *child_slot;

	/* Lookup the ready child slot (must be ready) */
	if (__lookup_allocated_slot(KS_HANDLE_TYPE_FROM_HANDLE(child), child, KS_TRUE, KS_HANDLE_FLAG_READY, NULL, NULL, &child_slot)) {
		return KS_STATUS_HANDLE_INVALID;
	}

	*parent = child_slot->parent;

	__unlock_slot(child_slot);
	return KS_STATUS_SUCCESS;
}

KS_DECLARE(ks_status_t) ks_handle_set_parent(ks_handle_t child, ks_handle_t parent)
{
	ks_handle_slot_t *child_slot, *parent_slot;
	ks_status_t status;

	ks_assertd(child != parent);

	/* Lookup the ready child slot (must be allocated) */
	if (status = __lookup_allocated_slot(KS_HANDLE_TYPE_FROM_HANDLE(child), child, KS_TRUE, KS_HANDLE_FLAG_ALLOCATED, NULL, NULL, &child_slot)) {
		ks_log(KS_LOG_DEBUG, "Attempt to set child on non-allocated child handle: %16.16llx", child);
		return status;
	}

	/* Lookup the parent slot (must be allocated) */
	if (status = __lookup_allocated_slot(KS_HANDLE_TYPE_FROM_HANDLE(parent), parent, KS_TRUE, KS_HANDLE_FLAG_ALLOCATED, NULL, NULL, &parent_slot)) {
		ks_log(KS_LOG_DEBUG, "Attempt to set child on non-allocated parent handle: %16.16llx", parent);
		__unlock_slot(child_slot);
		return status;
	}

	/* Set its parent (illegal to re-set an already set parent) */
	if (child_slot->parent && child_slot->parent != parent) {
		ks_log(KS_LOG_WARNING, "Attempt to set parent on child which already has parent, child handle: %16.16llx", child);
		ks_debug_break();
		__unlock_slot(child_slot);
		__unlock_slot(parent_slot);
		return KS_STATUS_INVALID_ARGUMENT;
	}

	child_slot->parent = parent;
	parent_slot->child_count++;

	/* Unlock it and we're done */
	__unlock_slot(child_slot);
	__unlock_slot(parent_slot);

	return KS_STATUS_SUCCESS;
}

KS_DECLARE(ks_status_t) ks_handle_enum_children(ks_handle_t parent, ks_handle_t *next_child)
{
	for (uint32_t group_index = KS_HANDLE_GROUP_FROM_HANDLE(*next_child); group_index < KS_HANDLE_MAX_GROUPS; group_index++) {
		for (uint32_t slot_index = KS_HANDLE_SLOT_INDEX_FROM_HANDLE(*next_child) + 1; slot_index < KS_HANDLE_MAX_SLOTS; slot_index++) {
			ks_handle_group_t *group = &g_handle_groups[group_index];
			ks_handle_slot_t *slot = &group->slots[slot_index];

			if (!__try_lock_slot(slot))
				continue;

			if (slot->flags & KS_HANDLE_FLAG_READY && slot->parent == parent) {
				*next_child = KS_HANDLE_MAKE_HANDLE(slot->type, slot->sequence, slot_index);
				__unlock_slot(slot);
				return KS_STATUS_SUCCESS;
			}

			__unlock_slot(slot);
		}
	}

	return KS_STATUS_END;
}

KS_DECLARE(ks_status_t) ks_handle_enum(ks_handle_t *handle)
{
	for (uint32_t group_index = KS_HANDLE_GROUP_FROM_HANDLE(*handle); group_index < KS_HANDLE_MAX_GROUPS; group_index++) {
		ks_handle_group_t *group = &g_handle_groups[group_index];
		for (uint32_t slot_index = KS_HANDLE_SLOT_INDEX_FROM_HANDLE(*handle) + 1; slot_index < KS_HANDLE_MAX_SLOTS; slot_index = next_allocated_slot(group, slot_index)) {
			ks_handle_slot_t *slot = &group->slots[slot_index];

			if (!__try_lock_slot(slot))
				continue;

			if (slot->flags & KS_HANDLE_FLAG_READY) {
				*handle = KS_HANDLE_MAKE_HANDLE(slot->type, slot->sequence, slot_index);
				__unlock_slot(slot);
				return KS_STATUS_SUCCESS;
			}

			__unlock_slot(slot);
		}
	}

	return KS_STATUS_END;
}

KS_DECLARE(uint32_t) ks_handle_count(ks_handle_type_t type)
{
	uint32_t total = 0;
	ks_handle_group_t *group;
	uint32_t group_id = KS_HANDLE_GROUP_FROM_TYPE(type);

	if (group_id >= KS_HANDLE_MAX_GROUPS)
		return total;

	group = &g_handle_groups[group_id];

	for (uint32_t slot_index = 1; slot_index < KS_HANDLE_MAX_SLOTS; slot_index = next_allocated_slot(group, slot_index)) {
		ks_handle_slot_t *slot = &group->slots[slot_index];

		if (!__try_lock_slot(slot))
			continue;

		if (slot->type == type) {
			if (slot->flags & KS_HANDLE_FLAG_ALLOCATED)
				total++;
		}

		__unlock_slot(slot);
	}

	return total;
}

KS_DECLARE(ks_status_t) ks_handle_enum_type(ks_handle_type_t type, ks_handle_t *handle)
{
	ks_handle_group_t *group;
	uint32_t group_id = KS_HANDLE_GROUP_FROM_TYPE(type);

	if (group_id >= KS_HANDLE_MAX_GROUPS)
		return KS_STATUS_INVALID_ARGUMENT;

	group = &g_handle_groups[group_id];

	for (uint32_t slot_index = KS_HANDLE_SLOT_INDEX_FROM_HANDLE(*handle) + 1; slot_index < KS_HANDLE_MAX_SLOTS; slot_index = next_allocated_slot(group, slot_index)) {
		ks_handle_slot_t *slot = &group->slots[slot_index];

		if (!__try_lock_slot(slot))
			continue;

		if (slot->type == type) {
			if (slot->flags & KS_HANDLE_FLAG_READY) {
				*handle = KS_HANDLE_MAKE_HANDLE(slot->type, slot->sequence, slot_index);
				ks_assert(slot->data->handle == *handle); /* sanity */
				__unlock_slot(slot);
				return KS_STATUS_SUCCESS;
			}
		}

		__unlock_slot(slot);
	}

	return KS_STATUS_END;
}

KS_DECLARE(ks_pool_t *) ks_handle_pool(ks_handle_t handle)
{
	ks_status_t status;
	ks_handle_slot_t *slot;
	ks_pool_t *pool = NULL;

	/* Lookup the ready slot */
	if (status = __lookup_allocated_slot(KS_HANDLE_TYPE_FROM_HANDLE(handle), handle, KS_TRUE, KS_HANDLE_FLAG_READY, NULL, NULL, &slot)) {
		return pool;
	}

	/* Grab its pool count */
	pool = slot->data->pool;

	/* Unlock it and we're done */
	__unlock_slot(slot);

	return pool;
}

KS_DECLARE(ks_status_t) ks_handle_refcount(ks_handle_t handle, uint32_t *refcount)
{
	ks_status_t status;
	ks_handle_slot_t *slot;

	/* Lookup the ready slot */
	if (status = __lookup_allocated_slot(KS_HANDLE_TYPE_FROM_HANDLE(handle), handle, KS_TRUE, KS_HANDLE_FLAG_READY, NULL, NULL, &slot)) {
		return status;
	}

	/* Grab its ref count */
	*refcount = slot->refcount;

	/* Unlock it and we're done */
	__unlock_slot(slot);

	return KS_STATUS_SUCCESS;
}

KS_DECLARE(ks_status_t) ks_handle_init(void)
{
	/* Seed all our groups with a random start */
	for (int gidx = 0; gidx < KS_HANDLE_MAX_GROUPS; gidx++) {
		while (!(
			g_handle_groups[gidx].sequence = (uint16_t)rand()	 /* guarantee a non zero random result */
		)) {}
	}

	/* Leave single threaded mode */
	g_initialized = KS_TRUE;

	return KS_STATUS_SUCCESS;
}

KS_DECLARE(void) ks_handle_shutdown(void)
{
	ks_handle_t next = 0;

	/* Enter single threaded mode (no locks) */
	g_initialized = KS_FALSE;

	/* First pass, destroy all un-released handles that have a refcount of zero and no parents */
	while (!ks_handle_enum(&next)) {
		uint32_t refcount;
		ks_handle_t parent = 0;
		ks_handle_slot_t *slot;

		if (ks_handle_refcount(next, &refcount))
			continue;

		if (ks_handle_parent(next, &parent))
			continue;

		if (__lookup_allocated_slot(KS_HANDLE_TYPE_FROM_HANDLE(next), next, KS_TRUE, KS_HANDLE_FLAG_ALLOCATED, NULL, NULL, &slot))
			continue;

#if defined(KS_BUILD_DEBUG)
		ks_log(KS_LOG_WARNING, "Un-released handle %16.16llx (%s) at location: %s:%lu:%s",
			next, ks_handle_describe_ctx(slot->data), slot->file, slot->line, slot->tag);
#elif defined(KS_DEBUG_HANDLE)
		ks_log(KS_LOG_WARNING, "Un-released handle %16.16llx (%s) at location: %s:%lu:%s, last checkout: %s:%lu:%s",
			next, ks_handle_describe_ctx(slot->data),  slot->file, slot->line, slot->tag, slot->last_get_file, slot->last_get_line, slot->last_get_tag);
#else
		ks_log(KS_LOG_WARNING, "Un-released handle %16.16llx (%s)", next, ks_handle_describe_ctx(slot->data));
#endif

		__unlock_slot(slot);

		if (refcount == 0 && parent == 0)
			ks_handle_destroy(&next);
	}

	/* Second pass, deal with the rest */
	while (!ks_handle_enum(&next)) {
		ks_handle_destroy(&next);
	}
}
