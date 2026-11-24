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
