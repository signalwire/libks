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

#define MAX_FAKTORY_COMMAND_LEN 256

#define FAKTORY_DEBUG

struct ks_faktory_s {
	ks_mutex_t *mutex;

	ks_faktory_state_t state;
	ks_faktory_state_callback_t state_callback;

	ks_sockaddr_t server;
	ks_bool_t server_set;

	ks_socket_t sock;

	uint8_t *inbuf;
	ks_size_t inbufsize;
	ks_size_t inbufused;

	uint8_t *outbuf;
	ks_size_t outbufsize;
	ks_size_t outbufused;

	ks_q_t *outq;
	ks_bool_t producer;

	ks_bool_t awaiting_response;
	ks_faktory_command_callback_t command_callback;
	void *command_data;

	ks_bool_t autoack;
	char *password;
	char *hostname;
	char *wid;
	ks_json_t *labels;

	ks_thread_t *thread;

	ks_cond_t *cond;

	ks_time_t last_beat;

};

struct ks_faktory_command_s {
	ks_faktory_t *faktory;
	char *message;
	ks_size_t messagelen;
	void *data;
	ks_faktory_command_callback_t callback;
};
typedef struct ks_faktory_command_s ks_faktory_command_t;

struct ks_faktory_fetch_data_s {
	ks_faktory_command_callback_t callback;
	void *data;
};
typedef struct ks_faktory_fetch_data_s ks_faktory_fetch_data_t;

static void ks_faktory_state_set(ks_faktory_t *faktory, ks_faktory_state_t state);
static void *ks_faktory_thread(ks_thread_t *thread, void *data);

ks_status_t ks_faktory_command_create(ks_faktory_command_t **commandP, ks_faktory_t *faktory, const char *cmd, ks_json_t *args, ks_faktory_command_callback_t callback, void *data);
ks_status_t ks_faktory_command_destroy(ks_faktory_command_t **commandP);


static void ks_faktory_cleanup(void *ptr, void *arg, ks_pool_cleanup_action_t action, ks_pool_cleanup_type_t type)
{
	ks_faktory_t *faktory = (ks_faktory_t *)ptr;

	switch (action) {
	case KS_MPCL_ANNOUNCE:
		break;
	case KS_MPCL_TEARDOWN:
		ks_json_delete(&faktory->labels);
		break;
	case KS_MPCL_DESTROY:
		break;
	}
}

KS_DECLARE(ks_status_t) ks_faktory_create(ks_faktory_t **faktoryP, ks_size_t inbufsize, ks_size_t outbufsize, ks_size_t outqmax, ks_bool_t producer)
{
	ks_pool_t *pool = NULL;
	ks_faktory_t *faktory = NULL;

	ks_assert(faktoryP);
	ks_assert(inbufsize >= 512);
	ks_assert(outbufsize >= 512);
	ks_assert(outqmax >= 32);

	ks_pool_open(&pool);

	faktory = ks_pool_alloc(pool, sizeof(ks_faktory_t));
	ks_assert(faktory);

	ks_mutex_create(&faktory->mutex, KS_MUTEX_FLAG_DEFAULT, pool);
	ks_assert(faktory->mutex);

	faktory->state = KS_FAKTORY_STATE_CONNECTING;
	faktory->server_set = KS_FALSE;

	faktory->sock = KS_SOCK_INVALID;

	faktory->inbuf = (uint8_t *)ks_pool_alloc(pool, inbufsize);
	ks_assert(faktory->inbuf);

	faktory->inbufsize = inbufsize - 1; // 1 bytes reserved for NULL
	faktory->inbufused = 0;

	faktory->outbuf = (uint8_t *)ks_pool_alloc(pool, outbufsize);
	ks_assert(faktory->outbuf);

	faktory->outbufsize = outbufsize;
	faktory->outbufused = 0;

	ks_q_create(&faktory->outq, pool, outqmax);
	ks_assert(faktory->outq);

	faktory->producer = producer;
	faktory->awaiting_response = KS_FALSE;
	faktory->command_callback = NULL;
	faktory->command_data = NULL;

	faktory->labels = ks_json_create_array();

	ks_cond_create(&faktory->cond, pool);

	ks_thread_create_ex(&faktory->thread, ks_faktory_thread, faktory, KS_THREAD_FLAG_DETACHED, (1024 * 256), KS_PRI_NORMAL, pool);

	ks_pool_set_cleanup(faktory, NULL, ks_faktory_cleanup);

	*faktoryP = faktory;

#ifdef FAKTORY_DEBUG
	ks_log(KS_LOG_DEBUG, "Created %s @ 0x%x\n", producer ? "Producer" : "Consumer", faktory);
#endif

	return KS_STATUS_SUCCESS;
}

KS_DECLARE(ks_status_t) ks_faktory_destroy(ks_faktory_t **faktoryP)
{
	ks_faktory_t *faktory = NULL;
	ks_pool_t *pool = NULL;
#ifdef FAKTORY_DEBUG
	ks_bool_t producer = KS_FALSE;
#endif

	ks_assert(faktoryP);
	ks_assert(*faktoryP);

	faktory = *faktoryP;
#ifdef FAKTORY_DEBUG
	producer = faktory->producer;
#endif

	ks_mutex_lock(faktory->mutex);
	if (faktory->state != KS_FAKTORY_STATE_SHUTDOWN && faktory->state != KS_FAKTORY_STATE_CLEANUP)
		ks_faktory_state_set(faktory, KS_FAKTORY_STATE_SHUTDOWN);
	ks_mutex_unlock(faktory->mutex);
	while (faktory->state != KS_FAKTORY_STATE_CLEANUP) ks_sleep_ms(1);

	pool = ks_pool_get(*faktoryP);
	ks_pool_close(&pool);

	*faktoryP = NULL;

#ifdef FAKTORY_DEBUG
	ks_log(KS_LOG_DEBUG, "Destroyed %s @ 0x%x\n", producer ? "Producer" : "Consumer", faktory);
#endif

	return KS_STATUS_SUCCESS;
}

KS_DECLARE(const char *) ks_faktory_state_string(ks_faktory_state_t state)
{
	switch (state) {
	case KS_FAKTORY_STATE_NONE: return "NONE";
	case KS_FAKTORY_STATE_CONNECTING: return "CONNECTING";
	case KS_FAKTORY_STATE_UNIDENTIFIED: return "UNIDENTIFIED";
	case KS_FAKTORY_STATE_IDENTIFIED: return "IDENTIFIED";
	case KS_FAKTORY_STATE_QUIET: return "QUIET";
	case KS_FAKTORY_STATE_TERMINATE: return "TERMINATE";
	case KS_FAKTORY_STATE_END: return "END";
	case KS_FAKTORY_STATE_SHUTDOWN: return "SHUTDOWN";
	case KS_FAKTORY_STATE_CLEANUP: return "CLEANUP";
	default: break;
	}
	return "UNKNOWN";
}

KS_DECLARE(ks_faktory_state_t) ks_faktory_state_get(ks_faktory_t *faktory)
{
	ks_assert(faktory);

	return faktory->state;
}

static void ks_faktory_state_set(ks_faktory_t *faktory, ks_faktory_state_t state)
{
	ks_assert(faktory);

	ks_mutex_lock(faktory->mutex);
	if (faktory->state != state) {
#ifdef FAKTORY_DEBUG
		ks_log(KS_LOG_DEBUG, "%s State Changed: %s to %s\n", faktory->producer ? "Producer" : "Consumer", ks_faktory_state_string(faktory->state), ks_faktory_state_string(state));
#endif
		faktory->state = state;
		if (faktory->state_callback) faktory->state_callback(faktory, faktory->state);
	}
	ks_mutex_unlock(faktory->mutex);
}

KS_DECLARE(ks_bool_t) ks_faktory_ready(ks_faktory_t *faktory)
{
	return faktory->state == KS_FAKTORY_STATE_IDENTIFIED;
}

KS_DECLARE(void) ks_faktory_state_handler_set(ks_faktory_t *faktory, ks_faktory_state_callback_t callback)
{
	faktory->state_callback = callback;
}

KS_DECLARE(void) ks_faktory_autoack_set(ks_faktory_t *faktory, ks_bool_t enable)
{
	ks_assert(faktory);
	ks_assert(!faktory->producer);

	faktory->autoack = enable;
#ifdef FAKTORY_DEBUG
	ks_log(KS_LOG_DEBUG, "Consumer AutoACK %s\n", enable ? "Enabled" : "Disabled");
#endif
}

KS_DECLARE(void) ks_faktory_password_set(ks_faktory_t *faktory, const char *password)
{
	ks_assert(faktory);

	if (faktory->password) ks_pool_free(&faktory->password);
	if (password) faktory->password = ks_pstrdup(ks_pool_get(faktory), password);

#ifdef FAKTORY_DEBUG
	ks_log(KS_LOG_DEBUG, "Consumer Identification Hostname: %s\n", faktory->hostname);
#endif
}

KS_DECLARE(void) ks_faktory_hostname_set(ks_faktory_t *faktory, const char *hostname)
{
	ks_assert(faktory);
	ks_assert(hostname);
	ks_assert(!faktory->producer);

	if (faktory->hostname) ks_pool_free(&faktory->hostname);
	faktory->hostname = ks_pstrdup(ks_pool_get(faktory), hostname);

#ifdef FAKTORY_DEBUG
	ks_log(KS_LOG_DEBUG, "Consumer Identification Hostname: %s\n", faktory->hostname);
#endif
}

KS_DECLARE(const char *) ks_faktory_wid_get(ks_faktory_t *faktory)
{
	ks_assert(faktory);
	return faktory->wid;
}

KS_DECLARE(void) ks_faktory_wid_set(ks_faktory_t *faktory, const char *wid)
{
	ks_assert(faktory);
	ks_assert(wid);
	ks_assert(!faktory->producer);

	if (faktory->wid) ks_pool_free(&faktory->wid);
	faktory->wid = ks_pstrdup(ks_pool_get(faktory), wid);

#ifdef FAKTORY_DEBUG
	ks_log(KS_LOG_DEBUG, "Consumer Identification WID: %s\n", faktory->wid);
#endif
}

KS_DECLARE(void) ks_faktory_label_add(ks_faktory_t *faktory, const char *label)
{
	ks_assert(faktory);
	ks_assert(label);
	ks_assert(!faktory->producer);

	ks_json_add_item_to_array(faktory->labels, ks_json_create_string(label));

#ifdef FAKTORY_DEBUG
	ks_log(KS_LOG_DEBUG, "Consumer Identification Label Added: %s\n", label);
#endif
}

KS_DECLARE(void) ks_faktory_label_remove(ks_faktory_t *faktory, const char *label)
{
	int len = 0;

	ks_assert(faktory);
	ks_assert(label);
	ks_assert(!faktory->producer);

	len = ks_json_get_array_size(faktory->labels);
	for (int index = 0; index < len; ++index) {
		ks_json_t *item = ks_json_get_array_item(faktory->labels, index);
		if (!strcmp(ks_json_value_string(item), label)) {
#ifdef FAKTORY_DEBUG
			ks_log(KS_LOG_DEBUG, "Consumer Identification Label Removed: %s\n", label);
#endif
			ks_json_delete_item_from_array(faktory->labels, index);
			break;
		}
	}
}

KS_DECLARE(ks_status_t) ks_faktory_connect(ks_faktory_t *faktory, const char *host, ks_port_t port)
{
	ks_status_t ret = KS_STATUS_SUCCESS;

	ks_assert(faktory);

	ks_mutex_lock(faktory->mutex);

	if ((ret = ks_addr_getbyname(host, port, AF_INET, &faktory->server)) == KS_STATUS_SUCCESS) {
#ifdef FAKTORY_DEBUG
		ks_log(KS_LOG_DEBUG, "%s Using Server: %s on port %u\n", faktory->producer ? "Producer" : "Consumer", ks_addr_get_host(&faktory->server), ks_addr_get_port(&faktory->server));
#endif
		faktory->server_set = KS_TRUE;
	}

	ks_mutex_unlock(faktory->mutex);

	return ret;
}

KS_DECLARE(void) ks_faktory_reset(ks_faktory_t *faktory, ks_bool_t clearq)
{
	ks_assert(faktory);

#ifdef FAKTORY_DEBUG
	ks_log(KS_LOG_DEBUG, "Resetting %s @ 0x%x\n", faktory->producer ? "Producer" : "Consumer", faktory);
#endif

	ks_mutex_lock(faktory->mutex);
	if (faktory->sock != KS_SOCK_INVALID) ks_socket_close(&faktory->sock);
	faktory->inbufused = 0;
	faktory->outbufused = 0;
	faktory->awaiting_response = KS_FALSE;
	if (clearq) {
		ks_faktory_command_t *command = NULL;
		while (ks_q_pop(faktory->outq, (void **)&command) == KS_STATUS_SUCCESS) ks_faktory_command_destroy(&command);
	}
	ks_faktory_state_set(faktory, KS_FAKTORY_STATE_CONNECTING);
	ks_mutex_unlock(faktory->mutex);
}

KS_DECLARE(ks_status_t) ks_faktory_send(ks_faktory_t *faktory, const char *cmd, ks_json_t *args, ks_faktory_command_callback_t callback, void *data)
{
	ks_status_t ret = KS_STATUS_SUCCESS;
	ks_faktory_command_t *command = NULL;

	ks_assert(faktory);
	ks_assert(cmd);

	ks_faktory_command_create(&command, faktory, cmd, args, callback, data);
#ifdef FAKTORY_DEBUG
	ks_log(KS_LOG_DEBUG, "%s Queueing Command @ 0x%x, %s\n", faktory->producer ? "Producer" : "Consumer", command, cmd);
#endif
	if ((ret = ks_q_trypush(faktory->outq, (void *)command)) == KS_STATUS_SUCCESS) ks_cond_signal(faktory->cond);
	return ret;
}

KS_DECLARE(ks_status_t) ks_faktory_push(ks_faktory_t *faktory, const char *jobtype, ks_json_t *args, const char *queue, int priority, ks_json_t *custom, const char **jidP, ks_faktory_command_callback_t callback, void *data)
{
	ks_status_t ret = KS_STATUS_SUCCESS;
	ks_json_t *unit = NULL;
	ks_uuid_t uuid;
	const char *jid = NULL;

	ks_assert(faktory);
	ks_assert(jobtype);
	ks_assert(priority >= 0 && priority <= 9);
	if (args) ks_assert(ks_json_type_is_array(args));

	unit = ks_json_create_object();
	ks_uuid(&uuid);
	jid = ks_uuid_str(ks_pool_get(faktory), &uuid);

	ks_json_add_string_to_object(unit, "jid", jid);
	ks_json_add_string_to_object(unit, "jobtype", jobtype);
	if (args) ks_json_add_item_to_object(unit, "args", ks_json_duplicate(args, 1));
	else ks_json_add_item_to_object(unit, "args", ks_json_create_array());
	if (queue) ks_json_add_string_to_object(unit, "queue", queue);
	if (priority > 0) ks_json_add_number_to_object(unit, "priority", priority);
	if (custom) ks_json_add_item_to_object(unit, "custom", ks_json_duplicate(custom, 1));

	if (jidP) *jidP = jid;
	else ks_pool_free(&jid);

	ret = ks_faktory_send(faktory, "PUSH", unit, callback, data);
	ks_json_delete(&unit);

	return ret;
}

void ks_faktory_fetch_handler(ks_faktory_t *faktory, respObject *resp, void *data)
{
	ks_faktory_fetch_data_t *fdata = (ks_faktory_fetch_data_t *)data;

	ks_assert(faktory);
	ks_assert(resp);
	ks_assert(data);

	if (fdata->callback) fdata->callback(faktory, resp, fdata->data);
	if (faktory->autoack && resp->type == RESP_OBJECT_BINARY && resp->str && resp->str[0] == '{') {
		const char *jid = NULL;
		ks_json_t *json = ks_json_parse((const char *)resp->str);

		ks_assert(json);

		jid = ks_json_get_object_cstr(json, "jid");

#ifdef FAKTORY_DEBUG
		ks_log(KS_LOG_DEBUG, "%s AutoACKing: %s\n", faktory->producer ? "Producer" : "Consumer", jid);
#endif

		ks_faktory_ack(faktory, jid, NULL, NULL);

		ks_json_delete(&json);
	}
	ks_pool_free(&fdata);
}

KS_DECLARE(ks_status_t) ks_faktory_fetch(ks_faktory_t *faktory, const char *queues, ks_faktory_command_callback_t callback, void *data)
{
	char cmd[MAX_FAKTORY_COMMAND_LEN];
	ks_faktory_fetch_data_t *fdata = NULL;

	ks_assert(faktory);

	fdata = ks_pool_alloc(ks_pool_get(faktory), sizeof(ks_faktory_fetch_data_t));
	fdata->callback = callback;
	fdata->data = data;

	snprintf(cmd, MAX_FAKTORY_COMMAND_LEN, "FETCH %s", queues ? queues : "default");
	return ks_faktory_send(faktory, cmd, NULL, ks_faktory_fetch_handler, fdata);
}

KS_DECLARE(ks_status_t) ks_faktory_ack(ks_faktory_t *faktory, const char *jid, ks_faktory_command_callback_t callback, void *data)
{
	ks_status_t ret = KS_STATUS_SUCCESS;
	ks_json_t *ack = ks_json_create_object();

	ks_assert(faktory);
	ks_assert(jid);

	ks_assert(ack);

	ks_json_add_string_to_object(ack, "jid", jid);

	ret = ks_faktory_send(faktory, "ACK", ack, callback, data);
	ks_json_delete(&ack);

	return ret;
}

KS_DECLARE(ks_status_t) ks_faktory_fail(ks_faktory_t *faktory, const char *jid, const char *errtype, const char *message, ks_faktory_command_callback_t callback, void *data)
{
	ks_status_t ret = KS_STATUS_SUCCESS;
	ks_json_t *fail = ks_json_create_object();

	ks_assert(faktory);
	ks_assert(jid);
	ks_assert(errtype);
	ks_assert(message);

	ks_assert(fail);

	ks_json_add_string_to_object(fail, "jid", jid);
	ks_json_add_string_to_object(fail, "errtype", errtype);
	ks_json_add_string_to_object(fail, "message", message);
	ks_json_add_item_to_object(fail, "backtrace", ks_json_create_array());

	ret = ks_faktory_send(faktory, "FAIL", fail, callback, data);
	ks_json_delete(&fail);

	return ret;
}

ks_bool_t ks_faktory_process(ks_faktory_t *faktory, respObject **resp)
{
	ks_bool_t ret = KS_FALSE;

	if (faktory->inbufused > 0) {
		int eol = respDecode(resp, faktory->inbuf, (int)faktory->inbufused);

#ifdef FAKTORY_DEBUG
		ks_log(KS_LOG_DEBUG, "%s Decoding Data: %u total, %d eol\n", faktory->producer ? "Producer" : "Consumer", faktory->inbufused, eol);
#endif

		if (eol >= 0 && resp && *resp) {
			if ((*resp)->elements) {
				for (unsigned int i = 0; i < (*resp)->elements; i++) {
					respObject *r = (*resp)->element[i];
					ks_assert(r);
#ifdef FAKTORY_DEBUG
					ks_log(KS_LOG_DEBUG, "%s Received ARRAY Message: %s\n", faktory->producer ? "Producer" : "Consumer", (char *)(*resp)->str);
#endif
					// @note this should probably be handled by the response handler when a response contains an array, check for it in appropriate response handlers
					// as the only likely scenario for this is if a FETCH can return multiple jobs in a single array somehow, otherwise all actions are syncronous
				}
			} else {
#ifdef FAKTORY_DEBUG
				ks_log(KS_LOG_DEBUG, "%s Received Message: %s\n", faktory->producer ? "Producer" : "Consumer", (char *)(*resp)->str);
#endif
			}

			ks_assert(eol <= (int)faktory->inbufused);

			faktory->inbufused -= eol;
			ret = KS_TRUE;
			if (faktory->inbufused > 0) memcpy((void *)faktory->inbuf, (void *)(faktory->inbuf + eol), faktory->inbufused);
		} else if (eol == RESP_ERROR_INCOMPLETE_MESSAGE) {
			// never mind, waiting for \r\n
		} else {
#ifdef FAKTORY_DEBUG
			ks_log(KS_LOG_DEBUG, "eol: %d flushing inbuf [%s]\n", eol, (char *)faktory->inbuf);
#endif
			faktory->inbufused = 0;
			//faktory->awaiting_response = KS_FALSE;
		}
	}

	return ret;
}

const char *ks_faktory_parseto(const char *input, char *arg, char term)
{
	const char *tmp = NULL;
	size_t len = 0;
	ks_bool_t found = KS_FALSE;

	for (tmp = input; *tmp; ++tmp) {
		if (*tmp == term) {
			found = KS_TRUE;
			break;
		}
	}

	len = (tmp - input);
	if (len >= MAX_FAKTORY_COMMAND_LEN) {
		len = MAX_FAKTORY_COMMAND_LEN - 1;
	}

	if (len > 0) memcpy(arg, input, len);
	arg[len] = '\0';
	if (found) return input + len + 1;
	return input + len;
}

void ks_faktory_process_unidentified(ks_faktory_t *faktory, respObject *resp)
{
	const char *args = NULL;
	char cmd[MAX_FAKTORY_COMMAND_LEN];

#ifdef FAKTORY_DEBUG
	ks_log(KS_LOG_DEBUG, "%s State %s Processing Message: %s\n", faktory->producer ? "Producer" : "Consumer", ks_faktory_state_string(faktory->state), (char *)resp->str);
#endif

	faktory->awaiting_response = KS_FALSE;

	if (!resp->str) return;

	args = ks_faktory_parseto((const char *)resp->str, cmd, ' ');
	if (strncmp(cmd, "HI", 2) == 0) {
		ks_json_t *json = ks_json_parse(args);
		ks_json_t *v = ks_json_get_object_item(json, "v");
		ks_json_t *s = ks_json_get_object_item(json, "s");
		const char *salt = NULL;
		ks_json_t *i = ks_json_get_object_item(json, "i");
		int iterations = 1;
		ks_json_t *params = NULL;

		if (!v || ks_json_type_is_number(v) || ks_json_value_number_int(v) != 2) {
			// @todo local callback for identification failing, and start disconnecting faktory client
			// this is the only case that can produce an error that may require a unilateral disconnect
			// without further TERMINATING/END state transition or transmissions
			ks_log(KS_LOG_ERROR, "%s Received 'HI' with missing or unexpected 'v'\n", faktory->producer ? "Producer" : "Consumer");
			ks_json_delete(&json);
			ks_faktory_state_set(faktory, KS_FAKTORY_STATE_SHUTDOWN);
			return;
		}

		if (s) {
			if (!ks_json_type_is_string(s)) {
				ks_log(KS_LOG_ERROR, "%s Received 'HI' with invalid 's' type\n", faktory->producer ? "Producer" : "Consumer");
				ks_json_delete(&json);
				ks_faktory_state_set(faktory, KS_FAKTORY_STATE_SHUTDOWN);
				return;
			}
			salt = ks_json_value_string(s);
		}

		if (i) {
			if (!ks_json_type_is_number(i)) {
				ks_log(KS_LOG_ERROR, "%s Received 'HI' with invalid 'i' type\n", faktory->producer ? "Producer" : "Consumer");
				ks_json_delete(&json);
				ks_faktory_state_set(faktory, KS_FAKTORY_STATE_SHUTDOWN);
				return;
			}
			iterations = ks_json_value_number_int(i);
		}

		params = ks_json_create_object();
		ks_json_add_number_to_object(params, "v", 2);

		if (salt) {
			unsigned char digest[SHA256_DIGEST_LENGTH];
			char pwdhash[(SHA256_DIGEST_LENGTH * 2) + 1];
			char *tmp = NULL;

			ks_assert(faktory->password);

			tmp = ks_psprintf(ks_pool_get(faktory), "%s%s", faktory->password, salt);

			SHA256((const unsigned char *)tmp, strlen(tmp), digest);
			ks_pool_free(&tmp);

			for (int iter = 1; iter < iterations; ++iter)
				SHA256(digest, SHA256_DIGEST_LENGTH, digest);

			ks_hex_string(digest, SHA256_DIGEST_LENGTH, pwdhash);

			ks_json_add_string_to_object(params, "pwdhash", pwdhash);
		}

		if (!faktory->producer) {
			ks_assert(faktory->hostname);
			ks_json_add_string_to_object(params, "hostname", faktory->hostname);

			ks_assert(faktory->wid);
			ks_json_add_string_to_object(params, "wid", faktory->wid);

#ifdef __WINDOWS__
			ks_json_add_number_to_object(params, "pid", _getpid());
#else
			ks_json_add_number_to_object(params, "pid", getpid());
#endif
			ks_json_add_item_to_object(params, "labels", ks_json_duplicate(faktory->labels, 1));
		}

		ks_faktory_send(faktory, "HELLO", params, NULL, NULL);
		ks_json_delete(&params);

		ks_json_delete(&json);
	} else if (strncmp(cmd, "OK", 2) == 0) {
#ifdef FAKTORY_DEBUG
		ks_log(KS_LOG_DEBUG, "%s Identified\n", faktory->producer ? "Producer" : "Consumer");
#endif
		ks_faktory_state_set(faktory, KS_FAKTORY_STATE_IDENTIFIED);
		faktory->last_beat = ks_time_now_sec();
	}
}

void ks_faktory_process_running(ks_faktory_t *faktory, respObject *resp)
{
#ifdef FAKTORY_DEBUG
	ks_log(KS_LOG_DEBUG, "%s State %s Processing Message: %s\n", faktory->producer ? "Producer" : "Consumer", ks_faktory_state_string(faktory->state), (char *)resp->str);
#endif

	if (faktory->command_callback) faktory->command_callback(faktory, resp, faktory->command_data);
	faktory->command_callback = NULL;
	faktory->command_data = NULL;

	faktory->awaiting_response = KS_FALSE;
}

void ks_faktory_end_handler(ks_faktory_t *faktory, respObject *resp, void *data)
{
	ks_faktory_reset(faktory, KS_FALSE);
}

void ks_faktory_process_terminating(ks_faktory_t *faktory, respObject *resp)
{
#ifdef FAKTORY_DEBUG
	ks_log(KS_LOG_DEBUG, "%s State %s Processing Message: %s\n", faktory->producer ? "Producer" : "Consumer", ks_faktory_state_string(faktory->state), (char *)resp->str);
#endif

	if (faktory->command_callback) faktory->command_callback(faktory, resp, faktory->command_data);
	faktory->command_callback = NULL;
	faktory->command_data = NULL;

	faktory->awaiting_response = KS_FALSE;
}

void ks_faktory_beat_handler(ks_faktory_t *faktory, respObject *resp, void *data)
{
	if (resp->type == RESP_OBJECT_STATUS && resp->str && resp->str[0] == '{') {
		ks_json_t *json = ks_json_parse((const char *)resp->str);
		const char *state = NULL;

		ks_assert(json);

#ifdef FAKTORY_DEBUG
		ks_log(KS_LOG_DEBUG, "%s State %s Processing Beat: %s\n", faktory->producer ? "Producer" : "Consumer", ks_faktory_state_string(faktory->state), (char *)resp->str);
#endif

		state = ks_json_get_object_cstr(json, "state");
		if (!strcmp(state, "quiet")) ks_faktory_state_set(faktory, KS_FAKTORY_STATE_QUIET);
		else if (!strcmp(state, "terminate")) {
			ks_faktory_state_set(faktory, KS_FAKTORY_STATE_TERMINATE);
			ks_faktory_send(faktory, "END", NULL, ks_faktory_end_handler, NULL);
		}
	}
}

KS_DECLARE(ks_status_t) ks_faktory_beat(ks_faktory_t *faktory, ks_bool_t force)
{
	ks_status_t ret = KS_STATUS_SUCCESS;
	if (faktory->producer) return KS_STATUS_NOT_ALLOWED;
	if (!force && ks_time_now_sec() - 15 < faktory->last_beat) return KS_STATUS_BREAK;
	//if (ks_time_now_sec() - 15 < faktory->last_beat) return;

	ks_json_t *beat = ks_json_create_object();
	ks_json_add_string_to_object(beat, "wid", faktory->wid);
	ret = ks_faktory_send(faktory, "BEAT", beat, ks_faktory_beat_handler, NULL);
	ks_json_delete(&beat);

	faktory->last_beat = ks_time_now_sec();
	return ret;
}

static void *ks_faktory_thread(ks_thread_t *thread, void *data)
{
	ks_faktory_t *faktory = (ks_faktory_t *)data;
	respObject *resp = NULL;
	ks_time_t wait = 0;
	ks_size_t queued = 0;
	ks_size_t queue50 = ks_q_maxlen(faktory->outq) >> 1;
	ks_size_t queue75 = (queue50 >> 1) * 3;

	ks_cond_lock(faktory->cond);

	while (faktory->state != KS_FAKTORY_STATE_SHUTDOWN) {
		if (wait > 0) ks_cond_timedwait(faktory->cond, wait);

		// @note stop waiting if the queue is more than half full, start sacrificing CPU
		// to poll for responses more frequently to attempt to catch up
		queued = ks_q_size(faktory->outq);
		if (queued == 0) wait = 50; // empty queue? sleep normally
		else if (queued < queue50) wait = 10; // less than 50% full? sleep a little bit
		else if (queued < queue75) wait = 1; // less than 75% full? sleep minimally
		else wait = 0; // more than 75% full? do not sleep

		// @todo temporary hack to see if this stuff is causing slow receiving of command responses
		if (wait > 0) wait = 1;

		if (faktory->sock != KS_SOCK_INVALID) {
			if (faktory->inbufused == faktory->inbufsize) {
				ks_faktory_reset(faktory, KS_FALSE);
			} else {
				ks_size_t recvd = faktory->inbufsize - faktory->inbufused;
				ks_status_t recvs = ks_socket_recv(faktory->sock, (void *)(faktory->inbuf + faktory->inbufused), &recvd);
				if (recvs == KS_STATUS_DISCONNECTED) {
					ks_faktory_reset(faktory, KS_FALSE);
				} else if (recvs == KS_STATUS_SUCCESS) {
					faktory->inbufused += recvd;
					*(faktory->inbuf + faktory->inbufused) = '\0';
					wait = 0; // received stuff? don't sleep in case more is available

#ifdef FAKTORY_DEBUG
					ks_log(KS_LOG_DEBUG, "%s Received Data: %u bytes, %u total\n", faktory->producer ? "Producer" : "Consumer", recvd, faktory->inbufused);
#endif
				} else if (recvs == KS_STATUS_BREAK) {
				} else {
#ifdef FAKTORY_DEBUG
					ks_log(KS_LOG_DEBUG, "%s Recv Error: %u\n", faktory->producer ? "Producer" : "Consumer", recvs);
#endif
				}
			}

			// ks_log(KS_LOG_ERROR, "awaiting: %d ou: %d oq: %d\n", faktory->awaiting_response, faktory->outbufused, ks_q_size(faktory->outq));

			if (!faktory->awaiting_response && faktory->outbufused == 0 && ks_q_size(faktory->outq) > 0) {
				ks_faktory_command_t *command = NULL;
				ks_q_pop(faktory->outq, (void **)&command);

#ifdef FAKTORY_DEBUG
				ks_log(KS_LOG_DEBUG, "Sending Command @ 0x%x, length %u\n", command, command->messagelen);
				ks_log(KS_LOG_DEBUG, "%s", command->message);
#endif

				ks_assert((command->messagelen + 1) <= faktory->outbufsize);
				ks_assert(command->messagelen > 0);

				memcpy(faktory->outbuf, command->message, command->messagelen);
				faktory->outbuf[command->messagelen] = 0;
				faktory->outbufused = command->messagelen;

				faktory->awaiting_response = KS_TRUE;
				faktory->command_callback = command->callback;
				faktory->command_data = command->data;

				ks_faktory_command_destroy(&command);
			}

			if (faktory->outbufused > 0) {
				ks_size_t sent = faktory->outbufused;
				ks_status_t sends = KS_STATUS_SUCCESS;

#ifdef FAKTORY_DEBUG
				ks_log(KS_LOG_DEBUG, "%s Sending Data: %u total\n", faktory->producer ? "Producer" : "Consumer", faktory->outbufused);
				ks_log(KS_LOG_DEBUG, "%s", (const char *)faktory->outbuf);
#endif

				sends = ks_socket_send(faktory->sock, (void *)faktory->outbuf, &sent);

#ifdef FAKTORY_DEBUG
				ks_log(KS_LOG_DEBUG, "%s Sent Data: %u result, %u sent, %u total\n", faktory->producer ? "Producer" : "Consumer", sends, sent, faktory->outbufused);
#endif

				if (sends == KS_STATUS_DISCONNECTED) {
					ks_faktory_reset(faktory, KS_FALSE);
				} else if (sends == KS_STATUS_SUCCESS) {
					faktory->outbufused -= sent;
					if (faktory->outbufused > 0) memcpy((void *)faktory->outbuf, (void *)(faktory->outbuf + sent), faktory->outbufused);
					wait = 0; // sent stuff? don't sleep in case more needs to be sent
				} else if (sends == KS_STATUS_BREAK) {
				} else {
#ifdef FAKTORY_DEBUG
					ks_log(KS_LOG_DEBUG, "%s Send Error: %u\n", faktory->producer ? "Producer" : "Consumer", sends);
#endif
				}
			}
		}

		switch (faktory->state) {
		case KS_FAKTORY_STATE_CONNECTING:
		{
			if (faktory->server_set) {
				faktory->sock = ks_socket_connect_ex(SOCK_STREAM, IPPROTO_TCP, &faktory->server, 5000);
				if (faktory->sock != KS_SOCK_INVALID) {
					if (ks_socket_option(faktory->sock, KS_SO_NONBLOCK, KS_TRUE) != KS_STATUS_SUCCESS) ks_socket_close(&faktory->sock);
					else {
						ks_faktory_state_set(faktory, KS_FAKTORY_STATE_UNIDENTIFIED);
					}
				}
			}
			break;
		}
		case KS_FAKTORY_STATE_UNIDENTIFIED:
			if (ks_faktory_process(faktory, &resp) && resp) ks_faktory_process_unidentified(faktory, resp);
			break;

		case KS_FAKTORY_STATE_IDENTIFIED:
		case KS_FAKTORY_STATE_QUIET:
			ks_faktory_beat(faktory, KS_FALSE);
			if (ks_faktory_process(faktory, &resp) && resp) ks_faktory_process_running(faktory, resp);
			break;
		case KS_FAKTORY_STATE_TERMINATE:
		case KS_FAKTORY_STATE_END:
			if (ks_faktory_process(faktory, &resp) && resp) ks_faktory_process_terminating(faktory, resp);
			break;
		default:
			break;
		}

		if (resp) {
			freeRespObject(resp);
			resp = NULL;
		}
	}

	ks_faktory_state_set(faktory, KS_FAKTORY_STATE_CLEANUP);

	ks_pool_free(&faktory->thread);

	return NULL;
}



static void ks_faktory_command_cleanup(void *ptr, void *arg, ks_pool_cleanup_action_t action, ks_pool_cleanup_type_t type)
{
	ks_faktory_command_t *command = (ks_faktory_command_t *)ptr;

	switch (action) {
	case KS_MPCL_ANNOUNCE:
		break;
	case KS_MPCL_TEARDOWN:
		ks_pool_free(&command->message);
		break;
	case KS_MPCL_DESTROY:
		break;
	}
}

ks_status_t ks_faktory_command_create(ks_faktory_command_t **commandP, ks_faktory_t *faktory, const char *cmd, ks_json_t *args, ks_faktory_command_callback_t callback, void *data)
{
	ks_pool_t *pool = NULL;
	ks_faktory_command_t *command = NULL;
	ks_size_t len = 0;
	ks_size_t cmdlen = 0;
	char *message = NULL;
	ks_size_t msglen = 0;
	ks_size_t offset = 0;

	ks_assert(faktory);
	ks_assert(cmd);

	pool = ks_pool_get(faktory);

	command = ks_pool_alloc(pool, sizeof(ks_faktory_command_t));
	ks_assert(command);

	command->faktory = faktory;

	cmdlen = strlen(cmd);
	len += cmdlen;
	if (args) {
		message = ks_json_print_unformatted(args);
		msglen = strlen(message);
		len += 1 + msglen;
	}
	len += 3;

	command->message = ks_pool_alloc(pool, len);
	ks_assert(command->message);

	memcpy(command->message, cmd, cmdlen);
	offset += cmdlen;

	if (message) {
		command->message[offset++] = ' ';
		memcpy(command->message + offset, message, msglen);
		offset += msglen;
		free(message);
	}

	command->message[offset] = '\r';
	command->message[offset + 1] = '\n';
	command->message[offset + 2] = '\0';

	command->messagelen = offset + 2;

	command->callback = callback;
	command->data = data;

	ks_pool_set_cleanup(command, NULL, ks_faktory_command_cleanup);

	*commandP = command;

#ifdef FAKTORY_DEBUG
	ks_log(KS_LOG_DEBUG, "Created @ 0x%x\n", command);
#endif

	return KS_STATUS_SUCCESS;
}

ks_status_t ks_faktory_command_destroy(ks_faktory_command_t **commandP)
{
#ifdef FAKTORY_DEBUG
	ks_faktory_command_t *command = NULL;
#endif

	ks_assert(commandP);
	ks_assert(*commandP);

#ifdef FAKTORY_DEBUG
	command = *commandP;
#endif

	ks_pool_free(commandP);

#ifdef FAKTORY_DEBUG
	ks_log(KS_LOG_DEBUG, "Destroyed @ 0x%x\n", command);
#endif

	return KS_STATUS_SUCCESS;
}
