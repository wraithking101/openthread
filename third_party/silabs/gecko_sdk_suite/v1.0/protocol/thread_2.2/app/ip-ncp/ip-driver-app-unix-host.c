// File: ip-driver-app-unix-host.c
//
// Description: Unix Host code for the ip driver app
//
// Copyright 2013 by Silicon Laboratories. All rights reserved.             *80*

// For sigaction(2) and sigemptyset(3) in glibc.
#define _POSIX_C_SOURCE 1

#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <signal.h>

#ifdef __linux__
  #include <linux/if_tun.h>
#endif

#include PLATFORM_HEADER
#include "stack/core/ember-stack.h"
#include "hal/hal.h"
#include "phy/phy.h"
#include "plugin/serial/serial.h"
#include "uart-link-protocol.h"
#include "ip-driver-log.h"
#include "host-stream.h"
#include "data-client.h"
#include "ip-driver.h"
#include "app/tmsp/tmsp-enum.h"
#include "hal/micro/generic/ash-v3.h"
#include "app/ip-ncp/ncp-uart-interface.h"
#include "stack/ip/tls/dtls-join.h"

#ifdef UNIX_HOST
  #define LOG(x) x
#else
  #define LOG(x)
#endif

extern int mgmtListenFd;
extern int commAppListenFd;
extern int commAppJoinerFd;
static int logging = 1;

#define ARG_LENGTH 40
#define STRINGIFY(x) #x
#define STRINGIFYX(x) STRINGIFY(x)

static void setupListener(int listenFd, uint16_t port)
{
  if (listenFd < 0) {
    perror("socket creation failed");
    exit(1);
  }

  int on = 1;

  if (setsockopt(listenFd,
                 SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) != 0) {
    perror("set SO_REUSEADDR");
    exit(1);
  }

  int flags = fcntl(listenFd, F_GETFL);
  assert(fcntl(listenFd, F_SETFL, flags | O_NONBLOCK) != -1);
  struct sockaddr_in6 address;
  memset((char *) &address, 0, sizeof(address));
  address.sin6_family = AF_INET6;
  address.sin6_addr = in6addr_any;
  address.sin6_port = htons(port);

  if (bind(listenFd, (struct sockaddr *) &address, sizeof(address)) < 0) {
    perror("bind");
    exit(1);
  }

  if (listen(listenFd, 1) < 0) {
    perror("listen");
    exit(1);
  }
}

static void openHostAppManagementSocket(uint16_t port)
{
  mgmtListenFd = socket(AF_INET6, SOCK_STREAM, 0);
  setupListener(mgmtListenFd, port);
}

static void openCommAppManagementSocket(uint16_t port)
{
  commAppListenFd = socket(AF_INET6, SOCK_STREAM, 0);
  setupListener(commAppListenFd, port);
}

static void openCommAppJoinerSocket(void)
{
  commAppJoinerFd = socket(AF_INET6, SOCK_STREAM, 0);
  setupListener(commAppJoinerFd, DTLS_JOIN_PORT);
}

static int handleConnect(int listenFd, int driverAcceptFd)
{
  struct sockaddr_in6 address = {0};
  socklen_t addressLength = sizeof(address);
  int acceptFd = accept(listenFd,
                        (struct sockaddr *) &address, &addressLength);
  if (acceptFd >= 0) {
    if (driverAcceptFd >= 0) {
      close(driverAcceptFd);
    }

    int flags = fcntl(acceptFd, F_GETFL);
    assert(fcntl(acceptFd, F_SETFL, flags | O_NONBLOCK) != -1);
  }
  return acceptFd;
}

static void handleHostAppManagementConnect(void)
{
  driverHostAppManagementFd = handleConnect(mgmtListenFd,
                                            driverHostAppManagementFd);
}

static void handleCommAppManagementConnect(void)
{
  driverCommAppManagementFd = handleConnect(commAppListenFd,
                                            driverCommAppManagementFd);
}

static void handleCommAppJoinerConnect(void)
{
  driverCommAppJoinerDataFd = handleConnect(commAppJoinerFd,
                                            driverCommAppJoinerDataFd);
}

static int signalCaught = 0;

static void signalHandler(int signal)
{
  // save signal for DSR processing to avoid race condition of processing now
  signalCaught = signal;
}

static void cleanupAndExit(int exitCode)
{
  if (signalCaught) {
    // we test this here because some syscalls, notably select(), return an
    // error when interrupted by a signal and we want to still be able to
    // inform the user that the signal was the underlying cause
    LOG(ipDriverLogStatus("Caught signal %d, terminating", signalCaught);)
  }

  LOG(ipDriverLogFlush();)
  close(driverDataFd); // unsure if necessary; Ed had a signal handler for
                       // this that was never actually called AFAICT
  exit(exitCode);
}

//------------------------------------------------------------------------------
// Connection to NCP, which may be either an IPv6 socket or a serial port.
static void connectNcpSocket(uint16_t port)
{
  driverNcpFd = socket(AF_INET6, SOCK_STREAM, 0);

  if (driverNcpFd < 0) {
    perror("socket creation failed");
    exit(1);
  }

  struct sockaddr_in6 address = {0};
  address.sin6_family = AF_INET6;
  address.sin6_addr.s6_addr[15] = 1;
  address.sin6_port = htons(port);

  if (connect(driverNcpFd,
              (struct sockaddr *) &address,
              sizeof(address))
      != 0) {
    perror("connect failed");
    exit(1);
  }

  int flags = fcntl(driverNcpFd, F_GETFL);
  assert(fcntl(driverNcpFd, F_SETFL, flags | O_NONBLOCK) != -1);
}

static int shutdownRequested = 0;
 
void ipDriverShutdown(void)
{
  shutdownRequested = 1;
}

bool initIpDriver(int argc, char **argv)
{
  if (argc < 4) {
    return false;
  }

  char uartArg[ARG_LENGTH + 1] = {0};
  char tunArg[ARG_LENGTH + 1] = {0};
  char mgmtArg[ARG_LENGTH + 1] = {0};
  char commAppArg[ARG_LENGTH + 1] = {0};
  char logArg[ARG_LENGTH + 1] = {0};

  bool ncpUsesSocket = false;

  while (true) {
    static struct option long_options[] = {
      { "nolog",         no_argument,       &logging,  0  },
      { "uart",          required_argument, 0,        'u' },
      { "tun",           required_argument, 0,        't' },
      { "mgmt",          required_argument, 0,        'm' },
      { "comm_app",      optional_argument, 0,        'c' },
      { "socket",        no_argument,       0,        's' },
      { "log",           required_argument, 0,        'l' },
      { "flow-control",  required_argument, 0,        'f' },
      { 0, 0, 0, 0 }
    };

    int option_index = 0;
    int c = getopt_long(argc, argv, "u:t:m:c:sf:", long_options, &option_index);
    if (c == -1) {
      if (option_index != argc && option_index != 0) {
        fprintf(stderr, "Unexpected argument %s\n", argv[option_index]);
        return false;
      }
      break;
    }

    switch (c) {
    case 0:
      break;
    case 'u':
      sscanf(optarg, "%" STRINGIFYX(ARG_LENGTH) "s", uartArg);
      break;
    case 't':
      sscanf(optarg, "%" STRINGIFYX(ARG_LENGTH) "s", tunArg);
      break;
    case 'm':
      sscanf(optarg, "%" STRINGIFYX(ARG_LENGTH) "s", mgmtArg);
      break;
    case 'c':
      sscanf(optarg, "%" STRINGIFYX(ARG_LENGTH) "s", commAppArg);
      break;
    case 's':
      ncpUsesSocket = true;
      break;
    case 'l':
      sscanf(optarg, "%" STRINGIFYX(ARG_LENGTH) "s", logArg);
      assert(emLogConfigFromName(logArg, emStrlen(logArg), true, APP_SERIAL));
      break;
    case 'f':
      switch (*optarg) {
      case 'r':
        rtsCts = true;
        break;    
      case 'x':
        rtsCts = false;
        break;    
      default:
        fprintf(stderr, "Invalid flow control choice %s.\
                        \nr - RTS/CTS\
                        \nx - XON/XOFF\n", optarg);
        exit(1);
      }
      break;
    default:
      return false;
    }
  }

  logEnabled = (bool) logging;

  if (ncpUsesSocket) {
    int ncpPort = atoi(uartArg);
    if (ncpPort <= 0 || ncpPort > 65535) {
      fprintf(stderr, "Invalid NCP port number %d\n", ncpPort);
      return false;
    }
    connectNcpSocket(ncpPort);
    ncpUartUseAsh = false;
  } else {
    ncpUartUseAsh = true;
    emOpenNcpUart(uartArg);
    LOG(ipDriverLogStatus("Opened UART device %s.", uartArg););
  }

  emOpenTunnel(tunArg);

  int mgmtPort = atoi(mgmtArg);
  if (mgmtPort <= 0 || mgmtPort > 65535) {
    fprintf(stderr, "Invalid management port number %d\n", mgmtPort);
    return false;
  }

  // configure signal handlers
  struct sigaction sigHandler;
  sigHandler.sa_handler = signalHandler;
  sigemptyset(&sigHandler.sa_mask);
  sigHandler.sa_flags = 0;
  sigaction(SIGINT, &sigHandler, NULL);
  sigaction(SIGTERM, &sigHandler, NULL);
  sigaction(SIGABRT, &sigHandler, NULL);

  openHostAppManagementSocket(mgmtPort);

  // Optional.
  int commAppPort = atoi(commAppArg);
  if (commAppPort > 0 && commAppPort <= 65535) {
    openCommAppManagementSocket(commAppPort);
    openCommAppJoinerSocket();
  } else {
    commAppListenFd = -1;
    commAppJoinerFd = -1;
  }

  return true;
}

void ipDriverTick(void)
{
  fd_set input;
  FD_ZERO(&input);
  FD_SET(driverDataFd, &input);
  FD_SET(driverNcpFd,  &input);
  FD_SET(mgmtListenFd, &input);
  if (commAppListenFd != -1 && commAppJoinerFd != -1) {
    FD_SET(commAppListenFd, &input);
    FD_SET(commAppJoinerFd, &input);
  }

  if (signalCaught) {
    cleanupAndExit(1);
  }

  if (shutdownRequested) {
    cleanupAndExit(0);
  }

  if (driverHostAppManagementFd >= 0) {
    FD_SET(driverHostAppManagementFd, &input);
  }

  if (driverCommAppManagementFd >= 0) {
    FD_SET(driverCommAppManagementFd, &input);
  }

  if (driverCommAppJoinerDataFd >= 0) {
    FD_SET(driverCommAppJoinerDataFd, &input);
  }

  int maxFd = driverDataFd;
  if (maxFd < driverNcpFd)        maxFd = driverNcpFd;
  if (maxFd < mgmtListenFd)       maxFd = mgmtListenFd;
  if (commAppListenFd != -1
      && maxFd < commAppListenFd) {
    maxFd = commAppListenFd;
  }
  if (commAppJoinerFd != -1
      && maxFd < commAppJoinerFd) {
    maxFd = commAppJoinerFd;
  }
  if (maxFd < driverHostAppManagementFd) maxFd = driverHostAppManagementFd;
  if (maxFd < driverCommAppManagementFd) maxFd = driverCommAppManagementFd;
  if (maxFd < driverCommAppJoinerDataFd) maxFd = driverCommAppJoinerDataFd;

  struct timeval timeout;
  timeout.tv_sec = 0;
  timeout.tv_usec = 100000;

  int n = select(maxFd + 1, &input, NULL, NULL, &timeout);

  if (n < 0) {
    perror("select failed");
    cleanupAndExit(1);
  } else if (n == 0) {
    // Timeout.
  } else {
    if (FD_ISSET(mgmtListenFd, &input)) {
      handleHostAppManagementConnect();
    }

    if (commAppListenFd != -1
        && FD_ISSET(commAppListenFd, &input)) {
      handleCommAppManagementConnect();
    }

    emCheckNcpUartInput(&input);

    if (commAppJoinerFd != -1
        && FD_ISSET(commAppJoinerFd, &input)) {
      handleCommAppJoinerConnect();
    }

    if (0 <= driverHostAppManagementFd
        && FD_ISSET(driverHostAppManagementFd, &input)) {
      IpModemReadStatus status = 
        readIpModemInput(driverHostAppManagementFd,
                         &managementStream,
                         managementHandler);
      emTestIpModemReadStatusResult(status, &managementStream);
      if (status == IP_MODEM_READ_EOF) {
        close(driverHostAppManagementFd);
        driverHostAppManagementFd = -1;
      }
    }

    if (0 <= driverCommAppManagementFd
        && FD_ISSET(driverCommAppManagementFd, &input)) {
      IpModemReadStatus status = 
        readIpModemInput(driverCommAppManagementFd,
                         &managementStream,
                         managementHandler);
      emTestIpModemReadStatusResult(status, &managementStream);
      if (status == IP_MODEM_READ_EOF) {
        close(driverCommAppManagementFd);
        driverCommAppManagementFd = -1;
      }
    }

    if (0 <= driverCommAppJoinerDataFd
        && FD_ISSET(driverCommAppJoinerDataFd, &input)) {
      readIpv6Input(driverCommAppJoinerDataFd, &dataStream, UART_LINK_TYPE_UNSECURED_DATA, dataHandler);
    }
  }
}
