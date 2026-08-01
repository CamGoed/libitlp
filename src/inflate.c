#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>
#include <zlib.h>

char* itlp_inflate(char *input_buffer, size_t input_buffer_len,
		   size_t *output_len) {
  z_stream stream;
  unsigned int chunk_size;
  Bytef *chunk_buffer;
  char *output = NULL;
  int status;

  chunk_size = 8192;
  *output_len = 0;

  if (input_buffer_len > UINT_MAX)
    return NULL;

  chunk_buffer = malloc(chunk_size);
  if (!chunk_buffer)
    return NULL;
  
  stream.zalloc = Z_NULL;
  stream.zfree = Z_NULL;
  stream.opaque = Z_NULL;

  stream.avail_in = input_buffer_len;
  stream.next_in = (Bytef *)input_buffer;

  status = inflateInit(&stream);
  if (status != Z_OK) {
    free(chunk_buffer);
    return NULL;
  }
  
  do {
    stream.avail_out = chunk_size;
    stream.next_out = chunk_buffer;
    status = inflate(&stream, Z_NO_FLUSH);

    if (stream.avail_out != chunk_size) {
      size_t produced = chunk_size-stream.avail_out;
      char *resized_output = realloc(output, *output_len+produced);
      if (!resized_output) {
        free(output);
        free(chunk_buffer);
        inflateEnd(&stream);
        *output_len = 0;
        return NULL;
      }
      output = resized_output;
      memcpy(output+*output_len, chunk_buffer, produced);
      *output_len += produced;
    }
  } while (status == Z_OK);

  /* zlib can return some output before discovering truncated or corrupt data.
   * Accept the payload only when the complete input reaches its stream end. */
  if (status != Z_STREAM_END || stream.avail_in != 0) {
    fprintf(stderr,
            "iTunes parser: Invalid compressed stream (zlib status %d, "
            "%u trailing bytes).\n",
            status, stream.avail_in);
    free(output);
    output = NULL;
    *output_len = 0;
  }

  inflateEnd(&stream);

  /* Cleanup */
  free(chunk_buffer);
  
  return output;
}
