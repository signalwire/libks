/*
 * Copyright (c) 2018-2023 SignalWire, Inc
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

#include "libks/ks.h"

#include "tap.h"

#ifdef __GLIBC__
#if __GLIBC_PREREQ(2, 33)
#include <malloc.h>
#define HAVE_HEAP_IN_USE 1
#endif
#endif

#define JSON_MESSAGES 200

/**
 * Writes the same message count times through the logger.
 * @param count How many messages to write.
 */
static void log_messages(int count)
{
	int i;

	for (i = 0; i < count; i++) {
		ks_log(KS_LOG_INFO, "message");
	}
}

int main(int argc, char **argv)
{
	char a[8196 * 5];
#ifdef HAVE_HEAP_IN_USE
	long before, grown;
#endif

	memset(a, 'a', sizeof(a));
	a[sizeof(a) - 1] = '\0';
	ks_init();
	ks_global_set_log_level(KS_LOG_LEVEL_DEBUG);
	ks_log(a, a, 9999999, KS_LOG_LEVEL_DEBUG, a);
	ks_log(a, a, 9999999, KS_LOG_LEVEL_INFO, a);
	ks_log(a, a, 9999999, KS_LOG_LEVEL_WARNING, a);
	ks_log(a, a, 9999999, KS_LOG_LEVEL_ERROR, a);
	ks_log(a, a, 9999999, KS_LOG_LEVEL_CRIT, a);
	ks_log(a, a, 9999999, KS_LOG_LEVEL_ALERT, a);
	ks_log(a, a, 9999999, KS_LOG_LEVEL_EMERG, a);

	/* The JSON logger with an enclosing name frees all it allocates for a message. The first
	 * messages are not measured: they allocate what stays for the whole run. */
	ks_log_jsonify();
	ks_log_json_set_enclosing_name("log");
	log_messages(10);

#ifdef HAVE_HEAP_IN_USE
	before = (long)mallinfo2().uordblks;
#endif

	log_messages(JSON_MESSAGES);

#ifdef HAVE_HEAP_IN_USE
	/* One JSON node is more than 32 bytes, so a node left by every message goes over the limit. */
	grown = (long)mallinfo2().uordblks - before;
	ok(grown < JSON_MESSAGES * 32, "%d JSON messages with an enclosing name grew the heap by %ld bytes",
		JSON_MESSAGES, grown);
#else
	tap_skip(1, "the heap in use is read with mallinfo2(), which needs glibc 2.33");
#endif

	ks_log_json_set_enclosing_name(NULL);

	ks_shutdown();
	done_testing();
}
