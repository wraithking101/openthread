// File: tftp-server.c
//
// Description: TFTP Server Functionality
//
// Copyright 2013 by Silicon Laboratories. All rights reserved.             *80*

#include PLATFORM_HEADER
#include "stack/include/ember.h"
#include "hal/hal.h"
#include "plugin/serial/serial.h"
#include "app/util/bootload/tftp/tftp.h"
#include "app/util/bootload/tftp/server/tftp-server.h"

#include <stdlib.h>

extern bool emTestingDone(void);

uint16_t emTftpScriptingTid = 0xFFFF;

void emInitializeTftpServer(void)
{
  // listen initially on TFTP_PORT
  emTftpLocalTid = TFTP_PORT;
  emTftpListen(false);
}

void emResetTftpServer(void)
{
  emResetTftp();
  emInitializeTftpServer();
}

void emTftpStatusHandler(TftpStatus status)
{
#if (defined UNIX_HOST) || (defined UNIX_HOST_SIM)
  if (status == TFTP_INTERFACE_CHOSEN) {
    // we must wait until an interface is chosen to listen on port TFTP_PORT
    emInitializeTftpServer();
  }
#endif
}

void emSendTftpAck(void)
{
  uint8_t payload[20] = {0};
  uint8_t *finger = payload;
  emberStoreHighLowInt16u(finger, TFTP_ACK);
  finger += sizeof(uint16_t);
  emberStoreHighLowInt16u(finger, emTftpBlockNumber);
  finger += sizeof(uint16_t);
  emSendTftpPacket(payload, finger - payload);
}

static const uint8_t *readString(const uint8_t **finger, const uint8_t *limit)
{
  const uint8_t *result = *finger;
  uint8_t length = emStrlen(result) + 1;
  *finger += length;

  if (*finger >= limit) {
    // error
  }

  return result;
}

static void readOptions(const uint8_t **finger, const uint8_t *limit)
{
  while (*finger < limit) {
    const uint8_t *key = readString(finger, limit);
    const uint8_t *value = readString(finger, limit);

    if (emStrcmp(key, (const uint8_t *)TFTP_BLOCK_SIZE_STRING) == 0) {
      emTftpBlockSize = atoi((char*)value);
    }
  }
}

void emProcessTftpPacket(const uint8_t *source,
                         uint16_t remotePort,
                         const uint8_t *payload,
                         uint16_t payloadLength)
{
  const uint8_t *finger = payload;
  const uint8_t *limit = payload + payloadLength;
  uint8_t opcode = emberFetchHighLowInt16u(finger);
  finger += sizeof(uint16_t);

  if (opcode == TFTP_WRITE_REQUEST) {
    assert(emTftpBlockNumber == 0);

    if (emTftpRemoteTid != 0xFFFF) {
      emberSerialPrintfLine(APP_SERIAL,
                            "TFTP server already has a remote TID: %u, exiting",
                            emTftpRemoteTid);
      return;
    }

    assert(emTftpRemoteTid == 0xFFFF);
    emTftpRemoteTid = remotePort;
    MEMCOPY(emTftpRemoteIp.bytes, source, 16);

    const uint8_t *filename = readString((const uint8_t **)&finger, limit);
    const uint8_t *mode = readString((const uint8_t **)&finger, limit);
    readOptions(&finger, limit);

    assert(emStrcmp(mode, (const uint8_t *)TFTP_DEFAULT_MODE) == 0);
    emTftpBlockNumber = 0;

    if (emTftpScripting) {
#ifdef UNIX_HOST
      // listen on the scripting TID
      emTftpLocalTid = emTftpScriptingTid;
      emTftpOpenTraceFd('s', 'c');
#endif
    } else {
      // choose a new random TID
      emTftpListen(true);
    }

    emTftpServerStatusHandler(TFTP_FILE_WRITE_REQUEST);
    emSendTftpAck();

    if (! emTftpScripting) {
      emberSerialPrintfLine(APP_SERIAL,
                            "[RX write request for file %s, "
                            "mode %s, "
                            "remote port %u, "
                            "new local port %u]",
                            filename,
                            mode,
                            remotePort,
                            emTftpLocalTid);
    }
  } else if (opcode == TFTP_DATA) {
    uint16_t blockNumber = emberFetchHighLowInt16u(finger);
    finger += sizeof(uint16_t);

    if (blockNumber == emTftpBlockNumber + 1) {
      emTftpBlockNumber = blockNumber;

      // 4 comes from 2 bytes for opcode, and 2 bytes for block number
      uint16_t blockSize = (limit - finger);
      assert(blockSize <= emTftpBlockSize);

      emStoreTftpFileChunk((emTftpBlockNumber - 1) * emTftpBlockSize,
                           finger,
                           blockSize);
      emSendTftpAck();

      if (! emTftpScripting) {
        //emberSerialPrintfLine(APP_SERIAL,
        //                      "[RX [%u/%u] on port %u]",
        //                      emTftpBlockNumber,
        //                      blockSize,
        //                      emTftpLocalTid);
      }

      if (blockSize < emTftpBlockSize) {
        if (! emTftpScripting) {
          emberSerialPrintfLine(APP_SERIAL, "[RX done receiving file]");
        }

        emTftpServerStatusHandler(TFTP_FILE_DONE);

        // we're done
        emResetTftpServer();

#ifdef UNIX_HOST
        if (emTftpScripting) {
          assert(emTestingDone());
          finishRunningTrace();
          exit(0);
        }
#endif
      }
    } else {
      // ignore ?
    }
  } else {
    // send error
    assert(false);
  }
}

void emTftpPrintHelp(void)
{
  const char *output =
    "\n********************************************************************\n\n"
    "TFTP Server Application\n\n";
  emberSerialPrintfLine(APP_SERIAL, output);
}
