/**
 * packet.c - packet handler
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.*
 */

#include "packet.h"

#include "slip.h"
#include "nrf_log.h"

#include "debug_flags.h"

#include "zboss_api.h"
#include "nrf_802154.h"

// Worse case is every byte is escaped, plus 2 for an END character
// at each end.
#define MAX_SLIP_PACKET_LEN   (MAX_PACKET_LEN * 2 + 2)

uint8_t   outSlipPacket[MAX_SLIP_PACKET_LEN];

static uint16_t PacketCrc(const Packet_t *packet) {
  uint16_t crc = 0;
  for (int i = 0; i < packet->len - 2; i++) {
    crc += packet->buf[i];
  }
  return ~crc + 1;
}

static void SendResponse(PacketHeader_t *response) {
  Packet_t pkt;

  pkt.len = response->frameLen + 2;   // CRC not included in frameLen
  pkt.buf = (uint8_t *)response;
  uint16_t crc = PacketCrc(&pkt);
  pkt.buf[response->frameLen] = crc & 0xff;
  pkt.buf[response->frameLen + 1] = (crc >> 8) & 0xff;

  size_t outLen = SLIP_encapsulate(&pkt, outSlipPacket, sizeof(outSlipPacket));
  WriteResponse(outSlipPacket, outLen);
}

static void HandleReadParameter(ReadParameter_t *pkt) {
  ReadParameter_t response;
  memset(&response, 0, sizeof(response));
  response.hdr.commandId = READ_PARAMETER;
  response.hdr.seqNum = pkt->hdr.seqNum;
  response.payloadLen = 1;  // covers parameterId
  response.parameterId = pkt->parameterId;

  switch (pkt->parameterId ) {
    case PARAM_ID_MAC_ADRESS: {
      zb_ieee_addr_t addr64;
      zb_get_long_address(addr64);
      memcpy(response.addr64, &addr64, sizeof(addr64));
      response.payloadLen += sizeof(response.addr64);
      break;
    }
    case PARAM_ID_PAN_ID64: {
      zb_ext_pan_id_t extPanId;
      zb_get_extended_pan_id(extPanId);
      memcpy(response.addr64, &extPanId, sizeof(extPanId));
      response.payloadLen += sizeof(response.addr64);
      break;
    }
    case PARAM_ID_SCAN_CHANNELS: {
      zb_uint32_t channelMask = zb_get_bdb_primary_channel_set();
      response.data32 = channelMask;
      response.payloadLen += sizeof(response.data32);
      break;
    }
    case PARAM_ID_OPERATING_CHANNEL: {
      uint8_t operatingChannel = nrf_802154_channel_get();
      response.data8 = operatingChannel;
      response.payloadLen += sizeof(response.data8);
      break;
    }
    default:
      NRF_LOG_ERROR("Unrecognized parameter ID: %u", pkt->parameterId);
      return;
  }
  response.hdr.frameLen = 7 + response.payloadLen;
  SendResponse(&response.hdr);
}

void PacketReceived(const Packet_t *packet) {
  PacketHeader_t *pktHdr;

  if (packet->len < 8) {
    // The smallest packet is 6 bytes + 2 bytes of CRC
    NRF_LOG_ERROR("Invalid packet (%d bytes) - too small", packet->len);
    return;
  }
  uint16_t frameLen = packet->buf[3] + (packet->buf[4] << 8);
  if (frameLen + 2 != packet->len) {
    NRF_LOG_ERROR("Invalid frame length");
    NRF_LOG_HEXDUMP_ERROR(packet->buf, packet->len);
    return;
  }
  uint16_t frameCrc = packet->buf[frameLen] + (packet->buf[frameLen + 1] << 8);
  uint16_t expectedCrc = PacketCrc(packet);
  if (frameCrc != expectedCrc) {
    NRF_LOG_ERROR("CRC mismatch: expected 0x%04x found: 0x%04x",
                  expectedCrc, frameCrc);
    return;
  }
  if (DEBUG_raw) {
    NRF_LOG_INFO("Rcvd Packet: %d bytes", packet->len - 2);
    NRF_LOG_HEXDUMP_INFO(packet->buf, packet->len - 2);
  }

  pktHdr = (PacketHeader_t *)packet->buf;

  switch (pktHdr->commandId) {
    case READ_PARAMETER:
      HandleReadParameter((ReadParameter_t *)pktHdr);
      break;
    default:
      NRF_LOG_ERROR("Unrecognized command 0x%02x - ignoring", pktHdr->commandId);
      return;
  }
}

//           MAC Address: 00212effff0279c0
//       Network PANID16: 17b3
//        Network Addr16: 0000
//       Network PANID64: 00212effff0279c0
//  APS Designated Coordinator: 1
//         Scan Channels: 02000000
//           APS PANID64: 0000000000000000
//   Trust Center Addr64: 00212effff0279c0
//         Security Mode: 3
//           Network Key: d09840d06c00fc2498646c4048000000
//     Operating Channel: 25
//      Protocol Version: 260
//     Network Update ID: 7
//           Permit Join: 0
//               Version: 260b0500
