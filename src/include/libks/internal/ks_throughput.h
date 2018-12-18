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

#pragma once

KS_BEGIN_EXTERN_C 

/* Declare our bucket entry, this describes a point in time
 * with a max and min amount of entries */
typedef struct ks_throughput_bucket_s {
	uint64_t size;
	uint64_t count;
} ks_throughput_bucket_t;

/**
 * The throughput structure is a rolling window averaging rate
 * calculator capable of calculating a throughout or a basic
 * per second rate.
 */
struct ks_throughput_ctx {
	ks_handle_base_t base;

	/* The interval for the bucket calculator, anytime we get
	 * called to update we'll figure out how long its been since the
	 * last and roll the buckets forward */
	uint32_t interval_sec;

	/* This flag indicates whether we are started or not */
	ks_bool_t started;

	/* Timestamps for calls to start and stop. */
	ks_time_t stop_time_sec;
	ks_time_t start_time_sec;

	/* For unit testing, when true causes the start_time_sec to
	 * not advance implicitly anymore. */
	ks_time_t static_now_sec;

	/* Totals kept between start/stop runs. */
	uint64_t total_size;
	uint64_t total_count;

	/* This is our 'current position' in time. */
	ks_time_t last_update_time_sec;

	/* Protection for state */
	ks_spinlock_t spin_lock;

	/* Our fixed queue, where each bucket lives. */
	ks_throughput_bucket_t *buckets;

	/* Max cap on the buckets, allocated once at create time
	 * is our window into the time slice to summarize rates from */
	uint32_t max_buckets;

	/* This is the next position for our bucket, as we
	 * copy the current buffer to the completed buckets
	 * array, this increments, until it reaches max buckets
	 * in which case it loops. As each time periode completes
	 * a new bucket enters this loop, in this way we keep a
	 * rolling window view into our current counts to calculate
	 * rate for. */
	ks_size_t next_bucket_slot;

	/* Current logical count of active buckets */
	ks_size_t count_bucket;

	/* This is our current bucket that gets lobbed onto the bucket
	 * list when the next interval passes */
	ks_throughput_bucket_t current_bucket;
};

KS_END_EXTERN_C 
