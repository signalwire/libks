/*
 * Copyright (c) 2018-2019 SignalWire, Inc
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "tap.h"
#define STR "ABCDEFGHIJKLMNOPQRSTUVWXYZ"

#ifdef KS_DEBUG_POOL

/* Include the private pool header so we can test some pool debug features */
#include "libks/internal/ks_pool.h"

#endif

static void fill(char *str, int bytes, char c)
{
	memset(str, c, bytes -1);
	*(str+(bytes-1)) = '\0';
}

struct foo {
	int x;
	char *str;
};


void cleanup(void *ptr, void *arg, ks_pool_cleanup_action_t action, ks_pool_cleanup_type_t type)
{
	struct foo *foo = (struct foo *) ptr;

	printf("Cleanup %p action: %d\n", ptr, action);

	switch(action) {
	case KS_MPCL_ANNOUNCE:
		break;
	case KS_MPCL_TEARDOWN:
		break;
	case KS_MPCL_DESTROY:
		printf("DESTROY STR [%s]\n", foo->str);
		free(foo->str);
		foo->str = NULL;
	}
}

int main(int argc, char **argv)
{
	ks_pool_t *pool;
	int err = 0;
	char *str = NULL;
	int bytes = 1024;
	ks_status_t status;
	struct foo *foo;

	ks_init();

	plan(14);

	if (argc > 1) {
		int tmp = atoi(argv[1]);

		if (tmp > 0) {
			bytes = tmp;
		} else {
			fprintf(stderr, "INVALID\n");
			exit(255);
		}
	}


	ks_pool_open(&pool);

	void *blah = ks_pool_alloc(pool, 64 * 1024);

	ks_pool_free(&blah);

	blah = ks_pool_alloc(pool, 2 * 1024);

	ks_pool_close(&pool);

	status = ks_pool_open(&pool);

	printf("OPEN: %p\n", (void *)pool);
	ok(status == KS_STATUS_SUCCESS);
	if (status != KS_STATUS_SUCCESS) {
		fprintf(stderr, "OPEN ERR: %d [%s]\n", err, ks_pool_strerror(status));
		exit(255);
	}

	printf("ALLOC:\n");
	str = ks_pool_alloc(pool, bytes);

	ok(str != NULL);
	if (!str) {
		fprintf(stderr, "ALLOC ERR\n");
		exit(255);
	}

	fill(str, bytes, '.');
	printf("%s\n", str);

	printf("FREE:\n");

	status = ks_pool_free(&str);
	if (status != KS_STATUS_SUCCESS) {
		fprintf(stderr, "FREE ERR: [%s]\n", ks_pool_strerror(err));
		exit(255);
	}

	printf("ALLOC2:\n");

	str = ks_pool_alloc(pool, bytes);

	ok(str != NULL);
	if (!str) {
		fprintf(stderr, "ALLOC2 ERR: [FAILED]\n");
		exit(255);
	}


	ks_snprintf(str, bytes, "%s", STR);
	printf("%s\n", str);


	printf("ALLOC3 (refs):\n");

	str = ks_pool_ref(str);

	printf("STR [%s]\n", str);

	ks_pool_free(&str);

	ok(str != NULL && !strcmp(str, STR));

	printf("STR [%s]\n", str);

	ks_pool_free(&str);

	ok(str == NULL);

	str = ks_pool_alloc(pool, bytes);

	ok(str != NULL);
	if (!str) {
		fprintf(stderr, "ALLOC2 ERR: [FAILED]\n");
		exit(255);
	}

	fill(str, bytes, '-');
	printf("%s\n", str);

	printf("ALLOC OBJ:\n");

	foo = ks_pool_alloc(pool, sizeof(struct foo));

	ok(foo != NULL);
	if (!foo) {
		fprintf(stderr, "ALLOC OBJ: [FAILED]\n");
		exit(255);
	} else {
		printf("ALLOC OBJ [%p]:\n", (void *) foo);
	}

	foo->x = 12;
	foo->str = strdup("This is a test 1234 abcd; This will be called on explicit free\n");
	ks_pool_set_cleanup(foo, NULL, cleanup);

	printf("FREE OBJ:\n");

	status = ks_pool_free(&foo);
	ok(status == KS_STATUS_SUCCESS);
	if (status != KS_STATUS_SUCCESS) {
		fprintf(stderr, "FREE OBJ ERR: [%s]\n", ks_pool_strerror(status));
		exit(255);
	}


	printf("ALLOC OBJ2:\n");

	foo = ks_pool_alloc(pool, sizeof(struct foo));

	ok(foo != NULL);
	if (!foo) {
		fprintf(stderr, "ALLOC OBJ2: [FAILED]\n");
		exit(255);
	} else {
		printf("ALLOC OBJ2 [%p]:\n", (void *) foo);
	}

	foo->x = 12;
	foo->str = strdup("This is a second test 1234 abcd; This will be called on pool clear/destroy\n");
	ks_pool_set_cleanup(foo, NULL, cleanup);


	printf("ALLOC OBJ3: %p\n", (void *)pool);

	foo = ks_pool_alloc(pool, sizeof(struct foo));

	ok(foo != NULL);
	if (!foo) {
		fprintf(stderr, "ALLOC OBJ3: [FAILED]\n");
		exit(255);
	} else {
		printf("ALLOC OBJ3 [%p]:\n", (void *) foo);
	}

	printf("CLEANUP: %p\n", (void *)pool);
	foo->x = 12;
	foo->str = strdup("This is a third test 1234 abcd; This will be called on pool clear/destroy\n");
	ks_pool_set_cleanup(foo, NULL, cleanup);



	printf("RESIZE: %p\n", (void *)pool);


	ks_snprintf(str, bytes, "%s", STR);
	printf("1 STR [%s]\n", str);
	bytes *= 2;
	str = ks_pool_resize(str, bytes);
	printf("2 STR [%s]\n", str);

	ok(!strcmp(str, STR));

	if (!str) {
		fprintf(stderr, "RESIZE ERR: [FAILED]\n");
		exit(255);
	}

	fill(str, bytes, '*');
	printf("%s\n", str);


	printf("FREE 2:\n");

	status = ks_pool_free(&str);
	ok(status == KS_STATUS_SUCCESS);
	if (status != KS_STATUS_SUCCESS) {
		fprintf(stderr, "FREE2 ERR: [%s]\n", ks_pool_strerror(status));
		exit(255);
	}


	printf("CLEAR:\n");
	status = ks_pool_clear(pool);

	ok(status == KS_STATUS_SUCCESS);
	if (status != KS_STATUS_SUCCESS) {
		fprintf(stderr, "CLEAR ERR: [%s]\n", ks_pool_strerror(status));
		exit(255);
	}

	printf("CLOSE:\n");
	status = ks_pool_close(&pool);

	ok(status == KS_STATUS_SUCCESS);
	if (status != KS_STATUS_SUCCESS) {
		fprintf(stderr, "CLOSE ERR: [%s]\n", ks_pool_strerror(err));
		exit(255);
	}

	ks_shutdown();

	done_testing();
}
