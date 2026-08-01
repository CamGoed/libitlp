#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "blocks.h"
#include "inflate.h"
#include "itlp/types.h"

#define CHECK(condition) do {                                                \
  if (!(condition)) {                                                        \
    fprintf(stderr, "CHECK failed at %s:%d: %s\n",                           \
            __FILE__, __LINE__, #condition);                                 \
    exit(EXIT_FAILURE);                                                      \
  }                                                                          \
} while (0)

static void put_u32(char *buffer, size_t offset, uint32_t value) {
  memcpy(buffer + offset, &value, sizeof(value));
}

/* Type 0x24 uses the record boundary instead of the number at offset 28. */
static void test_binary_mhoh_uses_record_boundary(void) {
  char record[760] = {0};
  char *position = record;
  struct mhoh *block;

  memcpy(record, "mhoh", 4);
  put_u32(record, 4, 24);
  put_u32(record, 8, 108);
  put_u32(record, 12, 0x24);
  put_u32(record, 24, 480);
  put_u32(record, 28, 720);

  block = itlp_parse_mhoh(&position, record + sizeof(record));
  CHECK(block != NULL);
  CHECK(position == record + 108);
  CHECK(block->type == 0x24);
  CHECK(block->length == 84);
  free(block->value);
  free(block);
}

/* Type 0x24 stays flat even if its data looks like a text record length. */
static void test_binary_mhoh_does_not_infer_layout_from_payload(void) {
  char record[108] = {0};
  char *position = record;
  struct mhoh *block;
  size_t i;

  memcpy(record, "mhoh", 4);
  put_u32(record, 4, 24);
  put_u32(record, 8, sizeof(record));
  put_u32(record, 12, 0x24);
  put_u32(record, 28, sizeof(record) - 40);
  for (i = 32; i < sizeof(record); i++)
    record[i] = (char)i;

  block = itlp_parse_mhoh(&position, record + sizeof(record));
  CHECK(block != NULL);
  CHECK(position == record + sizeof(record));
  CHECK(block->length == (int)sizeof(record) - 24);
  CHECK(memcmp(block->value, record + 24, sizeof(record) - 24) == 0);
  free(block->value);
  free(block);
}

/* Unknown mhoh types stay flat even if their data looks like a text length. */
static void test_unknown_binary_mhoh_stays_flat(void) {
  char record[108] = {0};
  char *position = record;
  struct mhoh *block;
  size_t i;

  memcpy(record, "mhoh", 4);
  put_u32(record, 4, 24);
  put_u32(record, 8, sizeof(record));
  put_u32(record, 12, 0x1234);
  for (i = 24; i < sizeof(record); i++)
    record[i] = (char)i;
  put_u32(record, 28, sizeof(record) - 40);

  block = itlp_parse_mhoh(&position, record + sizeof(record));
  CHECK(block != NULL);
  CHECK(position == record + sizeof(record));
  CHECK(block->length == (int)sizeof(record) - 24);
  CHECK(memcmp(block->value, record + 24, sizeof(record) - 24) == 0);
  free(block->value);
  free(block);
}

/* Known text types read their value after the extra text header. */
static void test_known_text_mhoh_uses_nested_value(void) {
  char record[45] = {0};
  char *position = record;
  struct mhoh *block;

  memcpy(record, "mhoh", 4);
  put_u32(record, 4, 24);
  put_u32(record, 8, sizeof(record));
  put_u32(record, 12, TRACK_TITLE);
  put_u32(record, 28, 5);
  memcpy(record + 40, "Title", 5);

  block = itlp_parse_mhoh(&position, record + sizeof(record));
  CHECK(block != NULL);
  CHECK(position == record + sizeof(record));
  CHECK(block->length == 5);
  CHECK(memcmp(block->value, "Title", 5) == 0);
  free(block->value);
  free(block);
}

/* Truncated zlib data should fail even if zlib returned some output. */
static void test_inflate_rejects_truncated_stream(void) {
  static const char source[] = "a complete zlib stream";
  uLong compressed_size = compressBound(sizeof(source));
  unsigned char *compressed = malloc(compressed_size);
  char *output;
  size_t output_size = 123;

  CHECK(compressed != NULL);
  CHECK(compress(compressed, &compressed_size,
                 (const Bytef *)source, sizeof(source)) == Z_OK);
  CHECK(compressed_size > 2);

  output = itlp_inflate((char *)compressed, compressed_size - 2,
                        &output_size);
  CHECK(output == NULL);
  CHECK(output_size == 0);
  free(compressed);
}

/* Type 0x13 is XML data, so skip it using the size from the msdh record. */
static void test_unknown_msdh_is_skipped_at_its_record_boundary(void) {
  char record[128] = {0};
  char *position = record;
  struct msdh *block;

  memcpy(record, "msdh", 4);
  put_u32(record, 4, 96);
  put_u32(record, 8, 128);
  put_u32(record, 12, 0x13);

  block = itlp_parse_msdh(&position, record + sizeof(record));
  CHECK(block != NULL);
  CHECK(position == record + 128);
  CHECK(block->type == 0x13);
  CHECK(block->subblock == NULL);
  free(block);
}

/* A cut-off mhoh record should fail without moving the read position. */
static void test_truncated_mhoh_is_rejected_without_advancing(void) {
  char record[32] = {0};
  char *position = record;

  memcpy(record, "mhoh", 4);
  put_u32(record, 4, 24);
  put_u32(record, 8, 108);
  put_u32(record, 12, 0x24);

  CHECK(itlp_parse_mhoh(&position, record + sizeof(record)) == NULL);
  CHECK(position == record);
}

/* Unknown msdh types are skipped using their record boundary. */
static void test_unknown_msdh_type_is_skipped(void) {
  char record[128] = {0};
  char *position = record;
  struct msdh *block;

  memcpy(record, "msdh", 4);
  put_u32(record, 4, 96);
  put_u32(record, 8, 128);
  put_u32(record, 12, 0x7fffffff);

  block = itlp_parse_msdh(&position, record + sizeof(record));
  CHECK(block != NULL);
  CHECK(position == record + sizeof(record));
  CHECK(block->type == 0x7fffffff);
  CHECK(block->subblock == NULL);
  free(block);
}

/* A cut-off unknown msdh block is still rejected. */
static void test_truncated_unknown_msdh_is_rejected(void) {
  char record[32] = {0};
  char *position = record;

  memcpy(record, "msdh", 4);
  put_u32(record, 4, 16);
  put_u32(record, 8, 128);
  put_u32(record, 12, 0x7fffffff);

  CHECK(itlp_parse_msdh(&position, record + sizeof(record)) == NULL);
  CHECK(position == record);
}

int main(void) {
  test_binary_mhoh_uses_record_boundary();
  test_binary_mhoh_does_not_infer_layout_from_payload();
  test_unknown_binary_mhoh_stays_flat();
  test_known_text_mhoh_uses_nested_value();
  test_inflate_rejects_truncated_stream();
  test_unknown_msdh_is_skipped_at_its_record_boundary();
  test_truncated_mhoh_is_rejected_without_advancing();
  test_unknown_msdh_type_is_skipped();
  test_truncated_unknown_msdh_is_rejected();
  return 0;
}
