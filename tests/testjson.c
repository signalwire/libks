/*
 * Copyright (c) 2019 SignalWire, Inc
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
#include <math.h>
#include "tap.h"

static void test_print_number(double number, const char *expected)
{
	ks_json_t *item = ks_json_create_number(number);
	char *str = ks_json_print_unformatted(item);

	is(str, expected, "print number %s", expected);

	free(str);
	ks_json_delete(&item);
}

static void test_parse_print(const char *input, const char *expected)
{
	ks_json_t *item = ks_json_parse(input);
	char *str = ks_json_print_unformatted(item);

	is(str, expected, "parse and print %s", input);

	free(str);
	ks_json_delete(&item);
}

int main(int argc, char **argv)
{
	ks_json_t *json = ks_json_create_object();
	const char *value;

	ks_init();

	plan(19);

	value = ks_json_get_object_string(json, "key", NULL);
	ok(value == NULL);

	value = ks_json_get_object_string(json, "key", "value");
	ok(strcmp(value, "value") == 0);

	/*
	 * Integral values print without a fraction, others with six decimals.
	 * This is the local print_number change in the bundled cJSON; an
	 * unmodified cJSON prints "1.5", "0.3", "1e+15" and fails these cases.
	 */
	test_print_number(0, "0");
	test_print_number(42, "42");
	test_print_number(-7, "-7");
	test_print_number(1e15, "1000000000000000");
	test_print_number(1.5, "1.500000");
	test_print_number(-2.25, "-2.250000");
	test_print_number(0.1 + 0.2, "0.300000");
	test_print_number(100 * 1.1, "110.000000");
	test_print_number(INFINITY, "null");
	test_parse_print("[0.1,10]", "[0.100000,10]");

	/* float values widened to double print rounded to six decimals */
	test_print_number((double)0.1f, "0.100000");
	test_print_number((double)99.9f, "99.900002");

	/* Integral values print in full across the long long range, beyond it with an exponent */
	test_print_number(9007199254740992.0, "9007199254740992");
	test_parse_print("[1700000000000000]", "[1700000000000000]");
	test_print_number(1e17, "100000000000000000");
	test_print_number(-9223372036854775808.0, "-9223372036854775808");
	test_print_number(1e19, "1e+19");

	ks_json_delete(&json);

	ks_shutdown();

	done_testing();
}
