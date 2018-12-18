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

typedef struct ks_lfq_node_s {
	struct ks_lfq_node_s *next;
	void *ptr;
} ks_lfq_node_t;

struct ks_lfq_s {
	struct ks_lfq_node_s *head;
	struct ks_lfq_node_s *tail;
	ks_size_t maxsize;
	ks_size_t size;
};

static void ks_lfq_cleanup(void *ptr, void *arg, ks_pool_cleanup_action_t action, ks_pool_cleanup_type_t type)
{
	ks_lfq_t *lfq = (ks_lfq_t *)ptr;
	ks_lfq_node_t *node = NULL;
	ks_lfq_node_t *next = NULL;

	if (type == KS_MPCL_GLOBAL_FREE) return;

	switch(action) {
	case KS_MPCL_ANNOUNCE:
		break;
	case KS_MPCL_TEARDOWN:
		node = lfq->head;
		while(node) {
			next = node->next;
			ks_pool_free(&node);
			node = next;
		}
		break;
	case KS_MPCL_DESTROY:
		break;
	}
}

KS_DECLARE(ks_status_t) ks_lfq_create(ks_lfq_t **lfqP, ks_pool_t *pool, ks_size_t maxsize)
{
	ks_lfq_t *lfq = NULL;

	ks_assert(lfqP);
	ks_assert(pool);
	ks_assert(maxsize > 0);

	lfq = ks_pool_alloc(pool, sizeof(ks_lfq_t));

	lfq->maxsize = maxsize;
	lfq->head = lfq->tail = ks_pool_alloc(pool, sizeof(ks_lfq_node_t));

	ks_pool_set_cleanup(lfq, NULL, ks_lfq_cleanup);

	*lfqP = lfq;

	return KS_STATUS_SUCCESS;
}

KS_DECLARE(ks_status_t) ks_lfq_destroy(ks_lfq_t **lfqP)
{
	ks_assert(lfqP);
	ks_assert(*lfqP);

	ks_pool_free(lfqP);

	return KS_STATUS_SUCCESS;
}

KS_DECLARE(ks_size_t) ks_lfq_size(ks_lfq_t *lfq)
{
	return lfq->size;
}

KS_DECLARE(ks_size_t) ks_lfq_max(ks_lfq_t *lfq)
{
	return lfq->maxsize;
}

KS_DECLARE(ks_bool_t) ks_lfq_push(ks_lfq_t *lfq, void *ptr)
{
	ks_lfq_node_t *oldtail = NULL;
	ks_lfq_node_t *oldtailnext = NULL;
	ks_lfq_node_t *node = NULL;
	ks_bool_t added = KS_FALSE;

	ks_assert(lfq);

	if (lfq->size >= lfq->maxsize) return KS_FALSE;

	node = ks_pool_alloc(ks_pool_get(lfq), sizeof(ks_lfq_node_t));
	node->ptr = ptr;

	while (!added) {
		oldtail = lfq->tail;
		oldtailnext = oldtail->next;
		if (lfq->tail == oldtail) {
			if (!oldtailnext) added = ks_atomic_cas_ptr((void **)&lfq->tail->next, NULL, (void *)node);
			else ks_atomic_cas_ptr((void **)&lfq->tail, (void *)oldtail, (void *)oldtailnext);
		}
	}

	ks_atomic_cas_ptr((void **)&lfq->tail, (void *)oldtail, (void *)node);
	ks_atomic_increment_size(&lfq->size);

	return KS_TRUE;
}

KS_DECLARE(ks_bool_t) ks_lfq_pop(ks_lfq_t *lfq, void **ptr)
{
	ks_bool_t havenext = KS_FALSE;
	ks_lfq_node_t *oldhead = NULL;
	ks_lfq_node_t *oldheadnext = NULL;
	ks_lfq_node_t *oldtail = NULL;

	ks_assert(lfq);
	ks_assert(ptr);

	*ptr = NULL;

	if (lfq->size == 0) return KS_FALSE;

	while (!havenext) {
		oldhead = lfq->head;
		oldheadnext = oldhead->next;
		oldtail = lfq->tail;

		if (!oldheadnext) return KS_FALSE;
		if (lfq->head == oldhead) {
			if (oldhead == oldtail) {
				ks_atomic_cas_ptr((void **)&lfq->tail, (void *)oldtail, (void *)oldheadnext); // @todo review this logic, need to update tail appropriately
			} else {
				*ptr = oldheadnext->ptr;
				havenext = ks_atomic_cas_ptr((void **)&lfq->head->next, (void *)oldheadnext, (void *)oldheadnext->next);
				if (havenext) ks_pool_free(&oldheadnext);

			}
		}
	}

	ks_atomic_decrement_size(&lfq->size);

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
