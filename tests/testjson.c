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

/* Appending walks the list from parent->child to its tail, so this also checks the links left behind */
static void append_and_check(ks_json_t *json, const char *expected, const char *what)
{
	char *str;

	if (ks_json_type_is_array(json)) {
		ks_json_add_number_to_array(json, 9);
	} else {
		ks_json_add_number_to_object(json, "z", 9);
	}

	str = ks_json_print_unformatted(json);
	is(str, expected, "%s", what);

	free(str);
}

static void test_replace_item(const char *input, int index, const char *expected)
{
	ks_json_t *json = ks_json_parse(input);
	ks_json_t *item = ks_json_get_array_item(json, index);

	ok(ks_json_replace_item(json, &item, ks_json_create_string("x")) && item == NULL, "replace %d in %s", index, input);
	append_and_check(json, expected, "replace and append");

	ks_json_delete(&json);
}

static void test_detach_item(const char *input, int index, const char *expected)
{
	ks_json_t *json = ks_json_parse(input);
	ks_json_t *item = ks_json_get_array_item(json, index);
	ks_json_t *detached = ks_json_detach_item_via_pointer(json, item);

	ok(detached == item && ks_json_enum_next(detached) == NULL, "detach %d from %s", index, input);
	ks_json_delete(&detached);
	append_and_check(json, expected, "detach and append");

	ks_json_delete(&json);
}

/* Replaces every child while iterating, continuing from each replacement; each replacement is a copy of a keyed member */
static void test_replace_while_iterating(const char *input, const char *expected)
{
	ks_json_t *json = ks_json_parse(input);
	ks_json_t *source = ks_json_parse("{\"k\":\"x\"}");
	ks_json_t *child;
	int replaced = 1;

	for (child = ks_json_enum_child(json); child; child = ks_json_enum_next(child)) {
		ks_json_t *replacement = ks_json_duplicate(ks_json_get_object_item(source, "k"), KS_TRUE);

		if (!ks_json_replace_item(json, &child, replacement)) {
			replaced = 0;
			ks_json_delete(&replacement);
			break;
		}

		child = replacement;
	}

	ok(replaced, "replace every child of %s while iterating", input);
	append_and_check(json, expected, "replace while iterating and append");

	ks_json_delete(&source);
	ks_json_delete(&json);
}

static void test_detach_several(void)
{
	ks_json_t *json = ks_json_parse("{\"a\":1,\"b\":2,\"c\":3,\"d\":4}");
	ks_json_t *items[3];
	int detached = 1;
	int i;

	/* All pointers are taken before the first detach: a middle child, then the head, then the tail */
	items[0] = ks_json_get_object_item(json, "c");
	items[1] = ks_json_get_object_item(json, "a");
	items[2] = ks_json_get_object_item(json, "d");

	for (i = 0; i < 3; i++) {
		ks_json_t *item = ks_json_detach_item_via_pointer(json, items[i]);

		if (item != items[i]) {
			detached = 0;
		}

		ks_json_delete(&item);
	}

	ok(detached, "detach several children in turn");
	append_and_check(json, "{\"b\":2,\"z\":9}", "detach several and append");

	ks_json_delete(&json);
}

static void test_replace_detach(void)
{
	ks_json_t *json;
	ks_json_t *empty;
	ks_json_t *item;
	ks_json_t *replacement;

	test_replace_item("[1,2,3]", 0, "[\"x\",2,3,9]");
	test_replace_item("[1,2,3]", 1, "[1,\"x\",3,9]");
	test_replace_item("[1,2,3]", 2, "[1,2,\"x\",9]");
	test_replace_item("[1]", 0, "[\"x\",9]");
	test_replace_item("{\"a\":1,\"b\":2,\"c\":3}", 0, "{\"a\":\"x\",\"b\":2,\"c\":3,\"z\":9}");
	test_replace_item("{\"a\":1,\"b\":2,\"c\":3}", 1, "{\"a\":1,\"b\":\"x\",\"c\":3,\"z\":9}");
	test_replace_item("{\"a\":1,\"b\":2,\"c\":3}", 2, "{\"a\":1,\"b\":2,\"c\":\"x\",\"z\":9}");
	test_replace_item("{\"a\":1}", 0, "{\"a\":\"x\",\"z\":9}");
	test_replace_item("{\"\":1}", 0, "{\"\":\"x\",\"z\":9}");

	test_detach_item("[1,2,3]", 0, "[2,3,9]");
	test_detach_item("[1,2,3]", 1, "[1,3,9]");
	test_detach_item("[1,2,3]", 2, "[1,2,9]");
	test_detach_item("[1]", 0, "[9]");
	test_detach_item("{\"a\":1,\"b\":2,\"c\":3}", 0, "{\"b\":2,\"c\":3,\"z\":9}");
	test_detach_item("{\"a\":1,\"b\":2,\"c\":3}", 1, "{\"a\":1,\"c\":3,\"z\":9}");
	test_detach_item("{\"a\":1,\"b\":2,\"c\":3}", 2, "{\"a\":1,\"b\":2,\"z\":9}");
	test_detach_item("{\"a\":1}", 0, "{\"z\":9}");

	test_replace_while_iterating("[1,2,3]", "[\"x\",\"x\",\"x\",9]");
	test_replace_while_iterating("{\"a\":1,\"b\":2,\"c\":3}", "{\"a\":\"x\",\"b\":\"x\",\"c\":\"x\",\"z\":9}");
	test_detach_several();

	/* A replacement that already has a key takes the replaced item's key instead */
	json = ks_json_parse("{\"a\":1,\"c\":3}");
	item = ks_json_get_object_item(json, "a");
	replacement = ks_json_duplicate(ks_json_get_object_item(json, "c"), KS_TRUE);
	ok(ks_json_replace_item(json, &item, replacement), "replace with a keyed item");
	append_and_check(json, "{\"a\":3,\"c\":3,\"z\":9}", "keyed replacement takes the old key");
	ks_json_delete(&json);

	/* Replacing an item with itself leaves the tree as it is */
	json = ks_json_parse("[1,2]");
	item = ks_json_get_array_item(json, 1);
	ok(ks_json_replace_item(json, &item, item) && item == NULL, "replace with itself");
	append_and_check(json, "[1,2,9]", "replace with itself and append");
	ks_json_delete(&json);

	/* Rejected arguments leave both the tree and the replacement as they were */
	json = ks_json_parse("{\"a\":1}");
	empty = ks_json_create_object();
	item = ks_json_get_object_item(json, "a");
	replacement = ks_json_create_string("x");
	ok(!ks_json_replace_item(NULL, &item, replacement), "replace with NULL parent");
	ok(!ks_json_replace_item(empty, &item, replacement), "replace in an empty parent");
	ok(!ks_json_replace_item(json, NULL, replacement), "replace with NULL item pointer");
	ok(!ks_json_replace_item(json, &item, NULL), "replace with NULL replacement");
	ok(item != NULL && ks_json_get_name(replacement) == NULL, "rejected replace changes nothing");
	ok(ks_json_detach_item_via_pointer(NULL, item) == NULL, "detach with NULL parent");
	ok(ks_json_detach_item_via_pointer(json, NULL) == NULL, "detach NULL item");
	item = NULL;
	ok(!ks_json_replace_item(json, &item, replacement), "replace NULL item");
	append_and_check(json, "{\"a\":1,\"z\":9}", "rejected calls leave the tree intact");
	ks_json_delete(&replacement);
	ks_json_delete(&empty);
	ks_json_delete(&json);
}

int main(int argc, char **argv)
{
	ks_json_t *json = ks_json_create_object();
	const char *value;

	ks_init();

	plan(72);

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

	test_replace_detach();

	ks_json_delete(&json);

	ks_shutdown();

	done_testing();
}
