#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define main itltocsv_main
#include "../examples/itltocsv.c"
#undef main

#define CHECK(condition) do {                                                \
  if (!(condition)) {                                                        \
    fprintf(stderr, "CHECK failed at %s:%d: %s\n",                           \
            __FILE__, __LINE__, #condition);                                 \
    exit(EXIT_FAILURE);                                                      \
  }                                                                          \
} while (0)

static void check_csv_field(const char *data, size_t length,
                            const char *expected, size_t expected_length) {
  struct csv_field field = {data, length};
  char actual[128];
  FILE *output = tmpfile();
  size_t actual_length;

  CHECK(output != NULL);
  CHECK(write_csv_field(output, field) == 0);
  CHECK(fflush(output) == 0);
  CHECK(fseek(output, 0, SEEK_SET) == 0);

  actual_length = fread(actual, 1, sizeof(actual), output);
  CHECK(actual_length == expected_length);
  CHECK(memcmp(actual, expected, expected_length) == 0);
  CHECK(fclose(output) == 0);
}

static void test_csv_fields_are_escaped(void) {
  static const char plain[] = {'a', 'b', 'c', 'x'};

  check_csv_field(NULL, 0, "", 0);
  check_csv_field(plain, 3, "abc", 3);
  check_csv_field("A,B", 3, "\"A,B\"", 5);
  check_csv_field("A\"B", 3, "\"A\"\"B\"", 6);
  check_csv_field("A\nB", 3, "\"A\nB\"", 5);
}

static void test_track_row_contains_metadata_and_numbers(void) {
  char title_value[] = {'A', ',', ' ', 'S', 'o', 'n', 'g'};
  char artist_value[] = {'A', 'r', 't', 'i', 's', 't'};
  struct mhoh title = {TRACK_TITLE, sizeof(title_value), title_value};
  struct mhoh artist = {ARTIST, sizeof(artist_value), artist_value};
  struct mhoh *metadata[] = {&title, &artist, NULL};
  struct mith track = {0};
  static const char expected[] =
    "\"A, Song\",Artist,,,,,,,0x00000123,0x0000000000000456,7,8,80,1\r\n";
  char actual[256];
  FILE *output = tmpfile();
  size_t actual_length;

  CHECK(output != NULL);
  track.mhohs = metadata;
  track.id = 0x123;
  track.persistent_id = 0x456;
  track.add_date = 7;
  track.playcount = 8;
  track.rating = 80;
  track.unchecked = 1;

  CHECK(write_track(output, &track) == 0);
  CHECK(fflush(output) == 0);
  CHECK(fseek(output, 0, SEEK_SET) == 0);

  actual_length = fread(actual, 1, sizeof(actual), output);
  CHECK(actual_length == sizeof(expected) - 1);
  CHECK(memcmp(actual, expected, sizeof(expected) - 1) == 0);
  CHECK(fclose(output) == 0);
}

static void test_existing_output_is_not_replaced(void) {
  static const char original[] = "keep this";
  char output_path[] = "/tmp/itltocsv-test-XXXXXX";
  char actual[sizeof(original)] = {0};
  char *arguments[] = {"itlToCsv", "missing.itl", output_path, NULL};
  FILE *output;
  int output_fd;

  output_fd = mkstemp(output_path);
  CHECK(output_fd >= 0);
  CHECK(write(output_fd, original, sizeof(original) - 1) ==
        (ssize_t)(sizeof(original) - 1));
  CHECK(close(output_fd) == 0);

  CHECK(itltocsv_main(3, arguments) == EXIT_FAILURE);

  output = fopen(output_path, "rb");
  CHECK(output != NULL);
  CHECK(fread(actual, 1, sizeof(original) - 1, output) ==
        sizeof(original) - 1);
  CHECK(memcmp(actual, original, sizeof(original) - 1) == 0);
  CHECK(fclose(output) == 0);
  CHECK(unlink(output_path) == 0);
}

static void test_output_file_is_created_exclusively(void) {
  char output_path[] = "/tmp/itltocsv-exclusive-test-XXXXXX";
  FILE *output;
  int output_fd;

  output_fd = mkstemp(output_path);
  CHECK(output_fd >= 0);
  CHECK(close(output_fd) == 0);

  errno = 0;
  output = open_new_output(output_path);
  CHECK(output == NULL);
  CHECK(errno == EEXIST);
  CHECK(unlink(output_path) == 0);
}

int main(void) {
  test_csv_fields_are_escaped();
  test_track_row_contains_metadata_and_numbers();
  test_existing_output_is_not_replaced();
  test_output_file_is_created_exclusively();
  return 0;
}
