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


#ifdef _MSC_VER
/* warning C4706: assignment within conditional expression*/
#pragma warning(disable: 4706)
#endif

#define WS_BLOCK 1
#define WS_NOBLOCK 0

#define WS_INIT_SANITY 5000
#define WS_WRITE_SANITY 200

#define SHA1_HASH_SIZE 20

static const char c64[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

//static ks_ssize_t ws_send_buf(kws_t *kws, kws_opcode_t oc);
//static ks_ssize_t ws_feed_buf(kws_t *kws, void *data, ks_size_t bytes);


struct kws_s {
	ks_socket_t sock;
	kws_type_t type;
	char *buffer;
	char *bbuffer;
	char *body;
	char *uri;
	ks_size_t buflen;
	ks_size_t bbuflen;
	ks_ssize_t datalen;
	ks_ssize_t wdatalen;
	char *payload;
	ks_ssize_t plen;
	ks_ssize_t rplen;
	ks_ssize_t packetlen;
	SSL *ssl;
	int handshake;
	uint8_t down;
	int secure;
	SSL_CTX *ssl_ctx;
	int destroy_ssl_ctx;
	int block;
	int sanity;
	int secure_established;
	int logical_established;
	char cipher_name[128];
	kws_flag_t flags;
	int x;
	void *write_buffer;
	ks_size_t write_buffer_len;
	char *req_uri;
	char *req_host;
	char *req_proto;

	ks_bool_t certified_client;
	char **sans;
	ks_size_t sans_count;
};



static int cheezy_get_var(char *data, char *name, char *buf, ks_size_t buflen)
{
  char *p=data;

  /* the old way didnt make sure that variable values were used for the name hunt
   * and didnt ensure that only a full match of the variable name was used
   */
  ks_assert(buflen > 0);

  do {
    if(!strncasecmp(p,name,strlen(name)) && *(p+strlen(name))==':') break;
  } while((p = (strstr(p,"\n")+1))!=(char *)1);


  if (p && p != (char *)1 && *p!='\0') {
    char *v, *e = 0;

    v = strchr(p, ':');
    if (v) {
      v++;
      while(v && *v == ' ') {
	v++;
      }
      if (v)  {
	e = strchr(v, '\r');
	if (!e) {
	  e = strchr(v, '\n');
	}
      }

      if (v && e) {
	size_t cplen;
	ks_size_t len = e - v;

	if (len > buflen - 1) {
	  cplen = buflen - 1;
	} else {
	  cplen = len;
	}

	strncpy(buf, v, cplen);
	*(buf+cplen) = '\0';
	return 1;
      }

    }
  }
  return 0;
}

static int b64encode(unsigned char *in, ks_size_t ilen, unsigned char *out, ks_size_t olen)
{
	int y=0,bytes=0;
	ks_size_t x=0;
	unsigned int b=0,l=0;

	if(olen) {
	}

	for(x=0;x<ilen;x++) {
		b = (b<<8) + in[x];
		l += 8;
		while (l >= 6) {
			out[bytes++] = c64[(b>>(l-=6))%64];
			if(++y!=72) {
				continue;
			}
			//out[bytes++] = '\n';
			y=0;
		}
	}

	if (l > 0) {
		out[bytes++] = c64[((b%16)<<(6-l))%64];
	}
	if (l != 0) while (l < 6) {
		out[bytes++] = '=', l += 2;
	}

	return 0;
}

static void sha1_digest(unsigned char *digest, char *in)
{
	SHA_CTX sha;

	SHA1_Init(&sha);
	SHA1_Update(&sha, in, strlen(in));
	SHA1_Final(digest, &sha);

}

/* fix me when we get real rand funcs in ks */
static void gen_nonce(unsigned char *buf, uint16_t len)
{
	int max = 255;
	uint16_t x;
	ks_time_t time_now = ks_time_now();
	srand((unsigned int)(((time_now >> 32) ^ time_now) & 0xffffffff));

	for (x = 0; x < len; x++) {
		int j = (int) (max * 1.0 * rand() / (RAND_MAX + 1.0));
		buf[x] = (char) j;
	}
}

static int verify_accept(kws_t *kws, const unsigned char *enonce, const char *accept)
{
	char input[256] = "";
	unsigned char output[SHA1_HASH_SIZE] = "";
	char b64[256] = "";

	snprintf(input, sizeof(input), "%s%s", enonce, WEBSOCKET_GUID);
	sha1_digest(output, input);
	b64encode((unsigned char *)output, SHA1_HASH_SIZE, (unsigned char *)b64, sizeof(b64));

	return !strcmp(b64, accept);
}

static int ws_client_handshake(kws_t *kws)
{
	unsigned char nonce[16];
	unsigned char enonce[128] = "";
	char req[2048] = "";

	gen_nonce(nonce, sizeof(nonce));
	b64encode(nonce, sizeof(nonce), enonce, sizeof(enonce));

	ks_snprintf(req, sizeof(req),
				"GET %s HTTP/1.1\r\n"
				"Host: %s\r\n"
				"Upgrade: websocket\r\n"
				"Connection: Upgrade\r\n"
				"Sec-WebSocket-Key: %s\r\n"
				"Sec-WebSocket-Version: 13\r\n"
				"%s%s%s"
				"\r\n",
				kws->req_uri, kws->req_host, enonce,
				kws->req_proto ? "Sec-WebSocket-Protocol: " : "",
				kws->req_proto ? kws->req_proto : "",
				kws->req_proto ? "\r\n" : "");

	kws_raw_write(kws, req, strlen(req));

	ks_ssize_t bytes;

	do {
		bytes = kws_raw_read(kws, kws->buffer + kws->datalen, kws->buflen - kws->datalen, WS_BLOCK);
	} while (bytes > 0 && !strstr((char *)kws->buffer, "\r\n\r\n"));

	if (bytes > 0) {
		char accept[128] = "";

		cheezy_get_var(kws->buffer, "Sec-WebSocket-Accept", accept, sizeof(accept));

		if (ks_zstr_buf(accept) || !verify_accept(kws, enonce, (char *)accept)) {
			return -1;
		}
	} else {
		return -1;
	}

	kws->handshake = 1;

	return 0;
}

static int ws_server_handshake(kws_t *kws)
{
	char key[256] = "";
	char version[5] = "";
	char proto[256] = "";
	char proto_buf[384] = "";
	char input[512] = "";
	unsigned char output[SHA1_HASH_SIZE] = "";
	char b64[256] = "";
	char respond[1024] = "";
	ks_ssize_t bytes;
	char *p, *e = 0;

	if (kws->sock == KS_SOCK_INVALID) {
		return -3;
	}

	while((bytes = kws_raw_read(kws, kws->buffer + kws->datalen, kws->buflen - kws->datalen, WS_BLOCK)) > 0) {
		kws->datalen += bytes;
		if (strstr(kws->buffer, "\r\n\r\n") || strstr(kws->buffer, "\n\n")) {
			break;
		}
	}

	if (bytes < 0 || ((ks_size_t)bytes) > kws->buflen - 1) {
		goto err;
	}

	*(kws->buffer + kws->datalen) = '\0';

	if (strncasecmp(kws->buffer, "GET ", 4)) {
		goto err;
	}

	p = kws->buffer + 4;

	e = strchr(p, ' ');
	if (!e) {
		goto err;
	}

	kws->uri = ks_pool_alloc(ks_pool_get(kws), (unsigned long)(e-p) + 1);
	strncpy(kws->uri, p, e-p);
	*(kws->uri + (e-p)) = '\0';

	cheezy_get_var(kws->buffer, "Sec-WebSocket-Key", key, sizeof(key));
	cheezy_get_var(kws->buffer, "Sec-WebSocket-Version", version, sizeof(version));
	cheezy_get_var(kws->buffer, "Sec-WebSocket-Protocol", proto, sizeof(proto));

	if (!*key) {
		goto err;
	}

	snprintf(input, sizeof(input), "%s%s", key, WEBSOCKET_GUID);
	sha1_digest(output, input);
	b64encode((unsigned char *)output, SHA1_HASH_SIZE, (unsigned char *)b64, sizeof(b64));

	if (*proto) {
		snprintf(proto_buf, sizeof(proto_buf), "Sec-WebSocket-Protocol: %s\r\n", proto);
	}

	snprintf(respond, sizeof(respond),
			 "HTTP/1.1 101 Switching Protocols\r\n"
			 "Upgrade: websocket\r\n"
			 "Connection: Upgrade\r\n"
			 "Sec-WebSocket-Accept: %s\r\n"
			 "%s\r\n",
			 b64,
			 proto_buf);
	respond[511] = 0;

	if (kws_raw_write(kws, respond, strlen(respond)) != (ks_ssize_t)strlen(respond)) {
		goto err;
	}

	kws->handshake = 1;

	return 0;

 err:

	if (!(kws->flags & KWS_STAY_OPEN)) {

		if (bytes > 0) {
			snprintf(respond, sizeof(respond), "HTTP/1.1 400 Bad Request\r\n"
					 "Sec-WebSocket-Version: 13\r\n\r\n");
			respond[511] = 0;

			kws_raw_write(kws, respond, strlen(respond));
		}

		kws_close(kws, WS_NONE);
	}

	return -1;

}

#define SSL_ERROR_WANT_READ_WRITE(err) (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)

KS_DECLARE(ks_ssize_t) kws_raw_read(kws_t *kws, void *data, ks_size_t bytes, int block)
{
	int r;
	int ssl_err = 0;

	kws->x++;
	if (kws->x > 250) ks_sleep_ms(1);

	if (kws->ssl) {
		do {
			r = SSL_read(kws->ssl, data, (int)bytes);
			if (r == 0) {
				ssl_err = SSL_get_error(kws->ssl, r);
				ks_log(KS_LOG_ERROR, "Weird SSL_read error: %d\n", ssl_err);
			}

			if (r < 0) {
				ssl_err = SSL_get_error(kws->ssl, r);

				if (SSL_ERROR_WANT_READ_WRITE(ssl_err)) {
					if (!block) {
						r = -2;
						goto end;
					}
					kws->x++;
					ks_sleep_ms(10);
				} else {
					r = -1;
					goto end;
				}
			}

		} while (r < 0 && SSL_ERROR_WANT_READ_WRITE(ssl_err) && kws->x < 1000);

		goto end;
	}

	do {
		r = recv(kws->sock, data, (int)bytes, 0);

		if (r == -1) {
			if (!block && ks_errno_is_blocking(ks_errno())) {
				r = -2;
				goto end;
			}

			if (block) {
				kws->x++;
				ks_sleep_ms(10);
			}
		}
	} while (r == -1 && ks_errno_is_blocking(ks_errno()) && kws->x < 1000);

 end:

	if (kws->x >= 10000 || (block && kws->x >= 1000)) {
		r = -1;
	}

	if (r > 0) {
		*((char *)data + r) = '\0';
	}

	if (r >= 0) {
		kws->x = 0;
	}

	return r;
}

KS_DECLARE(ks_ssize_t) kws_raw_write(kws_t *kws, void *data, ks_size_t bytes)
{
	int r;
	int sanity = WS_WRITE_SANITY;
	int ssl_err = 0;
	ks_size_t wrote = 0;

	if (kws->ssl) {
		do {
			r = SSL_write(kws->ssl, (void *)((unsigned char *)data + wrote), (int)(bytes - wrote));

			if (r == 0) {
				ssl_err = 42;
				break;
			}

			if (r > 0) {
				wrote += r;
			}

			if (sanity < WS_WRITE_SANITY) {
				int ms = 1;

				if (kws->block) {
					if (sanity < WS_WRITE_SANITY * 3 / 4) {
						ms = 50;
					} else if (sanity < WS_WRITE_SANITY / 2) {
						ms = 25;
					}
				}
				ks_sleep_ms(ms);
			}

			if (r < 0) {
				ssl_err = SSL_get_error(kws->ssl, r);

				if (!SSL_ERROR_WANT_READ_WRITE(ssl_err)) {
					break;
				}
				ssl_err = 0;
			}

		} while (--sanity > 0 && wrote < bytes);

		if (!sanity) ssl_err = 56;

		if (ssl_err) {
			r = ssl_err * -1;
		}

		return r;
	}

	do {
		r = send(kws->sock, (void *)((unsigned char *)data + wrote), (int)(bytes - wrote), 0);

		if (r > 0) {
			wrote += r;
		}

		if (sanity < WS_WRITE_SANITY) {
			int ms = 1;

			if (kws->block) {
				if (sanity < WS_WRITE_SANITY * 3 / 4) {
					ms = 50;
				} else if (sanity < WS_WRITE_SANITY / 2) {
					ms = 25;
				}
			}
			ks_sleep_ms(ms);
		}

		if (r == -1) {
			if (!ks_errno_is_blocking(ks_errno())) {
				break;
			}
		}

	} while (--sanity > 0 && wrote < bytes);

	//if (r<0) {
		//printf("wRITE FAIL: %s\n", strerror(errno));
	//}

	return r;
}

static void setup_socket(ks_socket_t sock)
{
	ks_socket_option(sock, KS_SO_NONBLOCK, KS_TRUE);
}

static void restore_socket(ks_socket_t sock)
{
	ks_socket_option(sock, KS_SO_NONBLOCK, KS_FALSE);
}

static int __log_ssl_errors(const char *err, size_t len, void *u)
{
	ks_log(KS_LOG_ERROR, "  %s", err);	// ssl adds a \n
	return 0;
}

static int establish_client_logical_layer(kws_t *kws)
{

	if (!kws->sanity) {
		return -1;
	}

	if (kws->logical_established) {
		return 0;
	}

	if (kws->secure && !kws->secure_established) {
		int code;

		if (!kws->ssl) {
			kws->ssl = SSL_new(kws->ssl_ctx);
			assert(kws->ssl);

			SSL_set_fd(kws->ssl, (int)kws->sock);
		}

		do {
			code = SSL_connect(kws->ssl);

			if (code == 1) {
				kws->secure_established = 1;
				break;
			}

			if (code == 0) {
				return -1;
			}

			if (code < 0) {
				int ssl_err = SSL_get_error(kws->ssl, code);
				if (code < 0 && !SSL_ERROR_WANT_READ_WRITE(ssl_err)) {
					ks_log(KS_LOG_ERROR, "Failed to negotiate ssl connection with ssl error code: %d (%s)\n", ssl_err, ERR_error_string(ssl_err, NULL));
					ERR_print_errors_cb(__log_ssl_errors, NULL);
					return -1;
				}
			}

			if (kws->block) {
				ks_sleep_ms(10);
			} else {
				ks_sleep_ms(1);
			}

			kws->sanity--;

			if (!kws->block) {
				return -2;
			}

		} while (kws->sanity > 0);

		if (!kws->sanity) {
			return -1;
		}
	}

	while (!kws->down && !kws->handshake) {
		int r = ws_client_handshake(kws);

		if (r < 0) {
			kws->down = 1;
			return -1;
		}

		if (!kws->handshake && !kws->block) {
			return -2;
		}

	}

	kws->logical_established = 1;
	if (kws->ssl) {
		strncpy(kws->cipher_name, SSL_get_cipher_name(kws->ssl), sizeof(kws->cipher_name) - 1);
		ks_log(KS_LOG_INFO, "SSL negotiation succeeded, negotiated cipher is: %s\n", kws->cipher_name);
	} else {
		memset(kws->cipher_name, 0, sizeof(kws->cipher_name));
	}

	return 0;
}

KS_DECLARE(ks_status_t) kws_get_cipher_name(kws_t *kws, char *name, ks_size_t name_len)
{
	if (kws->ssl) {
		strncpy(name, kws->cipher_name, name_len);
		return KS_STATUS_SUCCESS;
	}
	else
		return KS_STATUS_INVALID_ARGUMENT;
}

static int establish_server_logical_layer(kws_t *kws)
{

	if (!kws->sanity) {
		return -1;
	}

	if (kws->logical_established) {
		return 0;
	}

	if (kws->secure && !kws->secure_established) {
		int code;

		if (!kws->ssl) {
			kws->ssl = SSL_new(kws->ssl_ctx);
			assert(kws->ssl);

			SSL_set_fd(kws->ssl, (int)kws->sock);
		}

		do {
			code = SSL_accept(kws->ssl);

			if (code == 1) {
				kws->secure_established = 1;
				break;
			}

			if (code == 0) {
				return -1;
			}

			if (code < 0) {
				int ssl_err = SSL_get_error(kws->ssl, code);
				if (code < 0 && !SSL_ERROR_WANT_READ_WRITE(ssl_err)) {
					ks_log(KS_LOG_ERROR, "Failed to negotiate ssl connection with ssl error code: %d (%s)\n", ssl_err, ERR_error_string(ssl_err, NULL));
					ERR_print_errors_cb(__log_ssl_errors, NULL);
					return -1;
				}
			}

			if (kws->block) {
				ks_sleep_ms(10);
			} else {
				ks_sleep_ms(1);
			}

			kws->sanity--;

			if (!kws->block) {
				return -2;
			}

		} while (kws->sanity > 0);

		if (!kws->sanity) {
			return -1;
		}

	}

	while (!kws->down && !kws->handshake) {
		int r = ws_server_handshake(kws);

		if (r < 0) {
			kws->down = 1;
			return -1;
		}

		if (!kws->handshake && !kws->block) {
			return -2;
		}

	}

	kws->logical_established = 1;
	if (kws->ssl) {
		ks_log(KS_LOG_INFO, "SSL negotiation succeeded, negotiated cipher is: %s\n", SSL_get_cipher_name(kws->ssl));
	}

	return 0;
}

static int establish_logical_layer(kws_t *kws)
{
	if (kws->type == KWS_CLIENT) {
		return establish_client_logical_layer(kws);
	} else {
		return establish_server_logical_layer(kws);
	}
}

KS_DECLARE(ks_status_t) kws_init(kws_t **kwsP, ks_socket_t sock, SSL_CTX *ssl_ctx, const char *client_data, kws_flag_t flags, ks_pool_t *pool)
{
	kws_t *kws;

	kws = ks_pool_alloc(pool, sizeof(*kws));
	kws->flags = flags;

	if ((flags & KWS_BLOCK)) {
		kws->block = 1;
	}

	if (client_data) {
		char *p = NULL;
		kws->req_uri = ks_pstrdup(pool, client_data);

		if ((p = strchr(kws->req_uri, ':'))) {
			*p++ = '\0';
			kws->req_host = p;
			if ((p = strchr(kws->req_host, ':'))) {
				*p++ = '\0';
				kws->req_proto = p;
			}
		}

		kws->type = KWS_CLIENT;
	} else {
		kws->type = KWS_SERVER;
		kws->flags |= KWS_FLAG_DONTMASK;
	}

	kws->sock = sock;
	kws->sanity = WS_INIT_SANITY;
	kws->ssl_ctx = ssl_ctx;

	kws->buflen = 1024 * 64;
	kws->bbuflen = kws->buflen;

	kws->buffer = ks_pool_alloc(pool, (unsigned long)kws->buflen);
	kws->bbuffer = ks_pool_alloc(pool, (unsigned long)kws->bbuflen);
	//printf("init %p %ld\n", (void *) kws->bbuffer, kws->bbuflen);
	//memset(kws->buffer, 0, kws->buflen);
	//memset(kws->bbuffer, 0, kws->bbuflen);

	kws->secure = ssl_ctx ? 1 : 0;

	setup_socket(sock);

	if (establish_logical_layer(kws) == -1) {
		ks_log(KS_LOG_ERROR, "Failed to establish logical layer\n");
		goto err;
	}

	if (kws->down) {
		ks_log(KS_LOG_ERROR, "Link down\n");
		goto err;
	}

	if (kws->type == KWS_SERVER)
	{
		X509 *cert = SSL_get_peer_certificate(kws->ssl);

		if (cert && SSL_get_verify_result(kws->ssl) == X509_V_OK) {
			GENERAL_NAMES *sans = X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL);

			kws->certified_client = KS_TRUE;
			if (sans) {
				kws->sans_count = (ks_size_t)sk_GENERAL_NAME_num(sans);
				if (kws->sans_count) kws->sans = ks_pool_calloc(pool, kws->sans_count, sizeof(char *));
				for (ks_size_t i = 0; i < kws->sans_count; i++) {
					const GENERAL_NAME *gname = sk_GENERAL_NAME_value(sans, (int)i);
#if OPENSSL_VERSION_NUMBER >= 0x10100000
					const unsigned char *name = ASN1_STRING_get0_data((const ASN1_STRING *)gname->d.dNSName);
#else
					char *name = (char *)ASN1_STRING_data(gname->d.dNSName);
#endif
					kws->sans[i] = ks_pstrdup(pool, (char *)name);

				}
				sk_GENERAL_NAME_pop_free(sans, GENERAL_NAME_free);
			}
		}

		if (cert) X509_free(cert);
	}

	*kwsP = kws;

	return KS_STATUS_SUCCESS;

 err:
	kws_destroy(&kws);

	return KS_STATUS_FAIL;
}

KS_DECLARE(void) kws_destroy(kws_t **kwsP)
{
	kws_t *kws;
	ks_assert(kwsP);

	if (!(kws = *kwsP)) {
		return;
	}

	*kwsP = NULL;

	if (!kws->down) {
		kws_close(kws, WS_NONE);
	}

	if (kws->down > 1) {
		return;
	}

	kws->down = 2;

	if (kws->write_buffer) {
		ks_pool_free(&kws->write_buffer);
		kws->write_buffer = NULL;
		kws->write_buffer_len = 0;
	}

	if (kws->ssl) {
		int code, ssl_err, sanity = 100;
		do {
			code = SSL_shutdown(kws->ssl);
			if (code == 1) {
				break;
			}
			if (code < 0) {
				ssl_err = SSL_get_error(kws->ssl, code);
			}
			if (kws->block) {
				ks_sleep_ms(10);
			} else {
				ks_sleep_ms(1);
			}

		} while ((code == 0 || (code < 0 && SSL_ERROR_WANT_READ_WRITE(ssl_err))) && --sanity > 0);
		SSL_free(kws->ssl);
		kws->ssl = NULL;
	}

	if (kws->destroy_ssl_ctx && kws->ssl_ctx) {
		SSL_CTX_free(kws->ssl_ctx);
	}

	if (kws->buffer) ks_pool_free(&kws->buffer);
	if (kws->bbuffer) ks_pool_free(&kws->bbuffer);

	kws->buffer = kws->bbuffer = NULL;

	ks_pool_free(&kws);
	kws = NULL;
}

KS_DECLARE(ks_ssize_t) kws_close(kws_t *kws, int16_t reason)
{

	if (kws->down) {
		return -1;
	}

	kws->down = 1;

	if (kws->uri) {
		ks_pool_free(&kws->uri);
		kws->uri = NULL;
	}

	if (reason && kws->sock != KS_SOCK_INVALID) {
		uint16_t *u16;
		uint8_t fr[4] = {WSOC_CLOSE | 0x80, 2, 0};

		u16 = (uint16_t *) &fr[2];
		*u16 = htons((int16_t)reason);
		kws_raw_write(kws, fr, 4);
	}

	restore_socket(kws->sock);

	if ((kws->flags & KWS_CLOSE_SOCK) && kws->sock != KS_SOCK_INVALID) {
#ifndef WIN32
		close(kws->sock);
#else
		closesocket(kws->sock);
#endif
	}

	kws->sock = KS_SOCK_INVALID;

	return reason * -1;

}

#ifndef WIN32
#if defined(HAVE_BYTESWAP_H)
#include <byteswap.h>
#elif defined(HAVE_SYS_ENDIAN_H)
#include <sys/endian.h>
#elif defined (__APPLE__)
#include <libkern/OSByteOrder.h>
#define bswap_16 OSSwapInt16
#define bswap_32 OSSwapInt32
#define bswap_64 OSSwapInt64
#elif defined (__UCLIBC__)
#else
#ifndef bswap_16
	#define bswap_16(value) ((((value) & 0xff) << 8) | ((value) >> 8))
	#define bswap_32(value) (((uint32_t)bswap_16((uint16_t)((value) & 0xffff)) << 16) | (uint32_t)bswap_16((uint16_t)((value) >> 16)))
	#define bswap_64(value) (((uint64_t)bswap_32((uint32_t)((value) & 0xffffffff)) << 32) | (uint64_t)bswap_32((uint32_t)((value) >> 32)))
#endif
#endif
#endif

uint64_t hton64(uint64_t val)
{
#if __BYTE_ORDER == __BIG_ENDIAN
	return (val);
#else
	return bswap_64(val);
#endif
}

uint64_t ntoh64(uint64_t val)
{
#if __BYTE_ORDER == __BIG_ENDIAN
	return (val);
#else
	return bswap_64(val);
#endif
}

KS_DECLARE(ks_bool_t) kws_certified_client(kws_t *kws)
{
	ks_assert(kws);
	return kws->certified_client;
}

KS_DECLARE(ks_status_t) kws_peer_sans(kws_t *kws, char *buf, ks_size_t buflen)
{
	ks_status_t ret = KS_STATUS_SUCCESS;
	X509 *cert = NULL;

	ks_assert(kws);
	ks_assert(buf);
	ks_assert(buflen);

	cert = SSL_get_peer_certificate(kws->ssl);
	if (!cert) {
		ret = KS_STATUS_FAIL;
		goto done;
	}

	if (SSL_get_verify_result(kws->ssl) != X509_V_OK) {
		ret = KS_STATUS_FAIL;
		goto done;
	}

	//if (X509_NAME_get_text_by_NID(X509_get_subject_name(cert), NID_commonName, buf, (int)buflen) < 0) {
	//	ret = KS_STATUS_FAIL;
	//	goto done;
	//}

	GENERAL_NAMES *san_names = X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL);
	if (san_names) {
		int san_names_nb = sk_GENERAL_NAME_num(san_names);
		for (int i = 0; i < san_names_nb; i++) {
			const GENERAL_NAME *current_name = sk_GENERAL_NAME_value(san_names, i);

#if OPENSSL_VERSION_NUMBER >= 0x10100000
			const unsigned char *name = ASN1_STRING_get0_data((const ASN1_STRING *)current_name->d.dNSName);
#else
			char *name = (char *)ASN1_STRING_data(current_name->d.dNSName);
#endif

			if (name) continue;
		}
		sk_GENERAL_NAME_pop_free(san_names, GENERAL_NAME_free);
	}
done:
	if (cert) X509_free(cert);

	return ret;
}

KS_DECLARE(ks_ssize_t) kws_read_frame(kws_t *kws, kws_opcode_t *oc, uint8_t **data)
{

	ks_ssize_t need = 2;
	char *maskp;
	int ll = 0;
	int frag = 0;
	int blen;

	kws->body = kws->bbuffer;
	kws->packetlen = 0;

	*oc = WSOC_INVALID;

 again:
	need = 2;
	maskp = NULL;
	*data = NULL;

	ll = establish_logical_layer(kws);

	if (ll < 0) {
		ks_log(KS_LOG_ERROR, "Read frame error from logical layer: ll = %d\n", ll);
		return ll;
	}

	if (kws->down) {
		ks_log(KS_LOG_ERROR, "Read frame error because kws is down");
		return -1;
	}

	if (!kws->handshake) {
		ks_log(KS_LOG_ERROR, "Read frame error because kws handshake is incomplete");
		return kws_close(kws, WS_NONE);
	}

	if ((kws->datalen = kws_raw_read(kws, kws->buffer, 9, kws->block)) < 0) {
		ks_log(KS_LOG_ERROR, "Read frame error because kws_raw_read returned %ld\n", kws->datalen);
		if (kws->datalen == -2) {
			return -2;
		}
		return kws_close(kws, WS_NONE);
	}

	if (kws->datalen < need) {
		ssize_t bytes = kws_raw_read(kws, kws->buffer + kws->datalen, 9 - kws->datalen, WS_BLOCK);

		if (bytes < 0 || (kws->datalen += bytes) < need) {
			/* too small - protocol err */
			ks_log(KS_LOG_ERROR, "Read frame error because kws_raw_read: bytes = %ld, datalen = %ld, needed = %ld\n", bytes, kws->datalen, need);
			return kws_close(kws, WS_NONE);
		}
	}

	*oc = *kws->buffer & 0xf;

	switch(*oc) {
	case WSOC_CLOSE:
		{
			ks_log(KS_LOG_ERROR, "Read frame error because OPCODE = WSOC_CLOSE\n");
			kws->plen = kws->buffer[1] & 0x7f;
			*data = (uint8_t *) &kws->buffer[2];
			return kws_close(kws, 1000);
		}
		break;
	case WSOC_CONTINUATION:
	case WSOC_TEXT:
	case WSOC_BINARY:
	case WSOC_PING:
	case WSOC_PONG:
		{
			int fin = (kws->buffer[0] >> 7) & 1;
			int mask = (kws->buffer[1] >> 7) & 1;


			if (!fin && *oc != WSOC_CONTINUATION) {
				frag = 1;
			} else if (fin && *oc == WSOC_CONTINUATION) {
				frag = 0;
			}

			if (mask) {
				need += 4;

				if (need > kws->datalen) {
					/* too small - protocol err */
					ks_log(KS_LOG_ERROR, "Read frame error because not enough data for mask\n");
					*oc = WSOC_CLOSE;
					return kws_close(kws, WS_NONE);
				}
			}

			kws->plen = kws->buffer[1] & 0x7f;
			kws->payload = &kws->buffer[2];

			if (kws->plen == 127) {
				uint64_t *u64;
				ks_ssize_t more = 0;

				need += 8;

				if (need > kws->datalen) {
					/* too small - protocol err */
					//*oc = WSOC_CLOSE;
					//return kws_close(kws, WS_PROTO_ERR);

					more = kws_raw_read(kws, kws->buffer + kws->datalen, (int)(need - kws->datalen), WS_BLOCK);

					if (more < 0 || more < need - kws->datalen) {
						ks_log(KS_LOG_ERROR, "Read frame error because kws_raw_read: more = %ld, need = %ld, datalen = %ld\n", more, need, kws->datalen);
						*oc = WSOC_CLOSE;
						return kws_close(kws, WS_NONE);
					} else {
						kws->datalen += more;
					}


				}

				u64 = (uint64_t *) kws->payload;
				kws->payload += 8;
				kws->plen = (ks_ssize_t)ntoh64(*u64);
			} else if (kws->plen == 126) {
				uint16_t *u16;

				need += 2;

				if (need > kws->datalen) {
					/* too small - protocol err */
					ks_log(KS_LOG_ERROR, "Read frame error because kws_raw_read: not enough data for packet length\n");
					*oc = WSOC_CLOSE;
					return kws_close(kws, WS_NONE);
				}

				u16 = (uint16_t *) kws->payload;
				kws->payload += 2;
				kws->plen = ntohs(*u16);
			}

			if (mask) {
				maskp = (char *)kws->payload;
				kws->payload += 4;
			}

			need = (kws->plen - (kws->datalen - need));

			if (need < 0) {
				/* invalid read - protocol err .. */
				ks_log(KS_LOG_ERROR, "Read frame error because need = %ld\n", need);
				*oc = WSOC_CLOSE;
				return kws_close(kws, WS_NONE);
			}

			blen = (int)(kws->body - kws->bbuffer);

			if (need + blen > (ks_ssize_t)kws->bbuflen) {
				void *tmp;

				kws->bbuflen = need + blen + kws->rplen;
				if ((tmp = ks_pool_resize(kws->bbuffer, (unsigned long)kws->bbuflen))) {
					kws->bbuffer = tmp;
				} else {
					abort();
				}

				kws->body = kws->bbuffer + blen;
			}

			kws->rplen = kws->plen - need;

			if (kws->rplen) {
				ks_assert((kws->body + kws->rplen) <= (kws->bbuffer + kws->bbuflen));
				memcpy(kws->body, kws->payload, kws->rplen);
			}

			while(need) {
				ks_assert((kws->body + need + kws->rplen) <= (kws->bbuffer + kws->bbuflen));
				ks_ssize_t r = kws_raw_read(kws, kws->body + kws->rplen, need, WS_BLOCK);

				if (r < 1) {
					/* invalid read - protocol err .. */
					ks_log(KS_LOG_ERROR, "Read frame error because r = %ld\n", r);
					*oc = WSOC_CLOSE;
					return kws_close(kws, WS_NONE);
				}

				kws->datalen += r;
				kws->rplen += r;
				need -= r;
			}

			if (mask && maskp) {
				ks_ssize_t i;

				for (i = 0; i < kws->datalen; i++) {
					kws->body[i] ^= maskp[i % 4];
				}
			}

			if (*oc == WSOC_TEXT) {
				*(kws->body+kws->rplen) = '\0';
			}

			kws->packetlen += kws->rplen;
			kws->body += kws->rplen;

			if (frag) {
				goto again;
			}

			*data = (uint8_t *)kws->bbuffer;

			//printf("READ[%ld][%d]-----------------------------:\n[%s]\n-------------------------------\n", kws->packetlen, *oc, (char *)*data);


			return kws->packetlen;
		}
		break;
	default:
		{
			/* invalid op code - protocol err .. */
			ks_log(KS_LOG_ERROR, "Read frame error because unknown opcode = %ld\n", *oc);
			*oc = WSOC_CLOSE;
			return kws_close(kws, WS_PROTO_ERR);
		}
		break;
	}
}

#if 0
static ks_ssize_t ws_feed_buf(kws_t *kws, void *data, ks_size_t bytes)
{

	if (bytes + kws->wdatalen > kws->buflen) {
		return -1;
	}

	memcpy(kws->wbuffer + kws->wdatalen, data, bytes);

	kws->wdatalen += bytes;

	return bytes;
}

static ks_ssize_t ws_send_buf(kws_t *kws, kws_opcode_t oc)
{
	ks_ssize_t r = 0;

	if (!kws->wdatalen) {
		return -1;
	}

	r = ws_write_frame(kws, oc, kws->wbuffer, kws->wdatalen);

	kws->wdatalen = 0;

	return r;
}
#endif

KS_DECLARE(ks_ssize_t) kws_write_frame(kws_t *kws, kws_opcode_t oc, const void *data, ks_size_t bytes)
{
	uint8_t hdr[14] = { 0 };
	ks_size_t hlen = 2;
	uint8_t *bp;
	ks_ssize_t raw_ret = 0;
	int mask = (kws->flags & KWS_FLAG_DONTMASK) ? 0 : 1;

	if (kws->down) {
		return -1;
	}

	//printf("WRITE[%ld]-----------------------------:\n[%s]\n-----------------------------------\n", bytes, (char *) data);

	hdr[0] = (uint8_t)(oc | 0x80);

	if (bytes < 126) {
		hdr[1] = (uint8_t)bytes;
	} else if (bytes < 0x10000) {
		uint16_t *u16;

		hdr[1] = 126;
		hlen += 2;

		u16 = (uint16_t *) &hdr[2];
		*u16 = htons((uint16_t) bytes);

	} else {
		uint64_t *u64;

		hdr[1] = 127;
		hlen += 8;

		u64 = (uint64_t *) &hdr[2];
		*u64 = hton64(bytes);
	}

	if (kws->write_buffer_len < (hlen + bytes + 1 + mask * 4)) {
		void *tmp;

		kws->write_buffer_len = hlen + bytes + 1 + mask * 4;
		if (!kws->write_buffer) kws->write_buffer = ks_pool_alloc(ks_pool_get(kws), (unsigned long)kws->write_buffer_len);
		else if ((tmp = ks_pool_resize(kws->write_buffer, (unsigned long)kws->write_buffer_len))) {
			kws->write_buffer = tmp;
		} else {
			abort();
		}
	}

	bp = (uint8_t *) kws->write_buffer;
	memcpy(bp, (void *) &hdr[0], hlen);

	if (mask) {
		ks_size_t i;
		uint8_t masking_key[4];

		gen_nonce(masking_key, 4);

		*(bp + 1) |= 0x80;
		memcpy(bp + hlen, masking_key, 4);
		hlen += 4;

		for (i = 0; i < bytes; i++) {
			*(bp + hlen + i) = (*((uint8_t *)data + i)) ^ (*(masking_key + (i % 4)));
		}
	} else {
		memcpy(bp + hlen, data, bytes);
	}

	raw_ret = kws_raw_write(kws, bp, (hlen + bytes));

	if (raw_ret != (ks_ssize_t) (hlen + bytes)) {
		return raw_ret;
	}

	return bytes;
}

KS_DECLARE(ks_status_t) kws_get_buffer(kws_t *kws, char **bufP, ks_size_t *buflen)
{
	*bufP = kws->buffer;
	*buflen = kws->datalen;

	return KS_STATUS_SUCCESS;
}

KS_DECLARE(ks_size_t) kws_sans_count(kws_t *kws)
{
	ks_assert(kws);

	return kws->sans_count;
}

KS_DECLARE(const char *) kws_sans_get(kws_t *kws, ks_size_t index)
{
	ks_assert(kws);
	if (index >= kws->sans_count) return NULL;
	return kws->sans[index];
}

KS_DECLARE(ks_status_t) kws_connect(kws_t **kwsP, ks_json_t *params, kws_flag_t flags, ks_pool_t *pool)
{
	return kws_connect_ex(kwsP, params, flags, pool, NULL, 30000);
}

KS_DECLARE(ks_status_t) kws_connect_ex(kws_t **kwsP, ks_json_t *params, kws_flag_t flags, ks_pool_t *pool, SSL_CTX *ssl_ctx, uint32_t timeout_ms)
{
	ks_sockaddr_t addr = { 0 };
	ks_socket_t cl_sock = KS_SOCK_INVALID;
	int family = AF_INET;
	const char *ip = "127.0.0.1";
	ks_port_t port = 443;
	// char buf[50] = "";
	struct hostent *he;
	const char *url = ks_json_get_object_cstr(params, "url");
	// const char *headers = ks_json_get_object_cstr(params, "headers");
	const char *host = NULL;
	const char *protocol = ks_json_get_object_cstr(params, "protocol");
	const char *path = NULL;
	char *p = (char *)url;
	const char *client_data = NULL;
	int destroy_ssl_ctx = 0;

	if (!url) {
		ks_json_t *tmp;

		path = ks_json_get_object_cstr(params, "path");
		host = ks_json_get_object_cstr(params, "host");
		tmp = ks_json_get_object_item(params, "port");

		if (ks_json_type_is_number(tmp) && ks_json_value_number_int(tmp) > 0) {
			port = (ks_port_t)ks_json_value_number_int(tmp);
		}
	} else {
		if (!strncmp(url, "wss://", 6)) {
			if (!ssl_ctx) {
#if OPENSSL_VERSION_NUMBER >= 0x10100000
				ssl_ctx = SSL_CTX_new(TLS_client_method());
#else
				ssl_ctx = SSL_CTX_new(TLSv1_2_client_method());
#endif
				assert(ssl_ctx);
				destroy_ssl_ctx++;
			}

			p = (char *)url + 6;
		} else if (!strncmp(url, "ws://", 5))  {
			p = (char *)url + 5;
			port = 80;
		} else {
			*kwsP = NULL;
			return KS_STATUS_FAIL;
		}

		host = ks_pstrdup(pool, p);

		// if (*host == '[') // todo ipv6

		if ((p = strchr(host, ':'))) {
			*p++ = '\0';
			if (p) {
				port = (ks_port_t)atoi(p);
			}
		} else {
			p = (char *)host;
		}

		p = strchr(p, '/');

		if (p) {
			path = ks_pstrdup(pool, p);
			*p = '\0';
		} else {
			path = "/";
		}
	}

	if (!host || !path) return KS_STATUS_FAIL;

	he = gethostbyname(host);

	if (!he) {
		ip = host;

		if (strchr(ip, ':')) {
			family = AF_INET6;
		}

		ks_addr_set(&addr, ip, port, family);
	} else {
		ks_addr_set_raw(&addr, he->h_addr, port, ((struct sockaddr_in *)he->h_addr)->sin_family);
		// ip = ks_addr_get_host(&addr1);
	}

	cl_sock = ks_socket_connect_ex(SOCK_STREAM, IPPROTO_TCP, &addr, timeout_ms);

	if (protocol) {
		client_data = ks_psprintf(pool, "%s:%s:%s", path, host, protocol);
	} else {
		client_data = ks_psprintf(pool, "%s:%s", path, host);
	}

	if (kws_init(kwsP, cl_sock, ssl_ctx, client_data, flags, pool) != KS_STATUS_SUCCESS) {
		if (destroy_ssl_ctx) SSL_CTX_free(ssl_ctx);

		return KS_STATUS_FAIL;
	}

	(*kwsP)->destroy_ssl_ctx = 1;

	return KS_STATUS_SUCCESS;
}

KS_DECLARE(int) kws_wait_sock(kws_t *kws, uint32_t ms, ks_poll_t flags)
{
	if (kws->sock == KS_SOCK_INVALID) return KS_POLL_ERROR;

	return ks_wait_sock(kws->sock, ms, flags);
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
