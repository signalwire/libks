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

//KS_DECLARE_DATA extern ks_logger_t ks_logger;
KS_DECLARE(void) ks_log(const char *file, const char *func, int line, int level, const char *fmt, ...);

KS_DECLARE(const char *) ks_log_console_color(int level);
KS_DECLARE(ks_size_t) ks_log_format_output(char *buf, ks_size_t bufSize, const char *file, const char *func, int line, int level, const char *fmt, va_list ap);

/*! Gets the log level from a string name, returns -1 if invalid */
KS_DECLARE(int) ks_log_level_by_name(const char *name);
/*! Sets the logger for libks. Default is the null_logger */
KS_DECLARE(void) ks_global_set_logger(ks_logger_t logger);
/*! Sets the default log level for libks */
KS_DECLARE(void) ks_global_set_default_logger(int level);
/*! Sets the default log prefix for libks */
KS_DECLARE(void) ks_global_set_default_logger_prefix(ks_log_prefix_t prefix);
/*! Sets the global console log level */
KS_DECLARE(void) ks_global_set_log_level(int level);
/*! Sets the global file log level */
KS_DECLARE(void) ks_global_set_file_log_level(int level);
/*! Opens the global file log */
KS_DECLARE(ks_bool_t) ks_global_set_file_log_path(const char *path);
/*! Closes the global file log */
KS_DECLARE(void) ks_global_close_file_log(void);
/*! Sanitizes output strings */
KS_DECLARE(void) ks_log_sanitize_string(char *str);

KS_END_EXTERN_C
