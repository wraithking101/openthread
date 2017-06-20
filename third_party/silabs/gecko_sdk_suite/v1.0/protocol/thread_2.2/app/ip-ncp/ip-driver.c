// File: ip-driver.c
//
// Description: host-side driver for the IP NCP.
//
// Copyright 2013 by Silicon Laboratories. All rights reserved.             *80*

#include <stdlib.h>
#include <unistd.h>

#include PLATFORM_HEADER
#include "stack/core/ember-stack.h"
#include "app/ip-ncp/uart-link-protocol.h"
#include "app/ip-ncp/host-stream.h"
#include "app/ip-ncp/ip-driver.h"

#ifdef UNIX_HOST
  #include "ip-driver-log.h"
  #define LOG(x) x
#else
  #define LOG(x)
#endif

void ipDriverShutdown(void);

void emProcessNcpManagementCommand(SerialLinkMessageType type,
                                   const uint8_t *message,
                                   uint16_t length)
{
  uint8_t managementType = message[0];
  const uint8_t *body = message + 1;
  uint16_t identifier = emberFetchHighLowInt16u(body);

  if (driverCommAppManagementFd != -1
      && (identifier == CB_SET_COMM_APP_PARAMETERS_COMMAND_IDENTIFIER
          || identifier == CB_SET_COMM_APP_DATASET_COMMAND_IDENTIFIER
          || identifier == CB_SET_COMM_APP_SECURITY_COMMAND_IDENTIFIER
          || identifier == CB_SET_COMM_APP_ADDRESS_COMMAND_IDENTIFIER)) {
    // forwarded to comm app
    LOG(ipDriverLogEvent(LOG_DRIVER_TO_COMM_APP, message, length, type);)
    ipModemWrite(driverCommAppManagementFd, type, message, length);
  } else {
    // forwarded to the host
    LOG(ipDriverLogEvent(LOG_DRIVER_TO_APP, message, length, type);)
    ipModemWrite(driverHostAppManagementFd, type, message, length);
    if (identifier == CB_LAUNCH_STANDALONE_BOOTLOADER_COMMAND_IDENTIFIER) {
      // The ip-driver-app and spi-server processes are not used when 
      // communicating with the bootloader on the NCP so if we see a
      // successful launch of the bootloader we must exit.  Exiting
      // this process will close the socket to the spi-server which will
      // cause it to terminate as well.  Note if the the format of the
      // CB_LAUNCH_STANDALONE_BOOTLOADER_COMMAND_IDENTIFIER command
      // ever changes and the status is not the first byte in the message
      // the following check will fail.
      body += sizeof(identifier);
      if (*body == 0) {
        ipDriverShutdown();
      }
    }
  }
}
