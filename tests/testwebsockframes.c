/*
 * Copyright (c) 2026 SignalWire, Inc
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
 * kws_read_frame and frames shorter than its first read.
 *
 * kws_read_frame reads up to 9 bytes before it knows how long a frame is. When
 * the frame is shorter than that (an empty PING or PONG, a tiny text frame) and
 * the next frame is already waiting, that read takes the start of the next frame
 * with it. Those bytes were counted as this frame's payload, the remaining length
 * came out negative ("need < 0") and the connection was closed: a peer that
 * pings, or sends a short message followed by another, could drop it at any time.
 *
 * The raw server below writes its frames with a single send, and the client
 * does not read until they are all there, so the over-read happens in both
 * places the extra bytes can come from: after the handshake, and in the same
 * read as the handshake response, where kws keeps them as unprocessed bytes and
 * the over-read has to go back in front of what remains.
 *
 * Handing bytes back must not turn a malformed length into a valid-looking
 * over-read: a 64-bit length of 2^63 or more is a protocol error (RFC 6455:
 * its top bit must be 0), and the connection is closed, never read past.
 */

#include "libks/ks.h"
#include <tap.h>
#include <openssl/sha.h>
#include <openssl/evp.h>

#define FRAMES_PORT 8095
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

/* Unmasked server frames: an empty PING, TEXT "hello", an empty PONG, BINARY {1, 2}. */
static const uint8_t FRAMES[] = { 0x89, 0x00, 0x81, 0x05, 'h', 'e', 'l', 'l', 'o', 0x8A, 0x00, 0x82, 0x02, 0x01, 0x02 };

/* A BINARY frame whose 64-bit length has its top bit set, with one payload byte
 * behind it: a build that narrowed the length to 32 bits before checking it
 * would read that byte as a one-byte frame instead of closing. */
static const uint8_t BAD_LENGTH[] = { 0x82, 0x7F, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xAA };

struct frames_srv {
	ks_socket_t sock;
	int with_handshake;      /* the frames go out in the same send as the 101 */
	const uint8_t *frames;
	ks_size_t frames_len;
	volatile int client_up;  /* the client has finished its handshake */
	volatile int done;       /* the client has finished reading */
};

static int send_all(ks_socket_t sock, const void *data, ks_size_t len)
{
	ks_size_t off = 0;

	while (off < len) {
		ks_size_t n = len - off;

		if (ks_socket_send(sock, (char *)data + off, &n) != KS_STATUS_SUCCESS || n == 0) {
			return 0;
		}
		off += n;
	}

	return 1;
}

static void frames_serve(struct frames_srv *srv, ks_socket_t client_sock)
{
	char req[4096] = "", key[128] = "", src[256], out[1024];
	unsigned char sha[SHA_DIGEST_LENGTH], accept[64] = "";
	ks_size_t got = 0;
	char *k, *e;
	int n, sanity;

	/* The upgrade request, whole. */
	while (got < sizeof(req) - 1 && !strstr(req, "\r\n\r\n")) {
		ks_size_t bytes = sizeof(req) - 1 - got;

		if (ks_wait_sock(client_sock, 2000, KS_POLL_READ) <= 0 ||
			ks_socket_recv(client_sock, req + got, &bytes) != KS_STATUS_SUCCESS) {
			goto end;
		}
		got += bytes;
		req[got] = '\0';
	}

	if (!(k = strstr(req, "Sec-WebSocket-Key: ")) || !(e = strstr(k, "\r\n"))) {
		goto end;
	}
	k += strlen("Sec-WebSocket-Key: ");
	ks_snprintf(key, sizeof(key), "%.*s", (int)(e - k), k);
	ks_snprintf(src, sizeof(src), "%s%s", key, WS_GUID);
	SHA1((const unsigned char *)src, strlen(src), sha);
	EVP_EncodeBlock(accept, sha, SHA_DIGEST_LENGTH);

	n = ks_snprintf(out, sizeof(out),
		"HTTP/1.1 101 Switching Protocols\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Accept: %s\r\n"
		"\r\n", (char *)accept);

	if (srv->with_handshake) {
		memcpy(out + n, srv->frames, srv->frames_len);
		send_all(client_sock, out, n + srv->frames_len);
	} else {
		send_all(client_sock, out, n);
		for (sanity = 200; !srv->client_up && sanity > 0; sanity--) {
			ks_sleep_ms(10);
		}
		send_all(client_sock, srv->frames, srv->frames_len);
	}

	for (sanity = 500; !srv->done && sanity > 0; sanity--) {
		ks_sleep_ms(10);
	}

 end:
	ks_socket_close(&client_sock);
}

/* The listening socket is bound and listening before this thread starts, so the
 * client can never connect ahead of it. */
static void *frames_server_thread(ks_thread_t *thread, void *thread_data)
{
	struct frames_srv *srv = (struct frames_srv *)thread_data;
	ks_socket_t client_sock;

	(void)thread;
	if ((client_sock = accept(srv->sock, NULL, NULL)) != KS_SOCK_INVALID) {
		frames_serve(srv, client_sock);
	}

	return NULL;
}

/* Do not read until `len` bytes are waiting on the socket, so the first read
 * takes more than the first frame. */
static int wait_for_bytes(ks_socket_t sock, ks_size_t len)
{
	char peek[64];
	int sanity;

	for (sanity = 200; sanity > 0; sanity--) {
		if (ks_wait_sock(sock, 10, KS_POLL_READ) > 0 &&
			recv(sock, peek, (int)(len < sizeof(peek) ? len : sizeof(peek)), MSG_PEEK) >= (int)len) {
			return 1;
		}
		ks_sleep_ms(10);   /* part of it is there: readable at once, so wait out the rest */
	}

	return 0;
}

static int expect_frame(kws_t *kws, kws_opcode_t want_oc, const void *want, ks_ssize_t want_len)
{
	kws_opcode_t oc = WSOC_INVALID;
	uint8_t *data = NULL;
	ks_ssize_t r = kws_read_frame(kws, &oc, &data);

	if (r != want_len || oc != want_oc) {
		diag("read frame: got opcode %d, %ld bytes; wanted opcode %d, %ld bytes", (int)oc, (long)r, (int)want_oc, (long)want_len);

		return 0;
	}
	if (want_len > 0 && (!data || memcmp(data, want, (size_t)want_len))) {
		diag("read frame: payload differs");

		return 0;
	}

	return 1;
}

enum { CASE_SHORT_FRAMES, CASE_BAD_LENGTH };

static int test_frames(const char *ip, int with_handshake, int which)
{
	ks_thread_t *thread_p = NULL;
	ks_pool_t *pool = NULL;
	ks_sockaddr_t addr;
	ks_socket_t cl_sock = KS_SOCK_INVALID;
	struct frames_srv srv = { 0 };
	kws_t *kws = NULL;
	int r = 0;
	static const uint8_t two[] = { 0x01, 0x02 };

	ks_pool_open(&pool);
	srv.with_handshake = with_handshake;
	srv.frames = which == CASE_BAD_LENGTH ? BAD_LENGTH : FRAMES;
	srv.frames_len = which == CASE_BAD_LENGTH ? sizeof(BAD_LENGTH) : sizeof(FRAMES);

	if (ks_addr_set(&addr, ip, FRAMES_PORT, AF_INET) != KS_STATUS_SUCCESS ||
		(srv.sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == KS_SOCK_INVALID) {
		goto end;
	}
	ks_socket_option(srv.sock, SO_REUSEADDR, KS_TRUE);
	if (ks_addr_bind(srv.sock, &addr) != KS_STATUS_SUCCESS || listen(srv.sock, 1) != 0) {
		diag("listen failed");
		goto end;
	}
	ks_thread_create(&thread_p, frames_server_thread, &srv, pool);

	if ((cl_sock = ks_socket_connect(SOCK_STREAM, IPPROTO_TCP, &addr)) == KS_SOCK_INVALID) {
		diag("connect failed");
		goto end;
	}
	if (kws_init(&kws, cl_sock, NULL, "/frames:localhost", KWS_BLOCK, pool) != KS_STATUS_SUCCESS) {
		diag("client handshake failed");
		goto end;
	}
	srv.client_up = 1;

	if (with_handshake) {
		/* They came in the handshake read, so kws holds them: nothing may be
		 * left on the socket, or this case did not exercise that path. (The
		 * server sent the response and the frames in one send on loopback,
		 * which arrives as one unit.) */
		ks_sleep_ms(50);
		if (ks_wait_sock(cl_sock, 0, KS_POLL_READ) > 0) {
			diag("frames did not arrive with the handshake response");
			goto end;
		}
	} else if (!wait_for_bytes(cl_sock, srv.frames_len)) {
		diag("frames did not arrive");
		goto end;
	}

	if (which == CASE_BAD_LENGTH) {
		kws_opcode_t oc = WSOC_INVALID;
		uint8_t *data = NULL;
		ks_ssize_t got = kws_read_frame(kws, &oc, &data);

		/* kws_read_frame reports a protocol error as a close: opcode WSOC_CLOSE,
		 * nothing read (what kws_close returns), and the connection down. */
		r = (oc == WSOC_CLOSE && got <= 0);
		if (!r) {
			diag("bad length: got opcode %d, %ld bytes; wanted a close", (int)oc, (long)got);
		}
	} else {
		r = expect_frame(kws, WSOC_PING, NULL, 0) &&
			expect_frame(kws, WSOC_TEXT, "hello", 5) &&
			expect_frame(kws, WSOC_PONG, NULL, 0) &&
			expect_frame(kws, WSOC_BINARY, two, 2);
	}

 end:
	srv.done = 1;
	kws_destroy(&kws);

	/* Shut the listener first: if the client never connected, the server thread
	 * is still in accept(), and this is what returns it. */
	if (srv.sock != KS_SOCK_INVALID) {
		ks_socket_shutdown(srv.sock, 2);
		ks_socket_close(&srv.sock);
	}
	if (thread_p) {
		ks_thread_join(thread_p);
	}
	ks_socket_close(&cl_sock);
	ks_pool_close(&pool);

	return r;
}

int main(void)
{
	ks_init();

	plan(3);

	ok(test_frames("127.0.0.1", 0, CASE_SHORT_FRAMES), "short frames arriving together after the handshake are each read whole");
	ok(test_frames("127.0.0.1", 1, CASE_SHORT_FRAMES), "short frames arriving with the handshake response are each read whole");
	ok(test_frames("127.0.0.1", 0, CASE_BAD_LENGTH), "a 64-bit length of 2^63 or more closes the connection");

	ks_shutdown();

	done_testing();
}
