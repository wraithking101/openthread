// File: tftp-bootloader-server.h
//
// Description: TftpBootloader (a bootloading protocol) Client Functionality
//
// Copyright 2013 by Silicon Laboratories. All rights reserved.             *80*

void emInitializeTftpBootloaderServer(void);
void emInitializeAppBootloader(void);
void emNotifyTftpBootloaderNetworkUp(void);
void emRunTftpBootloaderEvents(void);
void emMarkTftpBootloaderServerBuffers(void);
void emHandleTftpBootloaderBootloadRequest(const uint8_t *source,
                                           const uint8_t *packet,
                                           uint16_t packetLength);
void emBootloadEventHandler(void);
extern EmberEventControl emBootloadEvent;
