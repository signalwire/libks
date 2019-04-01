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

/*
	./testwebsock2 wss://echo.websocket.org/echo
	./testwebsock2 ws://laml:9393/websocket
*/

#include "libks/ks.h"
#include <tap.h>

#ifdef _WINDOWS_
#undef unlink
#define unlink _unlink
#endif

#define __MSG "\"welcome\""

static int test_ws(char *url)
{
	kws_t *kws = NULL;
	ks_pool_t *pool;
	kws_opcode_t oc;
	uint8_t *rdata;
	ks_ssize_t ret;
	ks_json_t *req = ks_json_create_object();
	ks_json_add_string_to_object(req, "url", url);

	ks_global_set_default_logger(7);

	ks_pool_open(&pool);
	ks_assert(pool);

	ok(kws_connect_ex(&kws, req, KWS_BLOCK | KWS_CLOSE_SOCK, pool, NULL, 3000) == KS_STATUS_SUCCESS);
	printf("websocket connected to [%s]\n", url);
	ks_json_delete(&req);

	ks_ssize_t bytes;

	kws_write_frame(kws, WSOC_TEXT, __MSG, strlen(__MSG));

	bytes = kws_read_frame(kws, &oc, &rdata);
	printf("read bytes=%d oc=%d [%s]\n", bytes, oc, (char *)rdata);

	ok(oc == WSOC_TEXT);
	if (bytes < 0 || oc != WSOC_TEXT || !rdata || !strstr((char *)rdata, "\"welcome\"")) {
		printf("read bytes=%d oc=%d [%s]\n", bytes, oc, (char *)rdata);
	}

	ok(strstr((char *)rdata, __MSG) != NULL);

	kws_destroy(&kws);

	ks_pool_close(&pool);

	return 1;
}

int main(int argc, char *argv[])
{
	char *url = NULL;

	ks_init();

	if (argc > 1 && strstr(argv[1], "ws") == argv[1]) {
		url = argv[1];
		plan(3);
		test_ws(url);
	} else {
		plan(1);
		ok(1 == 1);
	}

	ks_shutdown();

	done_testing();
}
