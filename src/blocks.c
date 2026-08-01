#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "itlp/types.h"
#include "memtools.h"
#include "blocks.h"
#include "free.h"

/* Return whether [position, position + length) lies inside the parse range. */
static bool has_bytes(const char *position, const char *end, size_t length) {
  return position <= end && length <= (size_t)(end - position);
}

/* Validate a record with separate header and total-length fields. */
static bool bounded_record(const char *position, const char *end,
                           const char marker[4], size_t minimum_header,
                           uint32_t *header_length, uint32_t *record_length,
                           const char **record_end) {
  if (!has_bytes(position, end, 12) || memcmp(position, marker, 4) != 0) {
    fprintf(stderr, "iTunes parser: Expected %.4s record.\n", marker);
    return false;
  }

  *header_length = read_uint32t_offset((char *)position, 4);
  *record_length = read_uint32t_offset((char *)position, 8);
  if (*header_length < minimum_header || *record_length < *header_length ||
      !has_bytes(position, end, *record_length)) {
    fprintf(stderr,
            "iTunes parser: Invalid %.4s bounds (header %u, record %u, "
            "remaining %zu).\n",
            marker, *header_length, *record_length,
            position <= end ? (size_t)(end - position) : 0);
    return false;
  }

  *record_end = position + *record_length;
  return true;
}

/* Validate a record whose header length is also its consumed length. */
static bool bounded_header(const char *position, const char *end,
                           const char marker[4], size_t minimum_header,
                           uint32_t *header_length) {
  if (!has_bytes(position, end, 8) || memcmp(position, marker, 4) != 0) {
    fprintf(stderr, "iTunes parser: Expected %.4s record.\n", marker);
    return false;
  }

  *header_length = read_uint32t_offset((char *)position, 4);
  if (*header_length < minimum_header ||
      !has_bytes(position, end, *header_length)) {
    fprintf(stderr,
            "iTunes parser: Invalid %.4s header length %u (remaining %zu).\n",
            marker, *header_length,
            position <= end ? (size_t)(end - position) : 0);
    return false;
  }
  return true;
}

/* Allocate a zero-terminated child pointer array without size overflow. */
static void **allocate_pointer_array(uint32_t count) {
  if ((size_t)count > SIZE_MAX / sizeof(void *) - 1)
    return NULL;
  return calloc((size_t)count + 1, sizeof(void *));
}

/* Reject child counts that cannot fit in the containing record. */
static bool child_count_fits(uint32_t count, size_t minimum_size,
                             const char *position, const char *end) {
  if (!has_bytes(position, end, 0) ||
      (uint64_t)count * minimum_size > (uint64_t)(end - position)) {
    fprintf(stderr, "iTunes parser: Child record count exceeds parent bounds.\n");
    return false;
  }
  return true;
}

struct msdh* itlp_parse_msdh(char **msdh_string, const char *buffer_end) {
  struct msdh *msdh_block;
  char *record_start;
  const char *record_end;
  uint32_t msdh_header_length, record_length;

  record_start = *msdh_string;
  if (!bounded_record(record_start, buffer_end, "msdh", 16,
                      &msdh_header_length, &record_length, &record_end))
    return NULL;

  /* Allocating memory for the msdh block */
  msdh_block = malloc(sizeof(struct msdh));
  if (!msdh_block)
    return NULL;

  msdh_block->subblock = NULL;

  /* Reading header */
  msdh_block->type = read_uint32t_offset(*msdh_string,12);

  /* Skipping header section */
  *msdh_string += msdh_header_length;

  /* Parse msdh's subblock */
  switch (msdh_block->type) {
  case BLOCK_MLIH:
    msdh_block->subblock = (void*)itlp_parse_mlih(msdh_string, record_end);
    break;
  case BLOCK_MLTH_ALT:
  case BLOCK_MLTH:
    msdh_block->subblock = (void*)itlp_parse_mlth(msdh_string, record_end);
    break;
  case BLOCK_MFDH:
    msdh_block->subblock = (void*)itlp_parse_mfdh(msdh_string, record_end);
    break;
  case BLOCK_MLRH:
    msdh_block->subblock = (void*)itlp_parse_mlrh(msdh_string, record_end);
    break;
  case BLOCK_MLAH:
    msdh_block->subblock = (void*)itlp_parse_mlah(msdh_string, record_end);
    break;
  case BLOCK_MLSH:
    msdh_block->subblock = (void*)itlp_parse_mlsh(msdh_string, record_end);
    break;
  case BLOCK_MHGH:
    msdh_block->subblock = (void*)itlp_parse_mhgh(msdh_string, record_end);
    break;
  case BLOCK_STSH:
    msdh_block->subblock = (void*)itlp_parse_stsh(msdh_string, record_end);
    break;
  case BLOCK_MLPH_ALT:
  case BLOCK_MLPH:
    msdh_block->subblock = (void*)itlp_parse_mlph(msdh_string, record_end);
    break;
  case BLOCK_MLQH:
    msdh_block->subblock = (void*)itlp_parse_mlqh(msdh_string, record_end);
    break;
  case BLOCK_FILE:
    msdh_block->subblock = (void*)itlp_parse_file(msdh_string, record_end);
    break;
  case BLOCK_XML:
    /* Newer libraries can contain an auxiliary XML record that is not needed
     * for track recovery. Its outer msdh length lets us skip it safely. */
    fprintf(stderr,
            "iTunes parser: Skipping unsupported msdh block type 0x%08x.\n",
            msdh_block->type);
    *msdh_string = record_start + record_length;
    return msdh_block;
  default:
    fprintf(stderr, "iTunes parser: Unsupported msdh block type 0x%08x.\n",
            msdh_block->type);
    free(msdh_block);
    return NULL;
  }

  if (!msdh_block->subblock) {
    free(msdh_block);
    return NULL;
  }

  if (*msdh_string != record_end) {
    fprintf(stderr, "iTunes parser: msdh child does not fill record.\n");
    itlp_free_msdh(msdh_block);
    return NULL;
  }

  *msdh_string = record_start + record_length;
  
  return msdh_block;
}

struct mith* itlp_parse_mith(char **mith_string, const char *buffer_end) {
  struct mith *mith_block;
  struct mhoh *current_mhoh;
  const char *record_end;
  uint32_t header_length, record_length;
  unsigned int i, mhohs_number;

  if (!bounded_record(*mith_string, buffer_end, "mith", 0x88,
                      &header_length, &record_length, &record_end))
    return NULL;

  /* Memory allocation for mith block */
  mith_block = malloc(sizeof(struct mith));
  if (!mith_block)
    return NULL;

  /* Reading various mith informations */
  mhohs_number = read_uint32t_offset(*mith_string, 12);

  /* Reading mith metadata */
  mith_block->rating = read_uint8t_offset(*mith_string, 108);
  mith_block->id = read_uint32t_offset(*mith_string, 16);
  mith_block->playcount = read_uint32t_offset(*mith_string, 0x4C);
  mith_block->unchecked = read_uint8t_offset(*mith_string, 0x6E);
  mith_block->add_date = read_uint32t_offset(*mith_string, 0x78);
  mith_block->persistent_id = read_uint64t_offset(*mith_string, 0x80);

  /* Persistent ID at 0x80 */
  /* Track number at 0x10 */

  /* Skipping header */
  *mith_string += header_length;

  if (!child_count_fits(mhohs_number, 16, *mith_string, record_end)) {
    free(mith_block);
    return NULL;
  }

  /* Memory allocation for mhoh block embedded in mith */
  mith_block->mhohs = (struct mhoh **)allocate_pointer_array(mhohs_number);
  if (!mith_block->mhohs) {
    free(mith_block);
    return NULL;
  }

  /* Parsing embedded mhoh in mith */
  for (i = 0; i < mhohs_number; i++) {
    current_mhoh = itlp_parse_mhoh(mith_string, record_end);
    if (!current_mhoh) {
      itlp_free_mith(mith_block);
      return NULL;
    }
    
    mith_block->mhohs[i] = current_mhoh;
  }

  /* NULL-ending the array */
  mith_block->mhohs[mhohs_number] = NULL;

  if (*mith_string != record_end) {
    fprintf(stderr, "iTunes parser: mith child records do not fill record.\n");
    itlp_free_mith(mith_block);
    return NULL;
  }
  
  return mith_block;
}

struct mhoh* itlp_parse_mhoh(char **mhoh_string, const char *buffer_end) {
  struct mhoh *block;
  char *record_start;
  const char *record_end;
  size_t mhoh_value_size;
  uint32_t header_length, nested_value_size, record_length;

  record_start = *mhoh_string;
  if (!bounded_record(record_start, buffer_end, "mhoh", 16,
                      &header_length, &record_length, &record_end))
    return NULL;

  /* Allocating memory for mhoh structure */
  block = malloc(sizeof(struct mhoh));
  if (!block)
    return NULL;

  /* Reading mhoh's type and value size */
  block->type = read_uint32t_offset(*mhoh_string,12);
  
  /* Text mhoh records have 16 bytes of value metadata after their 24-byte
   * header: the value length is at offset 28 and the text begins at 40.
   * Flat binary records, notably type 0x24, begin at the declared header end;
   * their bytes at offset 28 are data rather than a value length. */
  nested_value_size = record_length >= 32
    ? read_uint32t_offset(record_start, 28)
    : 0;
  if (block->type != 0x24 && record_length >= 40 &&
      nested_value_size == record_length - 40) {
    mhoh_value_size = nested_value_size;
    *mhoh_string = record_start + 40;
  } else {
    mhoh_value_size = record_length - header_length;
    *mhoh_string = record_start + header_length;
  }

  if (mhoh_value_size > INT_MAX) {
    fprintf(stderr, "iTunes parser: mhoh value is too large.\n");
    free(block);
    return NULL;
  }
  
  /* Allocating memory for mhoh value and copy it */
  block->value = malloc(mhoh_value_size ? mhoh_value_size : 1);
  if (block->value == NULL) {
    free(block);
    return NULL;
  }
  
  block->length = mhoh_value_size;
  memcpy(block->value, *mhoh_string, mhoh_value_size);
  *mhoh_string = (char *)record_end;

  return block;
}

struct mfdh* itlp_parse_mfdh(char **pos_buffer, const char *buffer_end) {
  struct mfdh *block;
  size_t version_string_len;
  uint32_t header_length;

  if (!bounded_header(*pos_buffer, buffer_end, "mfdh", 14,
                      &header_length))
    return NULL;

  block = malloc(sizeof(struct mfdh));
  if (!block)
    return NULL;

  version_string_len = read_uint8t_offset(*pos_buffer, 13);
  if (version_string_len > header_length - 14) {
    fprintf(stderr, "iTunes parser: Invalid mfdh version string length.\n");
    free(block);
    return NULL;
  }
  block->application_version = malloc(sizeof(char)*(version_string_len+1));
  if (!block->application_version) {
    free(block);
    return NULL;
  }
  memcpy(block->application_version, *pos_buffer+14, version_string_len);
  block->application_version[version_string_len] = 0;

  *pos_buffer += header_length;

  return block;
}

struct miph* itlp_parse_miph(char **pos_buffer, const char *buffer_end) {
  struct miph *block;
  const char *record_end;
  uint32_t header_size, record_length, mhoh_number, mtph_number;
  unsigned int i;

  if (!bounded_record(*pos_buffer, buffer_end, "miph", 0xD44,
                      &header_size, &record_length, &record_end))
    return NULL;
  
  block = malloc(sizeof(struct miph));
  if (!block)
    return NULL;

  /* Reading header informations */
  mhoh_number = read_uint32t_offset(*pos_buffer, 12);
  mtph_number = read_uint32t_offset(*pos_buffer, 16);
  block->id = read_uint32t_offset(*pos_buffer, 0xD40);

  /* Playlist persistent ID at 0x1B8, 64 bits */
  /* Distinguished kind at 0x23A (maybe less than uint32_t) */

  *pos_buffer += header_size;

  if (!child_count_fits(mhoh_number, 16, *pos_buffer, record_end) ||
      !child_count_fits(mtph_number, 28, *pos_buffer, record_end) ||
      (uint64_t)mhoh_number * 16 + (uint64_t)mtph_number * 28 >
        (uint64_t)(record_end - *pos_buffer)) {
    free(block);
    return NULL;
  }

  /* Allocating memory for mhoh blocks and mtph blocks */
  block->mhohs = (struct mhoh **)allocate_pointer_array(mhoh_number);
  block->mtphs = (struct mtph **)allocate_pointer_array(mtph_number);
  if (!block->mhohs || !block->mtphs) {
    free(block->mhohs);
    free(block->mtphs);
    free(block);
    return NULL;
  }
  
  /* Ending arrays with NULL */
  block->mhohs[mhoh_number] = NULL;
  block->mtphs[mtph_number] = NULL;

  /* Parsing mhoh blocks */
  for (i = 0; i < mhoh_number; i++) {
    block->mhohs[i] = itlp_parse_mhoh(pos_buffer, record_end);
    if (!block->mhohs[i]) {
      itlp_free_miph(block);
      return NULL;
    }
  }

  /* Parsing mtph blocks */
  for (i = 0; i < mtph_number; i++) {
    block->mtphs[i] = itlp_parse_mtph(pos_buffer, record_end);
    if (!block->mtphs[i]) {
      itlp_free_miph(block);
      return NULL;
    }
  }

  if (*pos_buffer != record_end) {
    fprintf(stderr, "iTunes parser: miph child records do not fill record.\n");
    itlp_free_miph(block);
    return NULL;
  }

  return block;
}

struct mtph* itlp_parse_mtph(char **mtph_string, const char *buffer_end) {
  struct mtph* block;
  uint32_t block_size;

  if (!bounded_header(*mtph_string, buffer_end, "mtph", 28,
                      &block_size))
    return NULL;

  block = malloc(sizeof(struct mtph));
  if (!block)
    return NULL;

  /* Reading header informations */
  block->identifier = read_uint32t_offset(*mtph_string,24);

  *mtph_string += block_size;
  
  return block;
}

struct mlth* itlp_parse_mlth(char **mlth_string, const char *buffer_end) {
  struct mlth* block;
  uint32_t header_length;
  uint32_t mith_number;
  unsigned int i;

  if (!bounded_header(*mlth_string, buffer_end, "mlth", 12,
                      &header_length))
    return NULL;

  /* Parsing mlth header */
  mith_number = read_uint32t_offset(*mlth_string,8);
  *mlth_string += header_length;

  if (!child_count_fits(mith_number, 0x88, *mlth_string, buffer_end))
    return NULL;

  /* Allocating memory */
  block = malloc(sizeof(struct mlth));
  if (!block)
    return NULL;
  block->miths = (struct mith **)allocate_pointer_array(mith_number);
  if (!block->miths) {
    free(block);
    return NULL;
  }

  /* Parsing mith blocks */
  for (i = 0; i < mith_number; i++) {
    block->miths[i] = itlp_parse_mith(mlth_string, buffer_end);
    if (!block->miths[i]) {
      itlp_free_mlth(block);
      return NULL;
    }
  }

  /* Ending array with NULL */
  block->miths[i] = NULL;

  if (*mlth_string != buffer_end) {
    fprintf(stderr, "iTunes parser: mlth child records do not fill record.\n");
    itlp_free_mlth(block);
    return NULL;
  }
  
  return block;
}

struct mhgh* itlp_parse_mhgh(char **mhgh_string, const char *buffer_end) {
  struct mhgh* block;
  uint32_t header_length;
  uint32_t mhoh_number;
  unsigned int i;

  if (!bounded_header(*mhgh_string, buffer_end, "mhgh", 0x38,
                      &header_length))
    return NULL;

  /* Allocating memory */
  block = malloc(sizeof(struct mhgh));
  if (!block)
    return NULL;
  
  /* Reading header informations */
  mhoh_number = read_uint32t_offset(*mhgh_string, 8);
  block->list_size = read_uint8t_offset(*mhgh_string, 0x37);
  *mhgh_string += header_length;

  if (!child_count_fits(mhoh_number, 16, *mhgh_string, buffer_end)) {
    free(block);
    return NULL;
  }
  
  block->mhohs = (struct mhoh **)allocate_pointer_array(mhoh_number);
  if (!block->mhohs) {
    free(block);
    return NULL;
  }

  /* Parsing mhoh blocks */
  for (i = 0; i < mhoh_number; i++) {
    block->mhohs[i] = itlp_parse_mhoh(mhgh_string, buffer_end);
    if (!block->mhohs[i]) {
      itlp_free_mhgh(block);
      return NULL;
    }
  }

  /* Ending array with NULL */
  block->mhohs[mhoh_number] = NULL;

  if (*mhgh_string != buffer_end) {
    fprintf(stderr, "iTunes parser: mhgh child records do not fill record.\n");
    itlp_free_mhgh(block);
    return NULL;
  }
  
  return block;
}

/* We don't know yet what is the point of stsh.
 * adamish (github.com/adamish) suggested on a gist that this
 * could be some sort of a signature
 */
struct stsh* itlp_parse_stsh(char **stsh_string, const char *buffer_end) {
  struct stsh* block;
  uint32_t header_length;

  if (!bounded_header(*stsh_string, buffer_end, "stsh", 8,
                      &header_length))
    return NULL;

  block = malloc(sizeof(struct stsh));
  if (!block)
    return NULL;

  *stsh_string += header_length;
  
  return block;
}

struct mlah* itlp_parse_mlah(char **mlah_string, const char *buffer_end) {
  struct mlah* block;
  uint32_t header_length;
  uint32_t miah_number;
  unsigned int i;

  if (!bounded_header(*mlah_string, buffer_end, "mlah", 12,
                      &header_length))
    return NULL;
  
  block = malloc(sizeof(struct mlah));
  if (!block)
    return NULL;

  /* Read mlah header */
  miah_number = read_uint32t_offset(*mlah_string, 8);
  *mlah_string += header_length;

  if (!child_count_fits(miah_number, 16, *mlah_string, buffer_end)) {
    free(block);
    return NULL;
  }

  /* Allocating memory for miah blocks */
  block->miahs = (struct miah **)allocate_pointer_array(miah_number);
  if (!block->miahs) {
    free(block);
    return NULL;
  }

  /* Ending array with NULL */
  block->miahs[miah_number] = NULL;

  /* Parsing miah blocks */
  for (i = 0; i < miah_number; i++) {
    block->miahs[i] = itlp_parse_miah(mlah_string, buffer_end);
    if (!block->miahs[i]) {
      itlp_free_mlah(block);
      return NULL;
    }
  }

  if (*mlah_string != buffer_end) {
    fprintf(stderr, "iTunes parser: mlah child records do not fill record.\n");
    itlp_free_mlah(block);
    return NULL;
  }

  return block;
}

struct miah* itlp_parse_miah(char **miah_string, const char *buffer_end) {
  struct miah* block;
  const char *record_end;
  uint32_t header_length, record_length, mhoh_number;
  unsigned int i;

  if (!bounded_record(*miah_string, buffer_end, "miah", 16,
                      &header_length, &record_length, &record_end))
    return NULL;

  block = malloc(sizeof(struct miah));
  if (!block)
    return NULL;

  /* Read miah header */
  mhoh_number = read_uint32t_offset(*miah_string, 12);
  *miah_string += header_length;

  if (!child_count_fits(mhoh_number, 16, *miah_string, record_end)) {
    free(block);
    return NULL;
  }

  /* Allocating memory for mhoh blocks */
  block->mhohs = (struct mhoh **)allocate_pointer_array(mhoh_number);
  if (!block->mhohs) {
    free(block);
    return NULL;
  }

  /* Ending array with NULL */
  block->mhohs[mhoh_number] = NULL;
  
  /* Parsing mhoh blocks */
  for (i = 0; i < mhoh_number; i++) {
    block->mhohs[i] = itlp_parse_mhoh(miah_string, record_end);
    if (!block->mhohs[i]) {
      itlp_free_miah(block);
      return NULL;
    }
  }

  if (*miah_string != record_end) {
    fprintf(stderr, "iTunes parser: miah child records do not fill record.\n");
    itlp_free_miah(block);
    return NULL;
  }
  
  return block;
}

struct mlih* itlp_parse_mlih(char **mlih_string, const char *buffer_end) {
  struct mlih *block;
  uint32_t header_length;
  uint32_t miih_number;
  unsigned int i;

  if (!bounded_header(*mlih_string, buffer_end, "mlih", 12,
                      &header_length))
    return NULL;

  /* Reading header informations */
  miih_number = read_uint32t_offset(*mlih_string,8);
  *mlih_string += header_length;

  if (!child_count_fits(miih_number, 16, *mlih_string, buffer_end))
    return NULL;

  /* Allocating memory */
  block = malloc(sizeof(struct mlih));
  if (!block)
    return NULL;
  block->miihs = (struct miih **)allocate_pointer_array(miih_number);
  if (!block->miihs) {
    free(block);
    return NULL;
  }

  /* Parsing miih blocks */
  for (i = 0; i < miih_number; i++) {
    block->miihs[i] = itlp_parse_miih(mlih_string, buffer_end);
    if (!block->miihs[i]) {
      itlp_free_mlih(block);
      return NULL;
    }
  }

  /* Ending the array with NULL */
  block->miihs[miih_number] = NULL;

  if (*mlih_string != buffer_end) {
    fprintf(stderr, "iTunes parser: mlih child records do not fill record.\n");
    itlp_free_mlih(block);
    return NULL;
  }

  return block;
}

struct miih* itlp_parse_miih(char **miih_string, const char *buffer_end) {
  struct miih *block;
  const char *record_end;
  uint32_t header_length, record_length, mhoh_number;
  unsigned int i;

  if (!bounded_record(*miih_string, buffer_end, "miih", 16,
                      &header_length, &record_length, &record_end))
    return NULL;

  /* Reading header informations */
  mhoh_number = read_uint32t_offset(*miih_string, 12);
  *miih_string += header_length;

  if (!child_count_fits(mhoh_number, 16, *miih_string, record_end))
    return NULL;

  /* Allocating memory */
  block = malloc(sizeof(struct miih));
  if (!block)
    return NULL;
  block->mhohs = (struct mhoh **)allocate_pointer_array(mhoh_number);
  if (!block->mhohs) {
    free(block);
    return NULL;
  }

  /* Parsing mhoh blocks */
  for (i = 0; i < mhoh_number; i++) {
    block->mhohs[i] = itlp_parse_mhoh(miih_string, record_end);
    if (!block->mhohs[i]) {
      itlp_free_miih(block);
      return NULL;
    }
  }

  /* Ending array with NULL */
  block->mhohs[mhoh_number] = NULL;

  if (*miih_string != record_end) {
    fprintf(stderr, "iTunes parser: miih child records do not fill record.\n");
    itlp_free_miih(block);
    return NULL;
  }

  return block;
}

struct mlrh* itlp_parse_mlrh(char **mlrh_string, const char *buffer_end) {
  struct mlrh *block;
  uint32_t header_length;
  uint32_t mprh_number;
  unsigned int i;

  if (!bounded_header(*mlrh_string, buffer_end, "mlrh", 12,
                      &header_length))
    return NULL;

  /* Reading header informations */
  mprh_number   = read_uint32t_offset(*mlrh_string, 8);
  *mlrh_string += header_length;

  if (!child_count_fits(mprh_number, 8, *mlrh_string, buffer_end))
    return NULL;

  /* Allocating memory */
  block = malloc(sizeof(struct mlrh));
  if (!block)
    return NULL;
  block->mprhs = (struct mprh **)allocate_pointer_array(mprh_number);
  if (!block->mprhs) {
    free(block);
    return NULL;
  }

  /* Parsing mprh blocks */
  for (i = 0; i < mprh_number; i++) {
    block->mprhs[i] = itlp_parse_mprh(mlrh_string, buffer_end);
    if (!block->mprhs[i]) {
      itlp_free_mlrh(block);
      return NULL;
    }
  }

  /* Ending array with NULL */
  block->mprhs[mprh_number] = NULL;

  if (*mlrh_string != buffer_end) {
    fprintf(stderr, "iTunes parser: mlrh child records do not fill record.\n");
    itlp_free_mlrh(block);
    return NULL;
  }

  return block;
}

struct mlsh* itlp_parse_mlsh(char **mlsh_string, const char *buffer_end) {
  struct mlsh *block;
  uint32_t header_length;
  uint32_t msph_number;
  unsigned int i;

  if (!bounded_header(*mlsh_string, buffer_end, "mlsh", 12,
                      &header_length))
    return NULL;

  /* Reading header informations */
  msph_number = read_uint32t_offset(*mlsh_string, 8);

  /* Allocating memory */
  block = malloc(sizeof(struct mlsh));
  if (!block)
    return NULL;
  block->msphs = (struct msph **)allocate_pointer_array(msph_number);
  if (!block->msphs) {
    free(block);
    return NULL;
  }

  *mlsh_string += header_length;

  if (!child_count_fits(msph_number, 16, *mlsh_string, buffer_end)) {
    itlp_free_mlsh(block);
    return NULL;
  }

  /* Parsing msph blocks */
  for (i = 0; i < msph_number; i++) {
    block->msphs[i] = itlp_parse_msph(mlsh_string, buffer_end);
    if (!block->msphs[i]) {
      itlp_free_mlsh(block);
      return NULL;
    }
  }

  /* Ending array with NULL */
  block->msphs[msph_number] = NULL;

  if (*mlsh_string != buffer_end) {
    fprintf(stderr, "iTunes parser: mlsh child records do not fill record.\n");
    itlp_free_mlsh(block);
    return NULL;
  }

  return block;
}

struct msph* itlp_parse_msph(char **msph_string, const char *buffer_end) {
  struct msph *block;
  const char *record_end;
  uint32_t header_length, record_length, mhoh_number;
  unsigned int i;

  if (!bounded_record(*msph_string, buffer_end, "msph", 16,
                      &header_length, &record_length, &record_end))
    return NULL;

  /* Reading header informations */
  mhoh_number = read_uint32t_offset(*msph_string, 12);
  *msph_string += header_length;

  if (!child_count_fits(mhoh_number, 16, *msph_string, record_end))
    return NULL;

  /* Allocating memory */
  block = malloc(sizeof(struct msph));
  if (!block)
    return NULL;
  block->mhohs = (struct mhoh **)allocate_pointer_array(mhoh_number);
  if (!block->mhohs) {
    free(block);
    return NULL;
  }

  /* Parsing mhoh blocks */
  for (i = 0; i < mhoh_number; i++) {
    block->mhohs[i] = itlp_parse_mhoh(msph_string, record_end);
    if (!block->mhohs[i]) {
      itlp_free_msph(block);
      return NULL;
    }
  }

  /* Ending array with NULL */
  block->mhohs[mhoh_number] = NULL;

  if (*msph_string != record_end) {
    fprintf(stderr, "iTunes parser: msph child records do not fill record.\n");
    itlp_free_msph(block);
    return NULL;
  }

  return block;
}

struct mlph* itlp_parse_mlph(char **mlph_string, const char *buffer_end) {
  struct mlph *block;
  uint32_t header_length;
  uint32_t miph_number;
  unsigned int i;

  if (!bounded_header(*mlph_string, buffer_end, "mlph", 12,
                      &header_length))
    return NULL;

  /* Reading header informations */
  miph_number = read_uint32t_offset(*mlph_string, 8);
  
  *mlph_string += header_length;

  if (!child_count_fits(miph_number, 0xD44, *mlph_string, buffer_end))
    return NULL;

  /* Allocating memory */
  block = malloc(sizeof(struct mlph));
  if (!block)
    return NULL;
  block->miphs = (struct miph **)allocate_pointer_array(miph_number);
  if (!block->miphs) {
    free(block);
    return NULL;
  }

  /* Parsing miph blocks */
  for (i = 0; i < miph_number; i++) {
    block->miphs[i] = itlp_parse_miph(mlph_string, buffer_end);
    if (!block->miphs[i]) {
      itlp_free_mlph(block);
      return NULL;
    }
  }

  /* Ending array with NULL */
  block->miphs[miph_number] = NULL;

  if (*mlph_string != buffer_end) {
    fprintf(stderr, "iTunes parser: mlph child records do not fill record.\n");
    itlp_free_mlph(block);
    return NULL;
  }

  return block;
}

struct mprh* itlp_parse_mprh(char **mprh_string, const char *buffer_end) {
  struct mprh *block;
  uint32_t header_length;

  if (!bounded_header(*mprh_string, buffer_end, "mprh", 8,
                      &header_length))
    return NULL;

  /* Allocating memory */
  block = malloc(sizeof(struct mprh));
  if (!block)
    return NULL;

  /* Reading header informaations */
  *mprh_string += header_length;

  return block;
}

struct mlqh* itlp_parse_mlqh(char **mlqh_string, const char *buffer_end) {
  struct mlqh *block;
  uint32_t header_length;
  uint32_t miqh_number, mhoh_number;
  unsigned int i;

  if (!bounded_header(*mlqh_string, buffer_end, "mlqh", 20,
                      &header_length))
    return NULL;

  /* Reading header informations */
  mhoh_number = read_uint32t_offset(*mlqh_string, 12);
  miqh_number = read_uint32t_offset(*mlqh_string, 16);
  *mlqh_string += header_length;

  if (!child_count_fits(mhoh_number, 16, *mlqh_string, buffer_end) ||
      !child_count_fits(miqh_number, 16, *mlqh_string, buffer_end) ||
      ((uint64_t)mhoh_number + miqh_number) * 16 >
        (uint64_t)(buffer_end - *mlqh_string))
    return NULL;

  /* Allocating memory */
  block = malloc(sizeof(struct mlqh));
  if (!block)
    return NULL;
  block->mhohs = (struct mhoh **)allocate_pointer_array(mhoh_number);
  block->miqhs = (struct miqh **)allocate_pointer_array(miqh_number);
  if (!block->mhohs || !block->miqhs) {
    free(block->mhohs);
    free(block->miqhs);
    free(block);
    return NULL;
  }

  /* Reading mhoh and miqh blocks */
  for (i = 0; i < mhoh_number; i++) {
    block->mhohs[i] = itlp_parse_mhoh(mlqh_string, buffer_end);
    if (!block->mhohs[i]) {
      itlp_free_mlqh(block);
      return NULL;
    }
  }
  for (i = 0; i < miqh_number; i++) {
    block->miqhs[i] = itlp_parse_miqh(mlqh_string, buffer_end);
    if (!block->miqhs[i]) {
      itlp_free_mlqh(block);
      return NULL;
    }
  }

  /* Ending array with NULL */
  block->mhohs[mhoh_number] = NULL;
  block->miqhs[miqh_number] = NULL;

  if (*mlqh_string != buffer_end) {
    fprintf(stderr, "iTunes parser: mlqh child records do not fill record.\n");
    itlp_free_mlqh(block);
    return NULL;
  }

  return block;
}

struct file* itlp_parse_file(char **file_string, const char *buffer_end) {
  struct file *block;
  size_t string_length;

  /* A BLOCK_FILE payload is the raw file URI through the end of its parent
   * msdh record. It does not contain another nested block header. */
  if (*file_string > buffer_end)
    return NULL;
  string_length = (size_t)(buffer_end - *file_string);

  /* Allocating memory */
  block = malloc(sizeof(struct file));
  if (!block)
    return NULL;
  block->string = malloc((string_length+1)*sizeof(char));
  if (!block->string) {
    free(block);
    return NULL;
  }

  /* Parsing block */
  memcpy(block->string, *file_string, string_length);
  block->string[string_length] = 0;

  *file_string = (char *)buffer_end;
  
  return block;
}

struct miqh* itlp_parse_miqh(char **miqh_string, const char *buffer_end) {
  struct miqh *block;
  const char *record_end;
  uint32_t header_length, record_length, mhoh_number;
  unsigned int i;

  if (!bounded_record(*miqh_string, buffer_end, "miqh", 16,
                      &header_length, &record_length, &record_end))
    return NULL;

  /* Reading header informations */
  mhoh_number = read_uint32t_offset(*miqh_string, 12);
  *miqh_string += header_length;

  if (!child_count_fits(mhoh_number, 16, *miqh_string, record_end))
    return NULL;

  /* Allocating memory */
  block = malloc(sizeof(struct miqh));
  if (!block)
    return NULL;
  block->mhohs = (struct mhoh **)allocate_pointer_array(mhoh_number);
  if (!block->mhohs) {
    free(block);
    return NULL;
  }

  /* Parsing mhoh blocks */
  for (i = 0; i < mhoh_number; i++) {
    block->mhohs[i] = itlp_parse_mhoh(miqh_string, record_end);
    if (!block->mhohs[i]) {
      itlp_free_miqh(block);
      return NULL;
    }
  }

  /* Ending array with NULL */
  block->mhohs[mhoh_number] = NULL;

  if (*miqh_string != record_end) {
    fprintf(stderr, "iTunes parser: miqh child records do not fill record.\n");
    itlp_free_miqh(block);
    return NULL;
  }

  return block;
}
