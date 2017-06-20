// File: tftp-bootloader-15-4.c
//
// Description: non-POSIX TFTP Bootloader functionality
//
// Copyright 2013 by Silicon Laboratories. All rights reserved.             *80*

#include PLATFORM_HEADER
#include "stack/include/ember.h"
#include "stack/core/ember-stack.h"
#include "stack/ip/dispatch.h"
#include "stack/ip/ip-header.h"
#include "app/util/bootload/tftp/tftp.h"

bool emSendTftpPacket(const uint8_t *payload, uint16_t payloadLength)
{
  Ipv6Header ipHeader;
  PacketHeader query = emMakeUdpHeader(&ipHeader,
                                       IP_HEADER_NO_OPTIONS,
                                       emTftpRemoteIp.bytes,
                                       255,
                                       emTftpLocalTid,
                                       emTftpRemoteTid,
                                       (uint8_t*)payload,
                                       payloadLength,
                                       0);
  return emSubmitIpHeader(query, &ipHeader);
}

void emTftpListenStatusHandler(uint16_t port, EmberIpv6Address *address)
{
}

void emResetTftp(void)
{
  emReallyResetTftp();
}
