/* Host-only: the vendored uzlib ships tinflate.c without its adler32.c /
 * crc32.c. On device the reference inside uzlib_uncompress_chksum is
 * garbage-collected (--gc-sections); macOS ld64 resolves symbols first, so
 * supply the standard implementations (uzlib upstream semantics). */

#include <stdint.h>

#include "uzlib.h"

#define A32_BASE 65521
#define A32_NMAX 5552

uint32_t uzlib_adler32(const void *data, unsigned int length, uint32_t prev_sum) {
  const unsigned char *buf = (const unsigned char *)data;
  unsigned int s1 = prev_sum & 0xffff;
  unsigned int s2 = prev_sum >> 16;

  while (length > 0) {
    int k = length < A32_NMAX ? (int)length : A32_NMAX;
    int i;
    for (i = k / 16; i; --i, buf += 16) {
      int j;
      for (j = 0; j < 16; j++) {
        s1 += buf[j];
        s2 += s1;
      }
    }
    for (i = k % 16; i; --i) {
      s1 += *buf++;
      s2 += s1;
    }
    s1 %= A32_BASE;
    s2 %= A32_BASE;
    length -= k;
  }
  return (s2 << 16) | s1;
}

uint32_t uzlib_crc32(const void *data, unsigned int length, uint32_t crc) {
  const unsigned char *buf = (const unsigned char *)data;
  unsigned int i;
  crc = ~crc;
  for (i = 0; i < length; i++) {
    unsigned int j;
    crc ^= buf[i];
    for (j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1) + 1));
    }
  }
  return ~crc;
}
