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

 #include "libks/ks_atomic.h"

 #pragma once

struct ks_thread {
	const char *tag;	/* Must be constant literal string */
	ks_pid_t id;		/* The system thread id (same thing ks_thread_self_id returns) */

#ifdef KS_PLAT_WIN
	void *handle;
#else
	pthread_t handle;
	pthread_attr_t attribute;
#endif
	void *private_data;
	ks_thread_function_t function;
	size_t stack_size;
	uint32_t flags;

	/* We break up these states based on who is intended to modify them
	 * thread_state - Set by the thread itself
	 * caller_state - Set by the caller who is controlling the thread
	 *                (with one exception, when a threaed self deletes)
	 * We can eliminate a 'joined' flag through these states as well, a
	 * thread has been 'joined' on if the thread_state is running and the
	 * caller state is shutdown.
	 */
	volatile ks_thread_state_t thread_state;
	volatile ks_thread_state_t caller_state;

	/* Set to true while someone is actively blocking on join. This is a
	 * separate flag from the states as the states may describe a stop
	 * request as well. */
	volatile ks_bool_t active_join;

	uint8_t priority;
	void *return_data;

	/* Lightweight lock to protect against synchronization access to this structure */
	ks_spinlock_t state_spin_lock;

	/* Lightweight lock to make the transition from starting to running easy */
	ks_spinlock_t thread_start_spin_lock;
};
