// File: tftp-client-app.c
//
// Description: TFTP Client App Functionality
//
// Copyright 2013 by Silicon Laboratories. All rights reserved.             *80*

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>

#include PLATFORM_HEADER
#include "stack/include/ember-types.h"
#include "stack/core/ember-stack.h"
#include "hal/hal.h"
#include "app/util/serial/command-interpreter2.h"
#include "stack/framework/ip-packet-header.h"
#include "stack/ip/ip-header.h"
#include "stack/ip/6lowpan-header.h"
#include "app/util/bootload/tftp/client/tftp-client.h"
#include "app/util/bootload/tftp/tftp.h"
#include "app/util/bootload/tftp/tftp-app.h"
#include "stack/core/ember-stack.h"
#include "plugin/serial/serial.h"
#include "stack/ip/host/unix-interface.h"
#include "app/util/bootload/tftp/client/tftp-file-reader.h"
#include "stack/ip/host/host-udp-retry.h"

extern bool emReadingTrace;
extern bool emTestingDone(void);

#define TFTP_TOKEN 0xf1ee
#define TFTP_RETRY_LIMIT 5
#define TFTP_RETRY_DELAY 2000

static TftpClientState clientState = NO_CLIENT_STATE;
static struct ifaddrs *myAddressData = NULL;
const uint8_t *emTftpFile = NULL;
uint32_t emTftpFileLength = 0;
const uint8_t *emTftpFileName = NULL;

void emResetTftpClient(void)
{
  clientState = NO_CLIENT_STATE;
  emResetTftp();

  // let's talk to the standard port
  emTftpRemoteTid = TFTP_PORT;

  // listen on a random port
  emTftpListen(true);
}

#ifdef UNIX_HOST
#  define RETURN_TYPE int
#else
#  define RETURN_TYPE void
#  define argc 0
#  define argv NULL
#endif

void sendFileCommand(void)
{
  if (emberCommandArgumentCount() == 2) {
    emTftpBlockSize = emberUnsignedCommandArgument(1);
  }

  emTftpOpenAndSendFile(emberStringCommandArgument(0, NULL), 0);
}

static void addString(const char *string, uint8_t **finger)
{
  // add 1 for the NULL terminator
  uint8_t copyLength = emStrlen((uint8_t*)string) + 1;
  MEMCOPY(*finger, string, copyLength);
  *finger += copyLength;
}

static void addIntegerString(uint16_t value, uint8_t **finger)
{
  uint8_t valueString[20] = {0};
  // add 1 for the NULL terminator
  int8_t length = sprintf((char*)valueString, "%u", value) + 1;
  assert(length > 0 && length < sizeof(valueString));
  MEMCOPY(*finger, valueString, length);
  *finger += length;
}

static void addOption(const char *key, uint16_t value, uint8_t **finger)
{
  addString(key, finger);
  addIntegerString(value, finger);
}

static void retryEventHandler(uint16_t token, uint8_t retriesRemaining)
{
  // communication error
  if (retriesRemaining == 0) {
    emberSerialPrintfLine(APP_SERIAL, "Communication error, resetting");
    emberTftpClientStatusHandler(TFTP_CLIENT_FILE_TRANSMISSION_TIMEOUT);
    emResetTftpClient();
  } else {
    emberSerialPrintfLine(APP_SERIAL, "Resending...");
  }
}

static void addUdpRetry(const uint8_t *payload,
                        uint16_t length)
{
  emAddUdpRetry(&emTftpRemoteIp,
                emTftpLocalTid,
                emTftpRemoteTid,
                payload,
                length,
                TFTP_TOKEN,
                TFTP_RETRY_LIMIT,
                TFTP_RETRY_DELAY,
                &retryEventHandler);
}

static void sendFile(void)
{
  clientState = SEND_WRITE_REQUEST;

  uint8_t payload[100] = {0};
  uint8_t *finger = payload;

  // store the packet type
  emberStoreHighLowInt16u(finger, TFTP_WRITE_REQUEST);
  finger += sizeof(uint16_t);

  // add the file name
  addString((char*)emTftpFileName, &finger);

  // we're sending a file of type octet
  addString(TFTP_DEFAULT_MODE, &finger);

  // add the blocksize option
  addOption(TFTP_BLOCK_SIZE_STRING, emTftpBlockSize, &finger);

  if (! emTftpScripting) {
    emberSerialPrintfLine(APP_SERIAL,
                          "Sending %u bytes of file '%s' with mode '%s'",
                          emTftpFileLength,
                          emTftpFileName,
                          TFTP_DEFAULT_MODE);
  }

  assert(finger - payload < sizeof(payload));
  addUdpRetry(payload, finger - payload);
  emberTftpClientStatusHandler(TFTP_CLIENT_START_FILE_TRANSMISSION);
}

void emTftpSendFile(const uint8_t *fileName,
                    const uint8_t *fileData,
                    uint32_t fileLength)
{
  emTftpFileName = fileName;
  emTftpFile = fileData;
  emTftpFileLength = fileLength;
  sendFile();
}

static void printOk(void)
{
  emberSerialPrintfLine(APP_SERIAL, "OK");
}

void setServerCommand(void)
{
  if (emTftpSetServer(emberStringCommandArgument(0, NULL))) {
    printOk();
  } else {
    emberSerialPrintfLine(APP_SERIAL, "Failed");
  }
}

void emTftpPrintHelp(void)
{
  const char *output =
    "\n********************************************************************\n\n"
    "TFTP Client Application\n\n"
    "Directions: \n"
    "First, run the \"set_server\" command, or specify --server on the \n"
    "command line.\n"
    "Then choose an interface via the \"choose_interface\" command.\n"
    "Lastly, send a file with the \"send_file\" command.\n";
  emberSerialPrintfLine(APP_SERIAL, output);
}

static void printPrompt(void)
{
  if (! emTftpScripting) {
    emberSerialPrintf(APP_SERIAL, "tftp-client> ");
  }
}

//
// upload part of the file
//
static void sendData(void)
{
  // sanity
  uint32_t fileIndex = (emTftpBlockNumber - 1) * emTftpBlockSize;
  uint8_t payload[550] = {0};
  uint8_t *finger = payload;

  // store the packet type
  emberStoreHighLowInt16u(finger, TFTP_DATA);
  finger += sizeof(uint16_t);

  // store the block number
  emberStoreHighLowInt16u(finger, emTftpBlockNumber);
  finger += sizeof(uint16_t);

  // we can send a maximum of emTftpBlockSize bytes per TFTP_DATA packet
  uint32_t chunkLength = emTftpFileLength - fileIndex;

  if (chunkLength > emTftpBlockSize) {
    chunkLength = emTftpBlockSize;
  }

  assert(fileIndex <= emTftpFileLength);

  // copy the file chunk
  MEMCOPY(finger, emTftpFile + fileIndex, chunkLength);
  finger += chunkLength;

  if (! emTftpScripting) {
    emberSerialPrintfLine(APP_SERIAL,
                          "TX [%u/%u/%u]",
                          chunkLength,
                          fileIndex,
                          emTftpFileLength);
  }

  assert(finger - payload < sizeof(payload));
  addUdpRetry(payload, finger - payload);
}

void emProcessTftpPacket(const uint8_t *source,
                         uint16_t remotePort,
                         const uint8_t *packet,
                         uint16_t payloadLength)
{
  const uint8_t *finger = packet;
  uint16_t opcode = emberFetchHighLowInt16u(finger);
  finger += sizeof(uint16_t);

  if (opcode == TFTP_ACK || opcode == TFTP_OACK) {
    uint16_t blockNumber = emTftpBlockNumber;

    if (opcode == TFTP_ACK) {
      blockNumber = emberFetchHighLowInt16u(finger);
      finger += sizeof(uint16_t);
      assert(emTftpFileName != NULL_BUFFER);
    } else {
      if (! emTftpScripting) {
        emberSerialPrintfLine(APP_SERIAL, "OACK");
      }

      const uint8_t *fingerSave = finger;

      while ((finger - packet) < payloadLength) {
        if (*finger == 0) {
          if (! emTftpScripting) {
            emberSerialPrintfLine(APP_SERIAL, "OACK - %s", fingerSave);
          }

          fingerSave = finger + 1;
        }

        finger++;
      }
    }

    if (blockNumber == emTftpBlockNumber) {
      emRemoveUdpRetry(TFTP_TOKEN);

      if (emTftpBlockNumber * emTftpBlockSize
          <= emTftpFileLength) {
        if (emTftpBlockNumber == 0) {
          // this is the first ACK, so we can now set the server's TID
          assert(emTftpRemoteTid == TFTP_PORT);
          emTftpRemoteTid = remotePort;
          clientState = SEND_FILE;
        }

        emTftpBlockNumber++;
        sendData();
      } else {
        emTftpCleanupFile();

        // we're done
        if (emTftpScripting) {
          assert(emTestingDone());
          finishRunningTrace();
          exit(0);
        } else {
          emberSerialPrintfLine(APP_SERIAL, "Finished.");
          emResetTftpClient();
          emberTftpClientStatusHandler(TFTP_CLIENT_FILE_TRANSMISSION_SUCCESS);
        }
      }
    }
  } else if (opcode == TFTP_ERROR) {
    uint16_t code = emberFetchHighLowInt16u(finger);
    finger += 2;
    const uint8_t *message = finger;
    emberSerialPrintfLine(APP_SERIAL, "Error: code %u, message: %s", code, message);
  } else if (opcode == TFTP_OACK) {
  } else {
    assert(false);
  }
}

void emTftpStatusHandler(TftpStatus status)
{
  if (! emReadingTrace) {
    emTftpListen(true);
  }
}
