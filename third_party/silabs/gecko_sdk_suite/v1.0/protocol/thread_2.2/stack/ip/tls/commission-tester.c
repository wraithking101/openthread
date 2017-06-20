/*
 * File: commission-tester.c
 * Description: Thread commissioning test app
 *
 * Copyright 2015 Silicon Laboratories, Inc.                                *80*
 */

#include <stdlib.h>             // malloc() and free()
#include <unistd.h>             // close()
#include <netinet/in.h>
#include <poll.h>
#include <errno.h>
#include <sys/time.h>

#include "core/ember-stack.h"
#include "platform/micro/aes.h"
#include "framework/buffer-malloc.h"
#include "framework/event-queue.h"
#include "framework/ip-packet-header.h"
#include "ip/udp.h"
#include "ip/tcp.h"
#include "ip/zigbee/join.h"
#include "ip/network-data.h"
#include "ip/commission.h"
#include "ip/ip-address.h"
#include "ip/association.h"
#include "app/coap/coap.h"
#include "app/util/serial/command-interpreter2.h"

#include "tls.h"
#include "dtls.h"
#include "dtls-join.h"
#include "small-aes/aes.h"
#include "rsa.h"
#include "tls-handshake-crypto.h"
#include "tls-record.h"
#include "tls-handshake.h"
#include "tls-public-key.h"
#include "tls-session-state.h"
#include "certificate.h"
#include "sha256.h"
#include "debug.h"
#include "native-test-util.h"
#include "credentials/test-certificate.h"
#include "jpake-ecc.h"
#include "app/coap/coap-stack.h"

enum {
  NO_ROLE,
  ROUTER,
  COMMISSIONER,
  JOINER
};

uint8_t role = NO_ROLE;

// Pseudo-radio messages start with a byte that both distinguishes them from
// non-radio DTLS messages, which start with a DTLS command byte, and say
// which key was intended.  No MAC security is actually used.
#define MAC_KEY_BYTE            0xFF
#define JOINER_ENTRUST_KEY_BYTE 0xFE
#define BEACON_REQUEST_BYTE     0xFD
#define BEACON_BYTE             0xFC
#define MIN_KEY_BYTE            BEACON_BYTE

extern bool emBuffersUseMalloc;
extern uint32_t emMallocCount;
extern uint32_t emMallocSize;

extern void connectionEventHandler(Event *event);
extern void emBorderRouterMessageHandler(CoapMessage *coap);
extern void emCommissionerMessageHandler(CoapMessage *coap);

static void randomInit(void);

static bool useBuffers = false;
bool emMacDropIncomingPackets = false;

void *tlsMalloc(uint16_t size)
{
  if (useBuffers) {
    return emBufferMalloc(size);
  } else {
    return malloc(size);
  }
}

// Don't call free() because we don't know which malloc produced
// the pointer.

void tlsFree(void *pointer)
{
  if (useBuffers) {
    emBufferFree(pointer);
  }
}

const CertificateAuthority *emMyAuthorities[] = {
  NULL
};

//----------------------------------------------------------------

uint32_t lastNumBigInts = 0;
uint32_t lastSizeBigInts = 0;

void resetBigIntCounts(void)
{
//   if (emMallocCount != lastNumBigInts) {
//     fprintf(stderr, "[update: %d bigints using %d bytes]\n",
//             emMallocCount - lastNumBigInts,
//             emMallocSize - lastSizeBigInts);
//     lastNumBigInts = emMallocCount;
//     lastSizeBigInts = emMallocSize;
//   }
}

//----------------------------------------------------------------

static Buffer markTestBuffer;   // to make sure we trace TLS structs properly

static void markConnectionBuffer(void)
{
  emMarkBuffer(&markTestBuffer);
  emMarkUdpBuffers();
  emJpakeEccMarkData();
  emberMarkEventQueue(&emStackEventQueue);
  emCoapMarkBuffers();
  emMarkBuffer(&emNetworkData);
}

static BufferMarker markers[] = {
  markConnectionBuffer,
  NULL
};

static uint16_t heapSize = 0;
static uint16_t count = 0;
static uint16_t reclaimCount = 10000;

static void reclaimBuffers(void)
{
//  uint16_t before = heapSize - emBufferBytesRemaining();
  if (count == reclaimCount) {
    markTestBuffer = NULL_BUFFER;
  }
  count += 1;
  emMallocFreeList = NULL_BUFFER;
  emReclaimUnusedBuffers(markers);
//  fprintf(stderr, "[heap %d (%d) -> %d]\n", before, emMallocSize,
//        heapSize - emBufferBytesRemaining());
//  debug("heap %d (%d) -> %d", before, emMallocSize,
//        heapSize - emBufferBytesRemaining());
  //emMallocSize = 0; // why?
}

static void udpStatusHandler(EmberUdpConnectionData *connection,
                             UdpStatus status)
{
  if (role == ROUTER && (connection->flags & UDP_USING_DTLS)) {
    if (status & EMBER_UDP_CONNECTED) {
      fprintf(stdout, "Connected with commissioner.\n");
    } else if (status & EMBER_UDP_OPEN_FAILED) {
      fprintf(stdout, "Open failed with commissioner.\n");
    } else if (status & EMBER_UDP_DISCONNECTED) {
      fprintf(stdout, "Disconnected from commissioner.\n");
    }
    fflush(stdout);
  }
}

static void udpMessageHandler(EmberUdpConnectionData *connection,
                              uint8_t *packet,
                              uint16_t length)
{
  CoapMessage coapMessage;
  if (emParseCoapMessage(packet,
                         length,
                         &coapMessage)) {
    coapMessage.remotePort = connection->remotePort;
    coapMessage.localPort = connection->localPort;
    coapMessage.options = EMBER_COAP_SECURED;
    coapMessage.message = emFillBuffer(packet, length);
    coapMessage.dtlsHandle = connection->connection;
    MEMCOPY(&coapMessage.localAddress,
            connection->localAddress,
            sizeof(EmberIpv6Address));
    MEMCOPY(&coapMessage.remoteAddress,
            connection->remoteAddress,
            sizeof(EmberIpv6Address));
    emProcessCoapMessage(&coapMessage,
                         (role == ROUTER
                          ? emBorderRouterMessageHandler
                          : (role == COMMISSIONER
                             ? emCommissionerMessageHandler
                             : NULL)));
  } else {
    emLogBytesLine(COAP, "CoAP fail", packet, length);
    loseVoid(COAP);
  }
}

static int localPort;
static uint16_t tlsFlags = TLS_USING_DTLS;
static int connectionFd;

bool sendHelloVerifyRequest(struct sockaddr_in *destination,
                            uint8_t *packet,
                            uint16_t packetLength)
{
  Buffer helloVerifyRequest = NULL_BUFFER;
  switch (emParseInitialDtlsPacket(packet, 
                                   packetLength,
                                   DTLS_REQUIRE_COOKIE_OPTION,
                                   true,
                                   &helloVerifyRequest)) {
  case DTLS_PROCESS:
    break;
  case DTLS_DROP:
    assert(false);
    break;
  case DTLS_SEND_VERIFY_REQUEST:
    nativeWrite(connectionFd,
                emGetBufferPointer(helloVerifyRequest),
                emGetBufferLength(helloVerifyRequest),
                (struct sockaddr *) destination,
                sizeof(struct sockaddr_in));
    return true;
  }
  return false;
}

//----------------------------------------------------------------
// We need to translate back and forth between the IPv4 address and port
// that we actually use to communicate with remote devices and pseudo-IPv6
// address that the stack uses.  We have two types of links: DTLS connections
// between the border router and the commissioner and 802.15.4 connections
// between the border router and joiners.
//
// DTLS connections get translated use the ULA prefix with an IID of
//  xxxx:xxxx:0000:0000
// where xxxx:xxxx is the IPv4 address.  We use the ULA prefix to avoid
// confusing any of the IP header code about what source address to use.
//
// 802.15.4 connections get translated into FE8 addresses with the IID 
// derived from the device's pseudo long MAC address, which it prepends to
// all the messages sent.  We keep a table mapping the IIDs to the actual
// IPv4 address and port.

typedef struct JoinerAddress_s {
  uint8_t ipv4Address[4];
  uint16_t port;
  uint8_t iid[8];
  struct JoinerAddress_s *next;
} JoinerAddress;

static JoinerAddress *joinerAddresses = NULL;

static JoinerAddress *findJoinerAddress(struct sockaddr_in *ipv4)
{
  JoinerAddress *address;
  for (address = joinerAddresses; address != NULL; address = address->next) {
    if (MEMCOMPARE(&ipv4->sin_addr, address->ipv4Address, 4) == 0
        && (ipv4->sin_port == address->port)) {
      return address;
    }
  }
  return NULL;
}

static JoinerAddress *findJoinerAddressFromIid(const uint8_t *iid)
{
  JoinerAddress *address;
  for (address = joinerAddresses; address != NULL; address = address->next) {
    if (MEMCOMPARE(iid, address->iid, 8) == 0) {
      return address;
    }
  }
  return NULL;
}

static void addJoinerIid(struct sockaddr_in *ipv4, const uint8_t *iid)
{
  JoinerAddress *address = findJoinerAddress(ipv4);
  if (address == NULL) {
    address = (JoinerAddress *) malloc(sizeof(JoinerAddress));
    MEMCOPY(address->ipv4Address, &ipv4->sin_addr, 4);
    address->port = ipv4->sin_port;
    address->next = joinerAddresses;
    joinerAddresses = address;
  }
  MEMCOPY(address->iid, iid, 8);
}

static void storeRemoteAddress(struct sockaddr_in *ipv4,
                               uint8_t *ipv6,
                               bool dtls)
{
  MEMSET(ipv6, 0, 16);
  if (dtls) {
    memcpy(ipv6 + 8, &ipv4->sin_addr, 4);
    emStoreDefaultGlobalPrefix(ipv6);
  } else {
    JoinerAddress *address = findJoinerAddress(ipv4);
    MEMCOPY(ipv6, emFe8Prefix.bytes, 8);
    MEMCOPY(ipv6 + 8, address->iid, 8);
  }
}

// Copied from stack/ip/zigbee/join.c to avoid lots of stubbing.
void emComputeEui64Hash(const EmberEui64 *input, EmberEui64 *output)
{
  // create a big-endian version of the eui
  uint8_t bigEndianEui64[8];
  emberReverseMemCopy(bigEndianEui64, input->bytes, 8);

  // create a hash of the big-endian eui
  uint8_t tempOutput[32];
  MEMSET(tempOutput, 0, sizeof(tempOutput));
  emSha256Hash(bigEndianEui64, 8, tempOutput);

  // copy the most-significant bytes of the output to the new eui
  emberReverseMemCopy(output->bytes, tempOutput, 8);

  // set the U/L bit to not unique
  output->bytes[7] |= 0x02;

  emLogBytes(JOIN, "hashed eui:", input->bytes, 8);
  emLogBytes(JOIN, " to:", output->bytes, 8);
  emLog(JOIN, "\n");
}

static void storeLongMacAddress(uint8_t *to)
{
  emComputeEui64Hash(&emLocalEui64, (EmberEui64 *) to);
}

//----------------------------------------------------------------

static SharedKey theKey;

static UdpConnectionData *getConnection(struct sockaddr_in* address,
                                        bool secured)
{
  uint8_t remoteAddress[16];
  storeRemoteAddress(address, remoteAddress, secured);
  UdpConnectionData *connection = emFindConnection(remoteAddress,
                                                   localPort,
                                                   ntohs(address->sin_port));
  if (connection != NULL) {
    return connection;
  } else if (secured) {
    connection = emAddUdpConnection(remoteAddress,
                                    localPort,
                                    ntohs(address->sin_port),
                                    (UDP_USING_DTLS
                                     | (tlsFlags & TLS_CRYPTO_SUITE_FLAGS)),
                                    sizeof(DtlsConnection),
                                    udpStatusHandler,
                                    udpMessageHandler);
    
    if (tlsFlags & TLS_HAVE_JPAKE) {
      emSetJpakeKey((DtlsConnection *) connection,
                    theKey.key,
                    theKey.keyLength);
    }
    return connection;
  } else {
    return emAddUdpConnection(remoteAddress,
                              localPort,
                              ntohs(address->sin_port),
                              0,               // no flags
                              sizeof(DtlsConnection),
                              udpStatusHandler,
                              udpMessageHandler);
  }
}

const EmberCommandEntry managementCallbackCommandTable[] = {
  { NULL }
};

static uint8_t extendedPanId[8] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

int main(int argc, char *argv[])
{
  char *routerName = "localhost";
  bool requireCookie = true;    // tinydtls breaks with no cookie
  bool stretchKey = false;
  char *networkName = "";
  int routerPort = 0;

  int errors = 0;
  char *me = *argv;		// Save program name.
  argv++; argc--;		// Skip program name.

  for (; 0 < argc; argc--, argv++) {
    char *arg = argv[0];
    if (strcmp(arg, "--debug") == 0) {
#if defined(DEBUG)
      debugLevel = 100;
#endif
      continue;
    } else if (strcmp(arg, "--border_router") == 0) {
      if (role != NO_ROLE) {
        errors++;
      }
      role = ROUTER;
      continue;
    } else if (strcmp(arg, "--commissioner") == 0) {
      if (role != NO_ROLE) {
        errors++;
      }
      role = COMMISSIONER;
      emNodeType = EMBER_COMMISSIONER;
      continue;
    } else if (strcmp(arg, "--log") == 0) {
      uint8_t logType;
      
      if (0 < argc) {
        logType = emLogTypeFromName(argv[1], strlen(argv[1]));
      
        if (logType == 0xFF) {
          fprintf(stderr, "Invalid argument: unable to parse %s %s\n",
                  argv[0],
                  argv[1]);
          errors++;
          break;
        }
        emLogConfig(logType, EM_LOG_PORT_STDERR, true);
        argc--;
        argv++;
        continue;
      }
    } else if (strcmp(arg, "--joiner") == 0) {
      if (role != NO_ROLE) {
        errors++;
      }
      role = JOINER;
      continue;
    } else if (strcmp(arg, "--border_router_name") == 0) {
      if (0 < argc) {
        routerName = argv[1];
        argc--;
        argv++;
        continue;
      }
    } else if (strcmp(arg, "--network_name") == 0) {
      if (0 < argc) {
        networkName = argv[1];
        argc--;
        argv++;
        continue;
      }
    } else if (strcmp(arg, "--extended_pan_id") == 0) {
      if (0 < argc) {
        errno = 0;      // to check for errors
        uint64_t xPanId = strtoull(argv[1], NULL, 16);
        if (errno == 0) {
          emberStoreHighLowInt32u(extendedPanId    , xPanId >> 32);
          emberStoreHighLowInt32u(extendedPanId + 4, xPanId);
        } else {
          errors++;
          fprintf(stderr, "Error: unable to parse extended PAN ID.\n");
        }
        argc--;
        argv++;
        continue;
      }
    } else if (strcmp(arg, "--eui") == 0) {
      if (0 < argc) {
        errno = 0;      // to check for errors
        uint64_t temp = strtoull(argv[1], NULL, 16);
        uint8_t buffer[8];
        if (errno == 0) {
          emberStoreHighLowInt32u(buffer,     temp >> 32);
          emberStoreHighLowInt32u(buffer + 4, temp);
          emberReverseMemCopy(emLocalEui64.bytes, buffer, 8);
        } else {
          errors++;
          fprintf(stderr, "Error: unable to parse EUI64.\n");
        }
        argc--;
        argv++;
        continue;
      }
    } else if (strcmp(arg, "--border_router_port") == 0) {
      if (0 < argc) {
        routerPort = atoi(argv[1]);
        if (0 <= routerPort) {
          argc--;
          argv++;
          continue;
        }
      }
    } else if (strcmp(arg, "--cookie") == 0) {
      requireCookie = true;
      continue;
    } else if (strcmp(arg, "--no-cookie") == 0) {
      requireCookie = false;
      continue;
    } else if (strcmp(arg, "--stretch") == 0) {
      stretchKey = true;
      continue;
    } else if (strcmp(arg, "--psk") == 0) {
      if (0 < argc) {
        theKey.key = argv[1];
        theKey.keyLength = strlen(theKey.key);
        theKey.identityLength = 0;
        emMySharedKey = &theKey;
        argc--;
        argv++;
        tlsFlags |= TLS_HAVE_PSK;
        continue;
      }
    } else if (strcmp(arg, "--jpake") == 0) {
      if (0 < argc) {
        uint8_t *key = argv[1];
        uint16_t length = strlen(key);
        if (0 < length) {
          // Used in three places:
          theKey.key = key;                     // commissioner -> border router
          theKey.keyLength = length;
          emSetCommissionKey(key, length);      // border router -> commissioner
          emberSetJoinKey(NULL, key, length);   // commissioner <-> joiner
          argc--;
          argv++;
          tlsFlags |= TLS_HAVE_JPAKE;
          continue;
        } else {
          fprintf(stderr, "Error: empty JPAKE key.\n");
        }
      }
    }
    errors++;
  }

  if (role == NO_ROLE) {
    errors++;
  }

  if (errors != 0) {
    fprintf(stderr, "Usage: %s (--border_router | --joiner) [OPTION]...\n"
                    "  --jpake <key>\n"
                    "  --psk <key>\n"
                    "  --stretch\n"
                    "  --extended_pan_id <16 character hex string>\n"
                    "  --network_name <name>\n"
                    "  --border_router_name <hostname>\n"
                    "  --border_router_port <port>\n",
            me);
    exit(1);
  }

  // emBuffersUseMalloc = true;                 // needed if using valgrind
  emInitializeBuffers();
  emInitializeEventQueue(&emStackEventQueue);
  emberCommandReaderInit();
  markTestBuffer = emAllocateBuffer(50);        // make sure this is first
  useBuffers = true;
  heapSize = emBufferBytesRemaining();
  emTcpInit();

  emMallocCount = 0;
  emMallocSize = 0;

  randomInit();
  emCoapInitialize();
  emSetJoinSecuritySuites(tlsFlags);

  uint8_t stretchedKey[16];

  if (stretchKey) {
    emDerivePskc(theKey.key,
                 theKey.keyLength,
                 extendedPanId,
                 networkName,
                 stretchedKey);
    theKey.key = stretchedKey;
    theKey.keyLength = 16;
    int i;
    fprintf(stderr, "stretched key:");
    for (i = 0; i < 16; i++)
      fprintf(stderr, " %02X", stretchedKey[i]);
    fprintf(stderr, "\n");
  }

  localPort = (role == ROUTER) ? routerPort : 0;
  if (nativeOpenUdp(&localPort,
                    &connectionFd)
      != EMBER_SUCCESS) {
    fprintf(stderr, "Failed to open local UDP port %d\n", localPort);
    goto exit;
  }

  struct sockaddr_in routerUdpAddress;

  if (role == ROUTER) {
    emAmLeader = true;
    emSetDefaultGlobalPrefix("abcdefgh");
    emNoteExternalCommissioner(-1, false);
  } else if (role == COMMISSIONER
             || role == JOINER) {
    emMyHostname = routerName;
    emMyHostnameLength = strlen(routerName);
    if (! nativeLookupHost(routerName,
                           routerPort,
                           &routerUdpAddress)) {
      fprintf(stderr, "Failed to lookup router %s:%d\n",
              routerName,
              routerPort);
      goto exit;
    }
    if (role == COMMISSIONER) {
      DtlsConnection *connection =
        (DtlsConnection *) getConnection(&routerUdpAddress, true);
      
      emParentConnectionHandle = connection->udpData.connection;
      emOpenDtlsConnection(connection);
      emStackConfiguration |= STACK_CONFIG_NETWORK_IS_UP;
    } else {    // joiner
      uint8_t message[9];
      message[0] = BEACON_REQUEST_BYTE;
      storeLongMacAddress(message + 1);
      nativeWrite(connectionFd,
                  message,
                  9,
                  (struct sockaddr*) &routerUdpAddress,
                  sizeof(routerUdpAddress));
    }
  }
  reclaimBuffers();

  // emDtlsStatusHandler(dtlsConnection);

  // fprintf(stdout, "> ");
  // fflush(stdout);

  while (true) {
    uint8_t readBuffer[1 << 15];
    struct pollfd fds[2] = { { 0,            POLLIN, 0 },  // for commands
                             { connectionFd, POLLIN, 0 } };
    resetBigIntCounts();
    if (0 < poll(fds, 2, emberMsToNextQueueEvent(&emStackEventQueue))
        && fds[1].revents) {
      struct sockaddr_in sender;
      socklen_t length = sizeof(sender);
      uint8_t remoteAddress[16];
      uint16_t got =
        nativeReadWithSender(connectionFd,
                             readBuffer,
                             sizeof(readBuffer),
                             (struct sockaddr*) &sender,
                             &length);
      bool dtls = (readBuffer[0] < MIN_KEY_BYTE
                   && readBuffer[1] == 0xFE
                   && readBuffer[2] == 0xFD);
      
      if (! dtls) {
        addJoinerIid(&sender, readBuffer + 1);
      }
      storeRemoteAddress(&sender, remoteAddress, dtls);
      
      if (dtls) {
        UdpConnectionData *connection =
          emFindConnection(remoteAddress,
                           localPort,
                           ntohs(sender.sin_port));
        if (connection != NULL) {
          // OK
        } else if (role != ROUTER) {
          fprintf(stderr, "received %d bytes from unknown source\n",
                  got);
          emLogBytesLine(COMMISSION, "unknown sender %d", remoteAddress,
                         16,
                         ntohs(sender.sin_port));
          got = 0;
        } else {
          connection = getConnection(&sender, true);
          emOpenDtlsServer((DtlsConnection *) connection);
        }
        
        if (got == 0) {
          // nothing
        } else if (role == ROUTER
                   && requireCookie
                   && (connection->flags & UDP_USING_DTLS)
                   && (((TlsState *)
                        emGetBufferPointer(((DtlsConnection *)
                                            connection)->tlsState))->connection.state
                       == TLS_SERVER_EXPECT_HELLO)
                   && sendHelloVerifyRequest(&sender, readBuffer, got)) {
          debug("asking for cookie");
          // do nothing
        } else {
          Buffer buffer = emFillBuffer(readBuffer, got);
          if (buffer == NULL_BUFFER) {
            fprintf(stderr,  "out of buffers");
            goto exit;
          }
          // dump("read: ", readBuffer, got);
          emBufferQueueAdd(&((DtlsConnection *)connection)->incomingTlsData,
                           buffer);
          emDtlsStatusHandler((DtlsConnection *)connection);
          // connection->ticksRemaining = 0;
          // emStartConnectionTimerMs(0);
        }
      } else if (readBuffer[0] == BEACON_REQUEST_BYTE) {
        uint8_t beacon[100] = {0};
        beacon[0] = BEACON_BYTE;
        storeLongMacAddress(beacon + 1);
        uint8_t length = emBeaconPayloadSize - BEACON_STEERING_DATA_INDEX;
        if (length != 0) {
          length = emBeaconPayloadBuffer[BEACON_STEERING_DATA_INDEX + 1];
          MEMCOPY(beacon + 9,
                  emBeaconPayloadBuffer + BEACON_STEERING_DATA_INDEX + 2,
                  length);
        }
        length += 9;    // for BEACON_BYTE and EUI64
        nativeWrite(connectionFd,
                    beacon,
                    length,
                    (struct sockaddr *) &sender,
                    sizeof(struct sockaddr_in));
      } else if (readBuffer[0] == BEACON_BYTE) {
        if (emSteeringDataMatch(readBuffer + 9, got - 9)) {
          storeRemoteAddress(&routerUdpAddress, remoteAddress, false);
          emStartJoinClient(remoteAddress, 0, theKey.key, theKey.keyLength);
        } else {
          fprintf(stderr, "No steering match.\n");
          goto exit;
        }
      } else {
        Ipv6Header ipHeader;
        ipHeader.sourcePort = HIGH_LOW_TO_INT(readBuffer[11], readBuffer[12]);
        ipHeader.destinationPort = HIGH_LOW_TO_INT(readBuffer[9], readBuffer[10]);
        uint8_t *finger = readBuffer + 13;
        uint8_t *key = NULL;
        got -= 13;
        if (readBuffer[0] == JOINER_ENTRUST_KEY_BYTE) {
          key = finger;
          finger += 16;
          got -= 16;
        }
        Buffer buffer = emFillBuffer(finger, got);
        ipHeader.transportPayload = emGetBufferPointer(buffer);
        ipHeader.transportPayloadLength = got;
        MEMCOPY(ipHeader.source, remoteAddress, 16);
        if (readBuffer[0] == JOINER_ENTRUST_KEY_BYTE) {
          // We don't currently keep the track of the sender's long ID
          // (which should replace the NULL) below) and assume that the
          // joiner correctly repeats it back to use.  We do check the
          // key on the joiner, because that came from the commissioner.
          if (role == JOINER
              && MEMCOMPARE(key, emGetCommissioningMacKey(NULL), 16) != 0) {
            fprintf(stderr, "KEK doesn't match");
          } else {
            emHandleIncomingCoapCommission(emAllocateBuffer(20), &ipHeader);
          }
        } else if (role == JOINER) {
          emIncomingJoinMessageHandler(NULL_BUFFER, &ipHeader, false); 
        } else {
          emForwardToCommissioner(buffer, &ipHeader);
        }
      }
    }

    if (emberProcessCommandInput(APP_SERIAL)) {
      // No prompt because no customer commands so far.
      // fprintf(stdout, "> ");
      // fflush(stdout);
    }

    while (emberMsToNextQueueEvent(&emStackEventQueue) == 0) {
      emberRunEventQueue(&emStackEventQueue);
    }
    reclaimBuffers();
  }

 exit:

  useBuffers = false;
  finishRunningTrace();
  debug("closing connection");
  close(connectionFd);
  return 0;
}

//----------------------------------------------------------------

static void petitionCommand(void)
{
  emBecomeExternalCommissioner("my name", 7);
}

static void keepAliveCommand(void)
{
  bool accept = (bool) emberUnsignedCommandArgument(0);
  emExternalCommissionerKeepAlive(accept);
}

static void joinModeCommand(void)
{
  uint8_t mode = emberUnsignedCommandArgument(0);
  uint8_t steeringDataLength = emberUnsignedCommandArgument(1);
  emberSetJoiningMode(mode, steeringDataLength);
}

static void steeringEuiCommand(void)
{
  EmberEui64 eui64;
  emberGetEui64Argument(0, &eui64);
  emApiAddSteeringEui64(&eui64);
}

static void sendSteeringCommand(void)
{
  emApiSendSteeringData();
}

extern void emSendGetData(const uint8_t *tlvs,
                          uint8_t count,
                          EmberCoapResponseHandler handler);


static void getResponseHandler(EmberCoapStatus status,
                               EmberCoapMessage *coap,
                               void *appData,
                               uint16_t appDataLength)
{
  if (status == EMBER_COAP_MESSAGE_RESPONSE) {
    const uint8_t *finger = coap->payload;
    const uint8_t *end = finger + coap->payloadLength;

    fprintf(stderr, "[MGMT_GET reply:");

    while (finger + 2 <= end) {
      const uint8_t *tlv = finger;
      uint8_t tlvId = *finger++;
      uint16_t length = *finger++;
      uint16_t i;
      fprintf(stderr, "\n  %d:", tlvId);
      for (i = 0; i < length; i++) {
        fprintf(stderr, " %02X", tlv[2 + i]);
      }
      finger += length;
    }
    fprintf(stderr, "]\n");
  } else {
    fprintf(stderr, "MGMT_GET reply: failure %d", status);
  }
}

static void getDataCommand(void)
{
  uint8_t count = emberCommandArgumentCount();
  uint8_t tlvs[8];
  uint8_t i;
  if (8 < count) {
    count = 8;
  }
  for (i = 0; i < count; i++) {
    tlvs[i] = emberUnsignedCommandArgument(i);
  }
  emSendGetData(tlvs, count, &getResponseHandler);
}

const EmberCommandEntry emberCommandTable[] = {
  emberCommand("petition",      petitionCommand,     "",    NULL),
  emberCommand("keep_alive",    keepAliveCommand,    "u",   NULL),
  emberCommand("join_mode",     joinModeCommand,     "uu",  NULL),
  emberCommand("steering_eui",  steeringEuiCommand,  "b",   NULL),
  emberCommand("send_steering", sendSteeringCommand, "",    NULL),
  emberCommand("get_data",      getDataCommand,      "u*",  NULL),
  { NULL }
};

//----------------------------------------------------------------

bool emReallySubmitIpHeader(PacketHeader header,
                            Ipv6Header *ipHeader,
                            bool allowLoopback,
                            uint8_t retries,
                            uint16_t delayMs)
{
  uint8_t toSendBuffer[1<<16];
  uint8_t *toSend = toSendBuffer;
  uint16_t length = ipHeader->transportPayloadLength;
  uint16_t toDo = length;
  Buffer finger = header;
  uint8_t *from = ipHeader->transportPayload;
  bool dtls = emIsDefaultGlobalPrefix(ipHeader->destination);

  // dump("destination: ", ipHeader->destination, 16);

  // For non-DTLS ports we need to send the actual ports in the body.
  if (! dtls) {
    *toSend++ = (emHeaderTag(header) == HEADER_TAG_COMMISSIONING_KEY
                 ? JOINER_ENTRUST_KEY_BYTE
                 : MAC_KEY_BYTE);
    storeLongMacAddress(toSend);
    toSend += 8;
    *toSend++ = HIGH_BYTE(ipHeader->destinationPort);
    *toSend++ = LOW_BYTE(ipHeader->destinationPort);
    *toSend++ = HIGH_BYTE(ipHeader->sourcePort);
    *toSend++ = LOW_BYTE(ipHeader->sourcePort);
    length += 13;
    if (emHeaderTag(header) == HEADER_TAG_COMMISSIONING_KEY) {
      MEMCOPY(toSend, emGetBufferPointer(emGetPayloadLink(header)), 16);
      toSend += 16;
      length += 16;
    }
  }

  while (0 < toDo) {
    uint16_t have = ((emGetBufferPointer(finger) + emGetBufferLength(finger))
                     - from);
    uint16_t copy = (have < toDo
                     ? have
                     : toDo);
    MEMCOPY(toSend, from, copy);
    toSend += copy;
    toDo -= copy;
    finger = emGetPayloadLink(finger);
    from = emGetBufferPointer(finger);
  }

  struct sockaddr_in destination;
  destination.sin_family = AF_INET;
  if (dtls) {
    memcpy(&destination.sin_addr,
           ipHeader->destination + 8,
           sizeof(destination.sin_addr));
    destination.sin_port = htons(ipHeader->destinationPort);
  } else {
    JoinerAddress *address =
      findJoinerAddressFromIid(ipHeader->destination + 8);
    memcpy(&destination.sin_addr, address->ipv4Address, 4);
    destination.sin_port = address->port;
  }
  nativeWrite(connectionFd,
              toSendBuffer,
              length,
              (struct sockaddr*) &destination,
              sizeof(destination));
  return true;
}

// Randomness.

static FILE *devUrandom;

static void randomInit(void)
{
  devUrandom = fopen("/dev/urandom", "r");
  assert(devUrandom != NULL);
}

static uint8_t nextRandom = 0;

void randomize(uint8_t *blob, uint16_t length)
{
  uint16_t i;

  for (i = 0; i < length; i++) {
    int result = (usingTraceFile()
                  ? nextRandom++
                  : fgetc(devUrandom));
    assert(result != EOF);
    blob[i] = result & 0xFF;
  }
}

bool emRadioGetRandomNumbers(uint16_t *rn, uint8_t theCount)
{
  randomize((uint8_t *) rn, theCount * 2);
  return true;
}

uint16_t halCommonGetRandomTraced(char *file, int line)
{
  uint16_t x;
  randomize((uint8_t *) &x, 2);
  return x;
}

// Time.

uint32_t halCommonGetInt32uMillisecondTick(void)
{
  struct timeval tv;
  assert(gettimeofday(&tv, NULL) == 0);
  return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}

uint16_t halCommonGetInt16uMillisecondTick(void)
{
  return halCommonGetInt32uMillisecondTick();
}

EmberEui64 emLocalEui64 = { 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7 };

const uint8_t emDefaultSourceLongId[8] = {
  0x42, 0x8A, 0x2F, 0x98, 0x71, 0x37, 0x44, 0x91
};

static uint8_t defaultKey[] = {
  0xbf, 0xbe, 0xbd, 0xbc, 0xbb, 0xba, 0xb9, 0xb8,
  0xb7, 0xb6, 0xb5, 0xb4, 0xb3, 0xb2, 0xb1, 0xb0
};

bool emGetNetworkKeySequence(uint32_t *sequenceLoc)
{
  if (sequenceLoc != NULL) {
    *sequenceLoc = 12;
  }
  return true;
}

uint8_t *emGetNetworkMasterKey(uint8_t *storage)
{
  return defaultKey;
}

static uint8_t commissioningMacKey[16];
Buffer emCommissioningMessage;             // non-static for GC marking
static bool commissioningMacKeyIsValid;

void emSetCommissioningMacKey(uint8_t *key)
{
  MEMCOPY(commissioningMacKey, key, 16);
  commissioningMacKeyIsValid = false;
  emCommissioningMessage = NULL_BUFFER;
}

extern uint8_t *emGetOutgoingCommissioningMacKey(const uint8_t *senderEui64);

uint8_t *emGetCommissioningMacKey(const uint8_t *senderEui64)
{
  return commissioningMacKey;
}

void emCommissioningHandshakeComplete(void)
{
  assert(emStartAppCommissioning(true));
}   

void emCancelCommissioningKey(void)
{
  MEMSET(commissioningMacKey, 0, 16);
  emCommissioningMessage = NULL_BUFFER;
  commissioningMacKeyIsValid = false;
}  

void emJoinCommissionCompleteHandler(bool success)
{
  emCancelCommissioningKey();   // only one commissioning message is allowed
  fprintf(stdout, "Join complete.\n");
  fflush(stdout);
}

bool joinFinished = false;

void emJoinSecurityFailed(void)
{
  joinFinished = true;
}

bool emGetThreadNativeCommission(void) { return true; }
  
void emAppCommissionCompleteHandler(bool success)
{
  fprintf(stdout, "App commissioning complete.\n");
  fflush(stdout);
}

void emSetExtendedPanId(const uint8_t* extendedPanId) {}

void emberBecomeCommissionerReturn(EmberStatus status) {}

void emberCommissionerStatusHandler(uint16_t flags,
                                    const uint8_t *commissionerName,
                                    uint8_t commissionerNameLength)
{
  fprintf(stdout, "Commissioner status:");
  if (flags & EMBER_HAVE_COMMISSIONER) {
    fprintf(stdout, " have commissioner, joining %sabled.\n",
            ((flags & EMBER_JOINING_ENABLED)
             ? "en"
             : "dis"));
  } else {
    fprintf(stdout, " no commissioner.\n");
  }
  fflush(stdout);
}

void emberSendSteeringDataReturn(EmberStatus status) {}

void emberCoapMessageHandler(EmberCoapMessage *message)
{
  assert(false);
}

EmberNodeType emNodeType = EMBER_ROUTER;

#define THE_CHANNEL 0x77
#define THE_PAN_ID  0x89AB

void emApiGetNetworkParameters(EmberNetworkParameters *networkParams)
{
  MEMCOPY(networkParams->extendedPanId, extendedPanId, 8);
  networkParams->panId   = THE_PAN_ID;
  networkParams->channel = THE_CHANNEL;
}

//----------------------------------------------------------------

void vSimPrint(char *format, va_list argPointer)
{
  fprintf(stderr, "[");
  vfprintf(stderr, format, argPointer);
  putc(']', stderr);
  putc('\n', stderr);
}

void simPrint(char* format, ...)
{
  va_list argPointer;
  va_start(argPointer, format);
  vSimPrint(format, argPointer);
  va_end(argPointer);
}

uint32_t emExpandSequenceNumber(uint8_t sequenceNumber)
{
  return sequenceNumber;
}

void printBytes(const uint8_t *bytes, uint16_t length)
{
  uint16_t i;

  for (i = 0; i < length; i++) {
    printf("%X ", bytes[i]);
  }

  printf("\n");
}

#ifdef EMBER_TEST
  #define USE_STUB_makeMessage
  #define USE_STUB_emLoseHelper
#endif

#ifdef EMBER_SCRIPTED_TEST
  #define USE_STUB_setRandomDataType
#endif

#ifdef EMBER_TEST
void emberTcpReadHandler(uint8_t fd, uint8_t *incoming, uint16_t length) {}
void emberTcpStatusHandler(uint8_t fd, uint8_t status) {}
#endif

#ifdef EMBER_APPLICATION_HAS_TCP_READ_HANDLER
void emberTcpReadHandler(uint8_t fd, uint8_t *incoming, uint16_t length) {}
#endif
#ifdef EMBER_APPLICATION_HAS_TCP_STATUS_HANDLER
void emberTcpStatusHandler(uint8_t fd, uint8_t status) {}
#endif

#define USE_STUB_emberSetSteeringData
#define USE_STUB_emSetBeaconSteeringData
#define USE_STUB_emAddChannelMaskTlv
#define USE_STUB_emberSetCommissionerKeyReturn
#define USE_STUB_emberGetLocalIpAddress
#define USE_STUB_emCoapHostIncomingMessageHandler
#define USE_STUB_emberStartScan
#define USE_STUB_emHandleCommissionDatasetPost
#define USE_STUB_emNoteGlobalPrefix
#define USE_STUB_emMacPayloadPointer
#define USE_STUB_emMacPayloadIndex
#define USE_STUB_emMacDestinationMode
#define USE_STUB_emMacDestinationPointer
#define USE_STUB_emMacShortDestination
#define USE_STUB_halInternalGetTokenData
#define USE_STUB_halInternalSetTokenData
#define USE_STUB_emGetMacKey
#define USE_STUB_emberGetNodeId
#define USE_STUB_emStackConfiguration
#define USE_STUB_emSendDhcpSolicit
#define USE_STUB_emberDhcpServerChangeHandler
#define USE_STUB_emberAddressConfigurationChangeHandler
#define USE_STUB_emSendMleChildUpdate
#define USE_STUB_emberResignGlobalAddressReturn
#define USE_STUB_emSendDhcpAddressRelease
#define USE_STUB_emUncompressContextPrefix
#define USE_STUB_emBeaconPayloadBuffer
#define USE_STUB_emStackEventQueue
#define USE_STUB_emFetchIpv6Header
#define USE_STUB_emFetchUdpHeader
#define USE_STUB_emSetPermitJoin
#define USE_STUB_emBeaconPayloadSize
#define USE_STUB_emGetThreadJoin
#define USE_STUB_emNetworkDataPostToken
#define USE_STUB_emHandleAddressManagementPost
#define USE_STUB_emSetNetworkMasterKey
#define USE_STUB_emAddressCache
#define USE_STUB_emDhcpIncomingMessageHandler
#define USE_STUB_AddressData
#define USE_STUB_emStoreMulticastSequence
#define USE_STUB_emParentId
#define USE_STUB_emParentLongId
#define USE_STUB_emLookupNextHop
#define USE_STUB_emLookupLongId
#define USE_STUB_emCompressContextPrefix
#define USE_STUB_emMulticastContext
#define USE_STUB_emLinkIsEstablished
#define USE_STUB_emSendNetworkDataRequest
#define USE_STUB_emVerifyLocalServerData
#define USE_STUB_emSetAllSleepyChildFlags
#define USE_STUB_emSendNetworkData
#define USE_STUB_emFillIslandId
#define USE_STUB_emLocalRipId
#define USE_STUB_emSetIslandId
#define USE_STUB_emInitializeLeaderServerData
#define USE_STUB_emAttachState
#define USE_STUB_emRipTable
#define USE_STUB_emRouterIndex
#define USE_STUB_emGetRoute
#define USE_STUB_emGetRouteCost
#define USE_STUB_emFindAll6lowpanContexts
#define USE_STUB_ChildUpdateState
#define USE_STUB_emMacExtendedId
#define USE_STUB_emberGetNetworkDataTlvReturn
#define USE_STUB_emGetCompleteNetworkData
#define USE_STUB_emberLeaderDataHandler
#define USE_STUB_emSendDhcpSolicit
#define USE_STUB_emberSlaacServerChangeHandler
#define USE_STUB_emHandleThreadDiagnosticMessage
#include "stack/ip/stubs.c"
