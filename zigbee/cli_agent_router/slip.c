/**
 * slip.c - SLIP parser/builder
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.*
 */

#include "slip.h"

// The following come from RFC1055. which describes the framing used
// for SLIP.
#define END       0xc0    // 0300
#define ESC       0xdb    // 0333
#define ESC_END   0xdc    // 0334
#define ESC_ESC   0xdd    // 0335

void SLIP_initParser(SLIP_Parser_t *parser, SLIP_PacketRcvdCallback cb) {
  parser->packet.len = 0;
  parser->packet.buf = parser->packetBuf;
  parser->packetRcvdCallback = cb;
  parser->handling_esc = false;
}

void SLIP_parseChunk(SLIP_Parser_t *parser, const uint8_t *chunk, size_t chunkLen) {

  for (size_t i = chunkLen; i > 0; --i) {
    uint8_t ch = *chunk++;
    if (parser->handling_esc) {
      switch (ch) {
        case ESC_END:
          ch = END;
          break;
        case ESC_ESC:
          ch = ESC;
          break;
        // anything else is technically a protocol violation. We just
        // leave the byte alone.
      }
      parser->handling_esc = false;
      if (parser->packet.len < sizeof(parser->packetBuf)) {
        parser->packetBuf[parser->packet.len++] = ch;
      }
      continue;
    }
    switch (ch) {
      case END:
        if (parser->packet.len == 0) {
          // Back to back ENDs - ignore
          continue;
        }
        // Otherwise we've gotten to the end of the packet.
        parser->packetRcvdCallback(&parser->packet);
        parser->packet.len = 0;
        parser->handling_esc = false;
        break;
      case ESC:
        parser->handling_esc = true;
        break;
      default:
      if (parser->packet.len < sizeof(parser->packetBuf)) {
        parser->packetBuf[parser->packet.len++] = ch;
      }
      break;
    }
  }
}

size_t SLIP_encapsulate(const Packet_t *packet, uint8_t *outBuf, size_t outBufLen) {
  const uint8_t *src = packet->buf;
  uint8_t *dst = outBuf;
  uint8_t *dstEnd = &outBuf[outBufLen];

  #define STORE(ch) if (dst < dstEnd ) { *dst++ = ch; }
  STORE(END)
  for (size_t i = packet->len; i > 0; --i) {
    uint8_t ch = *src++;
    switch (ch) {
      case END:
        STORE(ESC)
        STORE(ESC_END)
        break;
      case ESC:
        STORE(ESC)
        STORE(ESC_ESC)
        break;
      default:
        STORE(ch)
        break;
    }
  }
  STORE(END)
  #undef STORE
  return dst - outBuf;
}
