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
#define MAX_STUFF 200

static ks_thread_t *threads[MAX_STUFF];
static ks_thread_t *thread_p;

static ks_pool_t *pool;
static ks_mutex_t *mutex;
static ks_mutex_t *mutex_non_recursive;
static ks_rwl_t *rwlock;
static ks_cond_t *cond;
static int counter1 = 0;
static int counter2 = 0;
static int counter3 = 0;
static int counter4 = 0;
static int counter5 = 0;
static int counter6 = 0;
static int threadscount = 0;
static int cpu_count = 0;

#define LOOP_COUNT 10000

static void *thread_priority(ks_thread_t *thread, void *data)
{
	while (!ks_thread_stop_requested(thread)) {
		ks_sleep(100000);
	}

	return NULL;
}

static void *thread_test_cond_producer_func(ks_thread_t *thread, void *data)
{
	for (;;) {
		ks_cond_lock(cond);
		if (counter5 >= LOOP_COUNT) {
			ks_cond_unlock(cond);
			break;
		}
		counter5++;
		if (counter6 == 0) {
			ks_cond_signal(cond);
		}
		counter6++;
		ks_cond_unlock(cond);
		*((int *) data) += 1;
	}

    return NULL;
}

static void *thread_test_cond_consumer_func(ks_thread_t *thread, void *data)
{
	int i;

	for (i = 0; i < LOOP_COUNT; i++) {
		ks_cond_lock(cond);
		while (counter6 == 0) {
			ks_cond_wait(cond);
		}
		counter6--;
		ks_cond_unlock(cond);
	}
    return NULL;
}

static void check_cond(void)
{
	int count[MAX_STUFF] = { 0 };
	ks_thread_t * threads[MAX_STUFF] = { 0 };
	int ttl = 0;

	ok( (ks_pool_open(&pool) == KS_STATUS_SUCCESS) );

	ok( (ks_cond_create(&cond, pool) == KS_STATUS_SUCCESS) );
	ks_log(KS_LOG_DEBUG, "Allocated condition");

	int i;
	for(i = 0; i < cpu_count; i++) {
		ok( (ks_thread_create(&threads[i], thread_test_cond_producer_func, &count[i], pool) == KS_STATUS_SUCCESS) );
	}
	ok( (ks_thread_create(&thread_p, thread_test_cond_consumer_func, NULL, pool) == KS_STATUS_SUCCESS) );

	for(i = 0; i < cpu_count; i++) {
		ks_thread_join(threads[i]);
		ttl += count[i];
	}

	ok( (ttl == LOOP_COUNT) );

	ks_thread_join(thread_p);

	ok( (ks_pool_close(&pool) == KS_STATUS_SUCCESS) );
}

static void *thread_test_rwlock_func(ks_thread_t *thread, void *data)
{
    int loop = 1;

    while (1) {
        ks_rwl_read_lock(rwlock);
        if (counter4 == LOOP_COUNT) {
            loop = 0;
		}
        ks_rwl_read_unlock(rwlock);

        if (!loop) {
            break;
		}

        ks_rwl_write_lock(rwlock);
        if (counter4 != LOOP_COUNT) {
			counter4++;
        }
        ks_rwl_write_unlock(rwlock);
    }
    return NULL;
}

static void check_rwl(void)
{
	ks_status_t status;

	ok( (ks_pool_open(&pool) == KS_STATUS_SUCCESS) );
	ok( (ks_rwl_create(&rwlock, pool) == KS_STATUS_SUCCESS) );
	ks_rwl_read_lock(rwlock);
	status = ks_rwl_try_read_lock(rwlock);
	ok( status == KS_STATUS_SUCCESS );
	if ( status == KS_STATUS_SUCCESS ) {
		ks_rwl_read_unlock(rwlock);
	}
	ks_rwl_read_unlock(rwlock);

	int i;
	for(i = 0; i < cpu_count; i++) {
		ok( (ks_thread_create(&threads[i], thread_test_rwlock_func, NULL, pool) == KS_STATUS_SUCCESS) );
	}

	for(i = 0; i < cpu_count; i++) {
		ks_thread_join(threads[i]);
	}

	ok( (ks_pool_close(&pool) == KS_STATUS_SUCCESS) );
	ok( (counter4 == LOOP_COUNT) );

}

static void *thread_test_function_cleanup(ks_thread_t *thread, void *data)
{
	int d = (int)(intptr_t)data;

	if ( d == 1 ) {
		ks_mutex_lock(mutex);
		counter3++;
		ks_mutex_unlock(mutex);
	}

	return NULL;
}

static void *thread_test_function_detatched(ks_thread_t *thread, void *data)
{
	int i;
	int d = (int)(intptr_t)data;

	for (i = 0; i < LOOP_COUNT; i++) {
		ks_mutex_lock(mutex);
		if (d == 1) {
			counter2++;
		}
		ks_mutex_unlock(mutex);
	}
	ks_mutex_lock(mutex);
	threadscount++;
	ks_mutex_unlock(mutex);

	return NULL;
}

static void *thread_test_function_attached(ks_thread_t *thread, void *data)
{
	int i;
	int d = (int)(intptr_t)data;
	void *mem, *last_mem = NULL;

	for (i = 0; i < LOOP_COUNT; i++) {
		if (last_mem) {
			free(last_mem);
		}
		mem = calloc(1, 1024);
		last_mem = mem;
	}

	for (i = 0; i < LOOP_COUNT; i++) {
		ks_mutex_lock(mutex);
		if (d == 1) {
			counter1++;
		}
		ks_mutex_unlock(mutex);
	}

	return NULL;
}

static void create_threads_cleanup(void)
{
	void *d = (void *)(intptr_t)1;
	int i;
	for(i = 0; i < cpu_count; i++) {
		ok( (ks_thread_create(&threads[i], thread_test_function_cleanup, d, pool) == KS_STATUS_SUCCESS) );
	}
	for(i = 0; i < cpu_count; i++) {
		ks_thread_destroy(&threads[i]);
	}
}

static void create_threads_attached(void)
{
	void *d = (void *)(intptr_t)1;

	int i;
	for(i = 0; i < cpu_count; i++) {
		ok( (ks_thread_create(&threads[i], thread_test_function_attached, d, pool) == KS_STATUS_SUCCESS) );
	}
}

static void create_threads_detatched(void)
{
	ks_status_t status;
	void *d = (void *)(intptr_t)1;

	int i;
	for(i = 0; i < cpu_count; i++) {
		status = ks_thread_create_ex(&threads[i], thread_test_function_detatched, d, KS_THREAD_FLAG_DETACHED, KS_THREAD_DEFAULT_STACK, KS_PRI_NORMAL, pool);
		ok( status == KS_STATUS_SUCCESS );
	}
}

static void check_thread_priority(void)
{
	ks_status_t status;
	void *d = (void *)(intptr_t)1;

	status = ks_thread_create_ex(&thread_p, thread_priority, d, 0, KS_THREAD_DEFAULT_STACK, KS_PRI_IMPORTANT, pool);
	ok( status == KS_STATUS_SUCCESS );
	todo("Add check to see if has permission to set thread priority\n");
	ok( ks_thread_priority(thread_p) == KS_PRI_IMPORTANT );
	ks_thread_destroy(&thread_p);
	end_todo;
}

static void join_threads(void)
{
	int i;
	for(i = 0; i < cpu_count; i++) {
		ok( (KS_STATUS_SUCCESS == ks_thread_join(threads[i])) );
	}
}

static void check_attached(void)
{
	ok( counter1 == (LOOP_COUNT * cpu_count) );
}

static void check_detached(void)
{
	ok( counter2 == (LOOP_COUNT * cpu_count) );
}

static void create_pool(void)
{
	ok( (ks_pool_open(&pool) == KS_STATUS_SUCCESS) );
}

static void check_cleanup(void)
{
	ok( (counter3 == cpu_count) );
}

static void check_pool_close(void)
{
	ok( (ks_pool_close(&pool) == KS_STATUS_SUCCESS) );
}

static void create_mutex(void)
{
	ok( (ks_mutex_create(&mutex, KS_MUTEX_FLAG_DEFAULT, pool) == KS_STATUS_SUCCESS) );
}

static void create_mutex_non_recursive(void)
{
	ok( (ks_mutex_create(&mutex_non_recursive, KS_MUTEX_FLAG_NON_RECURSIVE, pool) == KS_STATUS_SUCCESS) );
}

static void test_recursive_mutex(void)
{
	ks_status_t status;

	ks_mutex_lock(mutex);
	status = ks_mutex_trylock(mutex);
	if (status == KS_STATUS_SUCCESS) {
		ks_mutex_unlock(mutex);
	}
	ok(status == KS_STATUS_SUCCESS);
	ks_mutex_unlock(mutex);
}

static void test_non_recursive_mutex(void)
{
	ks_status_t status;
	ks_mutex_lock(mutex_non_recursive);
	status = ks_mutex_trylock(mutex_non_recursive);
	if (status == KS_STATUS_SUCCESS) {
		ks_mutex_unlock(mutex_non_recursive);
	}
	ok(status != KS_STATUS_SUCCESS);
	ks_mutex_unlock(mutex_non_recursive);
}


int main(int argc, char **argv)
{
	ks_init();
	cpu_count = ks_env_cpu_count() * 4;

	plan(21 + cpu_count * 6);

	diag("Starting testing for %d tests\n", 21 + cpu_count * 6);

	create_pool();
	create_mutex();
	create_mutex_non_recursive();
	test_recursive_mutex();
	test_non_recursive_mutex();
	check_thread_priority();
	create_threads_attached();
	join_threads();
	check_attached();
	create_threads_detatched();
	while (threadscount != cpu_count) ks_sleep(1000000);
	check_detached();
	create_threads_cleanup();
	check_pool_close();
	check_cleanup();
	check_rwl();
	check_cond();

	ks_shutdown();
	done_testing();
}
