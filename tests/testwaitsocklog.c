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

/* ks_wait_sock logs each way a wait can fail on its own line, naming the fd and the requested
 * flags, and says nothing on a plain ready or timeout. Its return values are unchanged. */

#include "libks/ks.h"
#include "tap.h"

#ifndef _WIN32
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct waiter_s {
	ks_socket_t sock;
	int result;
	int err;
	volatile int waiting;
} waiter_t;

static char last_log[8][256];
static int log_count;

/**
 * Logger that keeps the last ks_wait_sock line of each level.
 * @param file The source file (unused).
 * @param func The function (unused).
 * @param line The line (unused).
 * @param level The log level.
 * @param fmt The format, followed by its arguments.
 */
static void capture_logger(const char *file, const char *func, int line, int level, const char *fmt, ...)
{
	char buf[256];
	va_list ap;

	(void)file;
	(void)func;
	(void)line;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	if (level >= 0 && level < 8 && strstr(buf, "ks_wait_sock")) {
		snprintf(last_log[level], sizeof(last_log[level]), "%s", buf);
		log_count++;
	}
}

/**
 * SIGUSR1 handler: does nothing, so the signal only interrupts the wait.
 * @param sig The signal.
 */
static void on_usr1(int sig)
{
	(void)sig;
}

/**
 * Thread body: one 2 s read wait on w->sock, keeping its result and errno.
 * @param arg The waiter_t.
 * @return NULL.
 */
static void *wait_thread(void *arg)
{
	waiter_t *w = arg;

	w->waiting = 1;
	w->result = ks_wait_sock(w->sock, 2000, KS_POLL_READ);
	w->err = errno;

	return NULL;
}

int main(int argc, char **argv)
{
	struct sigaction sa;
	waiter_t w;
	pthread_t t;
	int fds[2];
	char c;

	ks_init();
	ks_global_set_logger(capture_logger);
	plan(10);

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_usr1;	/* no SA_RESTART: poll() returns EINTR */
	sigemptyset(&sa.sa_mask);
	sigaction(SIGUSR1, &sa, NULL);

	ok(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0, "socketpair");

	/* Timeout and plain readiness: nothing logged. */
	ok(ks_wait_sock(fds[0], 50, KS_POLL_READ) == 0, "timeout returns 0");

	if (write(fds[1], "x", 1) != 1) {
		diag("write");
	}

	ok(ks_wait_sock(fds[0], 50, KS_POLL_READ) == KS_POLL_READ, "readable returns READ");

	if (read(fds[0], &c, 1) != 1) {
		diag("read");
	}

	ok(log_count == 0, "timeout and ready log nothing (%d lines)", log_count);

	/* poll() failing (here: interrupted by a signal) returns -1 as before, logs errno by name,
	 * and leaves errno for the caller. */
	memset(&w, 0, sizeof(w));
	w.sock = fds[0];
	pthread_create(&t, NULL, wait_thread, &w);

	while (!w.waiting) {
		ks_sleep_ms(1);
	}

	ks_sleep_ms(200);
	pthread_kill(t, SIGUSR1);
	pthread_join(t, NULL);
	ok(w.result == -1, "interrupted wait still returns -1 (%d)", w.result);
	ok(w.err == EINTR, "errno is still EINTR after the log (%d)", w.err);
	ok(strstr(last_log[KS_LOG_LEVEL_ERROR], "ks_wait_sock(fd ") && strstr(last_log[KS_LOG_LEVEL_ERROR], "READ): poll() failed: EINTR (4)"),
		"poll() failure logged at ERROR: %s", last_log[KS_LOG_LEVEL_ERROR]);

	/* Peer closed: HUP, logged at INFO. */
	close(fds[1]);
	ok(ks_wait_sock(fds[0], 100, KS_POLL_READ) & KS_POLL_HUP, "closed peer reports HUP");
	ok(strstr(last_log[KS_LOG_LEVEL_INFO], "peer closed (POLLHUP)") != NULL, "POLLHUP logged: %s", last_log[KS_LOG_LEVEL_INFO]);

	/* fd not open: INVALID, logged at ERROR with the requested flags. */
	close(fds[0]);
	ks_wait_sock(fds[0], 100, KS_POLL_READ | KS_POLL_WRITE);
	ok(strstr(last_log[KS_LOG_LEVEL_ERROR], "is not open (POLLNVAL)") && strstr(last_log[KS_LOG_LEVEL_ERROR], "READ|WRITE"),
		"POLLNVAL logged with flags: %s", last_log[KS_LOG_LEVEL_ERROR]);

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
