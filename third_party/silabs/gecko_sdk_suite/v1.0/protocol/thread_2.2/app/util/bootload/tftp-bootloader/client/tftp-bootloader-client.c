// File: tftp-bootloader-client.c
//
// Description: Tftp-Bootloader Client functionality.
//              Tftp-Bootloader is a bootloading protocol.
//
// Copyright 2013 by Silicon Laboratories. All rights reserved.             *80*

#include PLATFORM_HEADER
#include "stack/include/ember-types.h"
#include "stack/core/ember-stack.h"
#include "hal/hal.h"
#include "plugin/serial/serial.h"
#include "app/util/bootload/tftp/tftp.h"
#include "app/util/bootload/tftp/client/tftp-file-reader.h"
#include "app/util/bootload/tftp/client/tftp-client.h"
#include "app/util/bootload/tftp-bootloader/tftp-bootloader.h"
#include "app/util/bootload/tftp-bootloader/client/tftp-bootloader-client.h"
#include "stack/ip/host/unix-address.h"
#include "app/util/serial/command-interpreter2.h"
#include "stack/ip/host/host-udp-retry.h"

TftpBootloaderClientState emTftpBootloaderClientState = KRAKEN_CLIENT_NO_STATE;
static Buffer fileNameBuffer = NULL_BUFFER;
extern bool doBootload;

#define KRAKEN_BOOTLOAD_REQUEST_TOKEN 0xb33f
#define KRAKEN_RETRY_LIMIT 5
#define KRAKEN_RETRY_DELAY 2000

void emInitializeTftpBootloader(void)
{
  emInitializeBaseTftpBootloaderFunctionality();
}

static void retryEventHandler(uint16_t token, uint8_t retriesRemaining)
{
  if (retriesRemaining == 0) {
    emLogLine(BOOTLOAD, "tftp-bootloader communication error");
    doBootload = false;
  } else {
    emLogLine(BOOTLOAD, "tftp-bootloader retry TX");
  }
}

static EmberStatus sendTftpBootloaderPacket(const uint8_t *packet,
                                            uint16_t payloadLength,
                                            uint16_t typeToken)
{
  assert(emTftpBootloaderRemoteAddress != NULL_BUFFER);
  const EmberIpv6Address *address =
    (const EmberIpv6Address*)emGetBufferPointer(emTftpBootloaderRemoteAddress);

  return emAddUdpRetry(address,
                       TFTP_BOOTLOADER_PORT,
                       TFTP_BOOTLOADER_PORT,
                       packet,
                       payloadLength,
                       typeToken,
                       KRAKEN_RETRY_LIMIT,
                       KRAKEN_RETRY_DELAY,
                       &retryEventHandler);
}

void emTftpBootloaderPerformBootload(const uint8_t *fileName,
                                     const uint8_t *targetAddress,
                                     bool resume,
                                     uint16_t manufacturerId,
                                     uint8_t deviceType,
                                     uint32_t versionNumber,
                                     uint32_t size)
{
  if (emChooseInterface(0xFF, targetAddress, true)) {
    EmberIpv6Address localAddress = {0};
    assert(emberGetLocalIpAddress(0, &localAddress));
    emberUdpListen(TFTP_BOOTLOADER_PORT, localAddress.bytes);
    emTftpStatusHandler(TFTP_INTERFACE_CHOSEN);

    uint8_t address[16] = {0};

    if (! emberIpv6StringToAddress(targetAddress, address)) {
      emLogLine(BOOTLOAD, "Unable to set tftp-bootloader remote server");
      return;
    }

    emSetTftpBootloaderRemoteAddress(address);
    fileNameBuffer = emFillStringBuffer(fileName);
    assert(fileNameBuffer != NULL_BUFFER);

    emTftpSetServer(targetAddress);
    emTftpBootloaderClientState = KRAKEN_CLIENT_BOOTLOAD_REQUEST_SENT_STATE;
    emLogLine(BOOTLOAD,
              "Sending bootload request with resume: %s, "
              "manufacturer Id: 0x%x, "
              "deviceType: %u, "
              "versionNumber: 0x%x, "
              "size: %u",
              (resume
               ? "yes"
               : "no"),
              manufacturerId,
              deviceType,
              versionNumber,
              size);

    if (emSendTftpBootloaderBootloadRequest(resume,
                                            manufacturerId,
                                            deviceType,
                                            versionNumber,
                                            size)
        == EMBER_SUCCESS) {
    } else {
      emLogLine(BOOTLOAD, "failed to send bootload request");
    }
  }
}

EmberStatus emSendTftpBootloaderBootloadRequest(bool resume,
                                                uint16_t manufacturerId,
                                                uint8_t deviceType,
                                                uint32_t versionNumber,
                                                uint32_t size)
{
  // + 1 for type at index 0
  uint8_t bootloadRequest[sizeof(TftpBootloaderBootloadRequest) + 1] = {0};
  bootloadRequest[0] = KRAKEN_BOOTLOAD_REQUEST;
  bootloadRequest[1] = resume;
  emberStoreHighLowInt16u(bootloadRequest + 2, manufacturerId);
  bootloadRequest[4] = deviceType;
  emberStoreHighLowInt32u(bootloadRequest + 5, versionNumber);
  emberStoreHighLowInt32u(bootloadRequest + 9, size);
  return sendTftpBootloaderPacket(bootloadRequest,
                                  sizeof(bootloadRequest),
                                  KRAKEN_BOOTLOAD_REQUEST_TOKEN);
}

void emProcessTftpBootloaderOkayToSendPacket(const uint8_t *payload, uint16_t payloadLength)
{
  assert(payloadLength == 6);
  bool success = payload[0];

  if (! success) {
    emLogLine(BOOTLOAD, "Target rejected bootload request");
    doBootload = false;
  } else {
    bool holdOff = payload[1];
    uint32_t fileIndex = emberFetchHighLowInt32u(payload + 2);
    emLogLine(BOOTLOAD,
              "RX okay to send, hold off: %s, file index: %u",
              (holdOff
               ? "yes"
               : "no"),
              fileIndex);

    emRemoveUdpRetry(KRAKEN_BOOTLOAD_REQUEST_TOKEN);

    if (! holdOff) {
      // we're done
      assert(fileNameBuffer != NULL_BUFFER);
      if (! emTftpOpenAndSendFile(emGetBufferPointer(fileNameBuffer),
                                  fileIndex)) {
        doBootload = false;
      }
    }
  }
}

void emProcessTftpBootloaderErrorPacket(const uint8_t *payload, uint16_t payloadLength)
{
  emLogLine(BOOTLOAD, "RX Error Packet");
  // TODO: implement me
  assert(false);
}

void emProcessTftpBootloaderPacket(const uint8_t *source,
                                   const uint8_t *payload,
                                   uint16_t payloadLength)
{
  assert(payloadLength >= 1);

  if (! emVerifyTftpBootloaderRemoteAddress(source)) {
    emTftpBootloaderErrorHandler(source);
  }

  TftpBootloaderPacketType type = payload[0];
  payload++;
  payloadLength--;

  if (type == KRAKEN_OKAY_TO_SEND) {
    emProcessTftpBootloaderOkayToSendPacket(payload, payloadLength);
  } else if (type == KRAKEN_ERROR) {
    emProcessTftpBootloaderErrorPacket(payload, payloadLength);
  }
}

void emMarkTftpBootloaderBuffers(void)
{
  emMarkBuffer(&emTftpBootloaderRemoteAddress);
  emMarkBuffer(&fileNameBuffer);
  emMarkUdpRetryBuffers();
}
