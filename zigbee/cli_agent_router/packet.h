/**
 * packet.h - packet handler
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.*
 */

#if !defined(PACKET_H)
#define PACKET_H

#include <stddef.h>
#include <stdint.h>

// The maximum zigbee packet size is 128 bytes (at the PHY layer)

#define MAX_PACKET_LEN  128

typedef struct {
  size_t    len;
  uint8_t  *buf;
} Packet_t;

void PacketReceived(const Packet_t *packet);

#endif // PACKET_H
