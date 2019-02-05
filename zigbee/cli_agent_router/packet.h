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
#include "zboss_api.h"

#define APS_DATA_CONFIRM      0x04
#define DEVICE_STATE          0x07
#define CHANGE_NETWORK_STATE  0x08
#define READ_PARAMETER        0x0a
#define WRITE_PARAMETER       0x0b
#define VERSION               0x0d
#define DEVICE_STATE_CHANGED  0x0e
#define APS_DATA_REQUEST      0x12
#define APS_DATA_INDICATION   0x17

// 01  8  Mac address           zb_get_long_address
// 05  2  PAN ID 16             not used
// 07  2  NETWORK address 16    0000
// 08  8  PAN ID 64             zb_get_extended_pan_id
// 09  1  APS_DESIGNATED COORDINATOR 1 = coordinator, 0 = router
// 0a  4  SCAN CHANNELS         mask
// 0b  8  APS_PANID64           not used
// 0e  8  TRUST CENTER ADDR64   zb_get_long_address
// 10  1  SECURITY MODE         3 - 0/None 1/preconfig 2/key from TC 3/No master but TC key
// 18 16  NETWORK KEY
// 1c  1  OPERATING CHANNEL     11-26
// 21  1  PERMIT JOIN           (seconds)
// 22  2  PROTOCOL VERSION
// 24  1  NETWORK UPDATE ID

#define PARAM_ID_MAC_ADRESS         0x01
#define PARAM_ID_PAN_ID64           0x08
#define PARAM_ID_SCAN_CHANNELS      0x0a
#define PARAM_ID_OPERATING_CHANNEL  0x1c
#define PARAM_ID_PERMIT_JOIN        0x21
#define PARAM_ID_PROTOCOL_VERSION   0x22
#define PARAM_ID_NETWORK_UPDATE_ID  0x24

// The maximum zigbee packet size is 128 bytes (at the PHY layer)

#define MAX_PACKET_LEN  128

typedef struct {
  size_t    len;
  uint8_t  *buf;
} Packet_t;

typedef struct {
  uint8_t   commandId;
  uint8_t   seqNum;
  uint8_t   reserved;
  uint16_t  frameLen;

} __attribute__((packed)) PacketHeader_t;

typedef struct {
  PacketHeader_t  hdr;
  uint16_t        payloadLen;
  uint8_t         parameterId;
  union {
    zb_64bit_addr_t addr64;
    uint32_t        data32;
    uint16_t        data16;
    uint8_t         data8;
  };
  uint16_t          crc;  // space for CRC, but not actual location
} __attribute__((packed)) ReadParameter_t;

void PacketReceived(const Packet_t *packet);

// Implemented in main.c
void WriteResponse(uint8_t *buf, size_t bufLen);

#endif // PACKET_H
