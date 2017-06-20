// File: tftp-server.h
//
// Description: TFTP Server Functionality
//
// Copyright 2013 by Silicon Laboratories. All rights reserved.             *80*

void emResetTftpServer(void);
void emInitializeTftpServer(void);
void emProcessTftpServerPacket(uint16_t header, Ipv6Header *ipHeader);
bool emStoreTftpFileChunk(uint32_t index, const uint8_t *data, uint16_t length);

// a TID used for testing / scripting
extern uint16_t emTftpScriptingTid;

typedef enum {
  TFTP_FILE_WRITE_REQUEST,
  TFTP_FILE_DONE
} TftpServerStatus;

void emTftpServerStatusHandler(TftpServerStatus status);
