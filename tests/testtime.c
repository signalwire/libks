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

#ifndef _WIN32
#include <time.h>

/**
 * Monotonic time in nanoseconds, for checking that a sleep is never shorter than asked:
 * wall clock adjustments and microsecond rounding cannot move it.
 * @return Nanoseconds since an arbitrary start.
 */
static int64_t mono_nsec(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}
#endif

int main(int argc, char **argv)
{
	int64_t now, then;
	int diff;
	int i;
#ifndef _WIN32
	int64_t start, elapsed, shortest = -1;
	int short_count = 0;
#endif

	ks_init();

#ifndef _WIN32
	plan(4);
#else
	plan(2);
#endif

	then = ks_time_now();

	ks_sleep(2000000);

	now = ks_time_now();

	diff = (int)((now - then) / 1000);
	printf("DIFF %ums\n", diff);

#ifdef KS_PLAT_MAC
	/* the clock on osx seems to be particularly bad at being accurate, we need a bit more room for error*/
	ok(diff > 1990 && diff < 2200);
#else
	ok(diff > 1990 && diff < 2010);
#endif

#ifndef _WIN32
	/* A sleep with a sub-second part lasts at least what was asked for. */
	start = mono_nsec();
	ks_sleep(1500000);
	elapsed = mono_nsec() - start;
	ok(elapsed >= 1500000000, "ks_sleep(1500000) lasted %lld us, at least 1500000", (long long)(elapsed / 1000));
#endif

	then = ks_time_now();

	for (i = 0; i < 100; i++) {
#ifndef _WIN32
		start = mono_nsec();
		ks_sleep(20000);
		elapsed = mono_nsec() - start;

		if (elapsed < 20000000) {
			short_count++;
		}

		if (shortest < 0 || elapsed < shortest) {
			shortest = elapsed;
		}
#else
		ks_sleep(20000);
#endif
	}

	now = ks_time_now();

	diff = (int)((now - then) / 1000);
	printf("DIFF %ums\n", diff);

#ifdef KS_PLAT_MAC
	/* the clock on osx seems to be particularly bad at being accurate, we need a bit more room for error*/
	/* GHA macos arm runners seem too slow */
	ok( diff > 1900 && diff < 12000 );
#else
	ok( diff > 1950 && diff < 2050 );
#endif

#ifndef _WIN32
	ok(short_count == 0, "%d of 100 ks_sleep(20000) were short; the shortest lasted %lld us",
		short_count, (long long)(shortest / 1000));
#endif

	ks_shutdown();
	done_testing();
}
