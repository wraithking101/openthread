// Copyright 2015 Silicon Laboratories, Inc.

#include PLATFORM_HEADER
#include CONFIGURATION_HEADER
#include "stack/include/ember.h"
#include "hal/hal.h"
#include "debug-print/debug-print.h"

#include "hal/micro/bootloader-interface-app.h"
#include "btl-interface.h"

static bool isBootloaderInitialized = false;

//------------------------------------------------------------------------------
// APIs

bool emberAfPluginBootloaderInterfaceIsBootloaderInitialized(void)
{
  return isBootloaderInitialized;
}

void emberAfPluginBootloaderInterfaceGetVersion(uint16_t *blVersion, uint16_t *batVersion)
{
  *blVersion = halAppBootloaderGetVersion();
  *batVersion = halBootloaderAddressTable.baseTable.version;
}

bool emberAfPluginBootloaderInterfaceInit(void)
{
  uint8_t result;
  const HalEepromInformationType *eepromInfo;
  
  result = halAppBootloaderInit();

  if(result != EEPROM_SUCCESS) {
    return false;
  }

  eepromInfo = halAppBootloaderInfo();

  if(eepromInfo != NULL) {
    #if 0
    emberAfCorePrintln("eeprom info: \r\ninfoVer %l\r\npartDesc %s\r\ncapabilities %l",
                 eepromInfo->version,
                 eepromInfo->partDescription,
                 eepromInfo->capabilitiesMask);
    // responsePrintf can't handle 32 bit decimals
    emberAfCorePrintln("{partSize:%l} {pageSize:%l}",
                       eepromInfo->partSize,eepromInfo->pageSize);
    emberAfCorePrintln("partEraseMs %d pageEraseMs %d",
                       eepromInfo->partEraseMs,
                       eepromInfo->pageEraseMs);
    #endif
  } else {
    emberAfCorePrintln("eeprom info not available");
  }

  isBootloaderInitialized = true;

  return true;
}

void emberAfPluginBootloaderInterfaceSleep(void)
{
  halAppBootloaderShutdown();
  isBootloaderInitialized = false;
}

bool emberAfPluginBootloaderInterfaceChipErase(void)
{
  uint16_t delayUs = 50000; 
  uint32_t timeoutUs;
 const HalEepromInformationType *eepromInfo;
  
  eepromInfo = halAppBootloaderInfo();
  if (eepromInfo == NULL) {
    return  false;
  }

  timeoutUs = (4 * eepromInfo->partEraseMs) * 1000;

  halResetWatchdog();

  if ( EEPROM_SUCCESS != halAppBootloaderEraseRawStorage(0, eepromInfo->partSize) ) {
    return false;
  }

  while(halAppBootloaderStorageBusy()) {
    halResetWatchdog();
    emberAfCorePrint(".");
    halCommonDelayMicroseconds(delayUs);

    // Exit if timeoutUs exeeded
    timeoutUs -= delayUs;
    if (timeoutUs < delayUs) {
      return false;
    }
  }

  emberAfCorePrintln("");

  return true;
}


uint16_t emberAfPluginBootloaderInterfaceValidateImage(void)
{
  uint16_t result;

  emberAfCorePrint("Verifying image");

  halResetWatchdog();
  halAppBootloaderImageIsValidReset();

  do {
    halResetWatchdog();
    result = halAppBootloaderImageIsValid();
    emberAfCorePrint(".");
  } while (result == BL_IMAGE_IS_VALID_CONTINUE);

  if(result == 0) {
    emberAfCorePrintln("failed!");
  } else {
    emberAfCorePrintln("done!");
  }

  return result;
}

void emberAfPluginBootloaderInterfaceBootload(void)
{
  uint16_t result;

  if(!emberAfPluginBootloaderInterfaceValidateImage()) {
    return;
  }

  emberAfCorePrintln("Installing new image and rebooting...");
  halCommonDelayMilliseconds(500);

  result = halAppBootloaderInstallNewImage();

  // We should not get here if bootload is succeeded.
  return;
}

bool emberAfPluginBootloaderInterfaceRead(uint32_t startAddress,
                                          uint32_t length,
                                          uint8_t *buffer)
{
  uint8_t result, len;
  uint32_t address, remainingLength;

  address = startAddress;
  remainingLength = length;

  // Implement block read so we can take care of the watchdog reset.
  // TODO: profile max block length
  while(remainingLength) {
    halResetWatchdog();
    len = (remainingLength > 255) ? 255 : remainingLength;
    result = halAppBootloaderReadRawStorage(address, buffer + address - startAddress, len);

    if (EEPROM_SUCCESS != result)
    {
      return false;
    }

    remainingLength -= len;
    address += len;
  }

  return true;
}

bool emberAfPluginBootloaderInterfaceWrite(uint32_t startAddress,
                                           uint32_t length,
                                           uint8_t *buffer)
{
  uint8_t result, len;
  uint32_t address, remainingLength;

  address = startAddress;
  remainingLength = length;

  // Implement block write so we can take care of the watchdog reset.
  // TODO: profile max block length
  while(remainingLength) {
    halResetWatchdog();
    len = (remainingLength > 255) ? 255 : remainingLength;
    result = halAppBootloaderWriteRawStorage(address, buffer + address - startAddress, len);

    if (EEPROM_SUCCESS != result)
    {
      return false;
    }

    remainingLength -= len;
    address += len;
  }

  return true;
}
