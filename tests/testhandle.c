/*
 * Copyright (c) 2018-2019 SignalWire, Inc
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
#include <tap.h>


#define MY_DATA_TYPE 1
typedef struct my_data {
	ks_handle_base_t base;
	int foo;
} my_data_t;

static void my_data_deinit(void *context)
{
	// TODO
}

static void *destroy_thread(ks_thread_t *thread, void *data)
{
	ks_handle_t handle = (ks_handle_t)data;

	ks_handle_destroy(&handle);
	return NULL;
}

int main(void)
{
	ks_init();
	ks_handle_init();

	plan(14);

	// create the handles and set up parent-child relationship
	ks_handle_t parent_handle = { 0 };
	ks_handle_t handle = { 0 };
	my_data_t *data = NULL;
	ks_handle_alloc(MY_DATA_TYPE, sizeof(my_data_t), &data, &parent_handle, my_data_deinit);
	ks_handle_set_ready(parent_handle);

	ok(data != NULL);
	ok(parent_handle > 0);

	ks_handle_alloc(MY_DATA_TYPE, sizeof(my_data_t), &data, &handle, my_data_deinit);
	ks_handle_set_ready(handle);

	ok(data != NULL);
	ok(handle > 0);

	ok(ks_handle_set_parent(handle, parent_handle) == KS_STATUS_SUCCESS);

	// test destroy when child has a ref count > 0
	ok(ks_handle_get(MY_DATA_TYPE, handle, &data) == KS_STATUS_SUCCESS);
	ok(ks_handle_destroy(&parent_handle) == KS_STATUS_HANDLE_PENDING_CHILDREN);
	ok(ks_handle_destroy(&parent_handle) == KS_STATUS_SUCCESS);
	ok(ks_handle_put(MY_DATA_TYPE, &data) == KS_STATUS_SUCCESS);
	ok(ks_handle_destroy(&handle) == KS_STATUS_SUCCESS);

	// reset setup to continue test with parent and child
	ks_handle_alloc(MY_DATA_TYPE, sizeof(my_data_t), &data, &parent_handle, my_data_deinit);
	ks_handle_alloc(MY_DATA_TYPE, sizeof(my_data_t), &data, &handle, my_data_deinit);
	ks_handle_set_parent(handle, parent_handle);
	ks_handle_set_ready(parent_handle);
	ks_handle_set_ready(handle);

	ks_pool_t *pool = NULL;
	ks_pool_open(&pool);

	// Get reference to parent and spawn 10 threads to attempt to destroy it
	// the threads will block trying to set the handle to not ready
	ok(ks_handle_get(MY_DATA_TYPE, parent_handle, &data) == KS_STATUS_SUCCESS);
	ks_thread_t *threads[10] = { 0 };
	for (int i = 0; i < 10; i++) {
		ks_thread_create(&threads[i], destroy_thread, (void *)parent_handle, pool);
	}
	ok(ks_handle_put(MY_DATA_TYPE, &data) == KS_STATUS_SUCCESS);

	for (int i = 0; i < 10; i++) {
		ks_thread_join(threads[i]);
	}

	// should already be destroyed now
	ok(ks_handle_destroy(&parent_handle) != KS_STATUS_SUCCESS);
	ok(ks_handle_destroy(&handle) != KS_STATUS_SUCCESS);

	ks_pool_close(&pool);

	ks_handle_shutdown();
	ks_shutdown();

	done_testing();
}

