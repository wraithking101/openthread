// File: tftp-bootloader.c
//
// Description: TftpBootloader functionality.
//              TftpBootloader is a bootloading protocol.
//
// Copyright 2013 by Silicon Laboratories. All rights reserved.             *80*

#include PLATFORM_HEADER
#include "stack/include/ember.h"
#include "stack/core/log.h"
#include "stack/framework/buffer-management.h"
#include "app/util/bootload/tftp-bootloader/tftp-bootloader.h"

Buffer emTftpBootloaderRemoteAddress = NULL_BUFFER;

bool emVerifyTftpBootloaderRemoteAddress(const uint8_t *source)
{
  if (emTftpBootloaderRemoteAddress != NULL_BUFFER) {
    const EmberIpv6Address *address =
      (const EmberIpv6Address*)emGetBufferPointer(emTftpBootloaderRemoteAddress);
    return (MEMCOMPARE(address->bytes, source, 16)
            == 0);
  }

  return false;
}

void emTftpBootloaderErrorHandler(const uint8_t *source)
{
  emLogLine(BOOTLOAD, "Got a packet from a server != server");
}

const EmberIpv6Address *emGetTftpBootloaderRemoteAddress(void)
{
  if (emTftpBootloaderRemoteAddress == NULL_BUFFER) {
    // help the debugger
    assert(false);
  }

  return (const EmberIpv6Address*)emGetBufferPointer(emTftpBootloaderRemoteAddress);
}

void emInitializeBaseTftpBootloaderFunctionality(void)
{
  EmberIpv6Address localAddress;
  assert(emberGetLocalIpAddress(0, &localAddress));
  emberUdpListen(TFTP_BOOTLOADER_PORT, localAddress.bytes);
  emLogLine(BOOTLOAD, "Initializing tftp-bootloader, listening on port %u", TFTP_BOOTLOADER_PORT);
}

void emSetTftpBootloaderRemoteAddress(const uint8_t *address)
{
  emLogLine(BOOTLOAD, "setting tftp-bootloader remote address");
  emTftpBootloaderRemoteAddress = emAllocateBuffer(sizeof(EmberIpv6Address));
  assert(emTftpBootloaderRemoteAddress != NULL_BUFFER);
  EmberIpv6Address *ipv6Address =
    (EmberIpv6Address*)emGetBufferPointer(emTftpBootloaderRemoteAddress);
  MEMCOPY(ipv6Address->bytes, address, 16);
}
