// File: tftp-bootloader-client.h
//
// Description: TftpBootloader Client functionality.
//              TftpBootloader is a bootloading protocol.
//
// Copyright 2013 by Silicon Laboratories. All rights reserved.             *80*

typedef enum {
  KRAKEN_CLIENT_NO_STATE,
  KRAKEN_CLIENT_BOOTLOAD_REQUEST_SENT_STATE,
  KRAKEN_CLIENT_WAITING_STATE,
} TftpBootloaderClientState;

extern TftpBootloaderClientState emTftpBootloaderClientState;

void doBootloadCommand(void);

EmberStatus emSendTftpBootloaderBootloadRequest(bool resume,
                                                uint16_t manufacturerId,
                                                uint8_t deviceType,
                                                uint32_t versionNumber,
                                                uint32_t size);

void emTftpBootloaderPerformBootload(const uint8_t *fileName,
                                     const uint8_t *serverAddress,
                                     bool resume,
                                     uint16_t manufacturerId,
                                     uint8_t deviceType,
                                     uint32_t versionNumber,
                                     uint32_t size);
