// File: tftp-bootloader-client-app.c
//
// Description: TFTP Client App Functionality
//
// Copyright 2013 by Silicon Laboratories. All rights reserved.             *80*

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>

#include PLATFORM_HEADER
#include "stack/include/ember-types.h"
#include "stack/core/ember-stack.h"
#include "hal/hal.h"
#include "app/util/serial/command-interpreter2.h"
#include "stack/framework/ip-packet-header.h"
#include "stack/ip/ip-header.h"
#include "stack/ip/6lowpan-header.h"
#include "app/util/bootload/tftp/client/tftp-client.h"
#include "app/util/bootload/tftp/tftp.h"
#include "app/util/bootload/tftp-bootloader/tftp-bootloader.h"
#include "app/util/bootload/tftp-bootloader/client/tftp-bootloader-client.h"
#include "stack/core/ember-stack.h"
#include "plugin/serial/serial.h"
#include "stack/ip/host/unix-interface.h"
#include "stack/ip/host/unix-address.h"
#include "stack/ip/host/host-udp-retry.h"
#include "stack/ip/ip-address.h"
#include "stack/ip/host/host-address-table.h"
#include "stack/ip/host/host-listener-table.h"

extern bool emReadingTrace;
extern bool emTestingDone(void);
static void retransmitEventHandler(void);
static EmberEventControl retransmitEvent = {0};
bool doBootload = false;

#define MAX_RETRANSMIT_COUNT 5

#ifdef UNIX_HOST
#  define RETURN_TYPE int
#else
#  define RETURN_TYPE void
#  define argc 0
#  define argv NULL
#endif

void helpCommand(void);

void emberTftpClientStatusHandler(TftpClientStatus status)
{
  if (status == TFTP_CLIENT_FILE_TRANSMISSION_SUCCESS
      || status == TFTP_CLIENT_FILE_TRANSMISSION_TIMEOUT) {
    // we're done
    doBootload = false;
  }
}

const EmberCommandEntry emberCommandTable[] = {
  // no commands
  {NULL}
};

void emberUdpHandler(const uint8_t *destination,
                     const uint8_t *source,
                     uint16_t localPort,
                     uint16_t remotePort,
                     const uint8_t *payload,
                     uint16_t payloadLength)
{
  if (localPort == emTftpLocalTid) {
    emProcessTftpPacket(source, remotePort, payload, payloadLength);
  } else if (localPort == TFTP_BOOTLOADER_PORT) {
    emProcessTftpBootloaderPacket(source, payload, payloadLength);
  }
}

static void printUsage(void)
{
  emberSerialPrintfLine(APP_SERIAL,
                        "Usage: tftp-bootloader-client-app [arguments]\n\n"
                        "Required arguments:\n\n"
                        "  --target    IPv6 address of the bootload target (string)\n"
                        "  --file      file name (string)\n\n"
                        "Optional arguments:\n\n"
                        "  --resume          attempt a resume, no value needed\n"
                        "  --manufacturer_id manufacturer id, 16 bit integer\n"
                        "  --device_type     device type, 8 bit integer\n"
                        "  --version_number  version number, 32 bit integer\n"
                        "  --size            the size, 32 bit integer\n");
}

static void initialize(int argc, char **argv)
{
  emberInitializeListeners();
  //emberInitializeHostAddressTable();

  struct option longOptions[] = {
    {"target",          required_argument, NULL, 'a'},
    {"file",            required_argument, NULL, 'b'},
    {"resume",          no_argument,       NULL, 'c'},
    {"manufacturer_id", required_argument, NULL, 'd'},
    {"device_type",     required_argument, NULL, 'e'},
    {"version_number",  required_argument, NULL, 'f'},
    {"size",            required_argument, NULL, 's'},
    {NULL}
  };

  int option = 0;
  int optionIndex = 0;
  const uint8_t *targetAddress = NULL;
  const uint8_t *fileName = NULL;
  bool resume = false;
  uint16_t manufacturerId = 0xFFFF;
  uint8_t deviceType = 0xFF;
  uint32_t versionNumber = 0xFFFFFFFF;
  uint32_t size = 0;

  while (true) {
    int option = getopt_long(argc,
                             argv,
                             "a:b:cd:e:f:",
                             longOptions,
                             &optionIndex);
    if (option == -1) {
      break;
    } else if (option == 'a') {
      // server
      targetAddress = optarg;
    } else if (option == 'b') {
      fileName = optarg;
    } else if (option == 'c') {
      // resume
      resume = true;
    } else if (option == 'd') {
      // manufacturer id
      manufacturerId = atoi(optarg);
    } else if (option == 'e') {
      // device type
      deviceType = atoi(optarg);
    } else if (option == 'f') {
      // version number
      versionNumber = atoi(optarg);
    } else if (option == 's') {
      size = atoi(optarg);
    } else {
      printUsage();
      exit(1);
    }
  }

  if (fileName != NULL && targetAddress != NULL) {
    // we'll stop after we're done sending the file
    doBootload = true;
    emTftpBootloaderPerformBootload(fileName,
                                    targetAddress,
                                    resume,
                                    manufacturerId,
                                    deviceType,
                                    versionNumber,
                                    size);
  }
}

void emMarkAppBuffers(void)
{
  emMarkTftpBootloaderBuffers();
}

RETURN_TYPE main(MAIN_FUNCTION_PARAMETERS)
{
  halInit();
  INTERRUPTS_ON();
  emberSerialInit(1, BAUD_115200, PARITY_NONE, 1);
  emberCommandReaderInit();
  emTftpRemoteTid = TFTP_PORT;
  emInitializeBuffers();
  emLogConfig(EM_LOG_BOOTLOAD, 1, true);
  emInitializeUdpRetry();
  initialize(argc, argv);

  if (! doBootload) {
    printUsage();
  }

  while (doBootload) {
    halResetWatchdog();
    emberTick();
    emRunUdpRetryEvents();
  }

  return 0;
}

bool emStoreIpSourceAddress(uint8_t *source, const uint8_t *destination)
{
  emberGetLocalIpAddress(0, (EmberIpv6Address *)source);
  return true;
}

// stubs
#define USE_STUB_emberGetNodeId
#define USE_STUB_emCoapHostIncomingMessageHandler
#define USE_STUB_emGetBinaryCommandIdentifierString
#define USE_STUB_emIsDefaultGlobalPrefix
#define USE_STUB_emIsLocalIpAddress
#define USE_STUB_emIsNdNewNodeId
#define USE_STUB_emMacExtendedId
#define USE_STUB_emStoreDefaultGlobalPrefix
#define USE_STUB_ipAddressToString
#include "stack/ip/stubs.c"
