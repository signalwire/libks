/*
 * Copyright (c) 2018-2025 SignalWire, Inc
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

#define KS_TLS_INIT_SANITY 5000
#define KS_TLS_WRITE_SANITY 200

#define KS_TLS_BLOCK_MS 10000      /* ms, blocks read operation for 10 seconds */
#define KS_TLS_SOFT_BLOCK_MS 1000  /* ms, blocks read operation for 1 second */

#define KS_TLS_BUFLEN  1024 * 64
#define KS_TLS_SHUTDOWN_BUFLEN 9

// TODO: These are already defined in kws.c
#define SSL_IO_ERROR(err) (err == SSL_ERROR_SYSCALL || err == SSL_ERROR_SSL)
#define SSL_ERROR_WANT_READ_WRITE(err) (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)


struct ks_tls_s {
	uint8_t down;
	ks_socket_t sock;
	int32_t sanity;
	ks_tls_type_t type;
	uint32_t block_ms; // TODO: Can we use flag instead of this value?
	ks_tls_flag_t flags;

	SSL *ssl;
	SSL_CTX *ssl_ctx;
	ks_bool_t ssl_io_error;
	ks_bool_t secure_established;
	ks_bool_t destroy_ssl_ctx; /* Always TRUE for client connections */

	ks_bool_t certified_client;
	char **sans;
	ks_size_t sans_count;

	ks_tls_init_callback_t init_callback;

	/* read-related */
	int32_t x; // TODO: rename to something meaningful

	char *req_host;
	char client_cipher_name[128]; /* Useless for server since it may has multiple peers with different ciphers. */
};

/* Returns (positive) bytes written or (negative) SSL error. */
KS_DECLARE(ks_ssize_t) ks_tls_write(ks_tls_t *ktls, void *data, ks_size_t bytes)
{
	int r;
	int sanity = KS_TLS_WRITE_SANITY;
	int ssl_err = 0;
	ks_size_t wrote = 0;

	ks_assert(ktls);

	if (!data || !bytes) {
		return 0;
	}

	do {
		ERR_clear_error();
		r = SSL_write(ktls->ssl, (void *)((unsigned char *)data + wrote), (int)(bytes - wrote));

		if (r == 0) {
			ssl_err = SSL_get_error(ktls->ssl, r);
			if (SSL_IO_ERROR(ssl_err)) {
				ktls->ssl_io_error = KS_TRUE;
			}

			ssl_err = 42;
			break;
		}

		if (r > 0) {
			wrote += r;
		}

		if (sanity < KS_TLS_WRITE_SANITY) {
			int ms = 1;

			if (ktls->block_ms) {
				if (sanity < KS_TLS_WRITE_SANITY * 3 / 4) {
					ms = 50;
				} else if (sanity < KS_TLS_WRITE_SANITY / 2) {
					ms = 25;
				}
			}

			ks_sleep_ms(ms);
		}

		if (r < 0) {
			ssl_err = SSL_get_error(ktls->ssl, r);

			if (!SSL_ERROR_WANT_READ_WRITE(ssl_err)) {
				if (SSL_IO_ERROR(ssl_err)) {
					ktls->ssl_io_error = KS_TRUE;
				}

				break;
			}

			ssl_err = 0;
		}

	} while (--sanity > 0 && wrote < bytes);

	if (!sanity) ssl_err = 56;

	if (ssl_err) {
		r = ssl_err * -1;
	}

	return r > 0 ? wrote : r;
}

KS_DECLARE(ks_ssize_t) ks_tls_read(ks_tls_t *ktls, void *data, ks_size_t bytes, int block_ms)
{
	int r;
	int ssl_err = 0;
	uint32_t block_n = block_ms / 10;

	assert(ktls);

	if (!data || !bytes) {
		return 0;
	}

	// TODO: Removed block with "unprocessed_buffer_len".

	ktls->x++;

	if (ktls->x > 250) {
		ks_sleep_ms(1);
	}

	do {
		ERR_clear_error();
		r = SSL_read(ktls->ssl, data, (int)bytes);
		if (r == 0) {
			ssl_err = SSL_get_error(ktls->ssl, r);
			if (ssl_err != SSL_ERROR_ZERO_RETURN) {
				ks_log(KS_LOG_WARNING, "Weird SSL_read error: %d\n", ssl_err);
				if (SSL_IO_ERROR(ssl_err)) {
					ktls->ssl_io_error = KS_TRUE;
				}
			}
		}

		if (r < 0) {
			ssl_err = SSL_get_error(ktls->ssl, r);

			if (SSL_ERROR_WANT_READ_WRITE(ssl_err)) {
				if (!block_ms) {
					r = -2;
					break;
				}
				ktls->x++;
				ks_sleep_ms(10);
			} else {
				if (SSL_IO_ERROR(ssl_err)) {
					ktls->ssl_io_error = KS_TRUE;
				}

				r = -1;
				break;
			}
		}

	} while (r < 0 && SSL_ERROR_WANT_READ_WRITE(ssl_err) && ktls->x < block_n);


	if (ktls->x >= 10000 || (block_ms && ktls->x >= block_n)) {
		r = -1;
	}

	if (r > 0 && r < bytes) {
		*((char *)data + r) = '\0';
	}

	if (r >= 0) {
		ktls->x = 0;
	}

	return r;
}

// TODO: Also copied from kws.c
static int log_ssl_errors(const char *err, size_t len, void *u)
{
	ks_log(KS_LOG_ERROR, "  %s", err);	// ssl adds a \n
	return 0;
}

static void ks_tls_close(ks_tls_t *ktls)
{
	char shutdown_buffer[KS_TLS_SHUTDOWN_BUFLEN] = {0};

	if (ktls->down) {
		return;
	}

	ktls->down = 1;

	if (ktls->sock == KS_SOCK_INVALID) {
		return;
	}

	/* ks_tls_init() may call ks_tls_destroy() -> ks_tls_close() on error, so we may not have ssl here. */
	if (ktls->ssl) {
		/* first invocation of SSL_shutdown() would normally return 0 and just try to send SSL protocol close request (close_notify_alert).
		   we just slightly polite, since we want to close socket fast and
		   not bother waiting for SSL protocol close response before closing socket,
		   since we want cleanup to be done fast for scenarios like:
		   client change NAT (like jump from one WiFi to another) and now unreachable from old ip:port, however
		   immidiately reconnect with new ip:port but old session id (and thus should replace the old session/channel)
		   However it is recommended to do bidirectional shutdown
		   and also to read all the remaining data sent by the client
		   before it indicates EOF (SSL_ERROR_ZERO_RETURN).
		   To avoid stuck in this process in case of dead peers,
		   we wait for KS_TLS_SOFT_BLOCK time (1s) before we give up.
		*/
		int code = 0, rcode = 0;
		int ssl_error = 0;
		int n = 0, block_n = KS_TLS_SOFT_BLOCK_MS / 10;

		/* SSL layer was never established or underlying IO error occured */
		if (!ktls->secure_established || ktls->ssl_io_error) {
			ks_log(KS_LOG_DEBUG, "Can't shutdown TLS. Secure is not established [%d] or TLS IO error [%d].\n", ktls->secure_established, ktls->ssl_io_error);
			goto close_end;
		}

		/* connection has been already closed */
		if (SSL_get_shutdown(ktls->ssl) & SSL_SENT_SHUTDOWN) {
			ks_log(KS_LOG_DEBUG, "Can't shutdown TLS. Already closed.\n");
			goto close_end;
		}

		/* peer closes the connection */
		if (SSL_get_shutdown(ktls->ssl) & SSL_RECEIVED_SHUTDOWN) {
			ERR_clear_error();
			SSL_shutdown(ktls->ssl);
			ks_log(KS_LOG_DEBUG, "Peer closed the connection.\n");
			goto close_end;
		}

		/* us closes the connection. We do bidirection shutdown handshake */
		for(;;) {
			ERR_clear_error();
			code = SSL_shutdown(ktls->ssl);
			ssl_error = SSL_get_error(ktls->ssl, code);
			if (code <= 0 && ssl_error == SSL_ERROR_WANT_READ) {
				/* need to make sure there are no more data to read */
				for(;;) {
					ERR_clear_error();
					if ((rcode = SSL_read(ktls->ssl, shutdown_buffer, KS_TLS_SHUTDOWN_BUFLEN)) <= 0) {
						ssl_error = SSL_get_error(ktls->ssl, rcode);
						if (ssl_error == SSL_ERROR_ZERO_RETURN) {
							break;
						} else if (SSL_IO_ERROR(ssl_error)) {
							goto close_end;
						} else if (ssl_error == SSL_ERROR_WANT_READ) {
							if (++n == block_n) {
								goto close_end;
							}

							ks_sleep_ms(10);
						} else {
							goto close_end;
						}
					}
				}
			} else if (code == 0 || (code < 0 && ssl_error == SSL_ERROR_WANT_WRITE)) {
				if (++n == block_n) {
					goto close_end;
				}

				ks_sleep_ms(10);
			} else { /* code != 0 */
				goto close_end;
			}
		}
	}

 close_end:
	/* restore to blocking here, so any further read/writes will block */
	ks_socket_option(ktls->sock, KS_SO_NONBLOCK, KS_FALSE);

	if ((ktls->flags & KS_TLS_CLOSE_SOCK) && ktls->sock != KS_SOCK_INVALID) {
		ks_log(KS_LOG_DEBUG, "Shuting down TCP socket...\n");
		/* signal socket to shutdown() before close(): FIN-ACK-FIN-ACK insead of RST-RST
		   do not really handle errors here since it all going to die anyway.
		   all buffered writes if any(like SSL_shutdown() ones) will still be sent.
		 */
#ifndef WIN32
		shutdown(ktls->sock, SHUT_RDWR);
		close(ktls->sock);
#else
		shutdown(kws->sock, SD_BOTH);
		closesocket(kws->sock);
#endif
	}

	ktls->sock = KS_SOCK_INVALID;

	return;
}

KS_DECLARE(void) ks_tls_destroy(ks_tls_t **ktlsP)
{
	ks_tls_t *ktls = NULL;

	ks_assert(ktlsP);

	if (!(ktls = *ktlsP)) {
		return;
	}

	*ktlsP = NULL;

	if (!ktls->down) {
		ks_tls_close(ktls);
	}

	if (ktls->down > 1) {
		return;
	}

	ktls->down = 2;

	if (ktls->ssl) {
		SSL_free(ktls->ssl);
		ktls->ssl = NULL;
	}

	if (ktls->destroy_ssl_ctx && ktls->ssl_ctx) {
		SSL_CTX_free(ktls->ssl_ctx);
	}

	if (ktls->req_host) {
		ks_pool_free(&ktls->req_host);
		ktls->req_host = NULL;
	}

	ks_pool_free(&ktls);
	ktls = NULL;
}

// TODO: Copied from kws.c
static void setup_socket(ks_socket_t sock)
{
	ks_socket_option(sock, KS_SO_NONBLOCK, KS_TRUE);
	ks_socket_option(sock, TCP_NODELAY, KS_TRUE);
	ks_socket_option(sock, SO_KEEPALIVE, KS_TRUE);
#ifdef KS_KEEP_IDLE_INTVL
#ifndef __APPLE__
	ks_socket_option(sock, TCP_KEEPIDLE, 30);
	ks_socket_option(sock, TCP_KEEPINTVL, 30);
#endif
#endif /* KS_KEEP_IDLE_INTVL */
}

/* Server side - checks peer's cert. */
// TODO: return status?
static void tls_peer_sans(ks_tls_t *ktls)
{
	// TODO: deprecated, use SSL_get1_peer_certificate()
	X509 *cert = SSL_get_peer_certificate(ktls->ssl);
	ks_pool_t *pool = ks_pool_get(ktls);

	if (cert && SSL_get_verify_result(ktls->ssl) == X509_V_OK) {
		GENERAL_NAMES *sans = X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL);

		ktls->certified_client = KS_TRUE;

		if (sans) {
			ktls->sans_count = (ks_size_t)sk_GENERAL_NAME_num(sans);

			if (ktls->sans_count) {
				ktls->sans = ks_pool_calloc(pool, ktls->sans_count, sizeof(char *));
			}

			for (ks_size_t i = 0; i < ktls->sans_count; i++) {
				const GENERAL_NAME *gname = sk_GENERAL_NAME_value(sans, (int)i);
#if OPENSSL_VERSION_NUMBER >= 0x10100000
				const unsigned char *name = ASN1_STRING_get0_data((const ASN1_STRING *)gname->d.dNSName);
#else
				char *name = (char *)ASN1_STRING_data(gname->d.dNSName);
#endif
				ktls->sans[i] = ks_pstrdup(pool, (char *)name);
			}

			sk_GENERAL_NAME_pop_free(sans, GENERAL_NAME_free);
		}
	}

	if (cert) {
		X509_free(cert);
	}
}

/* Client/Server side. */
static int establish_peer_tls(ks_tls_t *ktls)
{
	ks_bool_t is_client;

	ks_assert(ktls);

	is_client = (ktls->type == KS_TLS_CLIENT);

	if (!ktls->sanity) {
		return -1;
	}

	if (!ktls->secure_established) {
		int code;

		if (!ktls->ssl) {
			ktls->ssl = SSL_new(ktls->ssl_ctx);
			if (!ktls->ssl) {
				unsigned long ssl_new_error = ERR_peek_error();
				ks_log(KS_LOG_ERROR, "Failed to initiate SSL with error [%lu]\n", ssl_new_error);
				return -1;
			}

			SSL_set_fd(ktls->ssl, (int)ktls->sock);

			if (ktls->init_callback) {
				ktls->init_callback(ktls, ktls->ssl);
			}
		}

		if (is_client) {
			/* Provide the server name, allowing SNI to work. */
			SSL_set_tlsext_host_name(ktls->ssl, ktls->req_host);
		}

		do {
			int ssl_err;

			ERR_clear_error();

			code = is_client ? SSL_connect(ktls->ssl) : SSL_accept(ktls->ssl) ;

			if (code == 1) {
				ktls->secure_established = KS_TRUE;
				break;
			}

			ssl_err = SSL_get_error(ktls->ssl, code);

			if (!SSL_ERROR_WANT_READ_WRITE(ssl_err)) {
				ks_log(KS_LOG_ERROR, "Failed to negotiate ssl connection with ssl error code: %d (%s)\n", ssl_err, ERR_error_string(ssl_err, NULL));
				ERR_print_errors_cb(log_ssl_errors, NULL);
				return -1;
			}

			if (ktls->block_ms) {
				ks_sleep_ms(10);
			} else {
				ks_sleep_ms(1);
			}

			ktls->sanity--;

			if (!ktls->block_ms) {
				//TODO: Is this supposed to reestablish the connection? It doesn't work.
				return -2;
			}

		} while (ktls->sanity > 0);

		if (!ktls->sanity) {
			return -1;
		}

		// TODO: Moved the peer check here from the init() fn.
		// TODO: Should we set `ktls->secure_established` based on peer check results?
		if (!is_client)
		{
			tls_peer_sans(ktls);
		}
	}

	if (ktls->ssl) {
		const char *cipher_name = SSL_get_cipher_name(ktls->ssl);
		ks_log(KS_LOG_INFO, "SSL negotiation succeeded, negotiated cipher is: %s\n", SSL_get_cipher_name(ktls->ssl));

		if (is_client) {
			strncpy(ktls->client_cipher_name, cipher_name, sizeof(ktls->client_cipher_name) - 1);
		}
	} else {
		if (is_client) {
			memset(ktls->client_cipher_name, 0, sizeof(ktls->client_cipher_name));
		}
	}

	return 0;
}

/* If the `remote_host` is set then it's a client connection. */
KS_DECLARE(ks_status_t) ks_tls_init(ks_tls_t **ktlsP, ks_socket_t sock, SSL_CTX *ssl_ctx, const char *remote_host, ks_tls_flag_t flags, ks_pool_t *pool)
{
	ks_tls_t *ktls = *ktlsP;

	// TODO: It's not safe to use existing *ktlsP. We may free it on errors.
	// if (*ktlsP) {
	// 	ktls = *ktlsP;
	// } else {
	// 	ktls = ks_pool_alloc(pool, sizeof(ks_tls_t));
	// }

	ktls = ks_pool_alloc(pool, sizeof(ks_tls_t));

	ktls->flags = flags;

	if ((flags & KS_TLS_BLOCK)) {
		ktls->block_ms = KS_TLS_BLOCK_MS;
	}

	if (remote_host) {
		ktls->type = KS_TLS_CLIENT;
		ktls->req_host = ks_pstrdup(pool, remote_host);
	} else {
		ktls->type = KS_TLS_SERVER;
	}

	ktls->sock = sock;
	ktls->sanity = KS_TLS_INIT_SANITY;
	ktls->ssl_ctx = ssl_ctx;

	setup_socket(sock);

	if (establish_peer_tls(ktls) == -1) {
		ks_log(KS_LOG_ERROR, "Failed to establish TLS layer\n");
		goto err;
	}

	// TODO: Can "down" be 0 here?
	if (ktls->down) {
		ks_log(KS_LOG_ERROR, "Link down\n");
		goto err;
	}

	*ktlsP = ktls;

	return KS_STATUS_SUCCESS;

 err:
	ks_tls_destroy(&ktls);

	return KS_STATUS_FAIL;
}

KS_DECLARE(ks_status_t) ks_tls_connect(ks_tls_t **ktlsP, ks_tls_connect_params_t *params, ks_pool_t *pool)
{
	ks_sockaddr_t addr = { 0 };
	ks_socket_t sock = KS_SOCK_INVALID;
	int family = AF_INET;
	ks_port_t port;
	char *host = NULL;
	SSL_CTX *ssl_ctx = NULL;
	int destroy_ssl_ctx = 0;
	ks_status_t status;

	ks_assert(ktlsP);
	ks_assert(params);
	ks_assert(pool);

	host = params->host;
	port = params->port ? params->port : 443;
	ssl_ctx = params->ssl_ctx;

	if (!host) {
		return KS_STATUS_FAIL;
	}

	// TODO: Should we support old SSL versions?
	if (!ssl_ctx) {
#if OPENSSL_VERSION_NUMBER >= 0x10100000
		ssl_ctx = SSL_CTX_new(TLS_client_method());
#else
		ssl_ctx = SSL_CTX_new(TLSv1_2_client_method());
#endif

		if (!ssl_ctx) {
			unsigned long ssl_ctx_error = ERR_peek_error();
			ks_log(KS_LOG_ERROR, "Failed to initiate SSL context with ssl error [%lu].\n", ssl_ctx_error);
			return KS_STATUS_FAIL;
		}

		destroy_ssl_ctx = 1;

		// TODO: Add a flag to disable validation.
		/* Enable certs validation. */
		SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, NULL);
		/* OpenSSL rejects all certs without this call. */
		SSL_CTX_set_default_verify_paths(ssl_ctx);
	}

	status = ks_addr_getbyname(host, port, AF_UNSPEC, &addr);

	if (status != KS_STATUS_SUCCESS) {
		return status;
	}

	sock = ks_socket_connect_ex(SOCK_STREAM, IPPROTO_TCP, &addr, params->timeout_ms);

	if (ks_tls_init(ktlsP, sock, ssl_ctx, params->host, params->flags, pool) != KS_STATUS_SUCCESS) {
		if (destroy_ssl_ctx) {
			SSL_CTX_free(ssl_ctx);
		}

		return KS_STATUS_FAIL;
	}

	// TODO: Can we mark ssl_ctx as destroyable even if we didn't create it?
	(*ktlsP)->destroy_ssl_ctx = KS_TRUE;

	return KS_STATUS_SUCCESS;
}

KS_DECLARE(int) ks_tls_wait_sock(ks_tls_t *ktls, uint32_t ms, ks_poll_t flags)
{
	if (ktls->sock == KS_SOCK_INVALID) return KS_POLL_ERROR;

	if (ktls->ssl && SSL_pending(ktls->ssl) > 0) return KS_POLL_READ;

	return ks_wait_sock(ktls->sock, ms, flags);
}

KS_DECLARE(int) ks_tls_test_flag(ks_tls_t *ktls, kws_flag_t flag)
{
	return ktls->flags & flag;
}
KS_DECLARE(int) ks_tls_set_flag(ks_tls_t *ktls, kws_flag_t flag)
{
	return ktls->flags |= flag;
}
KS_DECLARE(int) ks_tls_clear_flag(ks_tls_t *ktls, kws_flag_t flag)
{
	return ktls->flags &= ~flag;
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
