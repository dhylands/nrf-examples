/**
 * slip.h - SLIP parser/builder
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.*
 */

#if !defined(SLIP_H)
#define SLIP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "packet.h"

typedef void (*SLIP_PacketRcvdCallback)(const Packet_t *packet);

typedef struct {
  Packet_t  packet;
  uint8_t   packetBuf[MAX_PACKET_LEN];
  bool      handling_esc;

  SLIP_PacketRcvdCallback packetRcvdCallback;

} SLIP_Parser_t;

void SLIP_initParser(SLIP_Parser_t *parser, SLIP_PacketRcvdCallback cb);
void SLIP_parseChunk(SLIP_Parser_t *parser, const uint8_t *chunk, size_t chunkLen);
size_t SLIP_encapsulate(const Packet_t *packet, uint8_t *outBuf, size_t outBufLen);

#endif // SLIP_H
