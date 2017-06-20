// File: tftp-bootloader-unix-client.c
//
// Description: TFTP Bootloader unix client.
// This program uploads an image to a TFTP server specified via the command line
// like: --server "FD01::1"
//
// Copyright 2013 by Silicon Laboratories. All rights reserved.             *80*

#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <netdb.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <ifaddrs.h>

#include PLATFORM_HEADER
#include "stack/include/ember-types.h"
#include "stack/core/ember-stack.h"
#include "hal/hal.h"
#include "stack/ip/host/unix-address.h"
#include "app/util/bootload/tftp/tftp.h"
#include "app/util/bootload/tftp/client/tftp-client.h"
#include "app/util/bootload/tftp/client/tftp-file-reader.h"
#include "stack/ip/host/unix-interface.h"
#include "stack/ip/ip-address.h"
#include "stack/ip/host/host-address-table.h"
#include "stack/ip/host/host-listener-table.h"
#include "stack/ip/tls/native-test-util.h"

static void printUsage(void)
{
  fprintf(stderr,
          "Usage: \n"
          "tftp-client-app [--server \"server IPv6 address\"]\n");
  exit(1);
}

void emInitializeTftp(int argc, char **argv)
{
  emberInitializeListeners();
  //emberInitializeHostAddressTable();

  //
  // parse the input options and set the server IP address
  //
  struct option longOptions[] = {
    {"server",           required_argument, NULL, 'a'},
    {"write_trace",      required_argument, NULL, 'b'},
    {"run_trace",        required_argument, NULL, 'c'},
    {"interface_number", required_argument, NULL, 'd'},
    {"send_file",        required_argument, NULL, 'e'},
    {"block_size",       required_argument, NULL, 'f'},
    {NULL}
  };

  bool sendFile = false;
  uint8_t interface = 0xFF;
  int optionIndex = 0;
  int option;
  const char *fileName = NULL;

  while ((option = getopt_long(argc,
                               argv,
                               "a:b:c:d:e:",
                               longOptions,
                               &optionIndex))
         != -1) {
    if (option == 'a') {
      // server
      uint8_t *serverAddress = optarg;

      if (! emTftpSetServer(serverAddress)) {
        fprintf(stderr, "Unable to resolve server: %s\n", serverAddress);
        exit(1);
      }
    } else if (option == 'b') {
      // write_trace
      openOutputTraceFile(optarg);
    } else if (option == 'c') {
      // run_trace
      emTftpLocalTid = openInputTraceFile(optarg);
      emTftpScripting = true;
    } else if (option == 'd') {
      // interface_number
      interface = atoi(optarg);
    } else if (option == 'e') {
      // send_file
      fileName = optarg;
      sendFile = true;
    } else if (option == 'f') {
      // block_size
      emTftpBlockSize = atoi(optarg);
    }
  }

  // choose the interface after arguments are done being processed
  if (interface != 0xFF) {
    emTftpReallyChooseInterface(interface, NULL);
  }

  if (sendFile) {
    if (interface == 0xFF) {
      fprintf(stderr,
              "Error: interface must be set via --interface_number when "
              "--send_file is present");
    }

    if (emReadingTrace) {
      emTftpOpenTraceFd('c', 's');
    }

    emTftpOpenAndSendFile(fileName, 0);
  }

  if (option == -1) {
    if (optind != argc) {
      fprintf(stderr, "Unexpected argument: %s\n", argv[optind]);
      printUsage();
    }
  }
}

bool emTftpSetServer(const uint8_t *serverAddress)
{
  return emSetIpv6Address(&emTftpRemoteIp, serverAddress);
}

void emTftpListenStatusHandler(uint16_t port, EmberIpv6Address *address)
{
  if (emWritingTrace) {
    HostListener *listener = emberFindListener(port, address->bytes);
    assert(listener != NULL);
    noteTraceFd(listener->socket, 'c', 's', port, address->bytes);
  }
}
