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
#include "libks/ks_atomic.h"
#include "libks/internal/ks_throughput.h"

static ks_status_t __update(ks_throughput_ctx_t *ctx);
static ks_time_t __run_time(ks_throughput_ctx_t *ctx);
static ks_status_t __current_size(ks_throughput_ctx_t *ctx, uint64_t *size);
static ks_status_t __current_count(ks_throughput_ctx_t *ctx, uint64_t *count);

#define SCOPE_BEG(throughput, ctx, status) \
	KS_HANDLE_SCOPE_BEG(KS_HTYPE_THROUGHPUT, throughput, ks_throughput_ctx_t, ctx, status)

#define SCOPE_END(throughput, ctx, status) \
	KS_HANDLE_SCOPE_END(KS_HTYPE_THROUGHPUT, throughput, ks_throughput_ctx_t, ctx, status)

// Returns the number of buckets if we are started, otherwise we
// return 0 since the buckets are inactive when stopped.
static ks_size_t __completed_bucket_count(ks_throughput_ctx_t *ctx)
{
	if (!ctx->started)
		return 0;
	return ctx->count_bucket;
}

// Calculates an average throughput.
static ks_time_t __current_timestamp_sec(ks_throughput_ctx_t *ctx)
{
	if (ctx->static_now_sec)
		return ctx->static_now_sec;
	return ks_time_now_sec();
}

static ks_status_t __initialize_buckets(ks_throughput_ctx_t *ctx)
{
	ks_pool_free(&ctx->buckets);
	if (!(ctx->buckets = (ks_throughput_bucket_t *) ks_pool_alloc(NULL, ctx->max_buckets * sizeof(ks_throughput_bucket_t))))
		return KS_STATUS_NO_MEM;

	ctx->current_bucket = (ks_throughput_bucket_t){0,0};

	/* Virutaly be at zero */
	ctx->count_bucket = 0;

	return KS_STATUS_SUCCESS;
}

static double __calculate_average(uint64_t size, ks_time_t duration)
{
	if (duration)
		return size / (double)duration;
	return 0;
}

static double __calculate_rate(ks_throughput_ctx_t *ctx, uint64_t amount)
{
	double rate = 0;
	ks_time_t completedSeconds = ctx->interval_sec * __completed_bucket_count(ctx);

	if (completedSeconds)
		return amount / (double)completedSeconds;

	return rate;
}

static ks_status_t __stats(ks_throughput_ctx_t *ctx, ks_throughput_stats_t *_stats)
{
	ks_throughput_stats_t stats = {0};
	uint64_t current_size, current_count;
	ks_status_t status;

	if (status = __update(ctx))
		return status;

	stats.run_time_sec = __run_time(ctx);
	stats.size = ctx->total_size;
	stats.count = ctx->total_count;

	if (!ctx->started && stats.run_time_sec) {
		stats.rate_size = __calculate_average(stats.size, stats.run_time_sec);
		stats.rate_count = __calculate_average(stats.count, stats.run_time_sec);
	} else if (ctx->total_count || ctx->total_size) {
		if (status = __current_size(ctx, &current_size))
			return status;
		if (status = __current_count(ctx, &current_count))
			return status;
		stats.rate_size = __calculate_rate(ctx, current_size);
		stats.rate_count = __calculate_rate(ctx, current_count);
	}

	*_stats = stats;
	return status;
}

static void __context_describe(ks_throughput_ctx_t *ctx, char *buffer, ks_size_t buffer_len)
{
	ks_throughput_stats_t stats;
	ks_status_t status;

	ks_spinlock_acquire(&ctx->spin_lock);
	status = __stats(ctx, &stats);
	ks_spinlock_release(&ctx->spin_lock);

	if (status) {
		snprintf(buffer, buffer_len, "KS Throughput: (Failed to render stats: %d)", status);
	} else {
		ks_throughput_stats_render_ex(&stats, "KS Throughput: ", buffer, buffer_len);
	}
}

static ks_status_t __context_init(ks_throughput_ctx_t *ctx, uint32_t max_buckets, uint32_t interval_sec)
{
	if (max_buckets == 0)
		return KS_STATUS_INVALID_ARGUMENT;

	if (!interval_sec)
		interval_sec = 1;

	ctx->interval_sec = interval_sec;
	ctx->max_buckets = max_buckets;
	return KS_STATUS_SUCCESS;
}

static void __context_deinit(ks_throughput_ctx_t *ctx)
{
	ks_pool_free(&ctx->buckets);
}

static ks_status_t __roll_forward(ks_throughput_ctx_t *ctx, uint32_t count)
{
	for (uint32_t pass = 0; pass < count; pass++) {

		/* Advance to the next bucket */
		ks_size_t current_bucket_slot = ctx->next_bucket_slot++;

		/* If we're looping wrap */
		ks_assertd(ctx->next_bucket_slot <= ctx->max_buckets);
		if (ctx->next_bucket_slot == ctx->max_buckets)
			ctx->next_bucket_slot = 0;

		/* Once more bucket if not max yet */
		ks_assertd(ctx->count_bucket <= ctx->max_buckets);
		if (ctx->count_bucket != ctx->max_buckets)
			ctx->count_bucket++;

		/* Now place our current bucket into the current bucket slot */
		ctx->buckets[current_bucket_slot] = ctx->current_bucket;
		ctx->current_bucket = (ks_throughput_bucket_t){0,0};
	}

	return KS_STATUS_SUCCESS;
}

static uint64_t __add_bucket_sizes(ks_throughput_ctx_t *ctx)
{
	uint64_t size = 0;

	for (ks_size_t i = 0; i < ctx->count_bucket; i++) {
		size += ctx->buckets[i].size;
	}

	return size;
}

static uint64_t __add_bucket_counts(ks_throughput_ctx_t *ctx)
{
	uint64_t count = 0;

	for (ks_size_t i = 0; i < ctx->count_bucket; i++) {
		count += ctx->buckets[i].count;
	}

	return count;
}

static ks_status_t __update(ks_throughput_ctx_t *ctx)
{
	ks_status_t status = KS_STATUS_SUCCESS;
	ks_time_t elapsed_time;
	uint32_t elapsed_buckets;

	// If we've been stopped, keep the buckets exactly as they are
	if (!ctx->started)
		return status;

	// Compare that to our last update, and divide that by our interval, thats how many
	// buckets we have to move forward
	elapsed_time = __current_timestamp_sec(ctx) - ctx->last_update_time_sec;
	elapsed_buckets = (uint32_t)elapsed_time / ctx->interval_sec;

	// If we've gone beyond the current one, push as many in as we've elapsed
	if (elapsed_buckets) {
		// We'll progress in fixed chunks of our interval time
		ctx->last_update_time_sec += ctx->interval_sec * elapsed_buckets;

		// Roll our buckets forward x times
		if (status = __roll_forward(ctx, elapsed_buckets))
			return status;
	}

	return status;
}

static void __stop(ks_throughput_ctx_t *ctx)
{
	if (ctx->started) {
		ctx->started = KS_FALSE;
		ctx->stop_time_sec = __current_timestamp_sec(ctx);
	}
}

static ks_status_t __start(ks_throughput_ctx_t *ctx)
{
	ks_status_t status;

	if (ctx->started)
		return KS_STATUS_INVALID_ARGUMENT;

	if (status = __initialize_buckets(ctx))
		return status;

	// Initialize our state
	ctx->start_time_sec = __current_timestamp_sec(ctx);
	ctx->stop_time_sec = 0;
	ctx->started = KS_TRUE;
	ctx->total_size = 0;
	ctx->total_count = 0;
	ctx->last_update_time_sec = ctx->start_time_sec;

	ctx->started = KS_TRUE;
	return status;
}

static ks_time_t __run_time(ks_throughput_ctx_t *ctx)
{
	if (ctx->started)
		return __current_timestamp_sec(ctx) - ctx->start_time_sec;
	else if (ctx->stop_time_sec)
		return ctx->stop_time_sec - ctx->start_time_sec;
	else
		return 0;
}

static ks_status_t __current_count(ks_throughput_ctx_t *ctx, uint64_t *count)
{
	ks_status_t status = KS_STATUS_SUCCESS;

	*count = 0;

	// If we've stopped, return total size
	if (!ctx->started && ctx->stop_time_sec)
		*count = ctx->total_count;
	else {
		// Update while we're here
		if (status = __update(ctx))
			return status;

		// Add up all the buckets sizes
		*count += __add_bucket_counts(ctx);
	}

	return status;
}

static ks_status_t __current_size(ks_throughput_ctx_t *ctx, uint64_t *size)
{
	ks_status_t status = KS_STATUS_SUCCESS;

	*size = 0;

	// If we've stopped, return total size
	if (!ctx->started)
		*size = ctx->total_size;
	else {
		// Update while we're here
		if (status = __update(ctx))
			return status;

		// Add up all the buckets sizes
		*size += __add_bucket_sizes(ctx);
	}

	return status;
}

KS_DECLARE(ks_status_t) ks_throughput_create_ex(
	ks_throughput_t *throughput,
	uint32_t max_buckets,
	uint32_t interval_sec)
{
	KS_HANDLE_ALLOC_TEMPLATE_M(
		NULL,
		KS_HTYPE_THROUGHPUT,
		throughput,
		ks_throughput_ctx_t,
		__context_describe,
		__context_deinit,
		__context_init,
		max_buckets,
		interval_sec);
}

KS_DECLARE(ks_status_t) ks_throughput_create(
	ks_throughput_t *throughput)
{
	return ks_throughput_create_ex(throughput, 32, 1);
}

KS_DECLARE(ks_status_t) ks_throughput_started(ks_throughput_t throughput, ks_bool_t *started)
{
	SCOPE_BEG(throughput, ctx, status)

	ks_spinlock_acquire(&ctx->spin_lock);

	*started = ctx->started;

	ks_spinlock_release(&ctx->spin_lock);

	SCOPE_END(throughput, ctx, status)
}

KS_DECLARE(ks_status_t) ks_throughput_total_size(ks_throughput_t throughput, uint64_t *size)
{
	SCOPE_BEG(throughput, ctx, status)

	ks_spinlock_acquire(&ctx->spin_lock);

	*size = ctx->total_size;

	ks_spinlock_release(&ctx->spin_lock);

	SCOPE_END(throughput, ctx, status)
}

KS_DECLARE(uint64_t) ks_throughput_total_count(ks_throughput_t throughput, uint64_t *count)
{
	SCOPE_BEG(throughput, ctx, status)

	ks_spinlock_acquire(&ctx->spin_lock);

	*count = ctx->total_count;

	ks_spinlock_release(&ctx->spin_lock);

	SCOPE_END(throughput, ctx, status)
}

KS_DECLARE(ks_status_t) ks_throughput_start(ks_throughput_t throughput)
{
	SCOPE_BEG(throughput, ctx, status)

	ks_spinlock_acquire(&ctx->spin_lock);
	status = __start(ctx);
	ks_spinlock_release(&ctx->spin_lock);

	SCOPE_END(throughput, ctx, status)
}

// Stops the virtual timer and sets the final stop time for final
// summary rate calculations.
KS_DECLARE(ks_status_t) ks_throughput_stop(ks_throughput_t throughput)
{
	SCOPE_BEG(throughput, ctx, status)
	ks_spinlock_acquire(&ctx->spin_lock);
	__stop(ctx);
	ks_spinlock_release(&ctx->spin_lock);
	SCOPE_END(throughput, ctx, status)
}

// Stops/starts the virtual rate timer.
KS_DECLARE(ks_status_t) ks_throughput_restart(ks_throughput_t throughput, ks_bool_t *started)
{
	SCOPE_BEG(throughput, ctx, status)

	ks_spinlock_acquire(&ctx->spin_lock);

	__stop(ctx);
	status = __start(ctx);

	ks_spinlock_release(&ctx->spin_lock);
	SCOPE_END(throughput, ctx, status)
}

// Returns the total size processed till now.
KS_DECLARE(ks_status_t) ks_throughput_current_size(ks_throughput_t throughput, uint64_t *current_size)
{
	SCOPE_BEG(throughput, ctx, status)

	uint64_t size = 0;

	ks_spinlock_acquire(&ctx->spin_lock);

	if (!(status = __current_size(ctx, &size))) {
		*current_size = size;
	}

	ks_spinlock_release(&ctx->spin_lock);

	SCOPE_END(throughput, ctx, status)
}

// Returns the total count of processed items till now.
KS_DECLARE(ks_status_t) ks_throughput_current_count(ks_throughput_t throughput, uint64_t *current_count)
{
	SCOPE_BEG(throughput, ctx, status)

	uint64_t count = 0;

	ks_spinlock_acquire(&ctx->spin_lock);

	// If we've stopped, return total count
	if (!ctx->started)
		count = ctx->total_count;
	else {
		// Update while we're here
		if (!(status = __update(ctx))) {
			// Add up all the buckets sizes
			count += __add_bucket_counts(ctx);
		}
	}

	*current_count = count;

	ks_spinlock_release(&ctx->spin_lock);

	SCOPE_END(throughput, ctx, status)
}

// This function is what gets called by the user as they complete work.
// It will populate the current bucket or roll to the next bucket as needed.
KS_DECLARE(ks_status_t) ks_throughput_report_ex(ks_throughput_t throughput, size_t size, ks_bool_t implicit_start)
{
	SCOPE_BEG(throughput, ctx, status)

	ks_spinlock_acquire(&ctx->spin_lock);

	// Start implicitly if need be
	if (!ctx->started) {
		if (implicit_start) {
			if (status = __start(ctx))
				goto done;
		} else {
			status = KS_STATUS_INVALID_ARGUMENT;
			goto done;
		}
	}

	// Roll our windows forward if needed
	if (status = __update(ctx))
		goto done;

	// Update the current bucket
	ctx->current_bucket.count += 1;
	ctx->current_bucket.size += size;

	ctx->total_size += size;
	ctx->total_count += 1;

done:
	ks_spinlock_release(&ctx->spin_lock);

	SCOPE_END(throughput, ctx, status)
}

KS_DECLARE(ks_status_t) ks_throughput_report(ks_throughput_t throughput, size_t size)
{
	return ks_throughput_report_ex(throughput, size, KS_TRUE);
}

// Moves time forward based on the current time distance from
// next interval. Once we cross that threshold we move new buckets
// in front, expiring old ones off the end as we go. This is the 'tick'
// function of this class and its what drives our sliding window forward.
KS_DECLARE(ks_status_t) ks_throughput_update(ks_throughput_t throughput)
{
	SCOPE_BEG(throughput, ctx, status)

	uint32_t elapsed_buckets;
	ks_time_t elapsed_time;

	ks_spinlock_acquire(&ctx->spin_lock);

	// If we've been stopped, keep the buckets exactly as they are
	if (!ctx->started) {
		status = KS_STATUS_FAIL;
		goto done;
	}

	// Compare that to our last update, and divide that by our interval, thats how many
	// buckets we have to move forward
	elapsed_time = __current_timestamp_sec(ctx) - ctx->last_update_time_sec;
	elapsed_buckets = (uint32_t)elapsed_time / ctx->interval_sec;

	// If we've gone beyond the current one, push as many in as we've elapsed
	if (elapsed_buckets) {
		// We'll progress in fixed chunks of our interval time
		ctx->last_update_time_sec += ctx->interval_sec * elapsed_buckets;

		// Roll our buckets forward x times
		__roll_forward(ctx, elapsed_buckets);
	}

done:
	ks_spinlock_release(&ctx->spin_lock);

	SCOPE_END(throughput, ctx, status)
}

// Returns the duration of the total time ran between start and now
// or start and stop.
KS_DECLARE(ks_status_t) ks_throughput_run_time(ks_throughput_t throughput, ks_time_t *run_time_sec)
{
	SCOPE_BEG(throughput, ctx, status)

	ks_spinlock_acquire(&ctx->spin_lock);

	*run_time_sec = __run_time(ctx);

	ks_spinlock_release(&ctx->spin_lock);

	SCOPE_END(throughput, ctx, status)
}

// Returns a summarized stats structure of all possible
// stored statistics, it is with this api that we allow for a
// custom bucket limit, allowing the caller to get stats for
// portions of the window.
KS_DECLARE(ks_status_t) ks_throughput_stats(ks_throughput_t throughput, ks_throughput_stats_t *stats)
{
	SCOPE_BEG(throughput, ctx, status)
	ks_spinlock_acquire(&ctx->spin_lock);
	status = __stats(ctx, stats);
	ks_spinlock_release(&ctx->spin_lock);
	SCOPE_END(throughput, ctx, status)
}

KS_DECLARE(const char *) ks_throughput_stats_render_ex(const ks_throughput_stats_t * const stats, const char *const prefix, char *str, ks_size_t str_len)
{
	char workspace[512] = {0};

	ks_snprintf(str,
		str_len,
		"%s%2.2f/sec:%lu(%s:%s)[%lus]",
		prefix,
		stats->rate_count,
		stats->count,
		ks_human_readable_size_double(
			stats->rate_size,
			1,
			sizeof(workspace),
			workspace),
		ks_human_readable_size(
			stats->size,
			1,
			sizeof(workspace),
			workspace),
		stats->run_time_sec);

	return str;
}

KS_DECLARE(const char *) ks_throughput_stats_render(const ks_throughput_stats_t * const stats, char *str, ks_size_t str_len)
{
	return ks_throughput_stats_render_ex(stats, "", str, str_len);
}
