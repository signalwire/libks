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
#include "libks/ks_atomic.h"
#include "libks/internal/ks_thread.h"

/* Keep some basic counters for some basic debugging info when needed */
static uint32_t g_active_detached_thread_count = 0, g_active_attached_thread_count = 0;

/* Define the max number of seconds we'll wait for the thread to set its state to ready */
#define KS_THREAD_SANITY_WAIT_MS 1000

static const char * __thread_state_str(ks_thread_state_t state)
{
	switch (state) {
		case KS_THREAD_CALLER_STATE_INIT:
			return "KS_THREAD_CALLER_STATE_INIT";
		case KS_THREAD_CALLER_STATE_ALLOC_FAILURE:
			return "KS_THREAD_CALLER_STATE_ALLOC_FAILURE";
		case KS_THREAD_CALLER_STATE_START_REQUESTED:
			return "KS_THREAD_CALLER_STATE_START_REQUESTED";
		case KS_THREAD_CALLER_STATE_STOP_REQUESTED:
			return "KS_THREAD_CALLER_STATE_STOP_REQUESTED";
		case KS_THREAD_CALLER_STATE_JOIN_REQUESTED:
			return "KS_THREAD_CALLER_STATE_JOIN_REQUESTED";
		case KS_THREAD_INIT:
			return "KS_THREAD_INIT";
		case KS_THREAD_RUNNING:
			return "KS_THREAD_RUNNING";
		case KS_THREAD_STARTING:
			return "KS_THREAD_STARTING";
		case KS_THREAD_STOPPED:
			return "KS_THREAD_STOPPED";
		default:
			return "INVALID THREAD STATE";
	}
}

/* Define a macro to set the thread state and log it for debugging */
#define KS_THREAD_SET_STATE(thread, member, state)	\
	do {											\
		ks_log(KS_LOG_DEBUG, "Thread state change: %s => %s, address: %p, tid: %8.8x\n", __thread_state_str(thread->member), __thread_state_str(state), (void *)&thread, thread->id);	\
		thread->member = state;				\
	} while (KS_FALSE)

/* Define a macro to assert a specific thread state */
#define KS_THREAD_ASSERT_STATE(thread, member, state)	\
	do {											\
		if (thread->member != state) {				\
			ks_abort_fmt("Unexpected thread state (%s) %s Expected: %s", #member, __thread_state_str(thread->member), __thread_state_str(state));	\
		}											\
	} while (KS_FALSE)

#define KS_THREAD_ASSERT_NOT_STATE(thread, member, state)	\
	do {											\
		if (thread->member == state) {				\
			ks_abort_fmt("Unexpected thread state (%s): %s", #member, __thread_state_str(thread->member), __thread_state_str(state));	\
		}											\
	} while (KS_FALSE)

#define KS_THREAD_ASSERT_STATE_MULTI(thread, member, state1, state2)	\
	do {											\
		if (thread->member != state1 && thread->member != state2) {				\
			ks_abort_fmt("Unexpected thread state (%s) %s Expected either : %s, or %s", #member, __thread_state_str(thread->member), __thread_state_str(state1), __thread_state_str(state2));	\
		}											\
	} while (KS_FALSE)

#ifdef WIN32
	/* Setup for thread name setting, pulled from MSDN example */
	const DWORD MS_VC_EXCEPTION = 0x406D1388;
	#pragma pack(push,8)
	typedef struct tagTHREADNAME_INFO
	{
		DWORD dwType; // Must be 0x1000.
		LPCSTR szName; // Pointer to name (in user addr space).
		DWORD dwThreadID; // Thread ID (-1=caller thread).
		DWORD dwFlags; // Reserved for future use, must be zero.
	 } THREADNAME_INFO;
	#pragma pack(pop)
	void SetThreadName(DWORD dwThreadID, const char* threadName) {
		THREADNAME_INFO info;
		info.dwType = 0x1000;
		info.szName = threadName;
		info.dwThreadID = dwThreadID;
		info.dwFlags = 0;
	#pragma warning(push)
	#pragma warning(disable: 6320 6322)
		__try{
			RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
		}
		__except (EXCEPTION_EXECUTE_HANDLER){
		}
	#pragma warning(pop)
	}
#endif

static size_t thread_default_stacksize = 240 * 1024;

#ifndef WIN32
pthread_once_t init_priority = PTHREAD_ONCE_INIT;
#endif

KS_DECLARE(ks_thread_os_handle_t) ks_thread_os_handle(ks_thread_t *thread)
{
	return thread->handle;
}

KS_DECLARE(ks_thread_os_handle_t) ks_thread_self(void)
{
#ifdef WIN32
	return GetCurrentThread();
#else
	return pthread_self();
#endif
}

KS_DECLARE(ks_pid_t) ks_thread_self_id(void)
{
#ifdef KS_PLAT_WIN
	return GetCurrentThreadId();
#elif KS_PLAT_LIN
	return syscall(SYS_gettid);
#else
	return pthread_self();
#endif
}

static void ks_thread_init_priority(void)
{
#ifdef WIN32
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#else
#ifdef USE_SCHED_SETSCHEDULER
    /*
     * Try to use a round-robin scheduler
     * with a fallback if that does not work
     */
    struct sched_param sched = { 0 };
    sched.sched_priority = KS_PRI_LOW;
    if (sched_setscheduler(0, SCHED_FIFO, &sched)) {
        sched.sched_priority = 0;
        if (sched_setscheduler(0, SCHED_OTHER, &sched)) {
            return;
        }
    }
#endif
#endif
    return;
}

void ks_thread_override_default_stacksize(size_t size)
{
	thread_default_stacksize = size;
}

static void ks_thread_cleanup(void *ptr, void *arg, ks_pool_cleanup_action_t action, ks_pool_cleanup_type_t type)
{
	ks_thread_t *thread = (ks_thread_t *) ptr;

	/* We should never be trying to free a detached thread from a pool free, it should free itself with no cleanup callback */
	ks_assertd(!(thread->flags & KS_THREAD_FLAG_DETACHED));

	ks_log(KS_LOG_DEBUG, "Thread cleanup called for thread address %p, tid: %lx\n", (void *)thread, thread->id);

	switch(action) {
	case KS_MPCL_ANNOUNCE:
		ks_log(KS_LOG_DEBUG, "Requesting stop for thread cleanup callback with address: %p, tid: %lx\n", (void *)thread, thread->id);
		ks_thread_request_stop(thread);
		break;
	case KS_MPCL_TEARDOWN:
		ks_log(KS_LOG_DEBUG, "Joining on thread from thread cleanup callback with address: %p, tid: %lx\n", (void *)thread, thread->id);
		ks_thread_join(thread);
		break;
	case KS_MPCL_DESTROY:
		ks_log(KS_LOG_DEBUG, "Destroying thread from thread cleanup callback with address: %p, tid: %lx\n", (void *)thread, thread->id);
		ks_thread_destroy(&thread);
		break;
	}
}

static void *KS_THREAD_CALLING_CONVENTION thread_launch(void *args)
{
	ks_thread_t *thread = (ks_thread_t *) args;
	void *ret = NULL;

	ks_log(KS_LOG_DEBUG, "Thread has launched with address: %p, tid: %8.8lx\n", (void *)thread, thread->id);

#ifdef HAVE_PTHREAD_SETSCHEDPARAM
	if (thread->priority) {
		int policy = SCHED_FIFO;
		struct sched_param param = { 0 };
		pthread_t tt = pthread_self();

		pthread_once(&init_priority, ks_thread_init_priority);
		pthread_getschedparam(tt, &policy, &param);
		param.sched_priority = thread->priority;
		pthread_setschedparam(tt, policy, &param);
	}
#endif
	ks_log(KS_LOG_DEBUG, "Marking thread as running, with address: %p, tid: %8.8lx\n", (void *)thread, thread->id);

	/* Now the caller will have thread_start_spin_lock held, and it will be looping on
	 * an atomic fetch of the thread state, waiting for us to become alive. We first
	 * atomically set our thread state to running, then we wait on the thread_start_spin_lock
	 * to wait for the caller to accept the transition and proceed */
	ks_spinlock_acquire(&thread->state_spin_lock);
	KS_THREAD_SET_STATE(thread, thread_state, KS_THREAD_RUNNING);
	thread->id = ks_thread_self_id();

#if KS_PLAT_WIN
	if (thread->tag)
		SetThreadName(thread->id, thread->tag);
#elif KS_PLAT_MAC
	if (thread->tag && pthread_setname_np)
		pthread_setname_np(thread->tag);
#else
	if (thread->tag && pthread_setname_np)
		pthread_setname_np(pthread_self(), thread->tag);
#endif

	ks_spinlock_release(&thread->state_spin_lock);

	ks_spinlock_acquire(&thread->thread_start_spin_lock);

	/* From here on out the thread start spin lock is no longer used */

	ks_log(KS_LOG_DEBUG, "START call user thread callback with address: %p, tid: %8.8lx\n", (void *)thread, thread->id);
	ret = thread->function(thread, thread->private_data);
	ks_log(KS_LOG_DEBUG, "STOP call user thread callback with address: %p\n", (void *)thread);

	/* Catch any memory corruptions */
	ks_assertd(thread->id == ks_thread_self_id());

	ks_log(KS_LOG_DEBUG, "Thread callback completed for addresss: %p, tid: %8.8lx\n", (void *)thread, thread->id);

	/* Now if we are detached it means we are in control of destroying ourselves so, take care of that now */
	if ((thread->flags & KS_THREAD_FLAG_DETACHED)) {
		/* We should have our pool prefix locked */
		ks_assertd(ks_pool_allocation_lock_try_acquire(thread) == KS_FALSE);
		ks_thread_destroy(&thread);

		/* Memory freed now no touch!  */
	} else {
		/* We should not have our pool prefix locked */
		ks_assertd(ks_pool_allocation_lock_try_acquire(thread) == KS_TRUE);
		ks_pool_allocation_lock_release(thread);

		ks_log(KS_LOG_DEBUG, "Thread is attached, marking as stopped for address: %p, tid: %8.8lx\n", (void *)thread, thread->id);
		ks_spinlock_acquire(&thread->state_spin_lock);
		KS_THREAD_SET_STATE(thread, thread_state, KS_THREAD_STOPPED);

		/* Set the thread data under the lock */
		thread->return_data = ret;

		ks_spinlock_release(&thread->state_spin_lock);
	}

	return ret;
}

KS_DECLARE(int) ks_thread_set_priority(int nice_val)
{
#ifdef WIN32
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#else
#ifdef USE_SCHED_SETSCHEDULER
    /*
     * Try to use a round-robin scheduler
     * with a fallback if that does not work
     */
    struct sched_param sched = { 0 };
    sched.sched_priority = KS_PRI_LOW;
    if (sched_setscheduler(0, SCHED_FIFO, &sched)) {
        sched.sched_priority = 0;
        if (sched_setscheduler(0, SCHED_OTHER, &sched)) {
            return -1;
        }
    }
#endif

	if (nice_val) {
#ifdef HAVE_SETPRIORITY
		/*
		 * setpriority() works on FreeBSD (6.2), nice() doesn't
		 */
		if (setpriority(PRIO_PROCESS, getpid(), nice_val) < 0) {
			ks_log(KS_LOG_CRIT, "Could not set nice level\n");
			return -1;
		}
#else
		if (nice(nice_val) != nice_val) {
			ks_log(KS_LOG_CRIT, "Could not set nice level\n");
			return -1;
		}
#endif
	}
#endif

    return 0;
}

KS_DECLARE(uint8_t) ks_thread_priority(ks_thread_t *thread) {
	uint8_t priority = 0;
#ifdef WIN32
	//int pri = GetThreadPriority(thread->handle);

	//if (pri >= THREAD_PRIORITY_TIME_CRITICAL) {
	//	priority = 99;
	//} else if (pri >= THREAD_PRIORITY_ABOVE_NORMAL) {
	//	priority = 50;
	//} else {
	//	priority = 10;
	//}
	priority = thread->priority;
#else
	int policy;
	struct sched_param param = { 0 };

	pthread_getschedparam(thread->handle, &policy, &param);
	priority = param.sched_priority;
#endif
	return priority;
}

static void __join_os_thread(ks_thread_t *thread) {
	if (ks_thread_self_id() != thread->id) {
		ks_log(KS_LOG_DEBUG, "Joining on thread address: %p, tid: %8.8lx\n", (void *)thread, thread->id);
#ifdef WIN32
		ks_assertd(WaitForSingleObject(thread->handle, INFINITE) == WAIT_OBJECT_0);
#else
		ks_assertd(pthread_join(thread->handle, NULL) == 0);
#endif
		ks_log(KS_LOG_DEBUG, "Completed join on thread address: %p, tid: %8.8lx\n", (void *)thread, thread->id);
	} else {
		ks_log(KS_LOG_DEBUG, "Not joining on self address: %p, tid: %8.8lx\n", (void *)thread, thread->id);
	}
}

/**
 * Uses operating system apis to wait for the thread to complete its run,
 * if called by the thread itself, will simply transition from STARTED to
 * STOPPING and release the join context in pthread as a result.
 */
KS_DECLARE(ks_status_t) ks_thread_join(ks_thread_t *thread) {
	ks_bool_t self_join = thread->id == ks_thread_self_id();

	ks_log(KS_LOG_DEBUG, "Join requested by thread: %8.8lx for thread address: %p, tid: %8.8lx\n", ks_thread_self_id(), (void *)&thread, thread->id);

	/* Now we want to be careful here as two threads may call
	 * onto the same thread to join at the same time, and both
	 * threads may also be interdependent on one another (yikes)*/
	ks_spinlock_acquire(&thread->state_spin_lock);

	switch (thread->caller_state) {
		/* Someone has already requested the thread to stop */
		case KS_THREAD_CALLER_STATE_STOP_REQUESTED:
		case KS_THREAD_CALLER_STATE_JOIN_REQUESTED:
			/* If this is a self join */
			if (self_join) {
				/* Acknowledge the request if we are running */
				if (thread->thread_state == KS_THREAD_RUNNING) {
					KS_THREAD_SET_STATE(thread, thread_state, KS_THREAD_STOPPED);
				} else {
					/* Otherwise we should be stopped*/
					KS_THREAD_ASSERT_STATE(thread, thread_state, KS_THREAD_STOPPED);
				}
			/* Join/stop request, check if thread already joined on */
			} else {
				/* Thread should be starting/running/stopped/stopping (but not init/starting)*/
				KS_THREAD_ASSERT_NOT_STATE(thread, thread_state, KS_THREAD_INIT);
				KS_THREAD_ASSERT_NOT_STATE(thread, thread_state, KS_THREAD_STARTING);

				/* Now if we're joining already, have to error on the second guy */
				if (thread->active_join || thread->caller_state == KS_THREAD_CALLER_STATE_JOIN_REQUESTED) {
					/* Unless the thread is already stopped, in which case, its a null op */
					if (thread->thread_state == KS_THREAD_STOPPED) {
						ks_spinlock_release(&thread->state_spin_lock);
						return KS_STATUS_SUCCESS;
					}

					ks_spinlock_release(&thread->state_spin_lock);

					ks_log(KS_LOG_WARNING, "Redundant join blocked, caller already requested join for thread: %8.8lx\n", thread->id);
					return KS_STATUS_THREAD_ALREADY_JOINED;
				}
			}

			/* So we're safe to transition to join if stop wasn't already requested */
			if (thread->caller_state != KS_THREAD_CALLER_STATE_STOP_REQUESTED)
				KS_THREAD_SET_STATE(thread, caller_state, KS_THREAD_CALLER_STATE_JOIN_REQUESTED);
			break;

		/* No one has requested the thread to stop */
		case KS_THREAD_CALLER_STATE_START_REQUESTED:
			if (self_join) {
				/* Well we should be running in any event  */
				KS_THREAD_ASSERT_STATE(thread, thread_state, KS_THREAD_RUNNING);

				/* Ok act aas a caller now and set the caller state to join requested */
				KS_THREAD_SET_STATE(thread, caller_state, KS_THREAD_CALLER_STATE_JOIN_REQUESTED);

				/* And acknowledge it now */
				KS_THREAD_SET_STATE(thread, thread_state, KS_THREAD_STOPPED);
			/* This is not a self stop so, do the caller role and request stop */
			} else {
				/* Thread may be running or stopped, just not init */
				KS_THREAD_ASSERT_NOT_STATE(thread, thread_state, KS_THREAD_INIT);

				/* Request the thread to stop  (we will join below next) */
				KS_THREAD_SET_STATE(thread, caller_state, KS_THREAD_CALLER_STATE_JOIN_REQUESTED);
				ks_log(KS_LOG_DEBUG, "Thread is running, and caller wants to join for address: %p, tid: %8.8lx\n", (void *)thread, thread->id);
			}
			break;
		case KS_THREAD_CALLER_STATE_ALLOC_FAILURE:
			ks_assertd(!"Invalid caller thread state - CALLER_STATE_ALLOC_FAILURE");
		case KS_THREAD_CALLER_STATE_INIT:
			ks_assertd(!"Invalid caller thread state - CALLER_STATE_INIT");
		default:
			ks_assertd(!"Invalid caller thread state - UNKNOWN");
	}

	thread->active_join = KS_TRUE;
	ks_spinlock_release(&thread->state_spin_lock);

	/* Don't deadlock here if we are the thread itself */
	__join_os_thread(thread);

	ks_spinlock_acquire(&thread->state_spin_lock);

	thread->active_join = KS_FALSE;

	/* After this function is called:
	 * thread_state == KS_THREAD_STOPPED
	 * caller_state == KS_THREAD_CALLER_STATE_JOIN_REQUESTED || KS_THREAD_CALLER_STATE_STOP_REQUESTED */
	KS_THREAD_ASSERT_STATE(thread, thread_state, KS_THREAD_STOPPED);
	KS_THREAD_ASSERT_STATE_MULTI(thread, caller_state, KS_THREAD_CALLER_STATE_JOIN_REQUESTED, KS_THREAD_CALLER_STATE_STOP_REQUESTED);

	ks_spinlock_release(&thread->state_spin_lock);

	return KS_STATUS_SUCCESS;
}

/**
 * Sets the thread state to KS_THREAD_CALLER_STATE_STOP_REQUESTED to indicate the thread should stop.
 * Does not block just sets the state. Not allowed on detached threads.
 */
KS_DECLARE(ks_status_t) ks_thread_request_stop(ks_thread_t *thread)
{
	ks_status_t status = KS_STATUS_SUCCESS;

	ks_spinlock_acquire(&thread->state_spin_lock);

	/* We'll allow the same thread to tell itself to stop */
	if (thread->id != ks_thread_self_id()) {
		/* Not safe to check if a thread is running that is flagged as detached as the thread may
		 * self delete at any moment */
		ks_assertd(!(thread->flags & KS_THREAD_FLAG_DETACHED));
	}

	/* Now, we will only allow this if the thread is running, and  not
	 * stop requested, or join requested */
	if (thread->caller_state == KS_THREAD_CALLER_STATE_START_REQUESTED && thread->thread_state == KS_THREAD_RUNNING) {
		KS_THREAD_SET_STATE(thread, caller_state, KS_THREAD_CALLER_STATE_STOP_REQUESTED);
	} else {
		ks_log(KS_LOG_DEBUG, "Stop request denied, thread state: %s, pointer: %p, tid: %8.8lx\n", __thread_state_str(thread->caller_state), (void *)thread, thread->id);

		/* Assume its already stopped as far as error codes are concenred */
		status = KS_STATUS_THREAD_ALREADY_STOPPED;
	}

	ks_spinlock_release(&thread->state_spin_lock);

	return status;
}

/**
 * Returns true if the thread was requested to exit by the caller state being set
 * to KS_THREAD_CALLER_STATE_STOP_REQUESTED.
 */
KS_DECLARE(ks_bool_t) ks_thread_stop_requested(ks_thread_t *thread)
{
	ks_thread_state_t caller_state;

	if (!thread) {
		ks_log(KS_LOG_DEBUG, "Null thread given, assuming thread stopped");
		return KS_TRUE;
	}

	ks_spinlock_acquire(&thread->state_spin_lock);

	/* We'll allow the same thread to ask itself this question */
	if (thread->id != ks_thread_self_id()) {
		/* Not safe to check if a thread is running that is flagged as detached as the thread may
		 * self delete at any moment */
		ks_assertd(!(thread->flags & KS_THREAD_FLAG_DETACHED));
	}

	caller_state = thread->caller_state;

	ks_spinlock_release(&thread->state_spin_lock);

	return caller_state == KS_THREAD_CALLER_STATE_STOP_REQUESTED;
}

/**
 * Returns true if the thread is still running. Will assert if the caller is calling into
 * a thread that is flagged as detached.
 */
KS_DECLARE(ks_bool_t) ks_thread_is_running(ks_thread_t *thread)
{
	ks_thread_state_t thread_state;

	ks_spinlock_acquire(&thread->state_spin_lock);

	/* Not safe to check if a thread is running that is flagged as detached as the thread may
	 * self delete at any moment */
	ks_assertd(!(thread->flags & KS_THREAD_FLAG_DETACHED));

	thread_state = thread->thread_state;

	ks_spinlock_release(&thread->state_spin_lock);

	return thread_state == KS_THREAD_RUNNING;
}

/**
 * Destroys a thread context, may be called by the caller or the thread itself
 * (if the thread is marked as detached).
 */
KS_DECLARE(void) ks_thread_destroy(ks_thread_t **threadp)
{
	ks_thread_t *thread = NULL;
	ks_bool_t detached, self_destroy;
	ks_status_t join_status;
	ks_pid_t tid;

	if (!threadp || !*threadp)
		return;

	thread = *threadp;

	detached = (thread->flags & KS_THREAD_FLAG_DETACHED) ? KS_TRUE : KS_FALSE;
	self_destroy = thread->id == ks_thread_self_id() ? KS_TRUE : KS_FALSE;
	tid = thread->id;

	/* Only allow self deletion if flagged for detached */
	if (!detached && self_destroy) {
		ks_abort("Illegal to self destroy when not detached");
	}

	ks_log(KS_LOG_DEBUG, "Thread destroy initiated for thread pointer: %p, tid: %8.8lx\n", (void *)thread, thread->id);

	/* Synchronize access to the thread state vars */
	ks_spinlock_acquire(&thread->state_spin_lock);

	/* Depending on the state of the callers request */
	switch (thread->caller_state) {
		case KS_THREAD_CALLER_STATE_STOP_REQUESTED:
		case KS_THREAD_CALLER_STATE_JOIN_REQUESTED:
			KS_THREAD_ASSERT_STATE_MULTI(thread, thread_state, KS_THREAD_RUNNING, KS_THREAD_STOPPED);
			/* Don't acknowledge it just yet, we'll let join to that below */
			break;

		case KS_THREAD_CALLER_STATE_INIT:
			ks_assertd(!"Invalid caller state CALLER_STATE_INIT");
		case KS_THREAD_CALLER_STATE_ALLOC_FAILURE:
			/* We will allow alloc failure only if we are not self deleting */
			if (self_destroy) {
				ks_assertd(!"Invalid caller state CALLER_STATE_ALLOC_FAILURE");
			}
			break;
		case KS_THREAD_CALLER_STATE_START_REQUESTED:
			if (!self_destroy) {
				/* If a thread is self destroying, we allow this */
			} else if (thread->thread_state != KS_THREAD_RUNNING) {
				ks_assertd(!"Invalid thread state THREAD_RUNNING for caller state CALLER_STATE_START_REQUESTED");
			}
			break;
		default:
			ks_assertd(!"Invalid caller state UNKNOWN");
	}

	/* State completed, release the lock */
	ks_spinlock_release(&thread->state_spin_lock);

	/* Request thread stop (a join does not by itself imply the thread should stop) */
	ks_thread_request_stop(thread);

	/* Now we must join, this will acknowledge the stop request by the caller and join (if this isn't a self destroy)
	 * on the thread, and free our pthread/windows handles/attributes as needed */
	 if (join_status = ks_thread_join(thread)) {
		/* Now we should only ever possibly fail here if we are NOT self destroying as
		 * the thread will properly guard against two async joins at once */
		 ks_assertd(!self_destroy);

		 /* If this is already joined, it means the thread is ready for destroy, do that now */
		 if (join_status != KS_STATUS_THREAD_ALREADY_JOINED) {
			 /* Note however as this is inherently racy as there is no way to lock the context that is
			  * getting deleted, so log a warning */
			 ks_log(KS_LOG_WARNING, "Racey attempt to destroy a already pending destroy thread, tid: %8.8lu\n", tid);
			 return;
		 }

		 // Allow the join case
	 }

	 ks_spinlock_acquire(&thread->state_spin_lock);

	 /* Ok finally post success, we should be now left with a state of
	  * thread_state - KS_THREAD_STOPPED
	  * caller_state - KS_THREAD_CALLER_STATE_JOIN_REQUESTED || KS_THREAD_CALLER_STATE_STOP_REQUESTED */
	 KS_THREAD_ASSERT_STATE(thread, thread_state, KS_THREAD_STOPPED);
	 KS_THREAD_ASSERT_STATE_MULTI(thread, caller_state, KS_THREAD_CALLER_STATE_JOIN_REQUESTED, KS_THREAD_CALLER_STATE_STOP_REQUESTED);

	 /* Shouldn't need the lock now no one will touch us */
	 ks_spinlock_release(&thread->state_spin_lock);

	 ks_log(KS_LOG_DEBUG, "Thread destroy complete, deleting os primitives for thread address %p, tid: %8.8lx", (void *)thread, thread->id);

#ifdef WIN32
	CloseHandle(thread->handle);
	thread->handle = NULL;
#else
	pthread_attr_destroy(&thread->attribute);
#endif

	ks_log(KS_LOG_DEBUG, "Current active and attached count: %u, current active and detatched count: %u\n",
		g_active_attached_thread_count, g_active_detached_thread_count);

	if (detached) {
		ks_atomic_decrement_uint32(&g_active_detached_thread_count);
	} else  {
		ks_atomic_decrement_uint32(&g_active_attached_thread_count);
	}

	 /* If we were detached, we need to unlock our prefix before the pool will allow a free */
	 if (thread->flags & KS_THREAD_FLAG_DETACHED) {
		ks_pool_allocation_lock_release(thread);
	 }

	 /* Now since we're freeing we don't need the pool cleanup anymore */
	 ks_pool_remove_cleanup(thread);

	 /* And free the memory */
	 ks_pool_free(threadp);
}

#if KS_PLAT_WIN
/* Windows thread startup */
static ks_status_t __init_os_thread(ks_thread_t *thread)
{
	thread->handle = (void *) _beginthreadex(
		NULL,
		(unsigned) thread->stack_size,
		(unsigned int (__stdcall *) (void *)) thread_launch,
		thread,
		0,
		NULL);

	if (!thread->handle) {
		ks_log(KS_LOG_CRIT, "System failed to allocate thread, lasterror: %d\n", GetLastError());
		return KS_STATUS_FAIL;
	}

	if (thread->priority >= 99) {
		SetThreadPriority(thread->handle, THREAD_PRIORITY_TIME_CRITICAL);
	} else if (thread->priority >= 50) {
		SetThreadPriority(thread->handle, THREAD_PRIORITY_ABOVE_NORMAL);
	} else if (thread->priority >= 10) {
		SetThreadPriority(thread->handle, THREAD_PRIORITY_NORMAL);
	} else if (thread->priority >= 1) {
		SetThreadPriority(thread->handle, THREAD_PRIORITY_LOWEST);
	}

	return KS_STATUS_SUCCESS;
}

#else

/* Gnu thread startup */
static ks_status_t __init_os_thread(ks_thread_t *thread)
{
	ks_status_t status = KS_STATUS_FAIL;

	if (pthread_attr_init(&thread->attribute) != 0)
		return KS_STATUS_FAIL;

	if ((thread->flags & KS_THREAD_FLAG_DETACHED) && pthread_attr_setdetachstate(&thread->attribute, PTHREAD_CREATE_DETACHED) != 0)
		goto done;

	if (thread->stack_size && pthread_attr_setstacksize(&thread->attribute, thread->stack_size) != 0)
		goto done;

	if (pthread_create(&thread->handle, &thread->attribute, thread_launch, thread) != 0)
		goto done;

	status = KS_STATUS_SUCCESS;

done:
	/* Cleanup if we failed past alloc of the attributes */
	if (status != KS_STATUS_SUCCESS)
		pthread_attr_destroy(&thread->attribute);

	return status;
}
#endif

KS_DECLARE(ks_status_t) __ks_thread_create_ex(
	ks_thread_t **rthread,
	ks_thread_function_t func,
	void *data,
	uint32_t flags,
	size_t stack_size,
	ks_thread_priority_t priority,
	ks_pool_t *pool,
	const char *file,
	int line,
	const char *tag)
{
	ks_thread_t *thread = NULL;
	ks_status_t status = KS_STATUS_FAIL;
	int sanity = KS_THREAD_SANITY_WAIT_MS;

	if (!rthread) return status;

	*rthread = NULL;

	if (!func) return status;

	/* Detatched threads are not bound to the lifetime by definition of anyone, hence we will not
	 * use the callers pool in that case (we will use the global pool) */
	 if (flags & KS_THREAD_FLAG_DETACHED || !pool) {
		pool = ks_global_pool();
	 }

	thread = (ks_thread_t *) __ks_pool_alloc(pool, sizeof(ks_thread_t), file, line, tag);

	ks_assertd(thread);

	/* Assign the callers ptr *right* away so the thread doesn't start before its assigned */
	*rthread = thread;

	/* Increment out stats */
	if (flags & KS_THREAD_FLAG_DETACHED) {
		ks_atomic_increment_uint32(&g_active_detached_thread_count);
	} else  {
		ks_atomic_increment_uint32(&g_active_attached_thread_count);
	}

	ks_log(KS_LOG_DEBUG, "Allocating new thread, current active and attached count: %u, current active and detatched count: %u\n",
		g_active_attached_thread_count, g_active_detached_thread_count);

	thread->private_data = data;
	thread->function = func;
	thread->stack_size = stack_size;
	thread->flags = flags;
	thread->priority = priority;
	thread->tag = tag;	/* We require a constant literal string here */

	/* We want to block the thread so we can manage its state, do that now */
	ks_spinlock_acquire(&thread->thread_start_spin_lock);

	/* Mark the initial thread state as starting, we'll lock step wait for this state to
	 * become running before returning */
	KS_THREAD_SET_STATE(thread, thread_state, KS_THREAD_STARTING);

	/* Initial state is we are requesting the thread to start */
	KS_THREAD_SET_STATE(thread, caller_state, KS_THREAD_CALLER_STATE_START_REQUESTED);

	/* Now allocate the os thread */
	if (__init_os_thread(thread) != KS_STATUS_SUCCESS)  {
		ks_log(KS_LOG_CRIT, "Failed to allocate os thread context for thread address: %p\n", (void *)thread);
		goto done;
	}

	ks_log(KS_LOG_DEBUG, "Waiting for thread thread to set running, with address: %p, tid: %8.8lx\n", (void *)thread, thread->id);

	/* Wait for the thread to set its id  while holding the thread_start lock so we can atomically
	 * transition states with the new thread */
	ks_spinlock_acquire(&thread->state_spin_lock);
	while(thread->thread_state == KS_THREAD_STARTING && --sanity > 0) {
		ks_spinlock_dispatch(&thread->state_spin_lock, 1000);
	}
	if (sanity) {
		ks_assertd(thread->id != 0);
		KS_THREAD_ASSERT_STATE(thread, thread_state, KS_THREAD_RUNNING);
	}

	// Leave state_spin_lock locked

	// Thread started, let it proceed by releasing the start lock
	ks_spinlock_release(&thread->thread_start_spin_lock);

	if (!sanity) {
		status = KS_STATUS_FAIL;
		ks_log(KS_LOG_CRIT, "Failed to wait for %d ms to wait for thread %8.8lx to set state to be ready\n", KS_THREAD_SANITY_WAIT_MS, thread->id);
		goto done;
	}

	if (thread->flags & KS_THREAD_FLAG_DETACHED) {
		ks_log(KS_LOG_DEBUG, "Allocated (detached) thread context ptr: %p, tid: %8.8lx\n", (void *)thread, thread->id);
	} else {
		ks_log(KS_LOG_DEBUG, "Allocated (attached) thread context ptr: %p, tid: %8.8lx\n", (void *)thread, thread->id);
	}

	/* Success! */
	status = KS_STATUS_SUCCESS;

	if (flags & KS_THREAD_FLAG_DETACHED) {
		/* Lock this allocation to an explicit lock on the pool prefix, since
		 * we are detached this is how we safeguard against rogue deletes, only we can
		 * delete it as no one can actually wait on us. */
		 ks_pool_allocation_lock_acquire(thread);
	} else {
		/* We are attached so, always associate it with the pool, so the pool
		 * may stop us or the user */
		ks_pool_set_cleanup(thread, NULL, ks_thread_cleanup);
	}

  done:
	if (status != KS_STATUS_SUCCESS) {
		ks_log(KS_LOG_CRIT, "Thread allocation failed for thread address: %p\n", (void *)thread);
		KS_THREAD_SET_STATE(thread, caller_state, KS_THREAD_CALLER_STATE_ALLOC_FAILURE);
		ks_spinlock_release(&thread->state_spin_lock);
		ks_thread_destroy(&thread);
		*rthread = NULL;
	} else {
		ks_spinlock_release(&thread->state_spin_lock);
	}

	return status;
}

KS_DECLARE(void) ks_thread_set_return_data(ks_thread_t *thread, void *return_data)
{
	ks_spinlock_acquire(&thread->state_spin_lock);

	/* Thread data usage not safe with detached threads */
	ks_assertd(!(thread->flags & KS_THREAD_FLAG_DETACHED));

	/* Return data can only be set in thread */
	ks_assertd(!(thread->id == ks_thread_self_id()));

	/* Thread state must be stopped */
	ks_assertd(thread->thread_state == KS_THREAD_STOPPED);

	thread->return_data = return_data;

	ks_spinlock_release(&thread->state_spin_lock);
}

/**
 *  Return data implicitly joins on the thread and returns the
 *  value that the thread itself returned from its callback.
 */
KS_DECLARE(void *) ks_thread_get_return_data(ks_thread_t *thread)
{
	void *thread_data;
	ks_status_t status;

	/* Join if needed (will assert thread is not detached) */
	if (status = ks_thread_join(thread)) {
		ks_log(KS_LOG_ERROR, "Return data blocked, thread join failed: %d\n", status);
		return NULL;
	}

	thread_data = thread->return_data;

	return thread_data;
}

KS_DECLARE(void) ks_thread_stats(uint32_t *active_attached, uint32_t *active_detached)
{
	/* Lean on the fact that integer assignments are atomic */
	if (active_detached) {
		*active_detached = g_active_detached_thread_count;
	}
	if (active_attached) {
		*active_attached = g_active_attached_thread_count;
	}
}

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
