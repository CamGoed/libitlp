#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <arpa/inet.h>

#include "decryption.h"
#include "memtools.h"
#include "inflate.h"
#include "itlp/types.h"
#include "blocks.h"
#include "free.h"

struct msdh** itlp_open_library(char *library_file) {
  FILE *fh;
  char header[4];
  char *encrypted_buffer;
  char *decrypted_buffer = NULL;
  char *decompressed_buffer = NULL;
  uint32_t header_length, file_length, max_crypt_size, num_msdh;
  size_t crypt_size, decompressed_buffer_size = 0, decrypted_size;
  size_t payload_size;
  unsigned int i;
  struct msdh **msdh_array = NULL;

  /* Opening iTunes library file */
  fh = fopen(library_file, "rb");
  if (!fh) {
    fprintf(stderr, "iTunes parser: Can't open iTunes Library file.");
    return NULL;
  }

  /* Checking for iTunes header */
  if (fread(&header, sizeof(char), 4, fh) != 4) {
    fprintf(stderr, "iTunes parser: Truncated iTunes Library header.\n");
    fclose(fh);
    return NULL;
  }
  if (memcmp(&header, "hdfm", 4) != 0) {
    fprintf(stderr, "iTunes parser: iTunes Library header is incorrect.");
    fclose(fh);
    return NULL;
  }

  /* Reading header length */
  if (fread(&header_length, sizeof(uint32_t), 1, fh) != 1) {
    fprintf(stderr, "iTunes parser: Cannot read header length.\n");
    fclose(fh);
    return NULL;
  }
  header_length = ntohl(header_length);

  /* Reading file length */
  if (fread(&file_length, sizeof(uint32_t), 1, fh) != 1) {
    fprintf(stderr, "iTunes parser: Cannot read declared file length.\n");
    fclose(fh);
    return NULL;
  }
  file_length = ntohl(file_length);
  
  /* Comparing actual file length and file length annouced by header */
  if (fseek(fh, 0, SEEK_END) != 0 || ftell(fh) != (long)file_length ||
      header_length < 96 ||
      header_length > file_length) {
    fprintf(stderr, "iTunes parser: Invalid header or file length.\n");
    fclose(fh);
    return NULL;
  }

  /* Reading msdh numbers */
  if (fseek(fh, 0x30, SEEK_SET) != 0 ||
      fread(&num_msdh, sizeof(uint32_t), 1, fh) != 1) {
    fprintf(stderr, "iTunes parser: Cannot read top-level record count.\n");
    fclose(fh);
    return NULL;
  }
  num_msdh = ntohl(num_msdh);

  /* Reading max crypt size */
  if (fseek(fh, 92, SEEK_SET) != 0 ||
      fread(&max_crypt_size, sizeof(uint32_t), 1, fh) != 1) {
    fprintf(stderr, "iTunes parser: Cannot read encrypted payload length.\n");
    fclose(fh);
    return NULL;
  }
  max_crypt_size = ntohl(max_crypt_size);

  /* On iTunes ≥ 10, the first 100k bytes are crypted */
  payload_size = (size_t)file_length - header_length;
  crypt_size = payload_size;
  if (crypt_size > max_crypt_size) {
    crypt_size = max_crypt_size;
  }

  /* AES decrypts complete 16-byte blocks. Any partial block belongs to the
   * unencrypted tail of the compressed stream and is appended below. */
  crypt_size -= crypt_size % 16;

  /* Reading encrypted part of the library */
  if (fseek(fh, header_length, SEEK_SET) != 0) {
    fprintf(stderr, "iTunes parser: Cannot seek to encrypted payload.\n");
    fclose(fh);
    return NULL;
  }
  encrypted_buffer = malloc(sizeof(char)*crypt_size);
  if (!encrypted_buffer ||
      fread(encrypted_buffer, sizeof(char), crypt_size, fh) != crypt_size) {
    fprintf(stderr, "iTunes parser: Cannot read encrypted payload.\n");
    free(encrypted_buffer);
    fclose(fh);
    return NULL;
  }

  /* Decrypting library */
#ifdef _CRYPTO_OPENSSL
  decrypted_buffer = itlp_decrypt_openssl(encrypted_buffer,
					  crypt_size,
					  &decrypted_size);
#endif

#ifdef _CRYPTO_CC
  decrypted_buffer = itlp_decrypt_commoncrypto(encrypted_buffer,
					       crypt_size,
					       &decrypted_size);
#endif

  free(encrypted_buffer);
  
  if (!decrypted_buffer) {
    fprintf(stderr, "iTunes parser: Decryption failed. ");
    fprintf(stderr, "Aborting iTunes Library parsing.\n");
    fclose(fh);
    return NULL;
  }

  if (decrypted_size != crypt_size) {
    fprintf(stderr, "iTunes parser: Decrypted payload length is incorrect.\n");
    free(decrypted_buffer);
    fclose(fh);
    return NULL;
  }

  /* Only the start of an .itl payload is encrypted. The remaining bytes are
   * still part of the same zlib stream, so append them before inflating it. */
  if (payload_size > crypt_size) {
    size_t remaining_size = payload_size - crypt_size;
    char *resized_buffer;

    resized_buffer = realloc(decrypted_buffer, payload_size);
    if (!resized_buffer) {
      free(decrypted_buffer);
      fclose(fh);
      return NULL;
    }

    decrypted_buffer = resized_buffer;

    if (fseek(fh, (long)(header_length + crypt_size), SEEK_SET) != 0 ||
        fread(decrypted_buffer + crypt_size,
              sizeof(char),
              remaining_size,
              fh) != remaining_size) {
      fprintf(stderr, "iTunes parser: Cannot read unencrypted payload tail.\n");
      free(decrypted_buffer);
      fclose(fh);
      return NULL;
    }

    crypt_size = payload_size;
  }

  fclose(fh);
 
  /* Decompression decrypted library */
  decompressed_buffer = itlp_inflate(decrypted_buffer, crypt_size,
				     &decompressed_buffer_size);
  if (!decompressed_buffer) {
    fprintf(stderr, "iTunes parser: Decompression failed. Aborting iTunes Library parsing.\n");
    free(decrypted_buffer);
    return NULL;
  }

  char *pos_buffer;
  pos_buffer = decompressed_buffer;

  /* Creating a NULL-ended array with all the msdh blocks */
  if (num_msdh > decompressed_buffer_size / 16) {
    fprintf(stderr, "iTunes parser: Invalid top-level record count.\n");
    free(decrypted_buffer);
    free(decompressed_buffer);
    return NULL;
  }
  msdh_array = calloc((size_t)num_msdh + 1, sizeof(struct msdh*));
  if (!msdh_array) {
    free(decrypted_buffer);
    free(decompressed_buffer);
    return NULL;
  }
  for (i = 0; i < num_msdh; i++) {
    msdh_array[i] = itlp_parse_msdh(
      &pos_buffer, decompressed_buffer + decompressed_buffer_size);

    if (!msdh_array[i]) {
      while (i > 0)
        itlp_free_msdh(msdh_array[--i]);
      free(msdh_array);
      free(decrypted_buffer);
      free(decompressed_buffer);
      return NULL;
    }
  }
  msdh_array[num_msdh] = NULL;

  if (pos_buffer != decompressed_buffer + decompressed_buffer_size) {
    fprintf(stderr, "iTunes parser: Top-level records do not fill stream.\n");
    for (i = 0; i < num_msdh; i++)
      itlp_free_msdh(msdh_array[i]);
    free(msdh_array);
    free(decrypted_buffer);
    free(decompressed_buffer);
    return NULL;
  }

  /* Cleanup */
  free(decrypted_buffer);
  free(decompressed_buffer);
  
  return msdh_array;
}
