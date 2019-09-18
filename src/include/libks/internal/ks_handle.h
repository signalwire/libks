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

/* We store slots in groups, each group gets a bucket of MAX_SLOTS
 * by using static memory we can avoid stress on the heap as our resource
 * tracking structures across ks are all small fixed sized items.
 */
#define KS_HANDLE_MAX_SIZE 		512
#define KS_HANDLE_MAX_SLOTS 	65535 	  /* max value of uint16_t */
#define KS_HANDLE_MAX_SLOT_CHUNKS 	2048  /* 32-bits to track slot allocations as a 32-slot chunk */
#define KS_HANDLE_MAX_SLOT_PAGES 	64 	  /* 32-bits top track slot allocations as a 32-chunk page */
#define KS_HANDLE_MAX_GROUPS	20

#define KS_HANDLE_MAX_NOTREADY_WAIT_MS	(30 * 1000)	/* Wait 30 seconds before considering a hung not ready call */

/* Handle flags are set under the slot lock, they indicate logical states for the slot
 * which effectively give us a read lock style usage of slots */
#define KS_HANDLE_FLAG_READY		1	/* The handle is ready for get/set operations */
#define KS_HANDLE_FLAG_NOT_READY	2	/* The handle has been flagged for tear down */
#define KS_HANDLE_FLAG_ALLOCATED	4	/* The handle has been reserved */
#define KS_HANDLE_FLAG_DESTROY		8	/* The handle is being destroyed */

/* Handles are stored in a fixed static array, wich each handle
 * entry being described in such a way that a lock free approach can
 * be used to allocate them
 */
typedef struct ks_handle_slot_s {
	ks_spinlock_t lock;     		/* a general purpose reservation of this slot using a spin */
	uint32_t refcount;				/* how many calls to ks_handle_get are outstanding */
	uint16_t sequence;				/* our sequence instance allocated at handle allocation */
	uint16_t flags;					/* flags related to the state of this handle */
	ks_handle_t parent;				/* if associated, the parent handle (may be in another group) */
	uint32_t type;					/* The type assigned to the handle (redundantly includes the group), used for
									 * re-creating the handle during enumeration of the slots. */
	uint32_t child_count;			/* Count of children, used as a hint to go look for children on destroy. */
	size_t size;					/* size the handle was allocated at */
	ks_handle_base_t *data;			/* the handle data buffer */
	ks_handle_deinit_cb_t deinit_cb;/* callback to call to free the context in the slot data ptr */

	ks_handle_describe_cb_t describe_cb; /* If set by the handle on alloc, gets called anytime someone
										  * calls ks_handle_describe, gives the handle to render some
										  * context concerning itself */

	/* Debug builds will always log who allocated the original handle (checkouts will not be tracked as that cuases
	 * performance impacts on get due to the required lock) */
#if KS_BUILD_DEBUG
	const char *file;
	const char *tag;
	int line;
#endif

	/* When KS_DEBUG_HANDLE is enabled we'll be able to historically track who allocated what and where */
#if KS_DEBUG_HANDLE
	const char *last_get_file;
	const char *last_get_tag;
	int last_get_line;
#endif

} ks_handle_slot_t;

typedef struct ks_handle_group_s {
	ks_handle_slot_t slots[KS_HANDLE_MAX_SLOTS];
	ks_spinlock_t lock;
	uint32_t slot_chunks[KS_HANDLE_MAX_SLOT_CHUNKS];
	uint32_t slot_pages[KS_HANDLE_MAX_SLOT_PAGES];
	uint32_t sequence;				/* This is an incrementing number that validates an allocated slot instance */
	volatile uint16_t next_free;   	/* set by visitors as they release slots to give us a quick way to
									 * find the next free one on allocation
									 */
} ks_handle_group_t;

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
