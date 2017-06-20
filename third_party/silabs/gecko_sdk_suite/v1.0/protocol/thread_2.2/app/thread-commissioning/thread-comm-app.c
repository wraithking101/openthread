// File: thread-comm-app.c
//
// Description: Thread commissioning plugin for the host
// (needs ip-driver-app)
//
// Copyright 2015 by Silicon Laboratories. All rights reserved.             *80*
//----------------------------------------------------------------

#include <stdlib.h>             // malloc() and free()
#include <unistd.h>             // close()
#include <sys/fcntl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <poll.h>
#include <errno.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "core/ember-stack.h"
#include "platform/micro/aes.h"
#include "framework/buffer-malloc.h"
#include "framework/event-queue.h"
#include "framework/ip-packet-header.h"
#include "ip/6lowpan-header.h"
#include "ip/udp.h"
#include "ip/tcp.h"
#include "ip/zigbee/join.h"
#include "ip/network-data.h"
#include "ip/commission.h"
#include "ip/commission-dataset.h"
#include "ip/ip-address.h"
#include "ip/ip-header.h"
#include "ip/association.h"
#include "app/coap/coap.h"
#include "app/util/serial/command-interpreter2.h"
#include "app/tmsp/tmsp-enum.h"

#include "stack/include/meshcop.h"
#include "stack/ip/host/host-address-table.h"
#include "stack/ip/host/host-listener-table.h"
#include "stack/ip/host/management-client.h"
#include "stack/ip/host/unix-interface.h"
#include "stack/ip/tls/debug.h"
#include "stack/ip/tls/tls.h"
#include "stack/ip/tls/dtls.h"
#include "stack/ip/tls/dtls-join.h"
#include "stack/ip/tls/small-aes/aes.h"
#include "stack/ip/tls/rsa.h"
#include "stack/ip/tls/tls-handshake-crypto.h"
#include "stack/ip/tls/tls-record.h"
#include "stack/ip/tls/tls-handshake.h"
#include "stack/ip/tls/tls-public-key.h"
#include "stack/ip/tls/tls-session-state.h"
#include "stack/ip/tls/certificate.h"
#include "stack/ip/tls/sha256.h"
#include "native-test-util.h"
#include "stack/ip/tls/credentials/test-certificate.h"
#include "stack/ip/tls/jpake-ecc.h"
#include "app/coap/coap-stack.h"
#include "app/ip-ncp/binary-management.h"
#include "app/ip-ncp/uart-link-protocol.h"

extern void emBorderRouterMessageHandler(CoapMessage *coap);
extern void connectionEventHandler(Event *event);
extern bool emBuffersUseMalloc;
extern uint32_t emMallocCount;
extern uint32_t emMallocSize;

static FILE *devUrandom;
static void randomInit(void);

static bool useBuffers = false;
bool emMacDropIncomingPackets = false;

static int managementFd = -1;

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

static Buffer markTestBuffer;   // to make sure we trace TLS structs properly
extern Buffer emExternalRouteTable;
extern Buffer emCommissioner;
extern Buffer emComScanBuffer;

static void markConnectionBuffer(void)
{
  emMarkBuffer(&markTestBuffer);
  emMarkUdpBuffers();
  emMarkBuffer(&emGlobalAddressTable);
  emMarkBuffer(&emExternalRouteTable);
  emJpakeEccMarkData();
  emberMarkEventQueue(&emStackEventQueue);
  emCoapMarkBuffers();
  emMarkBuffer(&emNetworkData);
  emMarkBuffer(&emCommissioner);
  emMarkBuffer(&emComScanBuffer);
  emMarkDtlsJoinBuffers();
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

void emNoteExternalCommissionerHost(uint8_t commissionerId,
                                bool available);

static void udpStatusHandler(EmberUdpConnectionData *connection,
                             UdpStatus status)
{
  if (connection->flags & UDP_USING_DTLS) {
    if (status & EMBER_UDP_CONNECTED) {
      fprintf(stdout, "Connected with commissioner.\n");
      emNoteExternalCommissionerHost(connection->connection, true);
    } else if (status & EMBER_UDP_OPEN_FAILED) {
      fprintf(stdout, "Open failed with commissioner.\n");
    } else if (status & EMBER_UDP_DISCONNECTED) {
      fprintf(stdout, "Disconnected from commissioner.\n");
    }
    fflush(stdout);
  }
}

static bool dtlsTransmitHandler(const uint8_t *payload,
                                uint16_t payloadLength,
                                uint16_t buffer,
                                const EmberIpv6Address *localAddress,
                                uint16_t localPort,
                                const EmberIpv6Address *remoteAddress,
                                uint16_t remotePort,
                                void *transmitHandlerData)
{
  EmberStatus status;
  uint8_t handle = (uint8_t) ((unsigned long) transmitHandlerData);
  UdpConnectionData *data = emFindConnectionFromHandle(handle);
  if (data == NULL) {
    return false;
  }
  status = emTlsSendBufferedApplicationData((TcpConnection *) data,
                                            payload,
                                            payloadLength,
                                            (Buffer) buffer,
                                            emTotalPayloadLength((Buffer) buffer));
  return (status == EMBER_SUCCESS);
}

static void udpMessageHandler(EmberUdpConnectionData *connection,
                              uint8_t *packet,
                              uint16_t length)
{
  // EMIPSTACK-1801: packet received from commissioner, we must proxy
  // it down to the commissioning code on the NCP.
  uint8_t temp[EMBER_IPV6_MTU + UART_LINK_HEADER_SIZE + 1];
  temp[0] = '[';
  temp[1] = UART_LINK_TYPE_COMMISSIONER_DATA;
  emberStoreHighLowInt16u(temp + 2, length + 1);
  temp[4] = connection->connection;  // prepend the handle
  MEMCOPY(temp + 5, packet, length);
  if (write(managementFd, temp, length + 5) != length + 5) {
    emLogLine(COMMISSION, "Problem with com data write on fd %d", managementFd);
  }
}

static uint16_t tlsFlags = TLS_USING_DTLS;

static int commissionerPort = DTLS_COMMISSION_PORT;
static int joinerPort = DTLS_JOIN_PORT;
static int commissionerFd = -1;
static int joinerFd = -1;

bool sendHelloVerifyRequest(struct sockaddr_in *destination,
                            socklen_t destLength,
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
    emLogLine(SECURITY, "Dropped hello verify request...");
    return true;
  case DTLS_SEND_VERIFY_REQUEST:
    nativeWrite(commissionerFd,
                emGetBufferPointer(helloVerifyRequest),
                emGetBufferLength(helloVerifyRequest),
                (struct sockaddr *) destination,
                destLength);
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
// (border router in this case is our thread commissioning app on the host)
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

static void storeRemoteAddress(struct sockaddr_in *ipv4, uint8_t *ipv6)
{
  MEMSET(ipv6, 0, 16);
  memcpy(ipv6 + 8, &ipv4->sin_addr, 4);
  emStoreDefaultGlobalPrefix(ipv6);
  // MEMCOPY(ipv6, emFe8Prefix.contents, 8);
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

static uint8_t localIpAddress[16] = {0};

bool emberGetLocalIpAddress(uint8_t index, EmberIpv6Address *address)
{
  if (index == 0 && ! emIsMemoryZero(localIpAddress, 16)) {
    MEMCOPY(address->bytes, localIpAddress, 16);
    return true;
  }
  return false;
}

//----------------------------------------------------------------
// Thread commissioning management

typedef struct {
  EmberNetworkParameters networkParams;
  CommissioningDataset activeDataset;
  uint32_t networkSequenceNumber;
  uint8_t commissionKey[JPAKE_MAX_KEY_LENGTH];
  uint8_t commissionKeyLength;
  uint8_t joinKey[JPAKE_MAX_KEY_LENGTH];
  uint8_t joinKeyLength;
  EmberNetworkStatus networkStatus;
  EmberEui64 localEui64;
} CommAppParameters;

static CommAppParameters paramsCache = { 0 };

#define COMMAND_LINE_MAX_LENGTH 255
bool avahiServiceStarted = false;

static void killDnsService(void)
{
  emStackConfiguration &= ~STACK_CONFIG_NETWORK_IS_UP;
  if (avahiServiceStarted) {
    avahiServiceStarted = false;
    pid_t killPid, pid;
    int status;
    char avahiKill[COMMAND_LINE_MAX_LENGTH] = "";
    switch ((killPid = fork()))
    {
      case -1:
        perror("fork");
        break;
      case 0:
        sprintf((char *) avahiKill, "sudo /etc/init.d/avahi-daemon restart &");
        fprintf(stderr, "command: %s\n", avahiKill);
        system(avahiKill);
        exit(1);
      default:
        do {
          pid = wait(&status);
        } while (pid != killPid);
        break;
    }
  }
}

static void commAppParametersCallback(void)
{
  // Set the network parameters.
  emberGetStringArgument(0, paramsCache.networkParams.extendedPanId, 8, false);
  uint8_t networkIdLength;
  const uint8_t *networkId = emberStringCommandArgument(1, &networkIdLength);
  if (EMBER_NETWORK_ID_SIZE < networkIdLength) {
    networkIdLength = EMBER_NETWORK_ID_SIZE;
  }
  MEMCOPY(paramsCache.networkParams.networkId, networkId, networkIdLength);

  emberGetStringArgument(2, paramsCache.networkParams.ulaPrefix.bytes, sizeof(paramsCache.networkParams.ulaPrefix.bytes), false);
  emSetDefaultGlobalPrefix(paramsCache.networkParams.ulaPrefix.bytes);

  paramsCache.networkParams.panId = emberUnsignedCommandArgument(3);
  paramsCache.networkParams.channel = emberUnsignedCommandArgument(4);
  emberGetStringArgument(5, paramsCache.localEui64.bytes, 8, false);
  MEMCOPY(emLocalEui64.bytes, paramsCache.localEui64.bytes, 8);
  emberGetStringArgument(6, emMacExtendedId, 8, false);
  paramsCache.networkStatus = emberUnsignedCommandArgument(7);

  // Set the active dataset.
  emberGetStringArgument(0, paramsCache.activeDataset.extendedPanId, 8, false);
  MEMCOPY(paramsCache.activeDataset.networkId, networkId, networkIdLength);
  emberGetStringArgument(2, paramsCache.activeDataset.ulaPrefix, 8, false);
  paramsCache.activeDataset.panId = emberUnsignedCommandArgument(3);

  // Setup the DNS service with the correct TXT record
  if (paramsCache.networkStatus != EMBER_JOINED_NETWORK_ATTACHED
      && paramsCache.networkStatus != EMBER_JOINED_NETWORK_NO_PARENT) {
    killDnsService();
  } else {
    if (! avahiServiceStarted) {
      killDnsService();
      emSetStackConfig(STACK_CONFIG_NETWORK_IS_UP); // Mark the network as up.
      char recordVersionString[8] = "1"; 
      char threadVersionString[8] = {0}; 
      MeshCopStateBitmap meshcop = {0};
      pid_t avahiPid, pid;
      int status;
      switch ((avahiPid = fork()))
      {
        case -1:
          // Fork() has failed
          perror ("fork");
          break;
        case 0:
          
          sprintf(threadVersionString, "%s", MESHCOP_THREAD_VERSION_STRING);

          // Assume the border-router reference design provides 'user' credentials 
          meshcop.connectionMode = SB_CONNECTION_MODE_USER_CREDENTIAL;

          // Thread Interface is active if the stack has attached a network
          if (paramsCache.networkStatus == EMBER_JOINED_NETWORK_ATTACHED) {
            // If the Thread Interface is active, and the node is a Router
            // then there is an active thread partition
            meshcop.threadIfStatus = 
                ((paramsCache.networkParams.nodeType == EMBER_ROUTER) ? 
                SB_THREAD_IF_STATUS_ACTIVE_WITH_THREAD_PARTITION : 
                SB_THREAD_IF_STATUS_ACTIVE_WITHOUT_THREAD_PARTITION);
          } else { 
            // If emNetworkIsUp is false, the Thread Interface is not active
            meshcop.threadIfStatus = SB_THREAD_IF_STATUS_INACTIVE;
          }

          // assume the border-router reference design provides "INFREQUENT" availability
          meshcop.availability = SB_AVAILABILITY_INFREQUENT;

          // This is processed by the child
          if (! avahiServiceStarted) {

            if (! emIsMemoryZero(paramsCache.networkParams.networkId, 16)
                && ! emIsMemoryZero(paramsCache.networkParams.extendedPanId, 8)) {
              uint8_t *xp = paramsCache.networkParams.extendedPanId;
              uint8_t *sb = meshcop.bytes;

              //NOTE: this does not currently comply with the Thread 1.1 specification
              //      because of missing the 'nn' options in the TXT record field
              //      Additionally, some payloads, like SB and XP should be in binary format,
              //      however, the current commissioning app does comprehend hex data.
              //      Rather than making a call to 'avahi-publish' in the near future the C-API
              //      will need to be used so that binary payloads (which aren't supported by avahi-daemon)
              //      can be included in the TXT mDNS field, as the 1.1 Thread spec requires.
              char avahiPublish[COMMAND_LINE_MAX_LENGTH] = {0};
              sprintf(avahiPublish,
                      "avahi-publish -s ThreadCommissioner " 
                      "_meshcop._udp %d "
                      "nn=%.*s "
                      "rv=%s "
                      "tv=%s "
                      "sb=%02X%02X%02X%02X "
                      "xp=%02X%02X%02X%02X%02X%02X%02X%02X &",
                      DTLS_COMMISSION_PORT,
                      EMBER_NETWORK_ID_SIZE,
                      paramsCache.networkParams.networkId,
                      recordVersionString,
                      threadVersionString,
                      sb[0], sb[1], sb[2], sb[3],
                      xp[0], xp[1], xp[2], xp[3], xp[4], xp[5], xp[6], xp[7]);
              avahiServiceStarted = true;
              fprintf(stdout, "command: %s\n", avahiPublish);
              system(avahiPublish);
              exit(1);
            }
          }
          break;
        default:
          avahiServiceStarted = true;
          do {
            pid = wait(&status);
          } while (pid != avahiPid);
          break;
      }
    }
  }
}

static void commAppDatasetCallback(void)
{
  emberGetStringArgument(0, paramsCache.activeDataset.timestamp, 8, false);
  emberGetStringArgument(1, paramsCache.activeDataset.pendingTimestamp, 8, false);
  paramsCache.activeDataset.delayTimer = emberUnsignedCommandArgument(2);
  emberGetStringArgument(3, paramsCache.activeDataset.securityPolicy, 3, false);
  emberGetStringArgument(4, paramsCache.activeDataset.channelMask, 6, false);
  emberGetStringArgument(5, paramsCache.activeDataset.channel, 3, false);
}

static void commAppSecurityCallback(void)
{
  emberGetStringArgument(0, paramsCache.networkParams.masterKey.contents, 16, false);
  emberGetStringArgument(0, paramsCache.activeDataset.masterKey, 16, false);
  paramsCache.networkSequenceNumber = emberUnsignedCommandArgument(1);
}

static void commAppAddressCallback(void)
{
  uint8_t address[16];
  emberGetStringArgument(0, address, 16, false);
  if (emIsDefaultGlobalPrefix(address)) {
    MEMCOPY(localIpAddress, address, 16);
    uint8_t meshLocalIdentifier[8];
    emberReverseMemCopy(meshLocalIdentifier,
                        address + 8,
                        8);
    meshLocalIdentifier[7] ^= 0x02;
    emSetMeshLocalIdentifierFromLongId(meshLocalIdentifier);

    if (commissionerFd == -1) {
      if (nativeOpenUdp(&commissionerPort,
                        &commissionerFd)
          != EMBER_SUCCESS) {
        fprintf(stderr, "Failed to open commissioner port %d\n", commissionerPort);
      }
    } else {
      fprintf(stderr, "Commissioner port %d fd %d already open\n", commissionerPort, commissionerFd);
    }
  }
}

static void becomeCommissionerCommand(void)
{
  uint8_t *deviceName;
  uint8_t deviceNameLength;
  deviceName = emberStringCommandArgument(0, &deviceNameLength);
  emberBecomeCommissioner(deviceName,
                          deviceNameLength);
}

static void stopCommissioningCommand(void)
{
  emberStopCommissioning();
}

static void getCommissionerCommand(void)
{
  emberGetCommissioner();
}

static void setCommissionerKeyCommand(void)
{
  uint8_t *key;
  uint8_t length;
  key = emberStringCommandArgument(0, &length);
  if (JPAKE_MAX_KEY_LENGTH < length) {
    length = JPAKE_MAX_KEY_LENGTH;
  }

  uint8_t stretchedKey[16];
  emDerivePskc(key,
               length,
               paramsCache.networkParams.extendedPanId,
               paramsCache.networkParams.networkId,
               stretchedKey);

  MEMCOPY(paramsCache.commissionKey, stretchedKey, 16);
  MEMCOPY(paramsCache.activeDataset.pskc, stretchedKey, 16);
  paramsCache.commissionKeyLength = 16;
  emberSetCommissionerKey(key, length);
}

static void setJoiningModeCommand(void)
{
  EmberJoiningMode mode;
  uint8_t length;
  mode = (EmberJoiningMode) emberUnsignedCommandArgument(0);
  length = (uint8_t) emberUnsignedCommandArgument(1);
  emberSetJoiningMode(mode,
                      length);
}

static void addSteeringEui64Command(void)
{
  EmberEui64 eui64;
  MEMSET(&eui64, 0, sizeof(EmberEui64));
  emberGetStringArgument(0, eui64.bytes, EUI64_SIZE, false);
  emberAddSteeringEui64(&eui64);
}

static void sendSteeringDataCommand(void)
{
  emberSendSteeringData();
}

static void setJoinKeyCommand(void)
{
  EmberEui64 eui64;
  MEMSET(&eui64, 0, sizeof(EmberEui64));
  emberGetStringArgument(0, eui64.bytes, EUI64_SIZE, false);
  uint8_t *key;
  uint8_t length;
  key = emberStringCommandArgument(1, &length);
  if (JPAKE_MAX_KEY_LENGTH < length) {
    length = JPAKE_MAX_KEY_LENGTH;
  }
  MEMCOPY(paramsCache.joinKey, key, length);
  paramsCache.joinKeyLength = length;
  emberSetJoinKey(&eui64,
                  paramsCache.joinKey,
                  paramsCache.joinKeyLength);
}

const EmberCommandEntry managementCallbackCommandTable[] = {
  emberBinaryCommandEntryAction(CB_SET_COMM_APP_PARAMETERS_COMMAND_IDENTIFIER,
                                commAppParametersCallback,
                                "bbbvubbu",
                                NULL),
  emberBinaryCommandEntryAction(CB_SET_COMM_APP_DATASET_COMMAND_IDENTIFIER,
                                commAppDatasetCallback,
                                "bbwbbb",
                                NULL),
  emberBinaryCommandEntryAction(CB_SET_COMM_APP_SECURITY_COMMAND_IDENTIFIER,
                                commAppSecurityCallback,
                                "bu",
                                NULL),
  emberBinaryCommandEntryAction(CB_SET_COMM_APP_ADDRESS_COMMAND_IDENTIFIER,
                                commAppAddressCallback,
                                "b",
                                NULL),
  emberBinaryCommandEntryAction(EMBER_BECOME_COMMISSIONER_COMMAND_IDENTIFIER,
                                becomeCommissionerCommand,
                                "b",
                                NULL),
  emberBinaryCommandEntryAction(EMBER_STOP_COMMISSIONING_COMMAND_IDENTIFIER,
                                stopCommissioningCommand,
                                "",
                                NULL),
  emberBinaryCommandEntryAction(EMBER_GET_COMMISSIONER_COMMAND_IDENTIFIER,
                                getCommissionerCommand,
                                "",
                                NULL),
  emberBinaryCommandEntryAction(EMBER_SET_COMMISSIONER_KEY_COMMAND_IDENTIFIER,
                                setCommissionerKeyCommand,
                                "b",
                                NULL),
  emberBinaryCommandEntryAction(EMBER_SET_JOINING_MODE_COMMAND_IDENTIFIER,
                                setJoiningModeCommand,
                                "uu",
                                NULL),
  emberBinaryCommandEntryAction(EMBER_ADD_STEERING_EUI64_COMMAND_IDENTIFIER,
                                addSteeringEui64Command,
                                "b",
                                NULL),
  emberBinaryCommandEntryAction(EMBER_SEND_STEERING_DATA_COMMAND_IDENTIFIER,
                                sendSteeringDataCommand,
                                "",
                                NULL),
  emberBinaryCommandEntryAction(EMBER_SET_JOIN_KEY_COMMAND_IDENTIFIER,
                                setJoinKeyCommand,
                                "bb",
                                NULL),
  { NULL }
};

//----------------------------------------------------------------

static UdpConnectionData *getConnection(struct sockaddr_in* address)
{
  uint8_t remoteAddress[16];
  storeRemoteAddress(address, remoteAddress);
  UdpConnectionData *connection = emFindConnection(remoteAddress,
                                                   commissionerPort,
                                                   ntohs(address->sin_port));
  if (connection != NULL) {
    return connection;
  } else {
    connection = emAddUdpConnection(remoteAddress,
                                    commissionerPort,
                                    ntohs(address->sin_port),
                                    (UDP_USING_DTLS
                                     | (tlsFlags & TLS_CRYPTO_SUITE_FLAGS)),
                                    sizeof(DtlsConnection),
                                    udpStatusHandler,
                                    udpMessageHandler);
    if (connection != NULL) {
      emLogLine(COAP, "got new connection handle:%d", connection->connection);
    }
    if (tlsFlags & TLS_HAVE_JPAKE) {
      emSetJpakeKey((DtlsConnection *) connection,
                    paramsCache.commissionKey,
                    paramsCache.commissionKeyLength);
    }
    return connection;
  }
}

//----------------------------------------------------------------
// network status

EmberEui64 emLocalEui64 = { 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7 };
int8u emMacExtendedId[8] = { 0 };

static uint8_t extendedPanId[8] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

const uint8_t emDefaultSourceLongId[8] = {
  0x42, 0x8A, 0x2F, 0x98, 0x71, 0x37, 0x44, 0x91
};

EmberNodeType emNodeType = EMBER_ROUTER;

#define THE_CHANNEL 0x77
#define THE_PAN_ID  0x89AB

void emberGetNetworkParameters(EmberNetworkParameters *networkParams)
{
  MEMCOPY(networkParams->extendedPanId,
          paramsCache.networkParams.extendedPanId,
          8);
  networkParams->panId   = paramsCache.networkParams.panId;
  networkParams->channel = paramsCache.networkParams.channel;
}

uint8_t emberGetRadioChannel(void)
{
  return paramsCache.networkParams.channel;
}

EmberPanId emberGetPanId(void)
{
  return paramsCache.networkParams.panId;
}

static uint8_t defaultKey[] = {
  0xbf, 0xbe, 0xbd, 0xbc, 0xbb, 0xba, 0xb9, 0xb8,
  0xb7, 0xb6, 0xb5, 0xb4, 0xb3, 0xb2, 0xb1, 0xb0
};

bool emGetNetworkKeySequence(uint32_t *sequenceLoc)
{
  if (sequenceLoc != NULL) {
    *sequenceLoc = paramsCache.networkSequenceNumber;
  }
  return true;
}

uint8_t *emGetNetworkMasterKey(uint8_t *storage)
{
  return paramsCache.networkParams.masterKey.contents;
}

void emberCoapMessageHandler(CoapMessage *message)
{
  assert(false);
}

const CommissioningDataset *emGetActiveDataset(void)
{
  return &(paramsCache.activeDataset);
}

//----------------------------------------------------------------

int main(int argc, char *argv[])
{
  int mgmtPort = 0;

  int errors = 0;
  char *me = *argv;   // Save program name.
  argv++; argc--;   // Skip program name.

  for (; 0 < argc; argc--, argv++) {
    char *arg = argv[0];
    if (strcmp(arg, "--debug") == 0) {
#if defined(DEBUG)
      debugLevel = 100;
#endif
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
    } else if (strcmp(arg, "--port") == 0) {
      if (0 < argc) {
        commissionerPort = atoi(argv[1]);
        if (0 <= commissionerPort) {
          argc--;
          argv++;
          continue;
        }
      }
    } else if (strcmp(arg, "--mgmt_port") == 0) {
      if (0 < argc) {
        mgmtPort = atoi(argv[1]);
        if (0 <= mgmtPort) {
          argc--;
          argv++;
          continue;
        }
      }
    }
    errors++;
  }

  tlsFlags |= TLS_HAVE_JPAKE;
  MEMCOPY(paramsCache.networkParams.extendedPanId, extendedPanId, 8);
  paramsCache.networkParams.panId = THE_PAN_ID;
  paramsCache.networkParams.channel = THE_CHANNEL;
  MEMCOPY(paramsCache.networkParams.masterKey.contents,
          defaultKey,
          16);
  paramsCache.networkSequenceNumber = 12;

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
  emDtlsJoinInit();
  emSetJoinSecuritySuites(tlsFlags);

  if (mgmtPort != 0) {
    managementFd = emConnectManagementSocket(mgmtPort);
  }

  if (joinerFd == -1) {
    joinerFd = socket(AF_INET6, SOCK_STREAM, 0);

    if (joinerFd < 0) {
      perror("joiner socket creation failed");
      return -1;
    }

    struct sockaddr_in6 address;
    memset(&address, 0, sizeof(struct sockaddr_in6));
    address.sin6_family = AF_INET6;
    address.sin6_addr.s6_addr[15] = 1;
    address.sin6_port = htons(joinerPort);

    if (connect(joinerFd, (struct sockaddr *) &address, sizeof(address))
        != 0) {
      fprintf(stderr, "Failed to connect to joiner port %d\n", joinerPort);
      return -1;
    }
  } else {
    fprintf(stderr, "Joiner port %d fd %d already open\n", joinerPort, joinerFd);
  }

  int flags = fcntl(joinerFd, F_GETFL);
  assert(fcntl(joinerFd, F_SETFL, flags | O_NONBLOCK) != -1);

  struct sockaddr_in routerUdpAddress;

  emAmLeader = true;
  emSetDefaultGlobalPrefix("abcdefgh");
  // Even though we don't have a connection handle to the commissioner yet,
  // the stack needs to know we are working on it otherwise it will get
  // confused and become a commissioner itself during the petition process.
  emNoteExternalCommissionerHost(-1, true);  

  // emDtlsStatusHandler(dtlsConnection);

  // fprintf(stdout, "> ");
  // fflush(stdout);

  while (true) {
    reclaimBuffers();

    uint8_t readBuffer[1 << 15];

    fd_set input;
    FD_ZERO(&input);
    FD_SET(managementFd, &input);
    FD_SET(commissionerFd, &input);
    FD_SET(joinerFd, &input);

    int maxFd = managementFd;
    if (maxFd < commissionerFd) maxFd = commissionerFd;
    if (maxFd < joinerFd) maxFd = joinerFd;

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;

    int n = select(maxFd + 1, &input, NULL, NULL, &timeout);
    if (n < 0) {
      perror("select failed");
    } else if (n == 0) {
      // Timeout
    } else {
      if (FD_ISSET(managementFd, &input)) {
        processManagementInputStream();
      }
      if (FD_ISSET(commissionerFd, &input)) {
        struct sockaddr_storage sender;
        socklen_t length = sizeof(sender);
        uint8_t remoteAddress[16];
        uint16_t got =
          nativeReadWithSender(commissionerFd,
                               readBuffer,
                               sizeof(readBuffer),
                               (struct sockaddr*) &sender,
                               &length);
        struct sockaddr_in6 *senderV6Address =
                   (struct sockaddr_in6 *)&sender;
        MEMCOPY(remoteAddress, senderV6Address->sin6_addr.s6_addr, 16);

        storeRemoteAddress((struct sockaddr_in *) &sender, remoteAddress);
        
        UdpConnectionData *connection =
          emFindConnection(remoteAddress,
                           commissionerPort,
                           ntohs(senderV6Address->sin6_port));
        if (connection != NULL) {
          // OK
          if ((connection->flags & UDP_USING_DTLS)) {
            if (tlsFlags & TLS_HAVE_JPAKE) {
              emSetJpakeKey((DtlsConnection *) connection,
                            paramsCache.commissionKey,
                            paramsCache.commissionKeyLength);
            }
          }
        } else {
          connection = getConnection((struct sockaddr_in *) &sender);
          emOpenDtlsServer((DtlsConnection *) connection);
        }

        if (got > 0) {
          if ((connection->flags & UDP_USING_DTLS)
              && (((TlsState *)
                  emGetBufferPointer(((DtlsConnection *)
                                     connection)->tlsState))->connection.state
                  == TLS_SERVER_EXPECT_HELLO)
              && sendHelloVerifyRequest((struct sockaddr_in *) &sender, length, readBuffer, got)) {
            debug("asking for cookie");
            // do nothing
          } else {
            uint8_t *contents = readBuffer;
            PacketHeader header = emFillBuffer(contents, got);
            if (header == NULL_BUFFER) {
              fprintf(stderr,  "out of buffers");
              goto exit;
            }
            emBufferQueueAdd(&((DtlsConnection *)connection)->incomingTlsData,
                             header);
            emDtlsStatusHandler((DtlsConnection *)connection);
          }
        }
      }
      if (FD_ISSET(joinerFd, &input)) {
        struct sockaddr_storage sender;
        socklen_t length = sizeof(sender);
        uint8_t remoteAddress[16];
        uint16_t got =
          nativeReadWithSender(joinerFd,
                               readBuffer,
                               sizeof(readBuffer),
                               (struct sockaddr*) &sender,
                               &length);
        if (got > 0) {
          uint8_t *contents = readBuffer;
          if (contents[1] == UART_LINK_TYPE_COMMISSIONER_DATA) {
            dtlsTransmitHandler(contents + 5,  // uart link header (4) plus handle (1)
                                got - 5,
                                NULL_BUFFER,
                                NULL, 0, NULL, 0,
                                (void *) (unsigned long) contents[4]);
          } else {
            PacketHeader header = emFillBuffer(contents + 4, // UART_LINK_HEADER_SIZE = 4
                                               got - 4);
            if (header == NULL_BUFFER) {
              fprintf(stderr,  "out of buffers");
              goto exit;
            }
            Ipv6Header ipHeader;
            emReallyFetchIpv6Header(emGetBufferPointer(header),
                                    &ipHeader);
            if (emGetBufferLength(header)
                == IPV6_HEADER_SIZE + ipHeader.ipPayloadLength) {
              uint8_t protocol = ipHeader.transportProtocol;
              if (protocol == IPV6_NEXT_HEADER_UDP
                  && ((emberFetchHighLowInt16u(ipHeader.transportHeader
                                               + UDP_CHECKSUM_INDEX) == 0)
                      || emTransportChecksum(header, &ipHeader) == 0)
                  && emFetchUdpHeader(&ipHeader)) {
                addJoinerIid((struct sockaddr_in *) &sender, ipHeader.source + 8);
                emIncomingJoinMessageHandler(header,
                                             &ipHeader,
                                             ! emAmThreadCommissioner());
              }
            } else {
              emLogLine(SECURITY, "Got a corrupt IPv6 packet... on fd %d", joinerFd);
            }
          }
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
  }

 exit:

  useBuffers = false;
  finishRunningTrace();
  killDnsService();
  debug("closing connection");
  fclose(devUrandom);
  close(commissionerFd);
  close(joinerFd);
  return 0;
}

bool emReallySubmitIpHeader(PacketHeader header,
                            Ipv6Header *ipHeader,
                            bool allowLoopback,
                            uint8_t retries,
                            uint16_t delayMs)
{
  uint8_t toSendBuffer[1<<16];
  uint8_t *toSend = toSendBuffer;
  bool commissionerMessage = (ipHeader->sourcePort == DTLS_COMMISSION_PORT);

  if (commissionerMessage) {
    uint16_t length = ipHeader->transportPayloadLength;
    uint16_t toDo = length;
    Buffer finger = header;
    uint8_t *from = ipHeader->transportPayload;
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
    memcpy(&destination.sin_addr,
           ipHeader->destination + 8,
           sizeof(destination.sin_addr));
    destination.sin_port = htons(ipHeader->destinationPort);
    nativeWrite(commissionerFd,
                toSendBuffer,
                length,
                (struct sockaddr*) &destination,
                sizeof(destination));
  } else {
    uint16_t length = 0;
    uint16_t sent;
    Buffer temp;
    uint16_t start;
 
    if (ipHeader->nextHeader == IPV6_NEXT_HEADER_UDP) {
      uint8_t checksumIndex = UDP_CHECKSUM_INDEX;
      emberStoreHighLowInt16u(ipHeader->ipPayload + checksumIndex, 0);
      emberStoreHighLowInt16u(ipHeader->ipPayload + checksumIndex,
                              emTransportChecksum(header, ipHeader));
    } else {
      emLogLine(SECURITY, "Expecting only UDP packets on joiner fd %d", joinerFd);
      return false;
    }
 
    for (temp = header, start = emMacPayloadIndex(header) + INTERNAL_IP_OVERHEAD;
         temp != NULL_BUFFER;
         temp = emGetPayloadLink(temp), start = 0) {
      uint8_t *data = emGetBufferPointer(temp) + start;
      uint16_t count = emGetBufferLength(temp) - start;
      MEMCOPY(toSend + length, data, count);
      length += count;
    }

    if (write(joinerFd, toSendBuffer, length) != length) {
      emLogLine(SECURITY, "Problem with write on joiner fd %d", joinerFd);
    }
  }
  return true;
}

//----------------------------------------------------------------
// management commands

void emNoteExternalCommissionerHost(uint8_t commissionerId,
                                bool available)
{
  emSendBinaryManagementCommand(EMBER_NOTE_EXTERNAL_COMMISSIONER_COMMAND_IDENTIFIER,
                                "uu",
                                commissionerId,
                                available);
}

void emberCommissionerStatusHandler(uint16_t flags,
                                    const uint8_t *commissionerName,
                                    uint8_t commissionerNameLength)
{
  emSendBinaryManagementCommand(CB_COMMISSIONER_STATUS_COMMAND_IDENTIFIER,
                                "vb",
                                flags,
                                commissionerName, commissionerNameLength);
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

void emberSetSteeringData(const uint8_t *steeringData, uint8_t steeringDataLength)
{
  fprintf(stdout, "steering data received: {");
  uint8_t i;
  for (i = 0; i < steeringDataLength; i++) {
    fprintf(stdout, "%X ", steeringData[i]);
  }
  fprintf(stdout, "}\n");
  emSendBinaryManagementCommand(EMBER_SET_STEERING_DATA_COMMAND_IDENTIFIER,
                                "b",
                                steeringData, steeringDataLength);
}

void emberBecomeCommissionerReturn(EmberStatus status)
{
  emSendBinaryManagementCommand(CB_BECOME_COMMISSIONER_COMMAND_IDENTIFIER,
                                "u",
                                status);
}

void emberSetCommissionerKeyReturn(EmberStatus status)
{
  emSendBinaryManagementCommand(CB_SET_COMMISSIONER_KEY_COMMAND_IDENTIFIER,
                                "u",
                                status);
}

void emberSendSteeringDataReturn(EmberStatus status)
{
  emSendBinaryManagementCommand(CB_SEND_STEERING_DATA_COMMAND_IDENTIFIER,
                                "u",
                                status);
}

void emberSendEntrust(const uint8_t *commissioningMacKey, const uint8_t *destination)
{
  emSendBinaryManagementCommand(EMBER_SEND_ENTRUST_COMMAND_IDENTIFIER,
                                "bb",
                                commissioningMacKey, 16,
                                destination, 16);
}

extern void emSendGetData(const uint8_t *tlvs,
                          uint8_t count,
                          EmberCoapResponseHandler handler);

static void getResponseHandler(EmberCoapStatus status,
                               EmberCoapCode code,
                               EmberCoapReadOptions *options,
                               uint8_t *payload,
                               uint16_t payloadLength,
                               EmberCoapResponseInfo *info)
{
  if (status == EMBER_COAP_MESSAGE_RESPONSE) {
    const uint8_t *finger = payload;
    const uint8_t *end = finger + payloadLength;

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

static void statusCommand(void)
{
  if (paramsCache.networkStatus != EMBER_NO_NETWORK) {
    fprintf(stderr, "ext pan: {");
    uint8_t i;
    for (i = 0; i < 8; i++) {
      fprintf(stderr, "%02X", paramsCache.networkParams.extendedPanId[i]);
    }
    fprintf(stderr, "}\nnetwork id: %s", paramsCache.networkParams.networkId);
    fprintf(stderr,
            "\npan id: 0x%2X channel: %d",
            paramsCache.networkParams.panId,
            paramsCache.networkParams.channel);
    fprintf(stderr, "\neui: {");
    for (i = 0; i < 8; i++) {
      fprintf(stderr, "%02X", emLocalEui64.bytes[7-i]);
    }
    fprintf(stderr, "}\nmac ext id: {");
    for (i = 0; i < 8; i++) {
      fprintf(stderr, "%02X", emMacExtendedId[7-i]);
    }
    fprintf(stderr, "}\nmaster key: {");
    for (i = 0; i < 16; i++) {
      fprintf(stderr, "%02X", paramsCache.networkParams.masterKey.contents[i]);
    }
    fprintf(stderr, "} sequenceNumber: %d", paramsCache.networkSequenceNumber);
    fprintf(stderr, "}\njoin key: %s", paramsCache.joinKey);
    fprintf(stderr, "\ncomm key: {");
    for (i = 0; i < 16; i++) {
      fprintf(stderr, "%02X", paramsCache.commissionKey[i]);
    }
    fprintf(stderr, "}\n");
  }
}

const EmberCommandEntry emberCommandTable[] = {
  emberCommand("get_data",      getDataCommand,      "u*",  NULL),
  emberCommand("status",        statusCommand,       "",    NULL),
  { NULL }
};

//----------------------------------------------------------------
// Randomness.

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

//----------------------------------------------------------------
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

//----------------------------------------------------------------
// Sim print

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

//----------------------------------------------------------------
// stubs

#ifdef EMBER_TEST
  #define USE_STUB_makeMessage
  #define USE_STUB_emLoseHelper
#endif

#ifdef EMBER_SCRIPTED_TEST
  #define USE_STUB_setRandomDataType
#endif

#define USE_STUB_emberGetNetworkDataReturn
#define USE_STUB_emChildCount
#define USE_STUB_emSetBeaconSteeringData
#define USE_STUB_tokTypeIpStackCommissioningDataset
#define USE_STUB_emSetActiveDataset
#define USE_STUB_emExternalRouteTable
#define USE_STUB_emProcessIncomingJoinBeacon
#define USE_STUB_emSetPermitJoin
#define USE_STUB_emberRequestSlaacAddressReturn
#define USE_STUB_emberNetworkDataChangeHandler
#define USE_STUB_emberAddressConfigurationChangeHandler
#define USE_STUB_emAddChannelMaskTlv
#define USE_STUB_emCoapHostIncomingMessageHandler
#define USE_STUB_emberStartScan
#define USE_STUB_emHandleCommissionDatasetPost
#define USE_STUB_emJoinSecurityFailed
#define USE_STUB_emJoinCommissionCompleteHandler
#define USE_STUB_emGetCommissioningMacKey
#define USE_STUB_emCommissioningHandshakeComplete
#define USE_STUB_emSetCommissioningMacKey
#define USE_STUB_emCancelCommissioningKey
#define USE_STUB_emGetThreadNativeCommission
#define USE_STUB_emAppCommissionCompleteHandler
#define USE_STUB_emSetPanId
#define USE_STUB_emSetExtendedPanId
#define USE_STUB_emMacPayloadPointer
#define USE_STUB_emMacPayloadIndex
#define USE_STUB_emMacDestinationMode
#define USE_STUB_emMacDestinationPointer
#define USE_STUB_emMacShortDestination
#define USE_STUB_emCancelCommissioningKey
#define USE_STUB_halInternalGetTokenData
#define USE_STUB_halInternalSetTokenData
#define USE_STUB_emGetMacKey
#define USE_STUB_emberGetNodeId
#define USE_STUB_emStackConfiguration
#define USE_STUB_emSendDhcpSolicit
#define USE_STUB_emberDhcpServerChangeHandler
#define USE_STUB_emSendMleChildUpdate
#define USE_STUB_emberResignGlobalAddressReturn
#define USE_STUB_emSendDhcpAddressRelease
#define USE_STUB_emUncompressContextPrefix
#define USE_STUB_emBeaconPayloadBuffer
#define USE_STUB_emStackEventQueue
#define USE_STUB_emFetchIpv6Header
#define USE_STUB_emFetchUdpHeader
#define USE_STUB_emSetThreadPermitJoin
#define USE_STUB_emBeaconPayloadSize
#define USE_STUB_emGetThreadJoin
#define USE_STUB_emNetworkDataPostToken
#define USE_STUB_emHandleAddressManagementPost
#define USE_STUB_emSetNetworkMasterKey
#define USE_STUB_emAddressCache
#define USE_STUB_emDhcpIncomingMessageHandler
#define USE_STUB_AddressData
#define USE_STUB_emRipEntry
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
#define USE_STUB_emberGetNetworkDataTlvReturn
#define USE_STUB_emGetCompleteNetworkData
#define USE_STUB_emberLeaderDataHandler
#define USE_STUB_emSendDhcpSolicit
#define USE_STUB_emberSlaacServerChangeHandler
#define USE_STUB_emHandleThreadDiagnosticMessage
#define USE_STUB_emberCoapRequestHandler
#define USE_STUB_emSecurityToUart
#define USE_STUB_emSerialTransmitCallback
#include "stack/ip/stubs.c"
