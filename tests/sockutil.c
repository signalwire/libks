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

#include "sockutil.h"

#ifdef __APPLE__
#include <netinet/tcp.h>
#include <netinet/tcp_fsm.h>
#endif

/**
 * Whether a socket is listening.
 * @param sock The socket.
 * @return 1 listening, 0 not (yet), -1 the lookup failed (the socket was closed).
 */
static int sockutil_listening(ks_socket_t sock)
{
#ifdef __APPLE__
	/* macOS has no SO_ACCEPTCONN for getsockopt (ENOPROTOOPT); the TCP state says it. */
	struct tcp_connection_info info;
	socklen_t len = sizeof(info);

	if (getsockopt(sock, IPPROTO_TCP, TCP_CONNECTION_INFO, &info, &len) != 0) {
		return -1;
	}

	return info.tcpi_state == TCPS_LISTEN;
#else
	int listening = 0;
	socklen_t len = sizeof(listening);

	if (getsockopt(sock, SOL_SOCKET, SO_ACCEPTCONN, (char *)&listening, &len) != 0) {
		return -1;
	}

	return listening != 0;
#endif
}

int sockutil_wait_listen(ks_socket_t sock, uint32_t ms)
{
	uint32_t waited;

	for (waited = 0; waited <= ms; waited++) {
		int listening = sockutil_listening(sock);

		if (listening != 0) {
			return listening > 0;
		}

		ks_sleep_ms(1);
	}

	return 0;
}

ks_port_t sockutil_port(ks_socket_t sock)
{
	struct sockaddr_storage ss;
	socklen_t len = sizeof(ss);

	memset(&ss, 0, sizeof(ss));

	if (getsockname(sock, (struct sockaddr *)&ss, &len) != 0) {
		return 0;
	}

	if (ss.ss_family == AF_INET6) {
		return ntohs(((struct sockaddr_in6 *)&ss)->sin6_port);
	}

	return ntohs(((struct sockaddr_in *)&ss)->sin_port);
}

int sockutil_listen_stop(volatile int *stop, ks_sockaddr_t *addr)
{
	ks_socket_t sock;

	*stop = 1;

	if ((sock = ks_socket_connect(SOCK_STREAM, IPPROTO_TCP, addr)) == KS_SOCK_INVALID) {
		return 0;
	}

	/* Connected means queued for accept; the listener sees it whether or not this end is still open. */
	ks_socket_close(&sock);

	return 1;
}

int sockutil_listen_stopping(volatile int *stop, ks_socket_t server_sock, ks_socket_t client_sock)
{
	if (!*stop) {
		return 0;
	}

	/* accept on a connected socket fails (EINVAL), so ks_listen_sock returns and closes server_sock. */
	dup2(client_sock, server_sock);
	ks_socket_close(&client_sock);

	return 1;
}

int sockutil_cert_make(const char *name, char *path, ks_size_t len)
{
	char dir[] = "testcert.XXXXXX";

	*path = '\0';

	if (!mkdtemp(dir)) {
		return 0;
	}

	ks_snprintf(path, len, "%s%s%s", dir, KS_PATH_SEPARATOR, name);
	ks_gen_cert(dir, name);

	if (access(path, R_OK) != 0) {
		sockutil_cert_remove(path);
		*path = '\0';

		return 0;
	}

	return 1;
}

void sockutil_cert_remove(const char *path)
{
	char dir[512];
	char *sep;

	if (ks_zstr(path)) {
		return;
	}

	unlink(path);
	ks_snprintf(dir, sizeof(dir), "%s", path);

	if ((sep = strrchr(dir, *KS_PATH_SEPARATOR))) {
		*sep = '\0';
		rmdir(dir);
	}
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
