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

#ifndef _KS_Q_H_
#define _KS_Q_H_

#include "ks.h"

KS_BEGIN_EXTERN_C

KS_DECLARE(ks_status_t) ks_q_pop_timeout(ks_q_t *q, void **ptr, uint32_t timeout);
KS_DECLARE(ks_status_t) ks_q_wake(ks_q_t *q);
KS_DECLARE(ks_status_t) ks_q_flush(ks_q_t *q);
KS_DECLARE(ks_status_t) ks_q_set_flush_fn(ks_q_t *q, ks_flush_fn_t fn, void *flush_data);
KS_DECLARE(ks_status_t) ks_q_wait(ks_q_t *q);
KS_DECLARE(ks_size_t) ks_q_term(ks_q_t *q);
KS_DECLARE(ks_size_t) ks_q_size(ks_q_t *q);
KS_DECLARE(ks_size_t) ks_q_maxlen(ks_q_t *q);
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
