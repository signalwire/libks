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

static void null_logger(const char *file, const char *func, int line, int level, const char *fmt, ...)
{
	if (file && func && line && level && fmt) {
		return;
	}
	return;
}

static const char *LEVEL_NAMES[] = {
	"EMERG",
	"ALERT",
	"CRIT",
	"ERROR",
	"WARN",
	"NOTICE",
	"INFO",
	"DEBUG",
	NULL
};

/* A simple spin flag for gating console writes */
static ks_spinlock_t g_log_spin_lock;

#ifdef _WIN32
	#define KNRM  ""
	#define KRED  ""
	#define KGRN  ""
	#define KYEL  ""
	#define KBLU  ""
	#define KMAG  ""
	#define KCYN  ""
	#define KWHT  ""
#else
	#define KNRM  "\x1B[0m"
	#define KRED  "\x1B[31m"
	#define KGRN  "\x1B[32m"
	#define KYEL  "\x1B[33m"
	#define KBLU  "\x1B[34m"
	#define KMAG  "\x1B[35m"
	#define KCYN  "\x1B[36m"
	#define KWHT  "\x1B[37m"
#endif

static const char *LEVEL_COLORS[] = {
	KRED, // EMERG
	KRED, // ALERT
	KRED, // CRIT
	KRED, // ERROR
	KYEL, // WARN
	KWHT, // NOTICE
	KWHT, // INFO
	KCYN, // DEBUG
	NULL
};

static int ks_log_level = 7;
static int ks_file_log_level = 7;
static FILE *ks_file_log_fp = NULL;
static ks_log_prefix_t ks_log_prefix = KS_LOG_PREFIX_DEFAULT;

KS_DECLARE(void) ks_log_sanitize_string(char *str)
{
	unsigned char *ptr, *s = (void*)str;
	while (*s != '\0') {

		// Ignore tabs (json pretty print) and new lines (legacy)
        switch (*s) {
            case '\n':
            case '\t':
                break;
            default:
                if (!isprint((int)*s))
                    *s = '.';
                break;
        }

		s++;
	}
}

KS_DECLARE(int) ks_log_level_by_name(const char *name) {
	int level = -1;
	for (int index = 0; LEVEL_NAMES[index]; ++index) {
		if (!ks_safe_strcasecmp(name, LEVEL_NAMES[index])) {
			level = index;
			break;
		}
	}
	return level;
}

static const char *cut_path(const char *in)
{
	const char *p, *ret = in;
	char delims[] = "/\\";
	char *i;

	for (i = delims; *i; i++) {
		p = in;
		while ((p = strchr(p, *i)) != 0) {
			ret = ++p;
		}
	}
	return ret;
}

static ks_size_t g_wakeup_stdout_fails = 0;
static ks_size_t g_wakeup_stdout_successes = 0;

#ifndef WIN32
static void __set_blocking(int fd, int blocking)
{
	/* Save the current flags */
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags == -1)
		return;
	if (blocking)
		flags &= ~O_NONBLOCK;
	else
		flags |= O_NONBLOCK;
	fcntl(fd, F_SETFL, flags);
}

static ks_bool_t wakeup_stdout()
{
	char procpath[256];
	FILE *fp = NULL;
	fd_set set;
	struct timeval timeout;
	ks_bool_t done_reading = KS_FALSE;
	char buf[1024];
	size_t skipped = 0;
	
	snprintf(procpath, sizeof(procpath), "/proc/%d/fd/%d", getpid(), fileno(stdout));
	if ((fp = fopen(procpath, "r")) == NULL) {
		g_wakeup_stdout_fails++;
		return KS_FALSE;
	}

	timeout.tv_sec = 0;
	timeout.tv_usec = 0;

	while (!done_reading) {
		FD_ZERO(&set);
		FD_SET(fileno(fp), &set);
		
		if (select(fileno(fp) + 1, &set, NULL, NULL, &timeout) != 1) {
			done_reading = KS_TRUE;
		} else {
			size_t consumed = fread(buf, 1, sizeof(buf), fp);
			skipped += consumed;
			done_reading = consumed < sizeof(buf);
		}
	}
	fclose(fp);

	g_wakeup_stdout_successes++;
	return KS_TRUE;
}
#endif

KS_DECLARE(const char *) ks_log_console_color(int level)
{
	if (level < KS_LOG_LEVEL_EMERG || level > KS_LOG_LEVEL_DEBUG) return KNRM;
	return LEVEL_COLORS[level];
}

KS_DECLARE(ks_size_t) ks_log_format_output(char *buf, ks_size_t bufSize, const char *file, const char *func, int line, int level, const char *fmt, va_list ap)
{
	const char *fp;
	char *data = NULL;
	int ret;
	int used = 0;

	fp = cut_path(file);

	ret = ks_vasprintf(&data, fmt, ap);
	ks_log_sanitize_string(data);

	buf[0] = '\0';
	used += 1;

	if (ret != -1) {
		if (g_wakeup_stdout_fails > 0) {
			used += snprintf(buf + used - 1, bufSize - used, "[LF:%zu] ", g_wakeup_stdout_fails);
			if (used >= bufSize) goto done;
		}
		if (g_wakeup_stdout_successes > 0) {
			used += snprintf(buf + used - 1, bufSize - used, "[LS:%zu] ", g_wakeup_stdout_successes);
			if (used >= bufSize) goto done;
		}
		if (ks_log_prefix & KS_LOG_PREFIX_LEVEL) {
			char tbuf[9];
			snprintf(tbuf, sizeof(tbuf), "[%s]", LEVEL_NAMES[level]);
			used += snprintf(buf + used - 1, bufSize - used, "%8s ", tbuf);
			if (used >= bufSize) goto done;
		}
		if (ks_log_prefix & (KS_LOG_PREFIX_DATE | KS_LOG_PREFIX_TIME)) {
			char tbuf[100];
			time_t now = time(0);
			struct tm nowtm;

#ifdef WIN32
			localtime_s(&nowtm, &now);
#else
			localtime_r(&now, &nowtm);
#endif

			if (ks_log_prefix & KS_LOG_PREFIX_DATE) {
				strftime(tbuf, sizeof(tbuf), "%Y-%m-%d", &nowtm);
				used += snprintf(buf + used - 1, bufSize - used, "%s ", tbuf);
				if (used >= bufSize) goto done;
			}
			if (ks_log_prefix & KS_LOG_PREFIX_TIME) {
				strftime(tbuf, sizeof(tbuf), "%H:%M:%S", &nowtm);
				used += snprintf(buf + used - 1, bufSize - used, "%s ", tbuf);
				if (used >= bufSize) goto done;
			}
		}
		if (ks_log_prefix & KS_LOG_PREFIX_THREAD) {
			uint32_t id = (uint32_t)ks_thread_self_id();
#ifdef __GNU__
			id = (uint32_t)syscall(SYS_gettid);
#endif
			used += snprintf(buf + used - 1, bufSize - used, "#%8.8X ", id);
			if (used >= bufSize) goto done;
		}
		if (ks_log_prefix & KS_LOG_PREFIX_FILE) {
			used += snprintf(buf + used - 1, bufSize - used, "%32.32s", fp);
			if (ks_log_prefix & KS_LOG_PREFIX_LINE) {
				used += snprintf(buf + used - 1, bufSize - used, ":%-5d", line);
				if (used >= bufSize) goto done;
			}
			used += snprintf(buf + used - 1, bufSize - used, " ");
			if (used >= bufSize) goto done;
		}
		if (ks_log_prefix & KS_LOG_PREFIX_FUNC) {
			used += snprintf(buf + used - 1, bufSize - used, "%-48.48s ", func);
			if (used >= bufSize) goto done;
		}

		used += snprintf(buf + used - 1, bufSize - used, "%s", data);

done:
		// Cap if snprintf exceeded its bounds (will return what may have been printed, not
		// the result of the cap)
		if (used >= (bufSize - 1)) {
			// reserve 1 character for newline if needed
			used = bufSize - 1;
			// reterminate
			buf[used - 1] = '\0';
		}

		if (used >= 2) {
			if (buf[used - 2] != '\n') {
				buf[used - 1] = '\n';
				buf[used] = '\0';
				++used;
			}
		}
	}
	if (data) free(data);
	return used - 1;
}

static void default_logger(const char *file, const char *func, int line, int level, const char *fmt, ...)
{
	va_list ap;
	char buf[32768];
	ks_size_t prefixlen;
	ks_size_t suffixlen;
	ks_size_t len;
	ks_bool_t toconsole = KS_FALSE;
	ks_bool_t tofile = KS_FALSE;

	if (level < 0 || level > 7) {
		level = 7;
	}
    toconsole = level <= ks_log_level;
	tofile = ks_file_log_fp && level <= ks_file_log_level;
	if (!toconsole && !tofile) return;
	
	va_start(ap, fmt);

	// Prefix the buffer with the color
	strcpy(buf, LEVEL_COLORS[level]);
	prefixlen = strlen(buf);
	suffixlen = strlen(KNRM);

	// Add to the buffer after the color, save space for color reset
	len = ks_log_format_output(buf + prefixlen, sizeof(buf) - prefixlen - suffixlen, file, func, line, level, fmt, ap);

	if (len > 0) {
		ks_spinlock_acquire(&g_log_spin_lock);
		if (toconsole) {
			ks_size_t total = prefixlen + len + suffixlen;
			strcpy(buf + prefixlen + len, KNRM);
		
			//fprintf(stdout, "[%s] %s:%d %s() %s", LEVEL_NAMES[level], fp, line, func, data);
#if KS_PLAT_WIN
			printf("%s", buf);			// Comment out and leave OutputDebugStringA on to catch timing related issues
			// (console is much slower then OutputDebugStringA on windows)
			OutputDebugStringA(buf);

#else
			{
				ks_bool_t done = KS_FALSE;
				ks_bool_t wakeup = KS_FALSE;
				ks_size_t written = 0;

				__set_blocking(fileno(stdout), KS_FALSE);
			
				while (!done) {
					ks_size_t thisWrite = fwrite(buf + written, 1, total - written, stdout);
					if (thisWrite > 0) {
						written += thisWrite;
						if (written == total) done = KS_TRUE;
					} else {
						if (wakeup) {
							// already tried to wakeup once before, skip logging... yuck
							break;
						}
						if (!wakeup_stdout()) {
							// couldn't wake up stdout? skip logging... yuck
							break;
						}
						// consumed stdout, try writing again
						wakeup = KS_TRUE;
					}
				}
				
				__set_blocking(fileno(stdout), KS_TRUE);
			}
#endif
		}
		if (tofile) {
			ks_bool_t done = KS_FALSE;
			ks_size_t written = 0;

			while (!done) {
				ks_size_t thisWrite = fwrite(buf + prefixlen + written, 1, len - written, ks_file_log_fp);
				if (thisWrite > 0) {
					written += thisWrite;
					if (written == len) done = KS_TRUE;
					if (fflush(ks_file_log_fp)) {
						// file log cannot be flushed, stream is no longer valid?
						fclose(ks_file_log_fp);
						ks_file_log_fp = NULL;
					}
				} else {
					// file log cannot be written to, skip logging... yuck
					break;
				}
			}
		}
		ks_spinlock_release(&g_log_spin_lock);
	}

	va_end(ap);
}

ks_logger_t ks_logger = null_logger;

KS_DECLARE(void) ks_global_set_logger(ks_logger_t logger)
{
	if (logger) {
		ks_logger = logger;
	} else {
		ks_logger = null_logger;
	}
}

KS_DECLARE(void) ks_global_set_default_logger(int level)
{
	if (level < 0 || level > 7) {
		level = 7;
	}

	ks_logger = default_logger;
	ks_log_level = level;
}

KS_DECLARE(void) ks_global_set_default_logger_prefix(ks_log_prefix_t prefix)
{
	ks_log_prefix = prefix;
}

KS_DECLARE(void) ks_global_set_log_level(int level)
{
  ks_log_level = level;
  if (ks_logger == null_logger) {
	  ks_logger = default_logger;
  }
}

KS_DECLARE(void) ks_global_set_file_log_level(int level)
{
	ks_file_log_level = level;
	if (ks_logger == null_logger) {
		ks_logger = default_logger;
	}
}

KS_DECLARE(ks_bool_t) ks_global_set_file_log_path(const char *path)
{
	if (ks_file_log_fp) fclose(ks_file_log_fp);
	ks_file_log_fp = fopen(path, "w");
	if (!ks_file_log_fp) return KS_FALSE;
	if (ks_logger == null_logger) {
		ks_logger = default_logger;
	}
	return KS_TRUE;
}

KS_DECLARE(void) ks_global_close_file_log(void)
{
	if (ks_file_log_fp) fclose(ks_file_log_fp);
}

KS_DECLARE(void) ks_log(const char *file, const char *func, int line, int level, const char *fmt, ...)
{
	char *data;
	va_list ap;

	if (!ks_logger) return;

	va_start(ap, fmt);

	if (ks_vasprintf(&data, fmt, ap) != -1) {
		ks_logger(file, func, line, level, "%s", data);
		//fprintf(stdout, "[%s] %s:%d %s() %s", LEVEL_NAMES[level], fp, line, func, data);
		//fprintf(stdout, "%s", buf);
		free(data);
	}

	va_end(ap);
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
