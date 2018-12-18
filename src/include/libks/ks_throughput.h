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

typedef ks_handle_t ks_throughput_t;

typedef struct ks_throughput_ctx ks_throughput_ctx_t;

/* Stats structure to render the current, or completed summary
 * view of both count and sec rate */
typedef struct ks_throughput_stats_s {
	uint64_t size;
	uint64_t count;

	/* Per/second rates for the total size/count reported in this structure */
	double rate_size;
	double rate_count;

	ks_time_t run_time_sec;
} ks_throughput_stats_t;

KS_DECLARE(ks_status_t) ks_throughput_create_ex(ks_throughput_t *throughput, uint32_t max_buckets, uint32_t interval_ms);
KS_DECLARE(ks_status_t) ks_throughput_create(ks_throughput_t *throughput);
KS_DECLARE(ks_status_t) ks_throughput_started(ks_throughput_t throughput, ks_bool_t *started);
KS_DECLARE(ks_status_t) ks_throughput_total_size(ks_throughput_t throughput, uint64_t *size);
KS_DECLARE(uint64_t) ks_throughput_total_count(ks_throughput_t throughput, uint64_t *count);
KS_DECLARE(ks_status_t) ks_throughput_start(ks_throughput_t throughput);
KS_DECLARE(ks_status_t) ks_throughput_stop(ks_throughput_t throughput);
KS_DECLARE(ks_status_t) ks_throughput_restart(ks_throughput_t throughput, ks_bool_t *started);
KS_DECLARE(ks_status_t) ks_throughput_report_ex(ks_throughput_t throughput, size_t size, ks_bool_t implicit_start);
KS_DECLARE(ks_status_t) ks_throughput_report(ks_throughput_t throughput, size_t size);
KS_DECLARE(ks_status_t) ks_throughput_update(ks_throughput_t throughput);
KS_DECLARE(ks_status_t) ks_throughput_run_time(ks_throughput_t throughput, ks_time_t *run_time_sec);
KS_DECLARE(ks_status_t) ks_throughput_stats(ks_throughput_t throughput, ks_throughput_stats_t *_stats);

KS_DECLARE(const char *) ks_throughput_stats_render(const ks_throughput_stats_t * const stats, char *str, ks_size_t str_len);
KS_DECLARE(const char *) ks_throughput_stats_render_ex(const ks_throughput_stats_t * const stats, const char * const prefix, char *str, ks_size_t str_len);

KS_END_EXTERN_C

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
