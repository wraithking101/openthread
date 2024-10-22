// ****************************************************************************
// * bootload-ncp-uart.c
// *
// * Routines for bootloading from a linux host connected via UART to an NCP
// *
// * Copyright 2015 Silicon Laboratories, Inc.
// *****************************************************************************

#include PLATFORM_HEADER
#include CONFIGURATION_HEADER
#include "stack/include/ember.h"
#include "hal/hal.h"
#include "bootload-ncp.h"

#include "hal/micro/generic/ash-v3.h"

#include "ezsp/ezsp-protocol.h"
#include "ezsp-host/ash/ash-host.h"
#include "ezsp-host/ash/ash-host-ui.h"
#include "ezsp-host/ezsp-host-io.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>

//------------------------------------------------------------------------------
// Globals

#define NULL_FILE_DESCRIPTOR  (-1)
static const char returnChar = 0x0A; // line feed
static const char runChar =  '2';
static const char beginDownload = '1';
static const char xmodemStartChar = 'C';

#define BOOTLOADER_DELAY  4     // seconds
#define MAX_ERROR_LENGTH  255

// This is setup to be greater than the time between 'C' characters
// spit out by the bootloader to indicate that it is ready for an Xmodem
// transfer.
#define READ_TIMEOUT_SEC  3

#define MAX_BYTES_WITHOUT_PROMPT 200

// We store the last few bytes so that we can check whether we received
// the expected bootloader prompt "BL >"
#define MAX_RECEIVED_BYTES  4
static uint8_t receivedBytesCache[MAX_RECEIVED_BYTES];
static const char menuPrompt[] = "BL >";

// This is sized to be big enough to read "\r\nbegin upload\r\n"
// with some additional extra fuzz.
#define MAX_BYTES_WITHOUT_XMODEM_START  20

//------------------------------------------------------------------------------
// Forward Declarations

static void delay(void);
static int checkFdForData(void);
static void storeReceivedByte(uint8_t byte);
static bool checkForBootloaderMenu(void);
static bool checkForXmodemStart(void);

#define errorPrint(...) fprintf(stderr, __VA_ARGS__)
#define messagePrint(...) printf(__VA_ARGS__)

static int serialFd = NULL_FILE_DESCRIPTOR;

//------------------------------------------------------------------------------

bool emStartNcpBootloaderCommunications(void)
{
  serialFd = NULL_FILE_DESCRIPTOR;
  char errorString[MAX_ERROR_LENGTH];

  delay();
  messagePrint("Setting up serial port\n");

  if (EZSP_SUCCESS != ezspSetupSerialPort(&serialFd,
                                         errorString,
                                         MAX_ERROR_LENGTH,
                                         true)) {          // bootloader mode?
    errorString[MAX_ERROR_LENGTH - 1] = '\0';
    errorPrint("Error: Could not setup serial port: %s\n", errorString);
  }

  if (!checkForBootloaderMenu()) {
    return false;
  }

  if (!emBootloadSendByte(beginDownload)) {
    errorPrint("Failed to set bootloader in download mode.\n");
    return false;
  }

  return checkForXmodemStart();
}

static bool checkForXmodemStart(void)
{
  uint8_t bytesSoFar = 0;
  while (bytesSoFar < MAX_BYTES_WITHOUT_XMODEM_START) {
    int status = checkFdForData();
    if (status <= 0) {
      // Timeout or error
      return false;
    }

    uint8_t data;
    ssize_t bytes = read(serialFd, &data, 1);
    if (bytes < 0) {
      errorPrint("Read failed: %s\n", strerror(errno));
      return false;
    }

    // debug
    //    printf("%c", (char)data);

    if (data == xmodemStartChar) {
      return true;
    }
    bytesSoFar++;
  }

  errorPrint("Failed to get Xmodem start message from bootloader.\n");
  return false;
}

bool emBootloadSendData(const uint8_t *data, uint16_t length)
{
  if (length != write(serialFd,
                      data,
                      length)) {         // count
    errorPrint("Error: Failed to write to serial port: %s\n",
               strerror(errno));
    return false;
  }
  fsync(serialFd);
  return true;
}

bool emBootloadSendByte(uint8_t byte)
{
  return emBootloadSendData(&byte, 1);
}

static bool checkForBootloaderMenu(void)
{
  // verbose debug
  //printf("checkForBootloaderMenu()\n");

  MEMSET(receivedBytesCache, 0, MAX_RECEIVED_BYTES);

  // Send a CR to the bootloader menu to trigger the prompt to echo back.
  if (!emBootloadSendByte(returnChar)) {
    return false;
  }

  bool done = false;
  int totalBytes = 0;
  while (!done) {
    int status = checkFdForData();

    if (status <= 0) {
      return false;
    }

    uint8_t data;
    ssize_t bytes = read(serialFd, &data, 1);
    if (bytes < 0) {
      errorPrint("Error: read() failed: %s\n", strerror(errno));
      return false;
    } else if (bytes == 0) {
      continue;
    }
    totalBytes++;
    storeReceivedByte(data);

    // verbose debug
    //printf("%c", data);

    if (0 == MEMCOMPARE(receivedBytesCache, menuPrompt, MAX_RECEIVED_BYTES)) {
      done = true;
      continue;
    }

    if (totalBytes > MAX_BYTES_WITHOUT_PROMPT) {
      errorPrint("Got %d bytes without seeing the bootloader menu prompt.\n",
                 MAX_BYTES_WITHOUT_PROMPT);
      return false;
    }
  }

  return true;
}

bool emRebootNcpAfterBootload(void)
{
  delay();
  messagePrint("Rebooting NCP\n");

  if (!checkForBootloaderMenu()) {
    errorPrint("Failed to get bootloader menu prompt.\n");
    return false;
  }

  if (!emBootloadSendByte(runChar)) {
    return false;
  }

  delay();        // arbitrary delay to give NCP time to reboot.
  close(serialFd);
  serialFd = NULL_FILE_DESCRIPTOR;
  return true;
}

static void storeReceivedByte(uint8_t newByte)
{
  // We right shift all the bytes in the array.  The first byte on the array
  // will be dumped, and the new byte will become the last byte.
  uint8_t i;
  for (i = 0; i < MAX_RECEIVED_BYTES - 1; i++) {
    receivedBytesCache[i] = receivedBytesCache[i+1];
  }
  receivedBytesCache[i] = newByte;
}

static void delay(void)
{
  // Empirically I have found that we have to delay to give the bootloader
  // time to launch before we see the LEDs flash and the bootloader menu
  // is available.  Not sure if this could be improved.
  messagePrint("Delaying %d seconds\n", BOOTLOADER_DELAY);
  sleep(BOOTLOADER_DELAY);
}

// CYGWIN NOTE
//   Cygwin does NOT properly handle select() with regard to serial ports.
//   I am not sure exactly what will happen in that case but I believe
//   it will not timeout.  If all goes well we will never get a timeout,
//   but if things go south we won't handle them gracefully.
static int checkFdForData(void)
{
  fd_set readSet;
  struct timeval timeout = {
    READ_TIMEOUT_SEC,
    0,                  // ms. timeout value
  };

  FD_ZERO(&readSet);
  FD_SET(serialFd, &readSet);

  int fdsWithData = select(serialFd + 1,         // per the select() manpage
                                                 //   highest FD + 1
                           &readSet,             // read FDs
                           NULL,                 // write FDs
                           NULL,                 // exception FDs
                           (READ_TIMEOUT_SEC > 0 // passing NULL means wait
                            ? &timeout           //   forever
                            : NULL));

  // Ideally we should check for EINTR and retry the operation.
  if (fdsWithData < 0) {
    errorPrint("FATAL: select() returned error: %s\n",
               strerror(errno));
    return -1;
  }

  if (fdsWithData == 0) {
    errorPrint("Error: Timeout occurred waiting for read data.\n");
  }
  return (fdsWithData);  // If timeout has occurred, this will be 0,
                         // otherwise it will be the number of FDs
                         // with data.
}

// TODO: would be better to make this work based on any qty of characters
//  being allowed to be received until the timeout is reached.  Current
//  behavior only looks at one character and waits up to the timeout for it to
//  arrive
bool emBootloadWaitChar(uint8_t *data, bool expect, uint8_t expected)
{
  int status = checkFdForData();
  if (status <= 0) {
    // Timeout or error
    return false;
  }

  ssize_t bytes = read(serialFd, data, 1);
  if (bytes < 0) {
    errorPrint("Read failed: %s\n", strerror(errno));
    return false;
  }

  // debug
  // printf("Got <%c,%x>\n", (char)*data, (char)*data);

  if(expect) {
    return ((*data)==expected);
  } else {
    return true;
  }
}

void emPostNcpBootload(bool success)
{
  // TODO:  Call an application callback.

  printf("\nReboot not supported.  Exiting instead.\n");
  exit(success ? 0 : 1);
}
