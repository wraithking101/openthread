// File: tftp-posix.c
//
// Description: TFTP Bootloader Posix Functionality
//
// Copyright 2013 by Silicon Laboratories. All rights reserved.             *80*

#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <netdb.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <ifaddrs.h>

#include PLATFORM_HEADER
#include "stack/include/ember-types.h"
#include "stack/core/ember-stack.h"
#include "hal/hal.h"
#include "stack/ip/host/unix-address.h"
#include "stack/ip/host/unix-interface.h"
#include "stack/ip/host/host-listener-table.h"
#include "app/util/bootload/tftp/tftp.h"
#include "app/util/serial/command-interpreter2.h"
#include "stack/ip/tls/native-test-util.h"
#include "plugin/serial/serial.h"

const char *emUnixInterface = NULL;

bool emSendTftpPacket(const uint8_t *payload, uint16_t payloadLength)
{
  return emberSendUdp(emTftpRemoteIp.bytes,
                      emTftpLocalTid,
                      emTftpRemoteTid,
                      (uint8_t*)payload, // fix me
                      payloadLength);
}

void emResetTftp(void)
{
  emberCloseListeners();
  emReallyResetTftp();
}

bool emberGetLocalIpAddress(uint8_t unusedIndex, EmberIpv6Address *address)
{
  MEMCOPY(address->bytes, emMyIpAddress.bytes, 16);
  return true;
}

extern void emMarkAppBuffers(void);

static void markStackBuffers(void)
{
  bufferUse("application");
  emMarkAppBuffers();
  endBufferUse();
}

static BufferMarker const markers[] = {
  markStackBuffers,
  NULL
};

void emberTick(void)
{
  emberListenerTick();
  emReclaimUnusedBuffers(markers);
}

//
// choose an interface so we can bind later
//
void emTftpReallyChooseInterface(uint8_t interfaceChoice,
                                 const uint8_t *prefixChoice)
{
  if (emChooseInterface(interfaceChoice,
                        prefixChoice,
                        ! emTftpScripting)) {
    emTftpStatusHandler(TFTP_INTERFACE_CHOSEN);
  }
}

void emTftpOpenTraceFd(char myTag, char theirTag)
{
  EmberIpv6Address localAddress = {0};
  assert(emberGetLocalIpAddress(0, &localAddress));
  int fd = openTraceFd(myTag, theirTag, emTftpLocalTid, localAddress.bytes);
  HostListener *listener = emberAddListener(emTftpLocalTid,
                                            localAddress.bytes,
                                            SOCK_DGRAM, 0); // UDP
  listener->socket = fd;
}

void helpCommand(void)
{
  emTftpPrintHelp();

  EmberCommandEntry *finger = emberCommandTable;
  emberSerialPrintf(APP_SERIAL, "Commands: \r\n\r\n");
  for (; finger->action != NULL; finger++) {
    emberPrintCommandUsage(finger);
  }
  emberSerialPrintf(APP_SERIAL, "\r\n");
  emberPrintCommandUsageNotes();
}

// unused data

EmberEui64 emLocalEui64 = {0};

Ipv6Address emTftpServerAddress = {0};
