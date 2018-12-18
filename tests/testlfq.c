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

#include "libks/ks.h"
#include "tap.h"

#define LOOPS 10000

static void *lfqtest1_pushthread(ks_thread_t *thread, void *data)
{
	ks_lfq_t *lfq = (ks_lfq_t *)data;
	ks_pool_t *pool = ks_pool_get(lfq);

	for (int i = 0; i < LOOPS; i++) {
		int *val = (int *)ks_pool_alloc(pool, sizeof(int));
		*val = i;
		ks_lfq_push(lfq, val);
	}

	return NULL;
}

static void *lfqtest1_popthread(ks_thread_t *thread, void *data)
{
	ks_lfq_t *lfq = (ks_lfq_t *)data;
	void *pop = NULL;

	while (ks_lfq_pop(lfq, &pop)) {
		ks_pool_free(&pop);
	}

	return NULL;
}

void lfqtest1()
{
	ks_pool_t *pool = NULL;
	ks_lfq_t *lfq = NULL;
	ks_thread_t *thread1 = NULL;
	ks_thread_t *thread2 = NULL;


	ks_pool_open(&pool);
	ks_lfq_create(&lfq, pool, LOOPS * 2);

	ks_thread_create(&thread1, lfqtest1_pushthread, lfq, pool);
	ks_thread_create(&thread2, lfqtest1_pushthread, lfq, pool);

	ks_thread_join(thread1);
	ks_thread_join(thread2);

	ok(ks_lfq_size(lfq) == (LOOPS * 2));
	ok(ks_lfq_push(lfq, NULL) == KS_FALSE);

	ks_thread_create(&thread1, lfqtest1_popthread, lfq, pool);
	ks_thread_create(&thread2, lfqtest1_popthread, lfq, pool);

	ks_thread_join(thread1);
	ks_thread_join(thread2);

	ks_lfq_destroy(&lfq);
}

int main(int argc, char **argv)
{
	//int ttl = ks_env_cpu_count() * 5;
	int runs = 1;

	ks_init();

	plan(runs * 2);

	//ttl = ks_env_cpu_count() * 5;

	for(int i = 0; i < runs; i++) {
		lfqtest1();
	}

	ks_shutdown();

	done_testing();

	return 0;
}
