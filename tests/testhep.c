/*
 * Copyright (c) 2026 SignalWire, Inc
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
#include "tap.h"

#define MAX_CHUNKS 32
#define MAX_PACKET 65535

/* Offsets of the timestamp data in a packet, zeroed before comparing whole packets */
#define TIME_SEC_DATA_OFFSET 42
#define TIME_USEC_DATA_OFFSET 52

/* Header, fixed chunks and the payload chunk header */
#define HEAD_LEN_V4 (73 + 20 + 6)
#define HEAD_LEN_V6 (73 + 44 + 6)

typedef struct {
	uint16_t vendor_id;
	uint16_t type_id;
	uint16_t length;
	const unsigned char *data;
	size_t data_len;
} chunk_t;

static const char PAYLOAD[] = "{\"jsonrpc\":\"2.0\",\"method\":\"verto.invite\"}";
static const char CORRELATION_ID[] = "9b2c6d1e-3f4a-4b5c-8d6e-7f8091a2b3c4";
static const char NODE_NAME[] = "switch-node-01";

/* Captured with KS_HEPV3_DIR_SEND, 192.0.2.10:8082 -> 198.51.100.7:51234, timestamp data zeroed */
static const unsigned char GOLDEN_V4[] = {
	0x48, 0x45, 0x50, 0x33, 0x00, 0x8c, 0x00, 0x00, 0x00, 0x01, 0x00, 0x07, 0x02, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x07, 0x06, 0x00, 0x00, 0x00, 0x07, 0x00, 0x08, 0x1f, 0x92, 0x00, 0x00, 0x00, 0x08,
	0x00, 0x08, 0xc8, 0x22, 0x00, 0x00, 0x00, 0x09, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x0a, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x07, 0x3d, 0x00,
	0x00, 0x00, 0x0c, 0x00, 0x0a, 0x00, 0x00, 0x00, 0xc8, 0x00, 0x00, 0x00, 0x03, 0x00, 0x0a, 0xc0,
	0x00, 0x02, 0x0a, 0x00, 0x00, 0x00, 0x04, 0x00, 0x0a, 0xc6, 0x33, 0x64, 0x07, 0x00, 0x00, 0x00,
	0x0f, 0x00, 0x2f, 0x7b, 0x22, 0x6a, 0x73, 0x6f, 0x6e, 0x72, 0x70, 0x63, 0x22, 0x3a, 0x22, 0x32,
	0x2e, 0x30, 0x22, 0x2c, 0x22, 0x6d, 0x65, 0x74, 0x68, 0x6f, 0x64, 0x22, 0x3a, 0x22, 0x76, 0x65,
	0x72, 0x74, 0x6f, 0x2e, 0x69, 0x6e, 0x76, 0x69, 0x74, 0x65, 0x22, 0x7d,
};

/* Captured with KS_HEPV3_DIR_RECV, local 2001:db8::10:8082, remote 2001:db8::7:51234, timestamp data zeroed */
static const unsigned char GOLDEN_V6[] = {
	0x48, 0x45, 0x50, 0x33, 0x00, 0xa4, 0x00, 0x00, 0x00, 0x01, 0x00, 0x07, 0x0a, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x07, 0x06, 0x00, 0x00, 0x00, 0x07, 0x00, 0x08, 0xc8, 0x22, 0x00, 0x00, 0x00, 0x08,
	0x00, 0x08, 0x1f, 0x92, 0x00, 0x00, 0x00, 0x09, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x0a, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x07, 0x3d, 0x00,
	0x00, 0x00, 0x0c, 0x00, 0x0a, 0x00, 0x00, 0x00, 0xc8, 0x00, 0x00, 0x00, 0x05, 0x00, 0x16, 0x20,
	0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00,
	0x00, 0x00, 0x06, 0x00, 0x16, 0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x2f, 0x7b, 0x22, 0x6a, 0x73, 0x6f,
	0x6e, 0x72, 0x70, 0x63, 0x22, 0x3a, 0x22, 0x32, 0x2e, 0x30, 0x22, 0x2c, 0x22, 0x6d, 0x65, 0x74,
	0x68, 0x6f, 0x64, 0x22, 0x3a, 0x22, 0x76, 0x65, 0x72, 0x74, 0x6f, 0x2e, 0x69, 0x6e, 0x76, 0x69,
	0x74, 0x65, 0x22, 0x7d,
};

static const uint16_t BASE_TYPES_V4[] = { 0x01, 0x02, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x03, 0x04 };

static uint16_t get16(const unsigned char *p)
{
	return (uint16_t)((p[0] << 8) | p[1]);
}

static void params_v4(ks_hepv3_capture_params_t *p)
{
	memset(p, 0, sizeof(*p));
	p->ip_family = AF_INET;
	p->local_ip = "192.0.2.10";
	p->remote_ip = "198.51.100.7";
	p->local_port = 8082;
	p->remote_port = 51234;
	p->direction = KS_HEPV3_DIR_SEND;
	p->capture_id = 200;
	p->protocol_type_id = 0x3d;
	p->payload = (char *)PAYLOAD;
	p->payload_size = sizeof(PAYLOAD) - 1;
}

static void params_v6(ks_hepv3_capture_params_t *p)
{
	params_v4(p);
	p->ip_family = AF_INET6;
	p->local_ip = "2001:db8::10";
	p->remote_ip = "2001:db8::7";
	p->direction = KS_HEPV3_DIR_RECV;
}

static void set_strings(ks_hepv3_capture_params_t *p, const char *correlation_id, const char *node_name)
{
	p->correlation_id = correlation_id;
	p->node_name = node_name;
}

/**
 * Splits a packet into chunks after checking the HEP3 header and the total length field.
 * @return chunk count, -1 when the header is wrong or the chunks do not tile the packet exactly
 */
static int parse_packet(const char *packet, size_t len, chunk_t *chunks)
{
	const unsigned char *buf = (const unsigned char *)packet;
	size_t pos = 6;
	int n = 0;

	if (len < 6 || len > MAX_PACKET || memcmp(buf, "HEP3", 4) || get16(buf + 4) != len) {
		return -1;
	}

	while (pos < len) {
		chunk_t *c = &chunks[n];

		if (n == MAX_CHUNKS || len - pos < 6) {
			return -1;
		}

		c->vendor_id = get16(buf + pos);
		c->type_id = get16(buf + pos + 2);
		c->length = get16(buf + pos + 4);

		if (c->length < 6 || c->length > len - pos) {
			return -1;
		}

		c->data = buf + pos + 6;
		c->data_len = c->length - 6;
		pos += c->length;
		n++;
	}

	return n;
}

static const chunk_t *find_chunk(const chunk_t *chunks, int n, uint16_t type_id)
{
	int i;

	for (i = 0; i < n; i++) {
		if (chunks[i].vendor_id == 0 && chunks[i].type_id == type_id) {
			return &chunks[i];
		}
	}

	return NULL;
}

static ks_size_t create(const ks_hepv3_capture_params_t *p, char **out)
{
	ks_size_t len;

	*out = NULL;
	len = ks_hepv3_capture_create(p, out);

	if (len > TIME_USEC_DATA_OFFSET + 4) {
		memset(*out + TIME_SEC_DATA_OFFSET, 0, 4);
		memset(*out + TIME_USEC_DATA_OFFSET, 0, 4);
	}

	return len;
}

static void test_golden(void)
{
	ks_hepv3_capture_params_t p;
	char *out = NULL;
	ks_size_t len;

	params_v4(&p);
	len = create(&p, &out);
	cmp_ok((int)len, "==", (int)sizeof(GOLDEN_V4), "v4 packet length matches the reference packet");
	cmp_mem(out, GOLDEN_V4, sizeof(GOLDEN_V4), "v4 packet bytes match the reference packet");
	free(out);

	params_v6(&p);
	len = create(&p, &out);
	cmp_ok((int)len, "==", (int)sizeof(GOLDEN_V6), "v6 packet length matches the reference packet");
	cmp_mem(out, GOLDEN_V6, sizeof(GOLDEN_V6), "v6 packet bytes match the reference packet");
	free(out);

	params_v4(&p);
	set_strings(&p, "", "");
	len = create(&p, &out);
	ok(len == sizeof(GOLDEN_V4) && !memcmp(out, GOLDEN_V4, len), "empty strings match the plain packet");
	free(out);
}

static void test_string_chunks(void)
{
	ks_hepv3_capture_params_t p;
	chunk_t chunks[MAX_CHUNKS];
	const chunk_t *c;
	char *out = NULL;
	ks_size_t len;
	int n, i, types_ok = 1;
	size_t extra = (6 + strlen(CORRELATION_ID)) + (6 + strlen(NODE_NAME));

	params_v4(&p);
	set_strings(&p, CORRELATION_ID, NODE_NAME);
	len = create(&p, &out);
	if (!out) {
		fail("packet with string chunks built");

		return;
	}

	cmp_ok((int)len, "==", (int)(sizeof(GOLDEN_V4) + extra), "packet is the reference packet plus both string chunks");

	n = parse_packet(out, len, chunks);
	cmp_ok(n, "==", 13, "13 chunks tile the packet and match the total length");

	for (i = 0; i < 10 && i < n; i++) {
		types_ok &= (chunks[i].vendor_id == 0 && chunks[i].type_id == BASE_TYPES_V4[i]);
	}
	ok(types_ok, "fixed chunks keep their order");
	ok(n == 13 && chunks[10].type_id == 0x0011 && chunks[11].type_id == 0x0013 && chunks[12].type_id == 0x000f,
	   "correlation id, node name, then payload");
	/* The fixed and address chunks: past the 6 octet header (its length field differs), before the payload chunk */
	cmp_mem(out + 6, GOLDEN_V4 + 6, HEAD_LEN_V4 - 6 - 6, "chunks before the string chunks match the reference packet");

	c = find_chunk(chunks, n, 0x0011);
	ok(c && c->length == 6 + strlen(CORRELATION_ID) && c->data_len == strlen(CORRELATION_ID) &&
	   !memcmp(c->data, CORRELATION_ID, c->data_len), "correlation id chunk carries the string without NUL");

	c = find_chunk(chunks, n, 0x0013);
	ok(c && c->length == 6 + strlen(NODE_NAME) && !memcmp(c->data, NODE_NAME, c->data_len),
	   "node name chunk carries the string without NUL");

	c = find_chunk(chunks, n, 0x000f);
	ok(c && c->data_len == sizeof(PAYLOAD) - 1 && !memcmp(c->data, PAYLOAD, c->data_len), "payload intact");
	free(out);

	set_strings(&p, NULL, NODE_NAME);
	len = create(&p, &out);
	n = parse_packet(out, len, chunks);
	ok(n == 12 && !find_chunk(chunks, n, 0x0011) && find_chunk(chunks, n, 0x0013), "node name alone");
	free(out);

	set_strings(&p, CORRELATION_ID, NULL);
	len = create(&p, &out);
	n = parse_packet(out, len, chunks);
	ok(n == 12 && find_chunk(chunks, n, 0x0011) && !find_chunk(chunks, n, 0x0013), "correlation id alone");
	free(out);

	params_v6(&p);
	set_strings(&p, CORRELATION_ID, NODE_NAME);
	len = create(&p, &out);
	n = parse_packet(out, len, chunks);
	ok(n == 13 && len == sizeof(GOLDEN_V6) + extra && find_chunk(chunks, n, 0x0011) && find_chunk(chunks, n, 0x0013),
	   "v6 packet carries both string chunks");
	free(out);
}

static void test_string_cap(void)
{
	ks_hepv3_capture_params_t p;
	chunk_t chunks[MAX_CHUNKS];
	const chunk_t *c;
	char *at_cap, *over_cap;
	char *out = NULL;
	ks_size_t len;
	int n;

	at_cap = malloc(KS_HEPV3_MAX_STRING_CHUNK_LEN + 2);
	over_cap = malloc(KS_HEPV3_MAX_STRING_CHUNK_LEN + 2);
	if (!at_cap || !over_cap) {
		fail("string cap buffers allocated");
		free(at_cap);
		free(over_cap);

		return;
	}

	memset(at_cap, 'a', KS_HEPV3_MAX_STRING_CHUNK_LEN);
	at_cap[KS_HEPV3_MAX_STRING_CHUNK_LEN] = '\0';
	memset(over_cap, 'b', KS_HEPV3_MAX_STRING_CHUNK_LEN + 1);
	over_cap[KS_HEPV3_MAX_STRING_CHUNK_LEN + 1] = '\0';

	params_v4(&p);

	set_strings(&p, at_cap, NULL);
	len = create(&p, &out);
	n = parse_packet(out, len, chunks);
	c = find_chunk(chunks, n, 0x0011);
	ok(c && c->data_len == KS_HEPV3_MAX_STRING_CHUNK_LEN, "correlation id at the cap is sent whole");
	free(out);

	set_strings(&p, over_cap, over_cap);
	len = create(&p, &out);
	ok(len == sizeof(GOLDEN_V4) && !memcmp(out, GOLDEN_V4, len), "strings over the cap are not sent");
	free(out);

	set_strings(&p, over_cap, NODE_NAME);
	len = create(&p, &out);
	n = parse_packet(out, len, chunks);
	ok(n == 12 && !find_chunk(chunks, n, 0x0011) && find_chunk(chunks, n, 0x0013), "an oversized value does not affect the other chunk");
	free(out);

	free(at_cap);
	free(over_cap);
}

/**
 * Builds a packet with payload_size octets of payload and checks it holds expect_total octets,
 * with the payload chunk last and carrying the leading expect_total - head octets of the payload.
 * @param head  packet length without the payload data
 */
static void check_clamp(ks_hepv3_capture_params_t *p, const char *payload, size_t payload_size, size_t head,
						size_t expect_total, const char *name)
{
	chunk_t chunks[MAX_CHUNKS];
	const chunk_t *c = NULL;
	char *out = NULL;
	ks_size_t len;
	int n;

	p->payload = (char *)payload;
	p->payload_size = payload_size;
	len = create(p, &out);
	n = parse_packet(out, len, chunks);

	if (n > 0) {
		c = &chunks[n - 1];
	}

	ok(len == expect_total && c && c->type_id == 0x000f && c->data_len == expect_total - head &&
	   !memcmp(c->data, payload, c->data_len), "%s", name);
	free(out);
}

static void test_clamp(void)
{
	ks_hepv3_capture_params_t p;
	char *payload, *max_strings;
	size_t big = 70000, head, i;

	if (!(payload = malloc(big))) {
		fail("clamp payload allocated");

		return;
	}

	for (i = 0; i < big; i++) {
		payload[i] = (char)('0' + (i % 64));
	}

	params_v4(&p);
	head = HEAD_LEN_V4;
	check_clamp(&p, payload, MAX_PACKET - head - 1, head, MAX_PACKET - 1, "v4 one below the limit");
	check_clamp(&p, payload, MAX_PACKET - head, head, MAX_PACKET, "v4 exactly at the limit");
	check_clamp(&p, payload, MAX_PACKET - head + 1, head, MAX_PACKET, "v4 one above the limit is truncated");
	check_clamp(&p, payload, big, head, MAX_PACKET, "v4 far above the limit is truncated");

	params_v6(&p);
	head = HEAD_LEN_V6;
	check_clamp(&p, payload, MAX_PACKET - head, head, MAX_PACKET, "v6 exactly at the limit");
	check_clamp(&p, payload, MAX_PACKET - head + 1, head, MAX_PACKET, "v6 one above the limit is truncated");

	/* String chunks count against the limit */
	set_strings(&p, CORRELATION_ID, NODE_NAME);
	head = HEAD_LEN_V6 + 6 + strlen(CORRELATION_ID) + 6 + strlen(NODE_NAME);
	check_clamp(&p, payload, MAX_PACKET - head, head, MAX_PACKET, "v6 with strings exactly at the limit");
	check_clamp(&p, payload, MAX_PACKET - head + 1, head, MAX_PACKET, "v6 with strings one above the limit is truncated");
	check_clamp(&p, payload, big, head, MAX_PACKET, "v6 with strings far above the limit is truncated");

	if (!(max_strings = malloc(KS_HEPV3_MAX_STRING_CHUNK_LEN + 1))) {
		fail("strings at the cap allocated");
		free(payload);

		return;
	}

	memset(max_strings, 'n', KS_HEPV3_MAX_STRING_CHUNK_LEN);
	max_strings[KS_HEPV3_MAX_STRING_CHUNK_LEN] = '\0';
	set_strings(&p, max_strings, max_strings);
	head = HEAD_LEN_V6 + 2 * (6 + KS_HEPV3_MAX_STRING_CHUNK_LEN);
	check_clamp(&p, payload, big, head, MAX_PACKET, "v6 with strings at the cap is truncated");

	free(max_strings);
	free(payload);
}

static void check_addrs(int family, const char *local_ip, const char *remote_ip, const unsigned char *expect_src,
						const unsigned char *expect_dst, size_t addr_len, const char *name)
{
	ks_hepv3_capture_params_t p;
	chunk_t chunks[MAX_CHUNKS];
	const chunk_t *src, *dst;
	char *out = NULL;
	ks_size_t len;
	int n;

	params_v4(&p);
	p.ip_family = family;
	p.local_ip = (char *)local_ip;
	p.remote_ip = (char *)remote_ip;

	len = create(&p, &out);
	n = parse_packet(out, len, chunks);
	src = find_chunk(chunks, n, family == AF_INET ? 0x0003 : 0x0005);
	dst = find_chunk(chunks, n, family == AF_INET ? 0x0004 : 0x0006);

	ok(n == 11 && src && dst && src->data_len == addr_len && dst->data_len == addr_len &&
	   !memcmp(src->data, expect_src, addr_len) && !memcmp(dst->data, expect_dst, addr_len), "%s", name);
	free(out);
}

static void test_addresses(void)
{
	static const unsigned char zero[16] = { 0 };
	static const unsigned char v4_local[4] = { 192, 0, 2, 10 };
	static const unsigned char v4_bcast[4] = { 255, 255, 255, 255 };
	static const unsigned char v6_local[16] = { 0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10 };

	check_addrs(AF_INET, NULL, NULL, zero, zero, 4, "v4 NULL addresses are 0.0.0.0");
	check_addrs(AF_INET, "192.0.2.10", NULL, v4_local, zero, 4, "v4 NULL remote is 0.0.0.0");
	check_addrs(AF_INET, "not-an-ip", "", zero, zero, 4, "v4 unparsable addresses are 0.0.0.0");
	check_addrs(AF_INET, "2001:db8::10", "192.0.2.256", zero, zero, 4, "v4 rejects a v6 address and an out of range octet");
	check_addrs(AF_INET, "255.255.255.255", "192.0.2.10", v4_bcast, v4_local, 4, "v4 255.255.255.255 is kept");

	check_addrs(AF_INET6, NULL, NULL, zero, zero, 16, "v6 NULL addresses are ::");
	check_addrs(AF_INET6, "2001:db8::10", "garbage", v6_local, zero, 16, "v6 unparsable remote is ::");
	check_addrs(AF_INET6, "192.0.2.10", NULL, zero, zero, 16, "v6 rejects a v4 address");
}

static void test_empty_payload(void)
{
	ks_hepv3_capture_params_t p;
	char *out = NULL;

	params_v4(&p);
	p.payload_size = 0;
	cmp_ok((int)ks_hepv3_capture_create(&p, &out), "==", 0, "zero payload size rejected");
	ok(out == NULL, "no buffer for zero payload size");

	params_v4(&p);
	p.payload = NULL;
	out = NULL;
	cmp_ok((int)ks_hepv3_capture_create(&p, &out), "==", 0, "NULL payload rejected");
	ok(out == NULL, "no buffer for NULL payload");
}

int main(int argc, char **argv)
{
	ks_init();

	plan(NO_PLAN);

	test_golden();
	test_string_chunks();
	test_string_cap();
	test_clamp();
	test_addresses();
	test_empty_payload();

	ks_shutdown();

	done_testing();
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
