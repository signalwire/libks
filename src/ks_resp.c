/*
* Copyright (c) 2015 José Carlos Nieto, https://menteslibres.net/xiam
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
* LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
* OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
* WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*
* */

#include <string.h>
#include <stdlib.h>

#include <stddef.h>
#include <limits.h>
#include <ctype.h>

#include "libks/ks_resp.h"

// createRespObject allocates a new object of the given type.
respObject *createRespObject(int type) {
	respObject *r;

	r = (respObject *)malloc(sizeof(respObject));
	if (r == NULL) {
		return NULL;
	}

	r->type = type;
	r->elements = 0;
	r->len = 0;

	r->str = NULL;
	r->element = NULL;

	return r;
}

// freeRespObject frees the allocated memory of the given object.
void freeRespObject(respObject *r) {
	unsigned int i;

	switch (r->type) {
	case RESP_OBJECT_INTEGER:
		break;
	case RESP_OBJECT_ARRAY:
		if (r->element != NULL) {
			for (i = 0; i < r->elements; i++) {
				if (r->element[i] != NULL) {
					freeRespObject(r->element[i]);
				}
			}
			free(r->element);
		}
		break;
	case RESP_OBJECT_BINARY:
	case RESP_OBJECT_ERROR:
	case RESP_OBJECT_STATUS:
		if (r->str != NULL) {
			free(r->str);
		}
		break;
	}

	free(r);
}

// createRespArray allocates a RESP object of type array with the given number
// of elements.
respObject *createRespArray(unsigned int elements) {
	respObject *r;
	unsigned int i;

	if (elements > RESP_ARRAY_MAX_LEN) {
		return NULL;
	}

	r = createRespObject(RESP_OBJECT_ARRAY);

	r->elements = elements;
	r->element = (respObject **)malloc(sizeof(respObject *)*elements);

	if (r->element == NULL) {
		freeRespObject(r);
		return NULL;
	}

	for (i = 0; i < r->elements; i++) {
		r->element[i] = NULL;
	}

	return r;
}

// createRespInteger allocates a RESP object of type integer with the given
// value.
respObject *createRespInteger(int integer) {
	respObject *r;

	r = createRespObject(RESP_OBJECT_INTEGER);

	r->integer = integer;

	return r;
}

// createRespNil allocates a RESP object of type nil.
respObject *createRespNil() {
	respObject *r;

	r = createRespObject(RESP_OBJECT_NIL);

	return r;
}

// createRespString allocates a RESP object of type string with the given
// value.
respObject *createRespString(int type, unsigned char *str) {
	respObject *r;
	int len;

	for (len = 0; *(str + ++len) != '\0';);

	if (len > RESP_STRING_MAX_LEN) {
		return NULL;
	}

	if (type == RESP_OBJECT_ERROR || type == RESP_OBJECT_STATUS) {

		r = createRespObject(type);
		if (r == NULL) {
			return NULL;
		}

		r->str = (unsigned char *)malloc((sizeof(unsigned char)*len) + 1);
		if (r->str == NULL) {
			freeRespObject(r);
			return NULL;
		}

		memcpy(r->str, str, len);
		r->str[len] = '\0';
		r->len = len;
		return r;
	}

	return NULL;
}

// createRespBulk allocates a RESP object that is binary safe.
respObject *createRespBulk(unsigned char *str, int len) {
	respObject *r;

	if (len > RESP_STRING_MAX_LEN) {
		return NULL;
	}

	r = createRespObject(RESP_OBJECT_BINARY);

	if (r == NULL) {
		return NULL;
	}

	if (len < 0) return r;

	r->str = (unsigned char *)malloc(sizeof(unsigned char)*len + 1);

	if (r->str == NULL) {
		freeRespObject(r);
		return NULL;
	}

	memcpy(r->str, str, len);
	r->str[len] = '\0';
	r->len = len;

	return r;
}

// respNextLine is an internal function used to read the next line to decode.
int respNextLine(unsigned char *src, int srclen) {
	int i;
	for (i = 1; i < RESP_LINE_MAX_LEN; i++) {
		if (srclen >= 0 && i >= srclen) break;
		if (src[i] == '\n' && src[i - 1] == '\r') {
			return i + 1;
		}
	}
	return -1;
}

// respDecode reads a RESP encoded message from src and dumps it into r.
int respDecode(respObject **r, unsigned char *src, int srclen) {
	int offset;

	int buflen;
	int i, t;

	offset = respNextLine(src, srclen);
	if (offset < 0) {
		return RESP_ERROR_INCOMPLETE_MESSAGE;
	}

	*r = NULL;
	switch (src[0]) {
	case ':':
		*r = createRespInteger((int)respAtoi(src + 1));
		if (*r == NULL) {
			return RESP_ERROR_CANNOT_ALLOCATE;
		}
		break;
	case '+':
		src[offset - 2] = '\0';
		*r = createRespString(RESP_OBJECT_STATUS, (unsigned char *)src + 1);
		if (*r == NULL) {
			return RESP_ERROR_CANNOT_ALLOCATE;
		}
		break;
	case '-':
		src[offset - 2] = '\0';
		*r = createRespString(RESP_OBJECT_ERROR, (unsigned char *)src + 1);
		if (*r == NULL) {
			return RESP_ERROR_CANNOT_ALLOCATE;
		}
		break;
	case '$':
		/* Get length of the buffer */
		buflen = respAtoi(src + 1);

		if (buflen < -1 || buflen > RESP_STRING_MAX_LEN) {
			return RESP_ERROR_WRONG_FORMAT;
		}

		if (buflen == -1) {
			buflen = -2;
			// @todo finish this so a NULL represented as "$-1\r\n" can be returned properly
		}

		/* Making sure we have a message. */
		if (src[offset + buflen] != '\r' || src[offset + buflen + 1] != '\n') {
			return RESP_ERROR_INCOMPLETE_MESSAGE;
		}

		src[offset + buflen] = '\0';

		/* Reading length of the buffer. */
		*r = createRespBulk(src + offset, buflen);
		if (*r == NULL) {
			return RESP_ERROR_CANNOT_ALLOCATE;
		}

		/* Length of the buffer plus \r\n */
		if (buflen > 0) offset += buflen + 2;
		break;
	case '*':
		/* Get length of the buffer */
		buflen = respAtoi(src + 1);
		if (buflen < 0 || buflen > RESP_ARRAY_MAX_LEN) {
			return RESP_ERROR_WRONG_FORMAT;
		}

		*r = createRespArray(buflen);
		if (*r == NULL) {
			return RESP_ERROR_CANNOT_ALLOCATE;
		}

		for (i = 0; i < buflen; i++) {
			t = respDecode(&((*r)->element[i]), src + offset, -1);
			if (t < 0) {
				freeRespObject(*r);
				return RESP_ERROR_INCOMPLETE_MESSAGE;
			}
			offset += t;
		}
		break;
	}

	if (*r == NULL) {
		return RESP_ERROR_UNKNOWN_MESSAGE;
	}

	return offset;
}

// respEncode marshals a RESP object into dest.
int respEncode(respObject *r, unsigned char *dest) {
	int l = 0;
	unsigned int j;
	unsigned char sizebuf[RESP_MAX_ITOA_LEN];

	switch (r->type) {
	case RESP_OBJECT_INTEGER:
		dest[0] = ':';
		l = respItoa(sizebuf, r->integer);
		memcpy(dest + 1, sizebuf, l);
		l = l + 1;
		break;
	case RESP_OBJECT_BINARY:
		dest[0] = '$';

		l = respItoa(sizebuf, (int)r->len);
		memcpy(dest + 1, sizebuf, l);

		dest[l + 1] = '\r';
		dest[l + 2] = '\n';

		memcpy(dest + l + 3, r->str, r->len);

		l = l + 3 + r->len;
		break;
	case RESP_OBJECT_STATUS:
		dest[0] = '+';
		memcpy(dest + 1, r->str, r->len);
		l = r->len + 1;
		break;
	case RESP_OBJECT_ERROR:
		dest[0] = '-';
		memcpy(dest + 1, r->str, r->len);
		l = r->len + 1;
		break;
	case RESP_OBJECT_ARRAY:
		dest[0] = '*';

		l = respItoa(sizebuf, (int)r->elements);

		memcpy(dest + 1, sizebuf, l);

		dest[l + 1] = '\r';
		dest[l + 2] = '\n';

		l = l + 3;

		for (j = 0; j < r->elements; j++) {
			l = l + respEncode(r->element[j], dest + l);
		}

		dest[l + 1] = '\0';

		return l;

		break;
	case RESP_OBJECT_NIL:
		memcpy(dest, "$-1", 3);
		l = 3;
		break;
	}

	dest[l] = '\r';
	dest[l + 1] = '\n';
	dest[l + 2] = '\0';

	return l + 2;
}

/* Stolen from https://github.com/redis/hiredis/blob/ec229678c22e673e8d48070593f7b028a514882a/sds.c#L299 */
int respItoa(unsigned char *s, int value) {
	unsigned char *p, aux;
	unsigned int v;
	unsigned int l;

	/* Generate the string representation, this method produces
	* an reversed string. */
	v = (value < 0) ? -value : value;
	p = s;
	do {
		*p++ = '0' + (v % 10);
		v /= 10;
	} while (v);
	if (value < 0) *p++ = '-';

	/* Compute length and add null term. */
	l = (unsigned int)(p - s);
	*p = '\0';

	/* Reverse the string. */
	p--;
	while (s < p) {
		aux = *s;
		*s = *p;
		*p = aux;
		s++;
		p--;
	}
	return l;
}

/* Stolen from
* http://tinodidriksen.com/2010/02/16/cpp-convert-string-to-int-speed/ */
int respAtoi(const unsigned char *p) {
	int x = 0;
	int neg = 0;
	if (*p == '-') {
		neg = 1;
		++p;
	}
	while (*p >= '0' && *p <= '9') {
		x = (x * 10) + (*p - '0');
		if (x > INT_MAX) {
			return -1;
		}
		++p;
	}
	if (neg) {
		x = -x;
	}
	return x;
}
