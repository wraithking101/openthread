// File: tftp-test-file-generator.c
//
// Description: TFTP Test EBL File Generator
//
// Copyright 2013 by Silicon Laboratories. All rights reserved.             *80*

#include PLATFORM_HEADER
#include "stack/include/ember-types.h"
#include "stack/core/ember-stack.h"
#include "hal/hal.h"
#include "stack/framework/ip-packet-header.h"
#include "stack/ip/ip-header.h"
#include "stack/ip/6lowpan-header.h"
#include "stack/ip/dispatch.h"
#include "app/util/serial/command-interpreter2.h"

#define TEST_IMAGE_FILE_SIZE 10300

static uint8_t theEblFile[TEST_IMAGE_FILE_SIZE] = {0};
static const uint8_t * const theFileName = "test-data";

void emGetTftpFile(const uint8_t **fileName, const uint8_t **file, uint16_t *length)
{
  uint16_t i;
  *fileName = theFileName;

  for (i = 0; i < TEST_IMAGE_FILE_SIZE; i++) {
    // modulo i by 511 so each chunk of 512 is shifted by 1
    // (and not identical)
    theEblFile[i] = i % 511;
  }

  *file = theEblFile;
  *length = TEST_IMAGE_FILE_SIZE;
}
