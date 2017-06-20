// Copyright 2015 Silicon Laboratories, Inc.

#ifndef _BOOTLOADER_INTERFACE_H_
#define _BOOTLOADER_INTERFACE_H_

// TODO: add doxygen

bool emberAfPluginBootloaderInterfaceIsBootloaderInitialized(void);

void emberAfPluginBootloaderInterfaceGetVersion(uint16_t *blVersion,
                                                uint16_t *batVersion);

bool emberAfPluginBootloaderInterfaceInit(void);

void emberAfPluginBootloaderInterfaceSleep(void);

bool emberAfPluginBootloaderInterfaceChipErase(void);

uint16_t emberAfPluginBootloaderInterfaceValidateImage(void);

void emberAfPluginBootloaderInterfaceBootload(void);

bool emberAfPluginBootloaderInterfaceRead(uint32_t startAddress,
                                          uint32_t length,
                                          uint8_t *buffer);

bool emberAfPluginBootloaderInterfaceWrite(uint32_t startAddress,
                                           uint32_t length,
                                           uint8_t *buffer);

#endif // _BOOTLOADER_INTERFACE_H_


