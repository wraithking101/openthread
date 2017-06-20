// File: tftp-bootloader-15-4.c
//
// Description: non-POSIX tftp-bootloader functionality
//
// Copyright 2013 by Silicon Laboratories. All rights reserved.             *80*

#include PLATFORM_HEADER
#include "stack/include/ember.h"
#include "stack/core/ember-stack.h"
#include "stack/ip/dispatch.h"
#include "stack/ip/ip-header.h"
#include "app/util/bootload/tftp-bootloader/tftp-bootloader.h"

bool emSendTftpBootloaderPacket(const uint8_t *payload, uint16_t payloadLength)
{
  Ipv6Header ipHeader;
  assert(emTftpBootloaderRemoteAddress != NULL_BUFFER);
  const uint8_t *remoteAddress = emGetBufferPointer(emTftpBootloaderRemoteAddress);
  PacketHeader query = emMakeUdpHeader(&ipHeader,
                                       IP_HEADER_NO_OPTIONS,
                                       remoteAddress,
                                       255,
                                       TFTP_BOOTLOADER_PORT,
                                       TFTP_BOOTLOADER_PORT,
                                       (uint8_t*)payload,
                                       payloadLength,
                                       0);
  return emSubmitIpHeader(query, &ipHeader);
}
