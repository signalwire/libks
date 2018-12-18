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

#ifndef _KS_FAKTORY_H_
#define _KS_FAKTORY_H_

#include "ks.h"

KS_BEGIN_EXTERN_C

KS_DECLARE(ks_status_t) ks_faktory_create(ks_faktory_t **faktoryP, ks_size_t inbufsize, ks_size_t outbufsize, ks_size_t outqmax, ks_bool_t producer);
KS_DECLARE(ks_status_t) ks_faktory_destroy(ks_faktory_t **faktoryP);
KS_DECLARE(const char *) ks_faktory_state_string(ks_faktory_state_t state);
KS_DECLARE(ks_faktory_state_t) ks_faktory_state_get(ks_faktory_t *faktory);
KS_DECLARE(ks_bool_t) ks_faktory_ready(ks_faktory_t *faktory);
KS_DECLARE(void) ks_faktory_state_handler_set(ks_faktory_t *faktory, ks_faktory_state_callback_t callback);
KS_DECLARE(void) ks_faktory_autoack_set(ks_faktory_t *faktory, ks_bool_t enable);
KS_DECLARE(void) ks_faktory_password_set(ks_faktory_t *faktory, const char *password);
KS_DECLARE(void) ks_faktory_hostname_set(ks_faktory_t *faktory, const char *hostname);
KS_DECLARE(const char *) ks_faktory_wid_get(ks_faktory_t *faktory);
KS_DECLARE(void) ks_faktory_wid_set(ks_faktory_t *faktory, const char *wid);
KS_DECLARE(void) ks_faktory_label_add(ks_faktory_t *faktory, const char *label);
KS_DECLARE(void) ks_faktory_label_remove(ks_faktory_t *faktory, const char *label);
KS_DECLARE(ks_status_t) ks_faktory_connect(ks_faktory_t *faktory, const char *host, ks_port_t port);
KS_DECLARE(void) ks_faktory_reset(ks_faktory_t *faktory, ks_bool_t clearq);
KS_DECLARE(ks_status_t) ks_faktory_send(ks_faktory_t *faktory, const char *cmd, ks_json_t *args, ks_faktory_command_callback_t callback, void *data);
KS_DECLARE(ks_status_t) ks_faktory_push(ks_faktory_t *faktory, const char *jobtype, ks_json_t *args, const char *queue, int priority, ks_json_t *custom, const char **jidP, ks_faktory_command_callback_t callback, void *data);
KS_DECLARE(ks_status_t) ks_faktory_fetch(ks_faktory_t *faktory, const char *queues, ks_faktory_command_callback_t callback, void *data);
KS_DECLARE(ks_status_t) ks_faktory_beat(ks_faktory_t *faktory, ks_bool_t force);
KS_DECLARE(ks_status_t) ks_faktory_ack(ks_faktory_t *faktory, const char *jid, ks_faktory_command_callback_t callback, void *data);
KS_DECLARE(ks_status_t) ks_faktory_fail(ks_faktory_t *faktory, const char *jid, const char *errtype, const char *message, ks_faktory_command_callback_t callback, void *data);

KS_END_EXTERN_C

#endif /* defined(_KS_FAKTORY_H_) */

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
