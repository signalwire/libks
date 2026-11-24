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

/* ks_wait_sock must not report a signal that interrupts its poll() as a socket error.
 * A handler installed without SA_RESTART makes poll() return -1/EINTR when the signal
 * lands on the waiting thread; the wait has to go on for the rest of its timeout. */

#include "libks/ks.h"
#include "tap.h"

#ifndef _WIN32
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct waiter_s {
	ks_socket_t sock;
	uint32_t ms;
	int result;
	ks_time_t elapsed_ms;
	volatile int waiting;
} waiter_t;

/**
 * SIGUSR1 handler: does nothing, so the signal only interrupts the wait.
 * @param sig The signal.
 */
static void on_usr1(int sig)
{
	(void)sig;
}

/**
 * Thread body: one read wait of w->ms on w->sock, keeping its result and how long it took.
 * @param arg The waiter_t.
 * @return NULL.
 */
static void *wait_thread(void *arg)
{
	waiter_t *w = arg;
	ks_time_t start = ks_time_now();

	w->waiting = 1;
	w->result = ks_wait_sock(w->sock, w->ms, KS_POLL_READ);
	w->elapsed_ms = ks_time_ms(ks_time_now() - start);

	return NULL;
}

/**
 * Wait on fds[0] in a thread, interrupt it with SIGUSR1 after 200 ms, then make it readable.
 * @param fds The socket pair: the thread waits on fds[0], the data is written to fds[1].
 * @param ms The wait's timeout.
 * @param write_at_ms When to write one byte, in ms from the start (0: never).
 * @param w The waiter_t that receives the result.
 */
static void run(int fds[2], uint32_t ms, int write_at_ms, waiter_t *w)
{
	pthread_t t;

	memset(w, 0, sizeof(*w));
	w->sock = fds[0];
	w->ms = ms;
	pthread_create(&t, NULL, wait_thread, w);

	while (!w->waiting) {
		ks_sleep_ms(1);
	}

	ks_sleep_ms(200);
	pthread_kill(t, SIGUSR1);

	if (write_at_ms) {
		ks_sleep_ms(write_at_ms - 200);

		if (write(fds[1], "x", 1) != 1) {
			ok(0, "write");
		}
	}

	pthread_join(t, NULL);
}

int main(int argc, char **argv)
{
	struct sigaction sa;
	int fds[2];
	waiter_t w;
	char c;

	ks_init();
	plan(5);

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_usr1;	/* no SA_RESTART: poll() returns EINTR */
	sigemptyset(&sa.sa_mask);
	sigaction(SIGUSR1, &sa, NULL);

	ok(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0, "socketpair");

	/* Interrupted, then readable: the wait reports READ, not an error. */
	run(fds, 2000, 400, &w);
	ok(w.result > 0 && (w.result & KS_POLL_READ) && !(w.result & KS_POLL_ERROR),
		"interrupted wait then data: result %d is READ without ERROR", w.result);

	if (read(fds[0], &c, 1) != 1) {
		diag("read");
	}

	/* Interrupted, nothing arrives: a timeout (0) after the whole wait, not -1 at the signal. */
	run(fds, 600, 0, &w);
	ok(w.result == 0, "interrupted wait without data: result %d is a timeout (0)", w.result);
	ok(w.elapsed_ms >= 550, "interrupted wait lasted its timeout: %d ms of 600", (int)w.elapsed_ms);

	/* Unchanged: plain readiness with no signal. */
	if (write(fds[1], "y", 1) != 1) {
		diag("write");
	}

	ok(ks_wait_sock(fds[0], 100, KS_POLL_READ) & KS_POLL_READ, "readable socket reports READ");

	close(fds[0]);
	close(fds[1]);
	ks_shutdown();
	done_testing();
}
#else
int main(int argc, char **argv)
{
	plan(1);
	ok(1, "no signals on Windows");
	done_testing();
}
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
