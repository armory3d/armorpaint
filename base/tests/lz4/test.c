// Round trip and corrupt input tests for lz4_decode.
// Run: sh run.sh (needs gcc or clang with AddressSanitizer)
#include "iron_array.h"
#include "iron_lz4.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

void iron_log(const char *format, ...) {}

static int failures = 0;

static void check(bool ok, const char *name) {
	printf("%s: %s\n", ok ? "ok  " : "FAIL", name);
	if (!ok) {
		failures++;
	}
}

static buffer_t *make(const uint8_t *data, uint32_t len) {
	buffer_t *b = buffer_create(len);
	memcpy(b->buffer, data, len);
	return b;
}

static void decode_corrupt(const char *name, const uint8_t *data, uint32_t len, uint32_t olen) {
	buffer_t *in  = make(data, len);
	buffer_t *out = lz4_decode(in, olen);
	check(out != NULL && out->length == olen, name);
}

int main(void) {
	// Round trip
	uint32_t  n  = 4096;
	buffer_t *in = buffer_create(n);
	for (uint32_t i = 0; i < n; ++i) {
		in->buffer[i] = (uint8_t)((i / 7) ^ (i % 5));
	}
	buffer_t *enc = lz4_encode(in);
	buffer_t *dec = lz4_decode(enc, n);
	check(dec != NULL && memcmp(dec->buffer, in->buffer, n) == 0, "round trip");

	// The tests below feed streams that would write or read out of bounds.
	// Decoding must stay inside the buffers and return olen zero padded bytes.

	// 15 literals announced, output has room for 4
	uint8_t literals_over_output[] = {0xf0, 0x00, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
	decode_corrupt("literals longer than output", literals_over_output, sizeof(literals_over_output), 4);

	// 15 literals announced, only 2 present
	uint8_t literals_over_input[] = {0xf0, 0x00, 1, 2};
	decode_corrupt("literals longer than input", literals_over_input, sizeof(literals_over_input), 64);

	// literal length byte missing
	uint8_t length_missing[] = {0xf0};
	decode_corrupt("literal length truncated", length_missing, sizeof(length_missing), 64);

	// 1 literal, then a match whose offset bytes are missing
	uint8_t offset_missing[] = {0x10, 'a', 0x01};
	decode_corrupt("match offset truncated", offset_missing, sizeof(offset_missing), 64);

	// 4 literals, then a match of 4 + 255 * 4 bytes, output has room for 64
	uint8_t match_over_output[] = {0x4f, 'a', 'b', 'c', 'd', 0x04, 0x00, 255, 255, 255, 255, 0};
	decode_corrupt("match longer than output", match_over_output, sizeof(match_over_output), 64);

	// match length byte missing
	uint8_t match_length_missing[] = {0x4f, 'a', 'b', 'c', 'd', 0x04, 0x00};
	decode_corrupt("match length truncated", match_length_missing, sizeof(match_length_missing), 64);

	// offset points before the start of the output
	uint8_t bad_offset[] = {0x10, 'a', 0x09, 0x00};
	decode_corrupt("match offset before start", bad_offset, sizeof(bad_offset), 64);

	printf("%d failure(s)\n", failures);
	return failures != 0;
}
