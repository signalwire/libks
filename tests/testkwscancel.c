/*
 * Copyright (c) 2018-2026 SignalWire, Inc
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
 * Tests for kws_cancel API.
 *
 * Verifies that kws_cancel can:
 *   1. Safely handle NULL
 *   2. Pre-cancel a kws before connection (connect fails immediately)
 *   3. Cancel a blocking kws_connect_ex from another thread
 *   4. Cancel a blocking kws_read_frame from another thread
 */

#include "libks/ks.h"
#include <tap.h>

#ifdef _WINDOWS_
#undef unlink
#define unlink _unlink
#endif

#define SHA1_HASH_SIZE 20
#define WEBSOCKET_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define B64BUFFLEN 1024

#define STALL_PORT 8191
#define NORMAL_PORT 8192

/*
 * Max allowed ms for a cancelled operation to return.
 * Must be well below WS_BLOCK (10 000 ms) to prove cancel
 * actually interrupted the operation rather than it timing out.
 */
#define CANCEL_MAX_MS 3000

/* ── base64 / sha1 helpers (same as testwebsock2) ── */

static const char c64[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int b64encode(unsigned char *in, size_t ilen, unsigned char *out, size_t olen)
{
	int y = 0, bytes = 0;
	size_t x = 0;
	unsigned int b = 0, l = 0;

	(void)olen;

	for (x = 0; x < ilen; x++) {
		b = (b << 8) + in[x];
		l += 8;
		while (l >= 6) {
			out[bytes++] = c64[(b >> (l -= 6)) % 64];
			if (++y != 72) {
				continue;
			}
			y = 0;
		}
	}

	if (l > 0) {
		out[bytes++] = c64[((b % 16) << (6 - l)) % 64];
	}
	if (l != 0) while (l < 6) {
		out[bytes++] = '=', l += 2;
	}

	return 0;
}

#ifdef NO_OPENSSL
static void sha1_digest(char *digest, unsigned char *in)
{
	SHA1Context sha;
	SHA1Init(&sha);
	SHA1Update(&sha, in, strlen(in));
	SHA1Final(&sha, digest);
}
#else
static void sha1_digest(unsigned char *digest, char *in)
{
	SHA_CTX sha;
	SHA1_Init(&sha);
	SHA1_Update(&sha, in, strlen(in));
	SHA1_Final(digest, &sha);
}
#endif

/* ── shared server data ── */

struct tcp_data {
	ks_socket_t sock;
	ks_sockaddr_t addr;
	volatile int ready;
	volatile int done;
};

/* ── stalling server: accepts TCP, reads WS upgrade, but never responds ── */

static void stall_server_callback(ks_socket_t server_sock, ks_socket_t client_sock, ks_sockaddr_t *addr, void *user_data)
{
	struct tcp_data *tcp_data = (struct tcp_data *)user_data;
	char buf[8192] = "";
	ks_size_t bytes;
	ks_status_t status;

	(void)server_sock;
	(void)addr;

	/* Read the full WS upgrade request */
	do {
		bytes = sizeof(buf);
		status = ks_socket_recv(client_sock, buf, &bytes);
		if (status != KS_STATUS_SUCCESS) break;
	} while (ks_zstr_buf(buf) || !strstr(buf, "\r\n\r\n"));

	/* Stall: don't send the WS handshake response, just wait */
	while (!tcp_data->done) {
		ks_sleep_ms(50);
	}

	ks_socket_close(&client_sock);
}

/* ── normal server: completes WS handshake, then holds connection open ── */

static void normal_server_callback(ks_socket_t server_sock, ks_socket_t client_sock, ks_sockaddr_t *addr, void *user_data)
{
	struct tcp_data *tcp_data = (struct tcp_data *)user_data;
	char buf[8192] = "";
	ks_size_t bytes;
	ks_status_t status;
	char key[1024] = "";
	char input[512] = "";
	unsigned char output[SHA1_HASH_SIZE] = "";
	char b64[256] = "";

	(void)server_sock;
	(void)addr;

	/* Read the full WS upgrade request */
	do {
		bytes = sizeof(buf);
		status = ks_socket_recv(client_sock, buf, &bytes);
		if (status != KS_STATUS_SUCCESS) break;
	} while (ks_zstr_buf(buf) || !strstr(buf, "\r\n\r\n"));

	/* Extract Sec-WebSocket-Key and compute accept */
	{
		char *k = strstr(buf, "Sec-WebSocket-Key:");
		char *p;
		ks_assert(k);
		k += strlen("Sec-WebSocket-Key:");
		if (*k == ' ') k++;
		p = strchr(k, '\r');
		ks_assert(p);
		*p = '\0';
		strncpy(key, k, sizeof(key) - 1);
	}

	snprintf(input, sizeof(input), "%s%s", key, WEBSOCKET_GUID);
	sha1_digest(output, input);
	b64encode(output, SHA1_HASH_SIZE, (unsigned char *)b64, sizeof(b64));

	/* Send the WS handshake response only (no initial frame) */
	snprintf(buf, sizeof(buf),
		"HTTP/1.1 101 Switching Protocols\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Accept: %s\r\n\r\n",
		b64);

	bytes = (ks_size_t)strlen(buf);
	ks_socket_send(client_sock, buf, &bytes);

	/* Hold the connection open until told to stop */
	while (!tcp_data->done) {
		ks_sleep_ms(50);
	}

	ks_socket_close(&client_sock);
}

/* ── server thread entry point ── */

struct server_config {
	struct tcp_data tcp_data;
	ks_listen_callback_t callback;
};

static void *server_thread_func(ks_thread_t *thread, void *thread_data)
{
	struct server_config *cfg = (struct server_config *)thread_data;

	(void)thread;

	cfg->tcp_data.ready = 1;
	ks_listen_sock(cfg->tcp_data.sock, &cfg->tcp_data.addr, 0, cfg->callback, &cfg->tcp_data);

	return NULL;
}

static ks_status_t start_server(struct server_config *cfg, ks_listen_callback_t callback,
								ks_port_t port, ks_pool_t *pool, ks_thread_t **thread_out)
{
	int family = AF_INET;
	int sanity = 200;

	memset(cfg, 0, sizeof(*cfg));
	cfg->callback = callback;

	if (ks_addr_set(&cfg->tcp_data.addr, "127.0.0.1", port, family) != KS_STATUS_SUCCESS) {
		return KS_STATUS_FAIL;
	}

	cfg->tcp_data.sock = socket(family, SOCK_STREAM, IPPROTO_TCP);
	if (cfg->tcp_data.sock == KS_SOCK_INVALID) {
		return KS_STATUS_FAIL;
	}

	ks_socket_option(cfg->tcp_data.sock, SO_REUSEADDR, KS_TRUE);
	ks_socket_option(cfg->tcp_data.sock, TCP_NODELAY, KS_TRUE);

	if (ks_thread_create(thread_out, server_thread_func, cfg, pool) != KS_STATUS_SUCCESS) {
		ks_socket_close(&cfg->tcp_data.sock);

		return KS_STATUS_FAIL;
	}

	while (!cfg->tcp_data.ready && --sanity > 0) {
		ks_sleep_ms(10);
	}

	return sanity > 0 ? KS_STATUS_SUCCESS : KS_STATUS_FAIL;
}

static void stop_server(struct server_config *cfg, ks_thread_t *thread)
{
	cfg->tcp_data.done = 1;
	ks_socket_shutdown(cfg->tcp_data.sock, 2);
	ks_socket_close(&cfg->tcp_data.sock);
	ks_thread_join(thread);
}

/* ── data shared between connect/read thread and main thread ── */

struct cancel_test_ctx {
	kws_t *kws;
	ks_pool_t *pool;
	ks_json_t *params;
	ks_status_t connect_status;
	ks_ssize_t read_result;
};

/* ── thread: blocking connect ── */

static void *connect_thread_func(ks_thread_t *thread, void *thread_data)
{
	struct cancel_test_ctx *ctx = (struct cancel_test_ctx *)thread_data;

	(void)thread;

	ctx->connect_status = kws_connect_ex(&ctx->kws, ctx->params, KWS_BLOCK | KWS_CLOSE_SOCK, ctx->pool, NULL, 30000);

	return NULL;
}

/* ── thread: blocking read ── */

static void *read_thread_func(ks_thread_t *thread, void *thread_data)
{
	struct cancel_test_ctx *ctx = (struct cancel_test_ctx *)thread_data;
	kws_opcode_t oc;
	uint8_t *rdata;

	(void)thread;

	ctx->read_result = kws_read_frame(ctx->kws, &oc, &rdata);

	return NULL;
}

/* ──────────────────────────────────────────────────────────────────── */
/*  TEST 1: kws_cancel(NULL) must not crash                           */
/* ──────────────────────────────────────────────────────────────────── */

static void test_cancel_null(void)
{
	kws_cancel(NULL);
	ok(1, "kws_cancel(NULL) does not crash");
}

/* ──────────────────────────────────────────────────────────────────── */
/*  TEST 2: Pre-cancelled kws should fail to connect                  */
/*  kws_create → kws_cancel → kws_connect_ex must return failure      */
/*  because the handshake loop checks the cancel flag immediately.    */
/* ──────────────────────────────────────────────────────────────────── */

static void test_pre_cancel(void)
{
	ks_pool_t *pool;
	ks_thread_t *srv_thread = NULL;
	struct server_config srv_cfg;
	kws_t *kws = NULL;
	ks_json_t *req;
	ks_status_t status;
	char url[256];

	ks_pool_open(&pool);

	/* Start a stalling server (it accepts TCP but never responds to WS handshake) */
	if (start_server(&srv_cfg, stall_server_callback, STALL_PORT, pool, &srv_thread) != KS_STATUS_SUCCESS) {
		fail("test_pre_cancel: could not start server");
		ks_pool_close(&pool);

		return;
	}

	snprintf(url, sizeof(url), "ws://127.0.0.1:%d/test", STALL_PORT);
	req = ks_json_create_object();
	ks_json_add_string_to_object(req, "url", url);

	/* Pre-create and pre-cancel */
	kws_create(&kws, pool);
	kws_cancel(kws);

	/* Connect should fail because cancel flag is already set */
	{
		ks_time_t start = ks_time_now();
		ks_time_t elapsed_ms;

		status = kws_connect_ex(&kws, req, KWS_BLOCK | KWS_CLOSE_SOCK, pool, NULL, 5000);
		elapsed_ms = ks_time_ms(ks_time_now() - start);

		ok(status != KS_STATUS_SUCCESS, "pre-cancelled kws_connect_ex returns failure");
		ok(elapsed_ms < CANCEL_MAX_MS, "pre-cancelled connect returned in %d ms (< %d ms)",
		   (int)elapsed_ms, CANCEL_MAX_MS);
	}

	ks_json_delete(&req);
	stop_server(&srv_cfg, srv_thread);
	ks_pool_close(&pool);
}

/* ──────────────────────────────────────────────────────────────────── */
/*  TEST 3: Cancel a blocking connect from another thread             */
/*  Client connects to stalling server in a thread. Main thread       */
/*  calls kws_cancel after a short delay. Connect must return         */
/*  failure within a bounded time.                                    */
/* ──────────────────────────────────────────────────────────────────── */

static void test_cancel_during_connect(void)
{
	ks_pool_t *pool;
	ks_thread_t *srv_thread = NULL;
	ks_thread_t *cli_thread = NULL;
	struct server_config srv_cfg;
	struct cancel_test_ctx ctx = { 0 };
	char url[256];

	ks_pool_open(&pool);

	if (start_server(&srv_cfg, stall_server_callback, STALL_PORT + 1, pool, &srv_thread) != KS_STATUS_SUCCESS) {
		fail("test_cancel_during_connect: could not start server");
		ks_pool_close(&pool);

		return;
	}

	snprintf(url, sizeof(url), "ws://127.0.0.1:%d/test", STALL_PORT + 1);
	ctx.params = ks_json_create_object();
	ks_json_add_string_to_object(ctx.params, "url", url);
	ctx.pool = pool;
	ctx.connect_status = KS_STATUS_SUCCESS; /* sentinel value */

	/* Pre-create so we have a handle to cancel */
	kws_create(&ctx.kws, pool);

	{
		ks_time_t start = ks_time_now();
		ks_time_t elapsed_ms;

		/* Spawn connect thread (will block in handshake waiting for server response) */
		ks_thread_create(&cli_thread, connect_thread_func, &ctx, pool);

		/* Give the connect thread time to enter the blocking handshake loop */
		ks_sleep_ms(500);

		/* Cancel from this thread */
		kws_cancel(ctx.kws);

		/* Wait for connect thread to finish */
		ks_thread_join(cli_thread);

		elapsed_ms = ks_time_ms(ks_time_now() - start);

		ok(ctx.connect_status != KS_STATUS_SUCCESS, "kws_cancel causes blocking connect to fail");
		ok(elapsed_ms < CANCEL_MAX_MS, "cancelled connect returned in %d ms (< %d ms)",
		   (int)elapsed_ms, CANCEL_MAX_MS);
	}

	ks_json_delete(&ctx.params);
	stop_server(&srv_cfg, srv_thread);
	ks_pool_close(&pool);
}

/* ──────────────────────────────────────────────────────────────────── */
/*  TEST 4: Cancel a blocking read from another thread                */
/*  Client connects to normal server (handshake completes). Then      */
/*  reads in a thread (no data coming → blocks). Main thread calls    */
/*  kws_cancel. Read must return within a bounded time.               */
/* ──────────────────────────────────────────────────────────────────── */

static void test_cancel_during_read(void)
{
	ks_pool_t *pool;
	ks_thread_t *srv_thread = NULL;
	ks_thread_t *read_thr = NULL;
	struct server_config srv_cfg;
	struct cancel_test_ctx ctx = { 0 };
	kws_t *kws = NULL;
	ks_json_t *req;
	char url[256];

	ks_pool_open(&pool);

	if (start_server(&srv_cfg, normal_server_callback, NORMAL_PORT, pool, &srv_thread) != KS_STATUS_SUCCESS) {
		fail("test_cancel_during_read: could not start server");
		ks_pool_close(&pool);

		return;
	}

	snprintf(url, sizeof(url), "ws://127.0.0.1:%d/test", NORMAL_PORT);
	req = ks_json_create_object();
	ks_json_add_string_to_object(req, "url", url);

	/* Connect (should succeed) */
	ok(kws_connect_ex(&kws, req, KWS_BLOCK | KWS_CLOSE_SOCK, pool, NULL, 5000) == KS_STATUS_SUCCESS,
	   "connected to normal server for read-cancel test");

	ks_json_delete(&req);

	/* Set up read context */
	ctx.kws = kws;
	ctx.pool = pool;
	ctx.read_result = 1; /* sentinel: positive means no error yet */

	{
		ks_time_t start = ks_time_now();
		ks_time_t elapsed_ms;

		/* Spawn read thread (will block - server sends no data after handshake) */
		ks_thread_create(&read_thr, read_thread_func, &ctx, pool);

		/* Give the read thread time to enter the blocking read loop */
		ks_sleep_ms(500);

		/* Cancel from this thread */
		kws_cancel(kws);

		/* Wait for read thread to finish */
		ks_thread_join(read_thr);

		elapsed_ms = ks_time_ms(ks_time_now() - start);

		ok(ctx.read_result <= 0, "kws_cancel causes blocking read to return error");
		ok(elapsed_ms < CANCEL_MAX_MS, "cancelled read returned in %d ms (< %d ms)",
		   (int)elapsed_ms, CANCEL_MAX_MS);
	}

	kws_destroy(&kws);
	stop_server(&srv_cfg, srv_thread);
	ks_pool_close(&pool);
}

/* ──────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;

	ks_init();

	plan(8);

	test_cancel_null();
	test_pre_cancel();
	test_cancel_during_connect();
	test_cancel_during_read();

	ks_shutdown();

	done_testing();
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
