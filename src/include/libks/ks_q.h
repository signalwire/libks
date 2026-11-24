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

#ifndef _KS_Q_H_
#define _KS_Q_H_

#include "ks.h"

KS_BEGIN_EXTERN_C

/*
 * Status codes of the push, pop and peek functions:
 *
 * KS_STATUS_SUCCESS   The item was pushed, popped or peeked.
 * KS_STATUS_INACTIVE  The queue was terminated by ks_q_term(). Items left in it are not returned.
 * KS_STATUS_BREAK     ks_q_trypush() found the queue full, or ks_q_trypop() or ks_q_trypeek() found it
 *                     empty. From ks_q_push(), ks_q_pop() and the timed pops: ks_q_wake() was called
 *                     while the thread waited, and the queue was still full or empty when it ran again.
 * KS_STATUS_TIMEOUT   The time given to ks_q_pop_timeout() or ks_q_timedpop_usec() passed and the
 *                     queue was empty.
 * KS_STATUS_FAIL      The wait of a timed pop failed.
 *
 * ks_q_push() waits while a queue created with a maxlen is full; a maxlen of 0 sets no limit.
 * ks_q_pop() waits while the queue is empty.
 */

/* Waits for an item for up to timeout milliseconds; a timeout of 0 waits without a time limit. */
KS_DECLARE(ks_status_t) ks_q_pop_timeout(ks_q_t *q, void **ptr, uint32_t timeout);
/*
 * Waits for an item for up to usec microseconds. 0 or a negative value does not wait, unlike
 * the 0 of ks_q_pop_timeout(). Windows waits in whole milliseconds.
 */
KS_DECLARE(ks_status_t) ks_q_timedpop_usec(ks_q_t *q, void **ptr, ks_time_t usec);
/* Ends the wait of every thread blocked in a push or a pop; see KS_STATUS_BREAK. */
KS_DECLARE(ks_status_t) ks_q_wake(ks_q_t *q);
KS_DECLARE(ks_status_t) ks_q_flush(ks_q_t *q);
KS_DECLARE(ks_status_t) ks_q_set_flush_fn(ks_q_t *q, ks_flush_fn_t fn, void *flush_data);
KS_DECLARE(ks_status_t) ks_q_wait(ks_q_t *q);
KS_DECLARE(ks_size_t) ks_q_term(ks_q_t *q);
KS_DECLARE(ks_size_t) ks_q_size(ks_q_t *q);
KS_DECLARE(ks_size_t) ks_q_maxlen(ks_q_t *q);
KS_DECLARE(ks_size_t) ks_q_pushers(ks_q_t *q);
KS_DECLARE(ks_status_t) ks_q_destroy(ks_q_t **qP);
KS_DECLARE(ks_status_t) ks_q_create(ks_q_t **qP, ks_pool_t *pool, ks_size_t maxlen);
KS_DECLARE(ks_status_t) ks_q_push(ks_q_t *q, void *ptr);
KS_DECLARE(ks_status_t) ks_q_trypush(ks_q_t *q, void *ptr);
KS_DECLARE(ks_status_t) ks_q_pop(ks_q_t *q, void **ptr);
KS_DECLARE(ks_status_t) ks_q_trypop(ks_q_t *q, void **ptr);
KS_DECLARE(ks_status_t) ks_q_trypeek(ks_q_t *q, void **ptr);

KS_END_EXTERN_C

#endif							/* defined(_KS_Q_H_) */

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
