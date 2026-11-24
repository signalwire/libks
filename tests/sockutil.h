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

/* Socket helpers shared by the tests that run a server and a client in one process. */

#ifndef _KS_TEST_SOCKUTIL_H_
#define _KS_TEST_SOCKUTIL_H_

#include "libks/ks.h"

/**
 * Wait for a server socket that another thread handed to ks_listen_sock to be
 * listening. ks_listen_sock binds and listens on its own and says nothing, so
 * asking the socket is the only way to know a client can connect.
 * @param sock The server socket.
 * @param ms How long to wait, in milliseconds.
 * @return 1 once the socket is listening, 0 when it is not within ms or it was closed.
 */
int sockutil_wait_listen(ks_socket_t sock, uint32_t ms);

/**
 * The local port a socket is bound to: the one the OS chose when it was bound
 * to port 0, which no other socket on the host holds.
 * @param sock A bound socket.
 * @return The port, or 0 when the socket is not bound or the lookup fails.
 */
ks_port_t sockutil_port(ks_socket_t sock);

/**
 * Stop a ks_listen_sock loop running in another thread. ks_listen_sock owns the
 * socket it is given: it closes it itself on every way out, and it has no stop
 * of its own. Closing or shutting the socket down from outside is a second close
 * of an fd number the listener still holds; whatever opened an fd in between (any
 * other thread) gets that number and loses its fd to the listener's close. So this
 * only sets *stop and connects to the listener; its callback, which must begin
 * with sockutil_listen_stopping, ends the loop from the listener's own thread.
 * @param stop The flag the listen callback checks.
 * @param addr The address the listener is listening on, its port included.
 * @return 1 the listener was reached, 0 the connect failed (nothing listening there).
 */
int sockutil_listen_stop(volatile int *stop, ks_sockaddr_t *addr);

/**
 * In a ks_listen_sock callback, first: whether the test asked the listener to stop
 * (sockutil_listen_stop), and if so end its loop. The connection takes over the
 * listening fd's number (dup2, which closes the listening socket and never leaves
 * the number free), so ks_listen_sock's next accept fails and it closes that fd
 * itself; the connection's own fd is closed here.
 * @param stop The flag sockutil_listen_stop sets.
 * @param server_sock The listening socket the callback was given.
 * @param client_sock The connection the callback was given.
 * @return 1 stopping: return from the callback at once; 0 a connection to serve.
 */
int sockutil_listen_stopping(volatile int *stop, ks_socket_t server_sock, ks_socket_t client_sock);

#endif

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
