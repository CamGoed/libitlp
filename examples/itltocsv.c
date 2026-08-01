/**
 * @file itltocsv.c
 * @brief Export recognized track metadata from an iTunes Library to CSV.
 */
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <itlp.h>

struct csv_field {
  const char *data;
  size_t length;
};

static const char csv_header[] =
  "title,artist,album,genre,composer,comments,kind,local_path,"
  "track_id,persistent_id,date_added,play_count,rating,unchecked\r\n";

static struct mlth *find_track_list(struct msdh **blocks) {
  size_t i;

  for (i = 0; blocks[i]; i++) {
    if (blocks[i]->type == BLOCK_MLTH)
      return (struct mlth *)blocks[i]->subblock;
  }

  return NULL;
}

static struct csv_field find_metadata(const struct mith *track, int type) {
  struct csv_field missing = {NULL, 0};
  size_t i;

  for (i = 0; track->mhohs[i]; i++) {
    if (track->mhohs[i]->type == type && track->mhohs[i]->length > 0) {
      struct csv_field found = {
        track->mhohs[i]->value,
        (size_t)track->mhohs[i]->length
      };
      return found;
    }
  }

  return missing;
}

static int write_csv_field(FILE *output, struct csv_field field) {
  size_t i;
  int quoted = 0;

  /* Metadata is length-delimited and may not have a terminating null byte.
   * Write exactly the recorded bytes instead of treating it as a C string. */
  if (!field.data || field.length == 0)
    return 0;

  for (i = 0; i < field.length; i++) {
    if (field.data[i] == ',' || field.data[i] == '"' ||
        field.data[i] == '\r' || field.data[i] == '\n') {
      quoted = 1;
      break;
    }
  }

  if (!quoted)
    return fwrite(field.data, 1, field.length, output) == field.length ? 0 : -1;

  if (fputc('"', output) == EOF)
    return -1;

  for (i = 0; i < field.length; i++) {
    if (field.data[i] == '"' && fputc('"', output) == EOF)
      return -1;
    if (fputc((unsigned char)field.data[i], output) == EOF)
      return -1;
  }

  return fputc('"', output) == EOF ? -1 : 0;
}

static int write_track(FILE *output, const struct mith *track) {
  static const int metadata_types[] = {
    TRACK_TITLE,
    ARTIST,
    ALBUM_TITLE,
    GENRE,
    COMPOSER,
    COMMENTS,
    KIND,
    LOCAL_PATH
  };
  size_t i;

  for (i = 0; i < sizeof(metadata_types) / sizeof(metadata_types[0]); i++) {
    if (write_csv_field(output, find_metadata(track, metadata_types[i])) != 0 ||
        fputc(',', output) == EOF)
      return -1;
  }

  return fprintf(output,
                 "0x%08" PRIx32 ",0x%016" PRIx64 ",%" PRIu32
                 ",%" PRIu32 ",%u,%u\r\n",
                 track->id,
                 track->persistent_id,
                 track->add_date,
                 track->playcount,
                 (unsigned int)track->rating,
                 (unsigned int)track->unchecked) < 0 ? -1 : 0;
}

static FILE *open_new_output(const char *path) {
  FILE *output;
  int output_fd;

  output_fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
  if (output_fd < 0)
    return NULL;

  output = fdopen(output_fd, "wb");
  if (!output) {
    int saved_errno = errno;
    close(output_fd);
    unlink(path);
    errno = saved_errno;
  }

  return output;
}

int main(int argc, char *argv[]) {
  struct stat output_stat;
  struct msdh **blocks;
  struct mlth *track_list;
  FILE *output;
  size_t track_count = 0;
  int write_failed = 0;

  if (argc != 3) {
    fprintf(stderr, "Usage: itlToCsv INPUT.itl OUTPUT.csv\n");
    return EXIT_FAILURE;
  }

  errno = 0;
  if (stat(argv[2], &output_stat) == 0) {
    fprintf(stderr, "Output already exists: %s\n", argv[2]);
    return EXIT_FAILURE;
  }
  if (errno != ENOENT) {
    fprintf(stderr, "Cannot inspect output path %s: %s\n",
            argv[2], strerror(errno));
    return EXIT_FAILURE;
  }

  blocks = itlp_open_library(argv[1]);
  if (!blocks) {
    fprintf(stderr, "Cannot parse iTunes Library: %s\n", argv[1]);
    return EXIT_FAILURE;
  }

  track_list = find_track_list(blocks);
  if (!track_list) {
    fprintf(stderr, "Primary track list not found in: %s\n", argv[1]);
    itlp_free(blocks);
    return EXIT_FAILURE;
  }

  output = open_new_output(argv[2]);
  if (!output) {
    fprintf(stderr, "Cannot create output %s: %s\n", argv[2], strerror(errno));
    itlp_free(blocks);
    return EXIT_FAILURE;
  }

  if (fwrite(csv_header, 1, sizeof(csv_header) - 1, output) !=
      sizeof(csv_header) - 1) {
    write_failed = 1;
  }

  while (!write_failed && track_list->miths[track_count]) {
    if (write_track(output, track_list->miths[track_count]) != 0)
      write_failed = 1;
    else
      track_count++;
  }

  if (fclose(output) != 0)
    write_failed = 1;

  itlp_free(blocks);

  if (write_failed) {
    fprintf(stderr, "Failed while writing CSV: %s\n", argv[2]);
    remove(argv[2]);
    return EXIT_FAILURE;
  }

  printf("Exported %zu tracks to %s\n", track_count, argv[2]);
  return EXIT_SUCCESS;
}
