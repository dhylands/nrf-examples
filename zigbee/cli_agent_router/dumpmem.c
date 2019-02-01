/**
 * dumpmem.c - memory dumper
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.*
 */

#include "dumpmem.h"

#include <stdint.h>

#include "nrf_log.h"

static char dumpBuf[256];

void DumpMem(const char *label, const void *buf, size_t len) {
  const char *hexDig = "0123456789abcdef";
  char *dst = dumpBuf;
  char *endBuf = &dumpBuf[sizeof dumpBuf - 4];
  const uint8_t *src = buf;
  for (; len > 0 && dst < endBuf; --len) {
    *dst++ = ' ';
    *dst++ = hexDig[(*src >> 4) & 0x0f];
    *dst++ = hexDig[*src & 0x0f];
    src++;
  }
  *dst++ = '\0';
  NRF_LOG_INFO("%s:%s", label, dumpBuf);
}
