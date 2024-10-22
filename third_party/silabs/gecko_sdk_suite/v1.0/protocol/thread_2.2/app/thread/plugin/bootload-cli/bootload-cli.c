// *******************************************************************
// * bootloader-cli.c
// *
// * Bootloader specific commands
// * 
// * Copyright 2015 Silicon Laboratories, Inc.
// *******************************************************************

#include PLATFORM_HEADER
#include CONFIGURATION_HEADER
#include EMBER_AF_API_STACK
#include EMBER_AF_API_COMMAND_INTERPRETER2
#ifdef EMBER_AF_API_DEBUG_PRINT
  #include EMBER_AF_API_DEBUG_PRINT
#endif
#include "app/util/bootload/serial-bootloader/bootload-ncp.h"
#include "hal/micro/bootloader-interface-standalone.h"

#ifdef UNIX_HOST
#include <stdio.h>
#include <stdlib.h>
#endif

void emberGetStandaloneBootloaderInfoReturn(uint16_t version,
                                            uint8_t platformId,
                                            uint8_t microId,
                                            uint8_t phyId)
{
  emberAfAppPrintln("BTL Version: 0x%2X", version);
  emberAfAppPrintln("Platform:    0x%X", platformId);
  emberAfAppPrintln("Micro:       0x%X", microId);
  emberAfAppPrintln("Phy:         0x%X", phyId);
}

void emberLaunchStandaloneBootloaderReturn(EmberStatus status)
{
  if (status == EMBER_SUCCESS) {
    emberAfAppPrintln("Launching Standalone Bootloader...");
  } else {
    emberAfAppPrintln("Standalone Bootloader Launch Failure 0x%2x", status);
  }
}

void bootloaderInfoCommand(void)
{
  emberGetStandaloneBootloaderInfo();
}

void bootloaderLaunchCommand(void)
{
  emberLaunchStandaloneBootloader(STANDALONE_BOOTLOADER_NORMAL_MODE);
}

#define COMMAND_LINE_MAX_LENGTH 255
void bootloaderLoadImageCommand(void)
{
#ifdef UNIX_HOST
  char* progpath;
  char* filepath;
  uint32_t offset = 0;
  uint32_t length = 0xFFFFFFFF;
  char* serialArgs = "";
  char commandLine[COMMAND_LINE_MAX_LENGTH] = "";
  
  progpath = (char*)emberStringCommandArgument(0, NULL);
  filepath = (char*)emberStringCommandArgument(1, NULL);
  offset = emberUnsignedCommandArgument(2);
  length = emberUnsignedCommandArgument(3);
  if (emberCommandArgumentCount() > 4) {
    serialArgs = (char*)emberStringCommandArgument(4, NULL);
  }

  snprintf(commandLine, COMMAND_LINE_MAX_LENGTH, "%s %s %d %d %s",
           progpath, filepath, offset, length, serialArgs);
  emberAfAppPrintln("Running: %s", commandLine);
  int status = system(commandLine);
  if (status != 0) {
    printf("Command execution failed.");
  } else {
    printf("\nApplication restart not supported.  Exiting instead.\n");
    exit(0);
  }
#else
  emberAfAppPrintln("Load command is only available to Unix host systems.");
#endif
}
