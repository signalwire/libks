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

KS_DECLARE(int) ks_config_open_file(ks_config_t *cfg, const char *file_path)
{
	FILE *f;
	const char *path = NULL;
	char path_buf[1024];

	if (file_path[0] == '/') {
		path = file_path;
	} else {
		ks_snprintf(path_buf, sizeof(path_buf), "%s%s%s", KS_CONFIG_DIR, KS_PATH_SEPARATOR, file_path);
		path = path_buf;
	}

	if (!path) {
		return 0;
	}

	memset(cfg, 0, sizeof(*cfg));
	cfg->lockto = -1;
	ks_log(KS_LOG_DEBUG, "Configuration file is %s.\n", path);
	f = fopen(path, "r");

	if (!f) {
		if (file_path[0] != '/') {
			int last = -1;
			char *var, *val;

			ks_snprintf(path_buf, sizeof(path_buf), "%s%sopenks.conf", KS_CONFIG_DIR, KS_PATH_SEPARATOR);
			path = path_buf;

			if ((f = fopen(path, "r")) == 0) {
				return 0;
			}

			cfg->file = f;
			ks_set_string(cfg->path, path);

			while (ks_config_next_pair(cfg, &var, &val)) {
				if ((cfg->sectno != last) && !strcmp(cfg->section, file_path)) {
					cfg->lockto = cfg->sectno;
					return 1;
				}
			}

			ks_config_close_file(cfg);
			memset(cfg, 0, sizeof(*cfg));
			return 0;
		}

		return 0;
	} else {
		cfg->file = f;
		ks_set_string(cfg->path, path);
		return 1;
	}
}

KS_DECLARE(void) ks_config_close_file(ks_config_t *cfg)
{

	if (cfg->file) {
		fclose(cfg->file);
	}

	memset(cfg, 0, sizeof(*cfg));
}



KS_DECLARE(int) ks_config_next_pair(ks_config_t *cfg, char **var, char **val)
{
	int ret = 0;
	char *p, *end;

	*var = *val = NULL;

	if (!cfg || !cfg->file) {
		return 0;
	}

	for (;;) {
		cfg->lineno++;

		if (!fgets(cfg->buf, sizeof(cfg->buf), cfg->file)) {
			ret = 0;
			break;
		}
		*var = cfg->buf;

		if (**var == '[' && (end = strchr(*var, ']')) != 0) {
			*end = '\0';
			(*var)++;
			if (**var == '+') {
				(*var)++;
				ks_copy_string(cfg->section, *var, sizeof(cfg->section));
				cfg->sectno++;

				if (cfg->lockto > -1 && cfg->sectno != cfg->lockto) {
					break;
				}
				cfg->catno = 0;
				cfg->lineno = 0;
				*var = (char *) "";
				*val = (char *) "";
				return 1;
			} else {
				ks_copy_string(cfg->category, *var, sizeof(cfg->category));
				cfg->catno++;
			}
			continue;
		}



		if (**var == '#' || **var == ';' || **var == '\n' || **var == '\r') {
			continue;
		}

		if (!strncmp(*var, "__END__", 7)) {
			break;
		}


		if ((end = strchr(*var, ';')) && *(end + 1) == *end) {
			*end = '\0';
			end--;
		} else if ((end = strchr(*var, '\n')) != 0) {
			if (*(end - 1) == '\r') {
				end--;
			}
			*end = '\0';
		}

		p = *var;
		while ((*p == ' ' || *p == '\t') && p != end) {
			*p = '\0';
			p++;
		}
		*var = p;


		if ((*val = strchr(*var, '=')) == 0) {
			ret = -1;
			/* log_printf(0, server.log, "Invalid syntax on %s: line %d\n", cfg->path, cfg->lineno); */
			continue;
		} else {
			p = *val - 1;
			*(*val) = '\0';
			(*val)++;
			if (*(*val) == '>') {
				*(*val) = '\0';
				(*val)++;
			}

			while ((*p == ' ' || *p == '\t') && p != *var) {
				*p = '\0';
				p--;
			}

			p = *val;
			while ((*p == ' ' || *p == '\t') && p != end) {
				*p = '\0';
				p++;
			}
			*val = p;
			ret = 1;
			break;
		}
	}


	return ret;

}

KS_DECLARE(int) ks_config_get_cas_bits(char *strvalue, unsigned char *outbits)
{
	char cas_bits[5];
	unsigned char bit = 0x8;
	char *double_colon = strchr(strvalue, ':');
	int x = 0;

	if (!double_colon) {
		ks_log(KS_LOG_ERROR, "No CAS bits specified: %s, :xxxx definition expected, where x is 1 or 0\n", double_colon);
		return -1;
	}

	double_colon++;
	*outbits = 0;
	cas_bits[4] = 0;

	if (sscanf(double_colon, "%c%c%c%c", &cas_bits[0], &cas_bits[1], &cas_bits[2], &cas_bits[3]) != 4) {
		ks_log(KS_LOG_ERROR, "Invalid CAS bits specified: %s, :xxxx definition expected, where x is 1 or 0\n", double_colon);
		return -1;
	}

	ks_log(KS_LOG_DEBUG, "CAS bits specification found: %s\n", cas_bits);

	for (; cas_bits[x]; x++) {
		if ('1' == cas_bits[x]) {
			*outbits |= bit;
		} else if ('0' != cas_bits[x]) {
			ks_log(KS_LOG_ERROR, "Invalid CAS pattern specified: %s, just 0 or 1 allowed for each bit\n", cas_bits);
			return -1;
		}
		bit >>= 1;
	}
	return 0;
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
