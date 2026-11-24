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
#define MAX 200

#ifndef MIN
#define MIN(a, b) ((a)<(b)?(a):(b))
#endif

static void *test1_thread(ks_thread_t *thread, void *data)
{
	ks_q_t *q = (ks_q_t *) data;
	void *pop;

	while(ks_q_pop(q, &pop) == KS_STATUS_SUCCESS) {
		//int *i = (int *)pop;
		//printf("POP %d\n", *i);
		ks_pool_free(&pop);
	}

	return NULL;
}

static void do_flush(ks_q_t *q, void *ptr, void *flush_data)
{
	//ks_pool_t *pool = (ks_pool_t *)flush_data;
	ks_pool_free(&ptr);

}

int qtest1(int loops)
{
	ks_thread_t *thread;
	ks_q_t *q;
	ks_pool_t *pool;
	int i;
	int r = 1;
	void *pop;

	ks_pool_open(&pool);
	ks_q_create(&q, pool, loops);

	ks_thread_create(&thread, test1_thread, q, pool);

	if (ks_q_pop_timeout(q, &pop, 500) != KS_STATUS_TIMEOUT) {
		r = 0;
		goto end;
	}

	for (i = 0; i < 10000; i++) {
		int *val = (int *)ks_pool_alloc(pool, sizeof(int));
		*val = i;
		ks_q_push(q, val);
	}

	ks_q_wait(q);

	ks_q_term(q);
	ks_thread_join(thread);

	ks_q_destroy(&q);

	ks_q_create(&q, pool, loops);
	ks_q_set_flush_fn(q, do_flush, pool);

	for (i = 0; i < loops; i++) {
		int *val = (int *)ks_pool_alloc(pool, sizeof(int));
		*val = i;
		ks_q_push(q, val);
	}

 end:

	ks_q_destroy(&q);

	ks_pool_close(&pool);

	return r;

}

struct test2_data {
	ks_q_t *q;
	int try;
	int ready;
	int running;
};

static void *test2_thread(ks_thread_t *thread, void *data)
{
	struct test2_data *t2 = (struct test2_data *) data;
	void *pop;
	ks_status_t status;
	int popped = 0;

	while (t2->running && (t2->try && !t2->ready)) {
		ks_sleep(10000);
		continue;
	}

	while (t2->running || ks_q_size(t2->q)) {
		if (t2->try) {
			status = ks_q_trypop(t2->q, &pop);
		} else {
			status = ks_q_pop(t2->q, &pop);
		}

		if (status == KS_STATUS_SUCCESS) {
			//int *i = (int *)pop;
			//printf("%p POP %d\n", (void *)pthread_self(), *i);
			popped++;
			ks_pool_free(&pop);
		} else if (status == KS_STATUS_INACTIVE) {
			break;
		} else if (t2->try && ks_q_size(t2->q)) {
			int s = rand() % 100;
			ks_sleep(s * 1000);
		}
	}

	return (void *) (intptr_t)popped;
}

ks_size_t qtest2(int ttl, int try, int loops)
{
	ks_thread_t *threads[MAX];
	ks_q_t *q;
	ks_pool_t *pool;
	int i;
	struct test2_data t2 = { 0 };
	ks_size_t r;
	int dropped = 0;
	int qlen = loops / 2;
	int total_popped = 0;

	ks_pool_open(&pool);
	ks_q_create(&q, pool, qlen);

	t2.q = q;
	t2.try = try;
	t2.running = 1;

	for (i = 0; i < ttl; i++) {
		ks_thread_create(&threads[i], test2_thread, &t2, pool);
	}

	//ks_sleep(loops00);

	for (i = 0; i < loops; i++) {
		int *val = (int *)ks_pool_alloc(pool, sizeof(int));
		*val = i;
		if (try > 1) {
			if (ks_q_trypush(q, val) != KS_STATUS_SUCCESS) {
				dropped++;
			}
		} else {
			ks_q_push(q, val);
		}
		if (i > qlen / 2) {
			t2.ready = 1;
		}
	}

	t2.running = 0;

	if (!try) {
		ks_q_wait(q);
		ks_q_term(q);
	}

	for (i = 0; i < ttl; i++) {
		int popped;
		ks_thread_join(threads[i]);
		popped = (int)(intptr_t)ks_thread_get_return_data(threads[i]);
		if (popped) {
			printf("%d/%d POPPED %d\n", i, ttl, popped);
		}
		total_popped += popped;
	}

	r = ks_q_size(q);
	ks_assert(r == 0);

	ks_q_destroy(&q);



	printf("TOTAL POPPED: %d DROPPED %d SUM: %d\n", total_popped, dropped, total_popped + dropped);fflush(stdout);

	if (try < 2) {
		ks_assert(total_popped == loops);
	} else {
		ks_assert(total_popped + dropped == loops);
	}

	ks_pool_close(&pool);

	return r;

}

ks_status_t qtest3()
{
	ks_q_t *q = NULL;
	ks_pool_t *pool = NULL;
	ks_status_t status = KS_STATUS_SUCCESS;

	ks_pool_open(&pool);
	ks_q_create(&q, pool, 0);

	int *val = (int*)ks_pool_alloc(pool, sizeof(int));
	int *tmp = NULL;

	if (ks_q_trypeek(q, (void **)&tmp) != KS_STATUS_BREAK) return KS_STATUS_FAIL;
	if ((status = ks_q_trypush(q, val)) != KS_STATUS_SUCCESS) return status;
	if (ks_q_size(q) != 1) return KS_STATUS_FAIL;
	if (ks_q_trypeek(q, (void **)&tmp) != KS_STATUS_SUCCESS) return KS_STATUS_FAIL;
	if (tmp != val) return KS_STATUS_FAIL;
	if (ks_q_size(q) != 1) return KS_STATUS_FAIL;

	ks_q_destroy(&q);
	ks_pool_close(&pool);

	return status;
}

static void *test4_thread(ks_thread_t *thread, void *data)
{
	ks_q_t *q = (ks_q_t *) data;

	/* The queue is full, so this blocks until qtest4() pops. Any pointer will do as the item. */
	while (ks_q_push(q, data) == KS_STATUS_BREAK) {
		continue;
	}

	return NULL;
}

ks_status_t qtest4()
{
	ks_thread_t *thread = NULL;
	ks_q_t *q = NULL;
	ks_pool_t *pool = NULL;
	ks_status_t status = KS_STATUS_FAIL;
	void *pop = NULL;
	int blocked = 0;
	int i;

	ks_pool_open(&pool);
	ks_q_create(&q, pool, 1);

	if (ks_q_pushers(q) != 0) goto end;
	if (ks_q_push(q, q) != KS_STATUS_SUCCESS) goto end;
	if (ks_q_pushers(q) != 0) goto end;

	if (ks_thread_create(&thread, test4_thread, q, pool) != KS_STATUS_SUCCESS) goto end;

	/* Up to 5 seconds for the thread to block in ks_q_push(). */
	for (i = 0; i < 500 && !blocked; i++) {
		ks_sleep(10000);
		blocked = ks_q_pushers(q) == 1;
	}

	if (!blocked) goto end;
	if (ks_q_pop(q, &pop) != KS_STATUS_SUCCESS) goto end;

	ks_thread_join(thread);
	thread = NULL;

	if (ks_q_pushers(q) != 0) goto end;
	if (ks_q_size(q) != 1) goto end;

	status = KS_STATUS_SUCCESS;

 end:

	ks_q_term(q);

	if (thread) {
		ks_thread_join(thread);
	}

	ks_q_destroy(&q);
	ks_pool_close(&pool);

	return status;
}

static void *test5_thread(ks_thread_t *thread, void *data)
{
	ks_q_t *q = (ks_q_t *) data;

	return (void *)(intptr_t)ks_q_push(q, data);
}

/* A blocked ks_q_push() waits on when another thread takes the slot it was woken for. */
ks_status_t qtest5()
{
	ks_thread_t *thread = NULL;
	ks_q_t *q = NULL;
	ks_pool_t *pool = NULL;
	ks_status_t status = KS_STATUS_FAIL;
	ks_status_t pushed = KS_STATUS_FAIL;
	void *pop = NULL;
	int blocked = 0;
	int taken = 0;
	int i;

	ks_pool_open(&pool);
	ks_q_create(&q, pool, 1);

	if (ks_q_push(q, q) != KS_STATUS_SUCCESS) goto end;
	if (ks_thread_create(&thread, test5_thread, q, pool) != KS_STATUS_SUCCESS) goto end;

	/* Up to 5 seconds for the thread to block in ks_q_push(). */
	for (i = 0; i < 500 && !blocked; i++) {
		ks_sleep(10000);
		blocked = ks_q_pushers(q) == 1;
	}

	if (!blocked) goto end;

	/*
	 * Each round frees the only slot and takes it back. The trypush fails only if the
	 * thread got the slot first.
	 */
	for (i = 0; i < 20 && !taken; i++) {
		if (ks_q_pop(q, &pop) != KS_STATUS_SUCCESS) goto end;

		taken = ks_q_trypush(q, q) != KS_STATUS_SUCCESS;
		ks_sleep(1000);
	}

	if (!taken && ks_q_pop(q, &pop) != KS_STATUS_SUCCESS) goto end;

	ks_thread_join(thread);
	pushed = (ks_status_t)(intptr_t)ks_thread_get_return_data(thread);
	thread = NULL;

	if (pushed != KS_STATUS_SUCCESS) goto end;
	if (ks_q_size(q) != 1) goto end;

	status = KS_STATUS_SUCCESS;

 end:

	ks_q_term(q);

	if (thread) {
		ks_thread_join(thread);
	}

	ks_q_destroy(&q);
	ks_pool_close(&pool);

	return status;
}

static void *test6_thread(ks_thread_t *thread, void *data)
{
	ks_q_t *q = (ks_q_t *) data;
	ks_status_t status;
	void *pop = NULL;

	/* The thread pops what it gets and ends on the first status that is not success. */
	do {
		status = ks_q_pop_timeout(q, &pop, 200);
	} while (status == KS_STATUS_SUCCESS);

	return (void *)(intptr_t)status;
}

/* A ks_q_pop_timeout() woken for an item that another thread took waits on and ends with a timeout. */
ks_status_t qtest6()
{
	ks_thread_t *thread = NULL;
	ks_q_t *q = NULL;
	ks_pool_t *pool = NULL;
	ks_status_t status = KS_STATUS_FAIL;
	ks_status_t popped = KS_STATUS_FAIL;
	void *pop = NULL;
	int i;

	ks_pool_open(&pool);
	ks_q_create(&q, pool, 0);

	if (ks_thread_create(&thread, test6_thread, q, pool) != KS_STATUS_SUCCESS) goto end;

	/* Each round pushes an item and takes it back, unless the thread popped it first. */
	for (i = 0; i < 50; i++) {
		if (ks_q_push(q, q) != KS_STATUS_SUCCESS) goto end;

		ks_q_trypop(q, &pop);
		ks_sleep(1000);
	}

	ks_thread_join(thread);
	popped = (ks_status_t)(intptr_t)ks_thread_get_return_data(thread);
	thread = NULL;

	if (popped != KS_STATUS_TIMEOUT) goto end;

	status = KS_STATUS_SUCCESS;

 end:

	ks_q_term(q);

	if (thread) {
		ks_thread_join(thread);
	}

	ks_q_destroy(&q);
	ks_pool_close(&pool);

	return status;
}

struct test7_data {
	ks_q_t *q;
	ks_mutex_t *mutex;
	ks_status_t status;
	int done;
};

static void *test7_thread(ks_thread_t *thread, void *data)
{
	struct test7_data *t7 = (struct test7_data *) data;
	void *pop = NULL;

	t7->status = ks_q_pop(t7->q, &pop);

	ks_mutex_lock(t7->mutex);
	t7->done = 1;
	ks_mutex_unlock(t7->mutex);

	return NULL;
}

/* ks_q_wake() ends a blocked ks_q_pop() and a blocked ks_q_push() with KS_STATUS_BREAK. */
ks_status_t qtest7()
{
	ks_thread_t *thread = NULL;
	ks_q_t *q = NULL;
	ks_pool_t *pool = NULL;
	ks_status_t status = KS_STATUS_FAIL;
	ks_status_t pushed = KS_STATUS_FAIL;
	struct test7_data t7 = { 0 };
	void *pop = NULL;
	int blocked = 0;
	int done = 0;
	int i;

	ks_pool_open(&pool);
	ks_q_create(&q, pool, 1);

	t7.q = q;

	if (ks_mutex_create(&t7.mutex, KS_MUTEX_FLAG_DEFAULT, pool) != KS_STATUS_SUCCESS) goto end;
	if (ks_thread_create(&thread, test7_thread, &t7, pool) != KS_STATUS_SUCCESS) goto end;

	/* A wake that comes before the thread waits does nothing, so it is repeated for up to 5 seconds. */
	for (i = 0; i < 500 && !done; i++) {
		ks_sleep(10000);
		ks_q_wake(q);

		ks_mutex_lock(t7.mutex);
		done = t7.done;
		ks_mutex_unlock(t7.mutex);
	}

	if (!done) goto end;

	ks_thread_join(thread);
	thread = NULL;

	if (t7.status != KS_STATUS_BREAK) goto end;

	if (ks_q_push(q, q) != KS_STATUS_SUCCESS) goto end;
	if (ks_thread_create(&thread, test5_thread, q, pool) != KS_STATUS_SUCCESS) goto end;

	/* Up to 5 seconds for the thread to block in ks_q_push(). */
	for (i = 0; i < 500 && !blocked; i++) {
		ks_sleep(10000);
		blocked = ks_q_pushers(q) == 1;
	}

	if (!blocked) goto end;

	ks_q_wake(q);

	ks_thread_join(thread);
	pushed = (ks_status_t)(intptr_t)ks_thread_get_return_data(thread);
	thread = NULL;

	if (pushed != KS_STATUS_BREAK) goto end;
	if (ks_q_pushers(q) != 0) goto end;
	if (ks_q_size(q) != 1) goto end;
	if (ks_q_pop(q, &pop) != KS_STATUS_SUCCESS || pop != q) goto end;

	status = KS_STATUS_SUCCESS;

 end:

	ks_q_term(q);

	if (thread) {
		ks_thread_join(thread);
	}

	ks_q_destroy(&q);
	ks_pool_close(&pool);

	return status;
}

/*
 * ks_q_pop_timeout() counts milliseconds and ks_q_timedpop_usec() microseconds; the
 * latter does not wait for 0 or less.
 */
ks_status_t qtest8()
{
	ks_q_t *q = NULL;
	ks_pool_t *pool = NULL;
	ks_status_t status = KS_STATUS_FAIL;
	void *pop = NULL;
	ks_time_t start;

	ks_pool_open(&pool);
	ks_q_create(&q, pool, 0);

	start = ks_time_now();
	if (ks_q_pop_timeout(q, &pop, 100) != KS_STATUS_TIMEOUT) goto end;
	if (ks_time_now() - start < 100000) goto end;

	start = ks_time_now();
	if (ks_q_timedpop_usec(q, &pop, 100000) != KS_STATUS_TIMEOUT) goto end;
	if (ks_time_now() - start < 100000) goto end;

	if (ks_q_timedpop_usec(q, &pop, 500) != KS_STATUS_TIMEOUT) goto end;
	if (ks_q_timedpop_usec(q, &pop, 0) != KS_STATUS_TIMEOUT) goto end;
	if (ks_q_timedpop_usec(q, &pop, -1) != KS_STATUS_TIMEOUT) goto end;

	if (ks_q_push(q, q) != KS_STATUS_SUCCESS) goto end;
	if (ks_q_timedpop_usec(q, &pop, 0) != KS_STATUS_SUCCESS || pop != q) goto end;

	if (ks_q_push(q, pool) != KS_STATUS_SUCCESS) goto end;
	if (ks_q_pop_timeout(q, &pop, 100) != KS_STATUS_SUCCESS || pop != pool) goto end;

	status = KS_STATUS_SUCCESS;

 end:

	ks_q_destroy(&q);
	ks_pool_close(&pool);

	return status;
}

int main(int argc, char **argv)
{
	int ttl;
	int size = 100000;
	int runs = 1;
	int i;

	ks_init();

	plan(4 * runs + 6);

	ttl = ks_env_cpu_count() * 5;
	//ttl = 5;
	ttl = MIN(MAX, ttl);


	ok(qtest3() == KS_STATUS_SUCCESS);
	ok(qtest4() == KS_STATUS_SUCCESS);
	ok(qtest5() == KS_STATUS_SUCCESS);
	ok(qtest6() == KS_STATUS_SUCCESS);
	ok(qtest7() == KS_STATUS_SUCCESS);
	ok(qtest8() == KS_STATUS_SUCCESS);
	for(i = 0; i < runs; i++) {
		ok(qtest1(size));
		ok(qtest2(ttl, 0, size) == 0);
		ok(qtest2(ttl, 1, size) == 0);
		ok(qtest2(ttl, 2, size) == 0);
	}

	printf("TTL %d RUNS %d\n", ttl, runs);

	ks_shutdown();

	done_testing();
}
