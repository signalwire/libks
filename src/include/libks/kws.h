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

#define WEBSOCKET_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define B64BUFFLEN 1024

KS_BEGIN_EXTERN_C

typedef enum {
	WS_NONE = 0,
	WS_NORMAL = 1000,
	WS_PROTO_ERR = 1002,
	WS_DATA_TOO_BIG = 1009
} kws_cause_t;

typedef enum {
	WSOC_CONTINUATION = 0x0,
	WSOC_TEXT = 0x1,
	WSOC_BINARY = 0x2,
	WSOC_CLOSE = 0x8,
	WSOC_PING = 0x9,
	WSOC_PONG = 0xA,
	WSOC_INVALID = 0xF	/* Per the rfc this is the highest reserved valuel treat it as invalid */
} kws_opcode_t;

typedef enum {
	KWS_CLIENT,
	KWS_SERVER
} kws_type_t;

typedef enum {
	KWS_NONE = 0,
	KWS_CLOSE_SOCK = (1 << 0),
	KWS_BLOCK = (1 << 1),
	KWS_STAY_OPEN = (1 << 2),
	KWS_FLAG_DONTMASK = (1 << 3)
} kws_flag_t;

struct kws_s;
typedef struct kws_s kws_t;

KS_DECLARE(ks_ssize_t) kws_read_frame(kws_t *kws, kws_opcode_t *oc, uint8_t **data);
KS_DECLARE(ks_ssize_t) kws_write_frame(kws_t *kws, kws_opcode_t oc, const void *data, ks_size_t bytes);
KS_DECLARE(ks_ssize_t) kws_raw_read(kws_t *kws, void *data, ks_size_t bytes, int block);
KS_DECLARE(ks_ssize_t) kws_raw_write(kws_t *kws, void *data, ks_size_t bytes);
KS_DECLARE(ks_status_t) kws_init(kws_t **kwsP, ks_socket_t sock, SSL_CTX *ssl_ctx, const char *client_data, kws_flag_t flags, ks_pool_t *pool);
KS_DECLARE(ks_ssize_t) kws_close(kws_t *kws, int16_t reason);
KS_DECLARE(ks_status_t) kws_create(kws_t **kwsP, ks_pool_t *pool);
KS_DECLARE(void) kws_destroy(kws_t **kwsP);
KS_DECLARE(ks_status_t) kws_connect(kws_t **kwsP, ks_json_t *params, kws_flag_t flags, ks_pool_t *pool);
KS_DECLARE(ks_status_t) kws_connect_ex(kws_t **kwsP, ks_json_t *params, kws_flag_t flags, ks_pool_t *pool, SSL_CTX *ssl_ctx, uint32_t timeout_ms);
KS_DECLARE(ks_status_t) kws_get_buffer(kws_t *kws, char **bufP, ks_size_t *buflen);
KS_DECLARE(ks_status_t) kws_get_cipher_name(kws_t *kws, char *name, ks_size_t name_len);
KS_DECLARE(ks_bool_t) kws_certified_client(kws_t *kws);
KS_DECLARE(ks_size_t) kws_sans_count(kws_t *kws);
KS_DECLARE(const char *) kws_sans_get(kws_t *kws, ks_size_t index);
KS_DECLARE(int) kws_wait_sock(kws_t *kws, uint32_t ms, ks_poll_t flags);


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
