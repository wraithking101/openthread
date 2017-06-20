// File: tftp.c
//
// Description: TFTP Bootloader headers and defines
//
// Copyright 2013 by Silicon Laboratories. All rights reserved.             *80*

#include PLATFORM_HEADER
#include "stack/include/ember.h"
#include "stack/core/log.h"
#include "hal/hal.h"
#include "plugin/serial/serial.h"
#include "app/util/bootload/tftp/tftp.h"

#include <stdlib.h>

uint16_t emTftpRemoteTid = 0xFFFF;
uint16_t emTftpLocalTid = 0xFFFF;
uint16_t emTftpBlockNumber = 0;
EmberIpv6Address emTftpRemoteIp = {{0}};
uint16_t emTftpBlockSize = TFTP_MAX_BLOCK_SIZE;
bool emTftpScripting = false;

void emReallyResetTftp(void)
{
  emTftpLocalTid = 0xFFFF;
  emTftpRemoteTid = 0xFFFF;
  emTftpBlockNumber = 0;
  emTftpBlockSize = TFTP_MAX_BLOCK_SIZE;
}

void emTftpListen(bool randomizeTid)
{
  if (randomizeTid) {
    emTftpLocalTid = rand() % 0xFFFF;
  }

  uint8_t maxTries = 15;
  uint8_t i = 0;
  EmberIpv6Address localAddress = {{0}};
  assert(emberGetLocalIpAddress(0, &localAddress));

  if (! emTftpScripting) {
    while (emberUdpListen(emTftpLocalTid, localAddress.bytes) != EMBER_SUCCESS) {
      assert(randomizeTid
             && ++i < maxTries);
      emTftpLocalTid = rand() % 0xFFFF;
    }

    emLogLine(BOOTLOAD, "TFTP Listening on port %u", emTftpLocalTid);
  }

  emTftpListenStatusHandler(emTftpLocalTid, &localAddress);
}

void quitCommand(void)
{
  emberSerialPrintfLine(APP_SERIAL, "Bye");
  exit(0);
}

void chooseInterfaceCommand(void)
{
#ifdef UNIX_HOST
  emTftpReallyChooseInterface((emberCommandArgumentCount() == 1
                               ? emberUnsignedCommandArgument(0)
                               : 0xFF),
                              NULL);
#endif
}
