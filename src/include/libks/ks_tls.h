/*
 * Copyright (c) 2018-2025 SignalWire, Inc
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

#include "ks.h"

KS_BEGIN_EXTERN_C

typedef enum {
	KS_TLS_CLIENT,
	KS_TLS_SERVER
} ks_tls_type_t;

typedef enum {
	KS_TLS_NONE = 0,
	KS_TLS_CLOSE_SOCK = (1 << 0),
	KS_TLS_BLOCK = (1 << 1),
} ks_tls_flag_t;

typedef struct {
	char *host;
	ks_port_t port;
	uint32_t timeout_ms;
	ks_tls_flag_t flags;
	SSL_CTX *ssl_ctx;
} ks_tls_connect_params_t;

typedef struct ks_tls_s ks_tls_t;

typedef void (*ks_tls_init_callback_t)(ks_tls_t *ktls, SSL* ssl);

KS_DECLARE(ks_status_t) ks_tls_connect(ks_tls_t **ktlsP, ks_tls_connect_params_t *params, ks_pool_t *pool);
KS_DECLARE(void) ks_tls_destroy(ks_tls_t **ktlsP);
KS_DECLARE(ks_ssize_t) ks_tls_write(ks_tls_t *ktls, void *data, ks_size_t bytes);
KS_DECLARE(ks_ssize_t) ks_tls_read(ks_tls_t *ktls, void *data, ks_size_t bytes, int block_ms);

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
