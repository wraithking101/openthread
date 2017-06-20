// File: tftp-file-reader.c
//
// Description: TFTP File Reader
//
// Copyright 2013 by Silicon Laboratories. All rights reserved.             *80*

#include PLATFORM_HEADER
#include "stack/include/ember-types.h"
#include "stack/core/ember-stack.h"
#include "hal/hal.h"
#include "plugin/serial/serial.h"
#include "app/util/bootload/tftp/client/tftp-client.h"

#include <stdio.h>
#include <stdlib.h>

static uint8_t *theBuffer = NULL;

bool emTftpOpenAndSendFile(const uint8_t *fileName, uint32_t offset)
{
  FILE *file = fopen((char*)fileName, "rb");
  bool result = true;

  if (file != NULL) {
    // get the file's length
    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    fseek(file, 0, SEEK_SET);
    assert(size >= offset);
    size -= offset;

    // fast forward to offset
    fseek(file, offset, SEEK_SET);

    theBuffer = malloc(size);
    assert(theBuffer != NULL);
    fread(theBuffer, size, 1, file);
    fclose(file);

    emTftpSendFile(fileName, theBuffer, size);
  } else {
    result = false;
    // error opening file
    emberSerialPrintfLine(APP_SERIAL, "Unable to open file: %s", fileName);
  }

  return result;
}

void emTftpCleanupFile(void)
{
  free(theBuffer);
}
