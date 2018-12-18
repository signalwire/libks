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

#ifndef __KS_ATOMIC_H__
#define __KS_ATOMIC_H__

KS_BEGIN_EXTERN_C

/**
 * ks_atomic_increment_* - Atomically increments the value, and returns the value before the increment ocurred.
 * ks_atomic_cas_* - Atomically compares, and performs an exchange on the types if they are the same.
 */
#ifdef KS_PLAT_WIN // Windows
#pragma warning(disable:4057)

static inline uint32_t ks_atomic_increment_uint32(volatile uint32_t *value) { return InterlockedIncrement(value) - 1; }

static inline uint64_t ks_atomic_increment_uint64(volatile uint64_t *value) { return InterlockedIncrement64((volatile LONG64 *)value) - 1; }

static inline ks_size_t ks_atomic_increment_size(volatile ks_size_t *value) { return InterlockedIncrementSizeT(value) - 1; }

static inline uint32_t ks_atomic_decrement_uint32(volatile uint32_t *value) { return InterlockedDecrement(value) + 1; }

static inline uint64_t ks_atomic_decrement_uint64(volatile uint64_t *value) { return InterlockedDecrement64((volatile LONG64 *)value) + 1; }

static inline ks_size_t ks_atomic_decrement_size(volatile ks_size_t *value) { return InterlockedDecrementSizeT(value) + 1; }


static inline ks_bool_t ks_atomic_cas_uint32(volatile uint32_t *destination, uint32_t comperand, uint32_t swapped)
{ return (ks_bool_t)(InterlockedCompareExchange(destination, swapped, comperand) == comperand); }

static inline ks_bool_t ks_atomic_cas_uint64(volatile uint64_t *destination, uint64_t comperand, uint64_t swapped)
{ return (ks_bool_t)(InterlockedCompareExchange64((volatile LONG64 *)destination, swapped, comperand) == comperand); }

static inline ks_bool_t ks_atomic_cas_size(volatile ks_size_t *destination, ks_size_t comperand, ks_size_t swapped)
{
#ifdef IS64BIT
	return (ks_bool_t)(InterlockedCompareExchange64((volatile LONG64 *)destination, swapped, comperand) == comperand);
#else
	return (ks_bool_t)(InterlockedCompareExchange((volatile DWORD *)destination, swapped, comperand) == comperand);
#endif
}

static inline ks_bool_t ks_atomic_cas_ptr(volatile void **destination, void *comperand, void *swapped)
{ return (ks_bool_t)(InterlockedCompareExchangePointer((void **)destination, swapped, comperand) == comperand); }

#else // GCC/CLANG

static inline uint32_t ks_atomic_increment_uint32(volatile uint32_t *value) { return __atomic_fetch_add(value, 1, __ATOMIC_SEQ_CST); }

static inline uint64_t ks_atomic_increment_uint64(volatile uint64_t *value) { return __atomic_fetch_add(value, 1, __ATOMIC_SEQ_CST); }

static inline ks_size_t ks_atomic_increment_size(volatile ks_size_t *value) { return __atomic_fetch_add(value, 1, __ATOMIC_SEQ_CST); }

static inline uint32_t ks_atomic_decrement_uint32(volatile uint32_t *value) { return __atomic_fetch_add(value, -1, __ATOMIC_SEQ_CST); }

static inline uint64_t ks_atomic_decrement_uint64(volatile uint64_t *value) { return __atomic_fetch_add(value, -1, __ATOMIC_SEQ_CST); }

static inline ks_size_t ks_atomic_decrement_size(volatile ks_size_t *value) { return __atomic_fetch_add(value, -1, __ATOMIC_SEQ_CST); }

static inline ks_bool_t ks_atomic_cas_uint32(volatile uint32_t *destination, uint32_t comperand, uint32_t swapped)
{ return (ks_bool_t)__atomic_compare_exchange_4(destination, &comperand, swapped, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); }

static inline ks_bool_t ks_atomic_cas_uint64(volatile uint64_t *destination, uint64_t comperand, uint64_t swapped)
{ return (ks_bool_t)__atomic_compare_exchange_8(destination, &comperand, swapped, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); }

static inline ks_bool_t ks_atomic_cas_size(volatile ks_size_t *destination, ks_size_t comperand, ks_size_t swapped)
{
#ifdef IS64BIT
	return (ks_bool_t)__atomic_compare_exchange_8(destination, &comperand, swapped, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
#else
	return (ks_bool_t)__atomic_compare_exchange_4(destination, &comperand, swapped, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
#endif
}

static inline ks_bool_t ks_atomic_cas_ptr(void **destination, void *comperand, void *swapped)
{
#ifdef IS64BIT
	return (ks_bool_t)__atomic_compare_exchange_8(destination, &comperand, (uintptr_t)swapped, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
#else
	return (ks_bool_t)__atomic_compare_exchange_4(destination, &comperand, (uintptr_t)swapped, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
#endif
}

#endif

/* Define spinlock macros */
typedef struct ks_spinlock_s
{
	volatile uint32_t count;
#if defined(KS_DEBUG_SPINLOCK)
	ks_pid_t owner_id;
#endif
} ks_spinlock_t;

static inline ks_bool_t ks_spinlock_try_acquire(const ks_spinlock_t *lock)
{
	if (ks_atomic_increment_uint32((volatile uint32_t *)&lock->count)) {
		ks_atomic_decrement_uint32((volatile uint32_t *)&lock->count);
#if KS_DEBUG_SPINLOCK
		printf("SPINLOCK TRY-ACQUIRE FAIL address: %p, count: %lu\n", (void *)lock, lock->count);
		ks_assert(lock->owner_id != ks_thread_self_id());
#endif

		return KS_FALSE;
	}

#if KS_DEBUG_SPINLOCK
	printf("SPINLOCK TRY-ACQUIRE address: %p, count: %lu\n", (void *)lock, lock->count);
	((ks_spinlock_t *)lock)->owner_id = ks_thread_self_id();
#endif

	return KS_TRUE;
}

static inline void ks_spinlock_acquire(const ks_spinlock_t *lock)
{
	uint64_t __wait_count = 0;
	while (!ks_spinlock_try_acquire(lock)) {

		/* Lower the priority as we wait */
		__wait_count++;
		if (__wait_count > 100000) {
			ks_sleep(100);
		} else if (__wait_count > 10000) {
			ks_sleep(10);
		} else if (__wait_count > 1000)	{
			ks_sleep(1);
		} else if (__wait_count > 100) {
			ks_sleep(0);
		}
	}

#if KS_DEBUG_SPINLOCK
	printf("SPINLOCK ACQUIRE address: %p, count: %lu\n", (void *)lock, lock->count);
#endif
}

static inline void ks_spinlock_release(const ks_spinlock_t *lock)
{
#if KS_DEBUG_SPINLOCK
	printf("SPINLOCK RELEASE address: %p, count: %lu\n", (void *)lock, lock->count);
	ks_assert(lock->count != 0);
	((ks_spinlock_t*)lock)->owner_id = 0;
#endif

	ks_atomic_decrement_uint32(&((ks_spinlock_t *)lock)->count);
	ks_assert(lock->count >= 0);
}

static inline void ks_spinlock_dispatch(const ks_spinlock_t *lock, ks_time_t sleep_delay)
{
	ks_spinlock_release(lock);
	ks_sleep(sleep_delay);
	ks_spinlock_acquire(lock);
}

static inline void ks_spinlock_dispatch_ms(const ks_spinlock_t *lock, ks_time_t sleep_delay)
{
	ks_spinlock_release(lock);
	ks_sleep_ms(sleep_delay);
	ks_spinlock_acquire(lock);
}

KS_END_EXTERN_C

#endif
