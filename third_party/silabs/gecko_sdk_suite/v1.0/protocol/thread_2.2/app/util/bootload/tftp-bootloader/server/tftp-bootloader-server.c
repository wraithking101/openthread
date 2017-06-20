// File: tftp-bootloader-server.c
//
// Description: TftpBootloader Server Functionality
//
// Copyright 2013 by Silicon Laboratories. All rights reserved.             *80*

#include PLATFORM_HEADER
#include "stack/include/ember.h"
#include "stack/core/log.h"
#include "stack/framework/buffer-management.h"
#include "hal/hal.h"
#include "plugin/serial/serial.h"
#include "app/util/bootload/tftp/server/tftp-server.h"
#include "app/util/bootload/tftp-bootloader/tftp-bootloader.h"
#include "app/util/bootload/tftp-bootloader/server/tftp-bootloader-server.h"

typedef enum {
  NO_BOOTLOAD_ACTION,
  ERASE_BOOTLOADER_IMAGE,
  INSTALL_BOOTLOAD_IMAGE_INIT,
  INSTALL_BOOTLOAD_IMAGE
} BootloadAction;

static uint32_t bootloadEraseAddress = 0;
static BootloadAction bootloadAction = NO_BOOTLOAD_ACTION;
void emBootloadEventHandler(void);
EmberEventControl emBootloadEvent = {0, 0};

void emInitializeTftpBootloader(void)
{
  emInitializeBaseTftpBootloaderFunctionality();
  emInitializeAppBootloader();
}

void emInitializeAppBootloader(void)
{
  if (halAppBootloaderInit() == EEPROM_SUCCESS) {
    const HalEepromInformationType *info = halAppBootloaderInfo();
    if (info != NULL) {
      emberSerialPrintfLine(APP_SERIAL,
                            "Bootloader version %u",
                            info->version);
    } else {
      assert(false);
    }
  } else {
    emberSerialPrintfLine(APP_SERIAL,
                          "warning: unable to initialize bootloader");
  }   
}

void emMarkTftpBootloaderBuffers(void)
{
  emMarkBuffer(&emTftpBootloaderRemoteAddress);
}

#ifndef EMBER_AF_PLUGIN_TFTP_BOOTLOAD_TARGET
// When running as a plugin, emBootloadEvent is run by the framework.
static EmberEventData appEvents[] = {
  {&emBootloadEvent, emBootloadEventHandler},
  {NULL, NULL}
};

void emRunTftpBootloaderEvents(void)
{
  emberRunEvents(appEvents);
}
#endif

static bool sendOkayToSend(bool success,
                              bool holdOff,
                              uint32_t fileIndex)
{
  uint8_t packet[7] = {0};
  packet[0] = KRAKEN_OKAY_TO_SEND;
  packet[1] = success;
  packet[2] = holdOff;
  emberStoreHighLowInt32u(packet + 3, fileIndex);

  emLogLine(BOOTLOAD,
            "sending okay to send | success: %s | holdOff: %u | fileIndex: %u",
            (success
             ? "yes"
             : "no"),
            holdOff,
            fileIndex);

  return emberSendUdp(emGetTftpBootloaderRemoteAddress()->bytes,
                      TFTP_BOOTLOADER_PORT,
                      TFTP_BOOTLOADER_PORT,
                      packet,
                      sizeof(packet));
}

static uint32_t nextFileIndex = 0;

void emBootloadEventHandler(void)
{
  switch (bootloadAction) {
  case ERASE_BOOTLOADER_IMAGE: {
    emberEventControlSetActive(emBootloadEvent);

    if (halAppBootloaderStorageBusy()) {
      return;
    }

    const HalEepromInformationType *info = halAppBootloaderInfo();
    assert(info != NULL);
    //cstat !PTR-null-assign-pos
    assert(bootloadEraseAddress <= info->partSize);

    if (bootloadEraseAddress < info->partSize) {
      halAppBootloaderEraseRawStorage(bootloadEraseAddress, info->pageSize);
      emLogLine(BOOTLOAD,
                "TftpBootloader erasing page at address: %u",
                bootloadEraseAddress);
      bootloadEraseAddress += info->pageSize;
    } else {
      bootloadAction = NO_BOOTLOAD_ACTION;
      emberEventControlSetInactive(emBootloadEvent);
      emLogLine(BOOTLOAD, "Done erasing storage");
      sendOkayToSend(true, false, nextFileIndex);
    }
    break;
  }
  case INSTALL_BOOTLOAD_IMAGE_INIT:
    halAppBootloaderImageIsValidReset();
    bootloadAction = INSTALL_BOOTLOAD_IMAGE;
    // fall through
  case INSTALL_BOOTLOAD_IMAGE: {
    uint16_t result = halAppBootloaderImageIsValid();

    if (result == BL_IMAGE_IS_VALID_CONTINUE) {
      emberEventControlSetActive(emBootloadEvent);
      return;
    }

    if (result > 0) {
      if (emLogIsActive(BOOTLOAD)) {
        emberSerialGuaranteedPrintf(APP_SERIAL, "Image is valid, rebooting\n");
      }
      halAppBootloaderInstallNewImage();
    } else {
      emLogLine(BOOTLOAD,
                "warning: bootload image is not valid: %u",
                result);
      emberEventControlSetInactive(emBootloadEvent);
    }
    break;
  }
  default:
    // shouldn't get here
    assert(false);
  }
}

void emTftpServerStatusHandler(TftpServerStatus status)
{
  if (status == TFTP_FILE_WRITE_REQUEST) {
    // start erasing the bootload image area
    // TODO: add the functionality below to tftp-bootloader support
    //bootloadAction = ERASE_BOOTLOADER_IMAGE;
    //emberEventControlSetActive(bootloadEvent);
    emLogLine(BOOTLOAD, "TFTP write request");
  } else if (status == TFTP_FILE_DONE) {
    bootloadAction = INSTALL_BOOTLOAD_IMAGE_INIT;
    emLogLine(BOOTLOAD, "Attempting to install new image");
    emberEventControlSetActive(emBootloadEvent);
  }
}

bool emStoreTftpFileChunk(uint32_t index, const uint8_t *data, uint16_t length)
{
  emLogLine(BOOTLOAD,
            "[TFTP RX %u bytes @ index %u]\n",
            length,
            index);

  if (halAppBootloaderStorageBusy()) {
    emLogLine(BOOTLOAD, "Warning: storage is busy");
    return false;
  } else {
    uint8_t writeResult =
      halAppBootloaderWriteRawStorage(nextFileIndex, data, length);

    if (writeResult == EEPROM_SUCCESS) {
      nextFileIndex += length;
    } else {
      emLogLine(BOOTLOAD,
                "[failed to write image at index %u, "
                "code: %u]\n",
                index,
                writeResult);
    }
  }

  return true;
}

void emHandleTftpBootloaderBootloadRequest(const uint8_t *source,
                                   const uint8_t *packet,
                                   uint16_t packetLength)
{
  // do some fancy authentication on the request first
  TftpBootloaderBootloadRequest request = {0};
  emLogLine(BOOTLOAD, "Received tftp-bootloader bootload request");

  if (packetLength >= sizeof(TftpBootloaderBootloadRequest)) {
    request.resume = packet[0];
    request.manufacturerId = emberFetchHighLowInt16u(packet + 1);
    request.deviceType = packet[3];
    request.versionNumber = emberFetchHighLowInt32u(packet + 4);
    request.size = emberFetchHighLowInt32u(packet + 8);

    emSetTftpBootloaderRemoteAddress(source);

    if (emberVerifyBootloadRequest(&request)) {
      if (! request.resume) {
        emLogLine(BOOTLOAD, "Reset file index");
        nextFileIndex = 0;
      } else {
        emLogLine(BOOTLOAD, "Resume requested");
      }

      // success
      emLogLine(BOOTLOAD, "Proceeding to erase image");
      emResetTftpServer();
      sendOkayToSend(true, true, nextFileIndex);
      bootloadAction = ERASE_BOOTLOADER_IMAGE;
      emberEventControlSetActive(emBootloadEvent);
    } else {
      // failure
      sendOkayToSend(false, false, 0);
    }
  } else {
    emLogLine(BOOTLOAD,
              "bootload request failure, "
              "%u bytes left but wanted %u",
              packetLength,
              sizeof(TftpBootloaderBootloadRequest));
  }
}

void emProcessTftpBootloaderPacket(const uint8_t *source,
                           const uint8_t *payload,
                           uint16_t payloadLength)
{
  assert(payloadLength > 0);
  TftpBootloaderPacketType type = (TftpBootloaderPacketType)payload[0];
  payload++;
  payloadLength--;

  emLogLine(BOOTLOAD, "Processing tftp-bootloader packet, type: %u,", type);

  if (type == KRAKEN_BOOTLOAD_REQUEST) {
    emHandleTftpBootloaderBootloadRequest(source, payload, payloadLength);
  } else {
    emLogLine(BOOTLOAD, "Confused for type: %u", type);
  }
}
