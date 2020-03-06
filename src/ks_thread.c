/*
 * Copyright (c) 2018-2020 SignalWire, Inc
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

	ks_log(KS_LOG_DEBUG, "START call user thread callback with address: %p, tid: %8.8lx\n", (void *)thread, thread->id);
	ret = thread->function(thread, thread->private_data);
	ks_log(KS_LOG_DEBUG, "STOP call user thread callback with address: %p, tid: %8.8lx\n", (void *)thread, thread->id);

	if (thread->flags & KS_THREAD_FLAG_DETACHED) {
		ks_thread_destroy(&thread);
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

static ks_status_t __join_os_thread(ks_thread_t *thread) {
	if (ks_thread_self_id() != thread->id) {
		ks_log(KS_LOG_DEBUG, "Joining on thread address: %p, tid: %8.8lx\n", (void *)thread, thread->id);
#ifdef WIN32
		ks_assertd(WaitForSingleObject(thread->handle, INFINITE) == WAIT_OBJECT_0);
#else
		int err = 0;
		if ((err = pthread_join(thread->handle, NULL)) != 0) {
			ks_log(KS_LOG_DEBUG, "Failed to join on thread address: %p, tid: %8.8lx, error = %s\n", (void *)thread, thread->id, strerror(err));
			return KS_STATUS_FAIL;
		}
#endif
		ks_log(KS_LOG_DEBUG, "Completed join on thread address: %p, tid: %8.8lx\n", (void *)thread, thread->id);
	} else {
		ks_log(KS_LOG_DEBUG, "Not joining on self address: %p, tid: %8.8lx\n", (void *)thread, thread->id);
	}
	return KS_STATUS_SUCCESS;
}

KS_DECLARE(ks_status_t) ks_thread_join(ks_thread_t *thread) {
	ks_log(KS_LOG_DEBUG, "Join requested by thread: %8.8lx for thread address: %p, tid: %8.8lx\n", ks_thread_self_id(), (void *)&thread, thread->id);

	return __join_os_thread(thread);
}

/**
 * Flag thread to stop
 */
KS_DECLARE(ks_status_t) ks_thread_request_stop(ks_thread_t *thread)
{
	thread->stop_requested = KS_TRUE;
	return KS_STATUS_SUCCESS;
}

/**
 * Returns true if the thread was requested to exit by the caller
 */
KS_DECLARE(ks_bool_t) ks_thread_stop_requested(ks_thread_t *thread)
{
	return thread->stop_requested;
}

/**
 * Destroys a thread context, may be called by the caller or the thread itself
 * (if the thread is marked as detached).
 */
KS_DECLARE(void) ks_thread_destroy(ks_thread_t **threadp)
{
	ks_thread_t *thread = NULL;
	ks_bool_t detached;
	ks_pid_t tid;

	if (!threadp || !*threadp)
		return;

	thread = *threadp;

	detached = (thread->flags & KS_THREAD_FLAG_DETACHED) ? KS_TRUE : KS_FALSE;
	tid = thread->id;

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

	if (!rthread) return status;

	*rthread = NULL;

	if (!func) return status;

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

	/* Now allocate the os thread */
	if (__init_os_thread(thread) != KS_STATUS_SUCCESS)  {
		ks_log(KS_LOG_CRIT, "Failed to allocate os thread context for thread address: %p\n", (void *)thread);
		goto done;
	}

	ks_log(KS_LOG_DEBUG, "Waiting for thread thread to set running, with address: %p, tid: %8.8lx\n", (void *)thread, thread->id);

	/* Success! */
	status = KS_STATUS_SUCCESS;

  done:
	if (status != KS_STATUS_SUCCESS) {
		ks_log(KS_LOG_CRIT, "Thread allocation failed for thread address: %p\n", (void *)thread);
		ks_thread_destroy(&thread);
		*rthread = NULL;
	}

	return status;
}

KS_DECLARE(void) ks_thread_set_return_data(ks_thread_t *thread, void *return_data)
{
	thread->return_data = return_data;
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
