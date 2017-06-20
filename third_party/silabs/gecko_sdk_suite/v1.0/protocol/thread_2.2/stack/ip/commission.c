/*
 * File: commission.c
 * Description: Thread commissioning
 *
 * Copyright 2015 Silicon Laboratories, Inc.                                *80*
 */
 
// For strnlen(3) in glibc.
#define _GNU_SOURCE

#include "core/ember-stack.h"
#include "hal/hal.h"
#include "framework/event-queue.h"
#include "framework/ip-packet-header.h"

// These two are for getting a neighbor's EUI64.
#include "routing/neighbor/neighbor.h"
#include "mac/802.15.4/802-15-4-ccm.h"
#include "mac/802.15.4/scan.h"
#include "zigbee/key-management.h"
#include "address-management.h"
#include "rip.h"

#include "ip-address.h"
#include "ip-header.h"
#include "network-data.h"
#include "dhcp.h"
#include "local-server-data.h"
#include "association.h"
#include "zigbee/join.h"
#include "app/coap/coap.h"
#include "app/coap/coap-stack.h"
#include "udp.h"
#include "tls/tls.h"
#include "tls/dtls.h"
#include "tls/dtls-join.h"
#include "commission.h"
#include "coap-diagnostic.h"
#include <string.h>
#include "stack/ip/commission-dataset.h"
#include "stack/ip/tls/debug.h"

//----------------------------------------------------------------
// Forward declarations

void emGenerateKek(TlsState *tls, uint8_t *key);
static void commissionerTimeoutEventHandler(Event *event);
static void joinerRelayRateLimitEventHandler(Event *event);
static void sendCommissionerStatus(void);
static void sendKeepalive(uint8_t status);

//----------------------------------------------------------------
// Parsing messages

// A macro to get the offset of a field within a CommissionMessage.
#define OFFSET(f)                                       \
  ((uint16_t) (long) &(((CommissionMessage *) NULL)->f))

#define FIELD(flag, f, type, min, max)       \
  { OFFSET(f), (COMMISSION_##flag##_TLV_FLAG), (type), (min), (max) }

#define UNUSED_TLV {-1, -1, NOT_USED, 0, 0}

// All of the TLVs.  These are in numerical order with no gaps so that this
// can be indexed into using the TLV types.
static TlvData tlvData[] = {
  FIELD(CHANNEL, channelTlv,                POINTER,  3,  0),
  FIELD(PAN_ID, panId,                      INT16,    0,  0),
  FIELD(EXTENDED_PAN_ID, extendedPanId,     POINTER,  8,  0),
  FIELD(NETWORK_NAME, networkNameTlv,       TLV,      0, 16),
  FIELD(PSKC,         pskcTlv,              TLV,      0, 16),
  FIELD(NETWORK_MASTER_KEY, masterKey,      POINTER, 16,  0),
  FIELD(NETWORK_KEY_SEQUENCE, keySequence,  INT32,    0,  0),
  FIELD(MESH_LOCAL_ULA, meshLocalUla,       POINTER,  8,  0),
  FIELD(STEERING_DATA, steeringDataTlv,     TLV,      0, 255),
  FIELD(BORDER_ROUTER_LOCATOR, borderRouterNodeId, INT16,    0,  0),
  FIELD(COMMISSIONER_ID,  commissionerIdTlv, TLV,      0, 64),
  FIELD(SESSION_ID,       sessionId,         INT16,    0,  0),
  FIELD(SECURITY_POLICY,  securityPolicy,    POINTER,  3,  0),
  FIELD(GET,              getTlv,            TLV,      1, 50),   // 50 just to put something
  FIELD(ACTIVE_TIMESTAMP, activeTimestamp,   POINTER,  8,  0),
  FIELD(COMMISSIONER_UDP_PORT, commissionerUdpPort, INT16, 0, 0),
  FIELD(STATE, state,              INT8,     0,  0),
  FIELD(JOINER_DTLS_ENCAP, joinerDtlsEncapTlv, TLV,      1, 1280), // shouldn't get near 1280
  FIELD(JOINER_UDP_PORT, joinerUdpPort,     INT16,    0,  0),
  FIELD(JOINER_ADDRESS, joinerAddress,      POINTER,  8,  0),
  FIELD(JOINER_ROUTER_LOCATOR, joinerRouterNodeId, INT16,    0,  0),
  FIELD(JOINER_ROUTER_KEK, joinerRouterKek,    POINTER, 16,  0),

// gap one, KEK is 21, Provisioning URL is 32
  FIELD(PROVISIONING_URL, provisioningUrlTlv,TLV,     0, 64),

  // UNUSED_TLV,  // FIELD(vendorNameTlv,      TLV,      0, 32),
  // UNUSED_TLV,  // FIELD(vendorModelTlv,     TLV,      0, 32),   // max is missing
  // UNUSED_TLV,  // FIELD(vendorSwVersionTlv, TLV,      0, 32),   // max is missing
  // UNUSED_TLV,  // FIELD(vendorDataTlv,      TLV,      0, 96),
  // UNUSED_TLV,  // FIELD(vendorStackVersion, POINTER,  6,  0),

// gap two, vendorStackVersion is 37, UDP Encapsulation is 48, but for
// us it is from provisioning URL to channel mask, because we don't use
// a bunch on either side of the gap.

  // UNUSED_TLV,  // FIELD(udpEncapTlv,        TLV,      1, 1280),
  // UNUSED_TLV,  // FIELD(ipv6AddressTlv,     POINTER, 16,  0)
  // UNUSED_TLV,  // forwarding pointer
  // UNUSED_TLV,  // FIELD(PENDING_TIMESTAMP, pendingTimestamp, POINTER, 8,  0),
  // UNUSED_TLV,  // FIELD(DELAY_TIMER,       delayTimer,       INT32,    0,  0),

  FIELD(CHANNEL_MASK,      channelMask,     POINTER,  6, 0),
  FIELD(SCAN_COUNT,        scanCount,       INT8,     0, 0),
  FIELD(SCAN_PERIOD,       scanPeriod,      INT16,    0, 0),
  FIELD(SCAN_DURATION,     scanDuration,    INT16,    0, 0),
  FIELD(SCAN_ENERGY_LIST,  scanEnergyListTlv, TLV,    1, 160),

// gap 3, scan energy list is 57, discovery request is 128  

  // UNUSED_TLV, // FIELD(DISCOVERY_REQUEST, discoveryRequest, int8u,   0, 0),
  FIELD(DISCOVERY_RESPONSE, discoveryResponseFlags, INT8, 0, 0),
};

// Data for mapping TLV numbers into indexes in the above array.
// The last TLV number in each block is given.  The blocks start with a
// used one and alternate between used and unused.

static const uint8_t tlvRanges[] = {
  COMMISSION_JOINER_ROUTER_KEK_TLV,      // end of good range
  COMMISSION_PROVISIONING_URL_TLV - 1,   // end of bad range
  COMMISSION_PROVISIONING_URL_TLV,       // end of good range
  COMMISSION_CHANNEL_MASK_TLV - 1,       // end of bad range
  COMMISSION_SCAN_ENERGY_LIST_TLV,       // end of good range
  COMMISSION_DISCOVERY_RESPONSE_TLV - 1, // end of bad range
  COMMISSION_DISCOVERY_RESPONSE_TLV,     // end of good range
  0xFF                                  
};

static TlvData *getTlvData(uint8_t tlvNumber)
{
  uint8_t index = tlvNumber;
  uint8_t i = 0;

  for (i = 0; tlvRanges[i] != 0; i += 2) {
    if (tlvNumber <= tlvRanges[i]) {      // at or before end of good range
      break;                              // so it's good
    }
    if (tlvNumber <= tlvRanges[i + 1]) {  // at or before end of bad range
      return NULL;      // we have no record of this TLV, return dummy
    }
    index -= (tlvRanges[i + 1] - tlvRanges[i]);
  }

  if (tlvData[index].type == NOT_USED) {
    return NULL;
  } else {
    return tlvData + index;
  }
}

// for the leader
static uint16_t nextSessionId;
uint16_t emCommissionerSessionId;     // session ID chosen by leader
// TODO: This is now redundant with the Commission TLV in the network data
// and probably can be done without.
Buffer emCommissioner = NULL_BUFFER;

// for the leader and commissioners

// size = 0               -> joining is not permitted
// size = 1, byte 0xFF    -> anyone can join
HIDDEN uint8_t steeringDataLength;
HIDDEN uint8_t steeringData[COMMISSION_MAX_STEERING_DATA_SIZE];

//----------------------------------------------------------------
// Steering is done using Bloom filters based on CRCs.

// Generic 16-bit CRC

static uint16_t crc16(const uint8_t *bytes, uint16_t count, uint16_t polynomial)
{
  uint16_t crc = 0;
  uint16_t i;

  for (i = 0; i < count; i++) {
    crc ^= bytes[i] << 8;

    uint8_t j;
    for (j = 0; j < 8; j++) {
      if (crc & 0x8000) {
        crc = (crc << 1) ^ polynomial;
      } else {
        crc = (crc << 1);
      }
    }
  }
  return crc;
}

// The two polynomials that are used.

static const uint16_t polys[] = { 0x1021,       // CCITT
                                  0x8005 };     // ANSI

// This is a little goofy to get the bit and byte order correct.

static bool steeringDataOp(uint8_t *steeringData,
                           uint8_t steeringDataLength,
                           const uint8_t *eui64,
                           bool set)
{
  uint8_t bigEndianEui[8];
  emberReverseMemCopy(bigEndianEui, eui64, 8);

  if (steeringDataLength == 0) {
    return false;
  }

  uint8_t i;
  // -1 because the first bit is a flag bit
  uint8_t bitCount = (steeringDataLength << 3);
  uint8_t eui64Length = 8;

  for (i = 0; i < 2; i++) {
    uint16_t crc = crc16(bigEndianEui, eui64Length, polys[i]);
    uint16_t index = bitCount - (crc % bitCount) - 1;
    uint8_t *loc = steeringData + (index >> 3);
    uint8_t bit = 0x80 >> (index & 0x07);
    if (set) {
      *loc |= bit;
    } else if (! (*loc & bit)) {
      return false;
    }
  }
  return true;
}

HIDDEN void addToSteeringData(const uint8_t *eui)
{
  EmberEui64 eui64;
  MEMCOPY(eui64.bytes, eui, 8);
  // In 1.1, the eui hash (joinerId) is used in the bloom filter.
  EmberEui64 joinerId;
  emComputeEui64Hash(&eui64, &joinerId);
  emLogBytesLine(COMMISSION, "add steering for", eui64.bytes, 8);
  emLogBytesLine(COMMISSION, "joinerId (hash)", joinerId.bytes, 8);
  steeringDataOp(steeringData,
                 steeringDataLength,
                 joinerId.bytes,
                 true);
  emLogBytesLine(COMMISSION, "new steering data", steeringData, steeringDataLength);
}

bool emSteeringDataMatch(uint8_t *data, uint8_t length)
{
  EmberEui64 eui64;
  MEMCOPY(eui64.bytes, emLocalEui64.bytes, 8);
  // In 1.1, the eui hash (joinerId) is used in the bloom filter.
  EmberEui64 joinerId;
  emComputeEui64Hash(&eui64, &joinerId);

  bool joinerIdResult = steeringDataOp(data, length, joinerId.bytes, false);

  emLogBytesLine(COMMISSION, "steering data", data, length);
  // 1.1 compliance
  emLogBytesLine(COMMISSION, "%ssteering for joinerId",
                 joinerId.bytes, 8,
                 joinerIdResult ? "": "no ");

  return joinerIdResult;
}

// This is somewhat redundant with borderRouterNodeId, but it just
// seems safer not to rely on node ID changes and commissioning data
// changes to be full coordinated.
static bool amCommissioner = false;

static bool amGateway = false;
static EmberUdpConnectionHandle commissionerConnectionHandle = NULL_UDP_HANDLE;

void emStackNoteExternalCommissioner(EmberUdpConnectionHandle handle,
                                     bool available)
{
  if (available) {
    amGateway = true;
    commissionerConnectionHandle = handle;
    emLogLine(COMMISSION, "acting as native gateway");
    emApiGetCommissioner();   // report to the application
  } else {
    emRemoveCommissioner();
    sendCommissionerStatus();
  }
}

// for everyone
EmberNodeId emBorderRouterNodeId;

//----------------------------------------------------------------
// On the leader this removes the commissioner if we don't hear from it.
// On the commissioner this sends keep-alive messages to the leader.

#define COMMISSIONER_TIMEOUT_INTERVAL_MS            120000
#define COMMISSIONER_KEEPALIVE_INTERVAL_MS           50000
#define COMMISSIONER_INITIAL_KEEPALIVE_INTERVAL_MS    1000

static void commissionerTimeoutEventMarker(Event *event) {}

static EventActions commissionerTimeoutEventActions = {
  &emStackEventQueue,
  commissionerTimeoutEventHandler,
  commissionerTimeoutEventMarker,
  "commissioner timeout"
};

static Event commissionerTimeoutEvent =
  { &commissionerTimeoutEventActions, NULL };

static void commissionerTimeoutEventHandler(Event *event)
{
  if (emAmLeader) {
    if (! amCommissioner
        && emCommissioner != NULL_BUFFER) {
      emLogLine(COMMISSION, "commissioner timed out");
      emRemoveCommissioner();
      sendCommissionerStatus();
    }
  } else if (amCommissioner) {
    if (emNodeType == EMBER_COMMISSIONER) {
      emExternalCommissionerKeepAlive(COMMISSION_SUCCESS);
    } else {
      sendKeepalive(COMMISSION_SUCCESS);
    }
    if (! emberEventIsScheduled(&commissionerTimeoutEvent)) {
      emberEventSetDelayMs(&commissionerTimeoutEvent,
                           COMMISSIONER_KEEPALIVE_INTERVAL_MS);
    }
  }
}

static void commissionerKeepalive(void)
{
  emberEventSetDelayMs(&commissionerTimeoutEvent,
                       COMMISSIONER_TIMEOUT_INTERVAL_MS);
}

//----------------------------------------------------------------

// This event limits the rate that joiner messages are relayed to
// JOINER_RELAY_RATE_LIMIT, which is specified as 10 per second.

#define JOINER_RELAY_RATE_LIMIT_COUNT 10
#define JOINER_RELAY_RATE_LIMIT_INTERVAL_MS 1000

static uint8_t joinerRelayCount = 0;

static EventActions joinerRelayRateLimitEventActions = {
  &emStackEventQueue,
  joinerRelayRateLimitEventHandler,
  NULL,          // no marking function is needed
  "joiner relay rate limit"
};

static Event joinerRelayRateLimitEvent =
  { &joinerRelayRateLimitEventActions, NULL };

static void joinerRelayRateLimitEventHandler(Event *event)
{
  joinerRelayCount = 0;
}

//----------------------------------------------------------------

void emCommissionInit(void)
{
  emBorderRouterNodeId = 0xFFFE;
  amCommissioner = false;
  emCommissioner = NULL_BUFFER;
  emberEventSetInactive(&commissionerTimeoutEvent);
  steeringDataLength = 0;
  joinerRelayCount = 0;

  // Shoot; if we were the commissioner we need to tell the app.
  // I think that will happen automatically when we join a fragment
  // and get new network data.
}

//----------------------------------------------------------------

static uint16_t largeTlvLength(const uint8_t *tlv)
{
  if (tlv[1] == 255) {
    return emberFetchHighLowInt16u(tlv + 2);
  } else {
    return tlv[1];
  }
}

static const uint8_t *largeTlvData(const uint8_t *tlv)
{
  return tlv + ((tlv[1] == 255) ? 4 : 2);
}

UriHandler* emLookupUriHandler(const uint8_t *uri, UriHandler *handlers)
{
  UriHandler *next;
  for (next = handlers; next->uri != NULL; next++) {
    if (strcmp((const char *)uri, next->uri) == 0)
      return next;
  }
  return NULL;
}

//----------------------------------------------------------------

bool emParseTlvMessage(void *message,
                       uint32_t *tlvMask,
                       const uint8_t *payload,
                       uint16_t length,
                       TlvDataGetter getTlvData)
{
  const uint8_t *finger = payload;
  const uint8_t *end = payload + length;

  while (finger + 2 <= end) {
    const uint8_t *tlv = finger;
    uint8_t tlvId = *finger++;
    uint16_t length = *finger++;

    if (length == 0xFF) {
      length = emberFetchHighLowInt16u(finger);
      finger += 2;
    }

    TlvData *data = getTlvData(tlvId);

    if (data == NULL) {
      // skip this one, we either don't know what it is or don't care
    } else if (*tlvMask & BIT(data->flagIndex)) {
      emLogBytesLine(COAP, "tlv duplicate id: %u bytes: ",
                     finger, length, tlvId);
      // skip it
    } else {
      // this produces too much output to be left on all the time
      // emLogBytesLine(COAP, "tlv id: %u bytes: ", finger, length, tlvId);

      *tlvMask |= BIT(data->flagIndex);
      void *field = ((uint8_t *) message) + data->offset;
      switch (data->type) {
      case INT8:
        if (length < 1) {
          lose(COAP, false);
        }
        *(uint8_t *)field = *finger;
        break;

      case INT16:
        if (length < 2) {
          lose(COAP, false);
        }
        *((uint16_t *) field) = emberFetchHighLowInt16u(finger);
        break;

      case INT32:
        if (length < 4) {
          lose(COAP, false);
        }
        *((uint32_t *) field) = emberFetchHighLowInt32u(finger);
        break;

      case POINTER:
        if (length < data->minLength) {
          lose(COAP, false);
        }
        *((const uint8_t **) field) = finger;
        break;

      case TLV:
        if (length < data->minLength || data->maxLength < length) {
          lose(COAP, false);
        }
        *((const uint8_t **) field) = tlv;
        break;

      default:
        assert(false);
        break;
      }
    }
    finger += length;
  }

  if (finger != end) {
    lose(COAP, false);
  }

  return finger == end;
}

static void printCommissionMessage(CommissionMessage *message)
{
  emLog(COMMISSION, "[commission");
  if (message->tlvMask & COMM_TLV_BIT(STATE_TLV)) {
    emLog(COMMISSION, " state %d", message->state);
  }
  if (message->tlvMask & COMM_TLV_BIT(SESSION_ID_TLV)) {
    emLog(COMMISSION, " sessionId %d", message->sessionId);
  }
  if (message->tlvMask & COMM_TLV_BIT(COMMISSIONER_ID_TLV)) {
    emLogBytes(COMMISSION, " commissionerId",
               message->commissionerIdTlv + 2,
               message->commissionerIdTlv[1]);
  }
  emLog(COMMISSION, "]\n");
}

uint8_t *emAddInt16uTlv(uint8_t *finger, uint8_t tlvType, uint16_t value)
{
  *finger++ = tlvType;
  *finger++ = 2;
  *finger++ = HIGH_BYTE(value);
  *finger++ = LOW_BYTE(value);
  return finger;
}

uint8_t *emAddInt32uTlv(uint8_t *finger, uint8_t tlvType, uint32_t value)
{
  *finger++ = tlvType;
  *finger++ = 4;
  emberStoreHighLowInt32u(finger, value);
  return finger + 4;
}

uint8_t *emAddSessionIdTlv(uint8_t *finger)
{
  return emAddInt16uTlv(finger, COMMISSION_SESSION_ID_TLV, emCommissionerSessionId);
}

static uint8_t *addStateTlv(uint8_t *finger, uint8_t state)
{
  return emAddTlv(finger, COMMISSION_STATE_TLV, &state, 1);
}

#define addBorderRouterLocatorTlv(finger) \
  (emAddInt16uTlv((finger), \
                COMMISSION_BORDER_ROUTER_LOCATOR_TLV, \
                emBorderRouterNodeId))

static const uint8_t successTlv[] =
  { COMMISSION_STATE_TLV, 1, COMMISSION_SUCCESS};

static const uint8_t failureTlv[] =
  { COMMISSION_STATE_TLV, 1, COMMISSION_FAILURE};

uint8_t *emAddTlv(uint8_t *finger, uint8_t type, const uint8_t *data, uint8_t length)
{
  *finger++ = type;
  *finger++ = length;
  MEMCOPY(finger, data, length);
  return finger + length;
}

// STEERING_DATA_TLV          2 + 16
// BORDER_ROUTER_LOCATOR      2 + 2
// SESSION_ID_TLV             2 + 2
// STATE_TLV                  2 + 1

#define TLV_OVERHEAD 2
#define MAX_COMMISSIONER_DATA (4 * TLV_OVERHEAD + 16 + 2 + 2 + 1)

#define TEMPORARY_TYPE (emNetworkDataTemporaryType(NWK_DATA_COMMISSION))

static void sendCommissionerStatus(void)
{
  uint8_t *existing = emFindTemporaryNetworkDataTlv(NWK_DATA_COMMISSION);
  uint8_t tlv[2 + MAX_COMMISSIONER_DATA];
  uint8_t *finger = tlv;
  *finger++ = TEMPORARY_TYPE;
  *finger++ = 0;      // length goes here
  finger = emAddSessionIdTlv(finger);

  if (emCommissioner != NULL_BUFFER) {
    finger = addBorderRouterLocatorTlv(finger);
    if (steeringDataLength == 0) {
      finger = emAddTlv(finger, COMMISSION_STEERING_DATA_TLV, NULL, 0);
    } else {
      finger = emAddTlv(finger,
                        COMMISSION_STEERING_DATA_TLV,
                        steeringData,
                        steeringDataLength);
    }
  }
  assert(finger - tlv <= sizeof(tlv));
  uint8_t tlvSize = finger - (tlv + 2);
  emSetNetworkDataSize(tlv, tlvSize);

  // What to do if this fails?
  if (existing == NULL) {
    // This changes the network data, which causes emSetCommissionState()
    // to be called.
    emAddCommissionTlv(tlv);
  } else if (! (tlvSize == emNetworkDataSize(existing)
                && MEMCOMPARE(emNetworkDataPointer(existing),
                              emNetworkDataPointer(tlv),
                              tlvSize) == 0)) {
    existing = emSetNetworkDataTlvLength(existing, tlvSize);
    if (existing != NULL) {
      MEMCOPY(emNetworkDataPointer(existing),
              emNetworkDataPointer(tlv),
              tlvSize);
    }
    // The network data code may not have detected a change, so we need
    // to propagate the new data.
    emSetCommissionState(existing);
    // We need to add a check, if the network data has updated
    emAnnounceNewNetworkData(false);
  }
}

// Need to compare it to the old version to know whether or not to tell
// the app.  The network data knows, but it has its own complexities.
//
// Need to compare:
//   steering data - that's saved
//   everything else is conditioned on the session
// For now, just report it.

void emSetCommissionState(const uint8_t *networkDataTlv)
{
  if (networkDataTlv == NULL) {
    emSetBeaconSteeringData(NULL, 0);
    emBorderRouterNodeId = 0xFFFE;
  } else {
    CommissionMessage message;
    MEMSET(&message, 0, sizeof(message));
    if (! emParseTlvMessage(&message,
                            &message.tlvMask,
                            emNetworkDataPointer(networkDataTlv),
                            emNetworkDataSize(networkDataTlv),
                            &getTlvData)) {
      emLogLine(COMMISSION, "failed to parse commission state");
      return;   // don't try to report anything
    } else {
      if (amCommissioner
          && (message.tlvMask & (COMM_TLV_BIT(SESSION_ID_TLV)))
          && timeGTorEqualInt16u(message.sessionId, emCommissionerSessionId)
          && (! (message.tlvMask & (COMM_TLV_BIT(BORDER_ROUTER_LOCATOR_TLV)))
              || message.borderRouterNodeId != emberGetNodeId())) {
        amCommissioner = false;
      }

      if (message.tlvMask & (COMM_TLV_BIT(BORDER_ROUTER_LOCATOR_TLV))) {
        emBorderRouterNodeId = message.borderRouterNodeId;
      } else {
        emBorderRouterNodeId = 0xFFFE;
      }

      if ((emberGetNodeId() == emBorderRouterNodeId) && ! amGateway) {
        amCommissioner = true;
      }

      uint16_t tlvDataLength = (message.steeringDataTlv == NULL
                              ? 0
                              : emNetworkDataSize(message.steeringDataTlv));
      if (tlvDataLength == 0
          || emIsMemoryZero(emNetworkDataPointer(message.steeringDataTlv),
                            tlvDataLength)) {
        steeringDataLength = 0;
        emSetBeaconSteeringData(NULL, 0);
      } else {
        steeringDataLength = tlvDataLength;
        MEMCOPY(steeringData, message.steeringDataTlv + 2, tlvDataLength);
        emSetBeaconSteeringData(message.steeringDataTlv + 2, tlvDataLength);
      }
    }
  }
  emLogLine(JOIN, "new commissioning, joining %sabled",
            emGetThreadJoin() ? "en" : "dis");
  // Only want to report if something changed.  Use the sequence number?
  emApiGetCommissioner();   // report to the application
}

static bool parseCommissionMessage(CommissionMessage *message,
                                   const uint8_t *payload,
                                   uint16_t payloadLength)
{
  return (emParseTlvMessage(message,
                            &message->tlvMask,
                            payload,
                            payloadLength,
                            &getTlvData));
}

uint8_t *emAddChannelTlv(uint8_t *finger)
{
  uint8_t channel = emberGetRadioChannel();
  *finger++ = COMMISSION_CHANNEL_TLV;
  *finger++ = 3;
  *finger++ = 0;
  emberStoreHighLowInt16u(finger, channel);
  finger += 2;
  return finger;
}

uint8_t *emAddExtendedPanIdTlv(uint8_t *finger)
{
  return emAddTlv(finger, COMMISSION_EXTENDED_PAN_ID_TLV, emExtendedPanId, 8);
}

uint8_t *emAddNetworkNameTlv(uint8_t *finger)
{
  return emAddTlv(finger,
                  COMMISSION_NETWORK_NAME_TLV,
                  emNetworkId,
                  strnlen((char const*)emNetworkId, EMBER_NETWORK_ID_SIZE));
}

uint8_t *emAddMeshLocalUlaTlv(uint8_t *finger)
{
  *finger++ = COMMISSION_MESH_LOCAL_ULA_TLV;
  *finger++ = 8;
  emStoreDefaultGlobalPrefix(finger);
  return finger + 8;
}

uint8_t *emAddDiscoveryResponseTlvs(uint8_t *finger)
{
  bool nativeCommissionEnabled = (emGetActiveDataset()->securityPolicy[2]
                                  & NATIVE_COMMISSIONING);
  *finger++ = COMMISSION_DISCOVERY_RESPONSE_TLV;
  *finger++ = 2;
  *finger++ = ((THREAD_1_1_PROTOCOL_VERSION << 4)
               | (nativeCommissionEnabled
                  ? DISCOVERY_RESPONSE_NATIVE_COMMISSIONER_FLAG
                  : 0));
  *finger++ = 0;
  finger = emAddExtendedPanIdTlv(finger);
  finger = emAddNetworkNameTlv(finger);

  finger = emAddTlv(finger,
                    COMMISSION_STEERING_DATA_TLV,
                    steeringData,
                    steeringDataLength);
  finger = emAddInt16uTlv(finger, 
                          COMMISSION_JOINER_UDP_PORT_TLV, 
                          emUdpJoinPort);
  if (nativeCommissionEnabled) {
    finger = emAddInt16uTlv(finger,
                            COMMISSION_COMMISSIONER_UDP_PORT_TLV,
                            emUdpCommissionPort);
  }
  return finger;
}

void emSendJoinerEntrust(DtlsConnection *connection, const uint8_t *key)
{
  const uint8_t *ipDestination = connection->udpData.remoteAddress;
  emLogBytesLine(COMMISSION, "entrust sent to", ipDestination, 16);

  uint8_t keyBuf[16];
  if (key == NULL) {
    emGenerateKek(connectionDtlsState(connection), keyBuf);
    key = keyBuf;
  }

  emLogBytesLine(COMMISSION, "entrust key", key, 16);

#ifdef UNIX_HOST
  emberSendEntrust(key, ipDestination);
  emLogLine(COMMISSION, "sent joiner entrust");
#else
  uint8_t payload[125];
  uint8_t *finger = payload;
  uint32_t sequence;
  CoapMessage coap;
 
  uint8_t storage[EMBER_ENCRYPTION_KEY_SIZE];
  finger = emAddTlv(finger,
                    COMMISSION_NETWORK_MASTER_KEY_TLV,         
                    emGetNetworkMasterKey(storage),
                    EMBER_ENCRYPTION_KEY_SIZE);
  assert(emGetNetworkKeySequence(&sequence));

  const CommissioningDataset *active = emGetActiveDataset();

  finger = emAddInt32uTlv(finger, COMMISSION_NETWORK_KEY_SEQUENCE_TLV, sequence);
  finger = emAddMeshLocalUlaTlv(finger);
  finger = emAddExtendedPanIdTlv(finger);
  finger = emAddNetworkNameTlv(finger);
  finger = emAddTlv(finger,
                    COMMISSION_SECURITY_POLICY_TLV,
                    active->securityPolicy,
                    3);
  finger = emAddTlv(finger, COMMISSION_PSKC_TLV, active->pskc, 16);
  finger = emAddTlv(finger,
                    COMMISSION_CHANNEL_MASK_TLV,
                    active->channelMask,
                    6);
  finger = emAddTlv(finger, 
                    COMMISSION_ACTIVE_TIMESTAMP_TLV,
                    active->timestamp,
                    8);
  assert(finger - payload <= sizeof(payload));

  Buffer keyBuffer = emFillBuffer(key, 16);
  if (keyBuffer == NULL_BUFFER) {
    return;
  }

  emInitStackCoapMessage(&coap, 
                         (EmberIpv6Address *) ipDestination,
                         payload, 
                         finger - payload);
  coap.transmitHandler = &emJoinerEntrustTransmitHandler;
  if (emSubmitCoapMessage(&coap, JOINER_ENTRUST_URI, keyBuffer)
      == EMBER_SUCCESS) {
    emLogLine(COMMISSION, "sent joiner entrust");
  } else {
    emLogLine(COMMISSION, "failed to send joiner entrust");
  }
#endif
}

//----------------------------------------------------------------
// Handlers for the various messages we receive.

#define MAX_COMMISSIONER_ID   64
#define MAX_PETITION_RESPONSE (3 * TLV_OVERHEAD + 64 + 2 + 1)

static bool addCommissioner(const uint8_t* id,
                            uint8_t idLength,
                            EmberNodeId nodeId)
{
  emCommissioner = emFillBuffer(id, idLength);
  if (emCommissioner == NULL_BUFFER) {
    // better would be to use the incoming message
    return false;
  }
  emCommissionerSessionId = nextSessionId++;
  emBorderRouterNodeId = nodeId;
  if (nodeId != emberGetNodeId()) {           // don't time ourselves out
    commissionerKeepalive();
  }
  emLogBytesLine(COMMISSION, "new commissioner 0x%2X", id, idLength, emBorderRouterNodeId);
  steeringDataLength = 0;
  sendCommissionerStatus();
  return true;
}

static void petitionHandler(CommissionMessage *message,
                            const uint8_t *payload,
                            uint16_t payloadLength,
                            Buffer header,
                            const EmberCoapRequestInfo *info)
{
  const uint8_t* id = message->commissionerIdTlv + 2;
  uint8_t idLength = message->commissionerIdTlv[1];
  uint8_t response[MAX_PETITION_RESPONSE];
  uint8_t *finger = response;
  EmberNodeId senderId;
  EmberCoapCode code;

  if (MAX_COMMISSIONER_ID < idLength) {
    idLength = MAX_COMMISSIONER_ID;
  }

  if (! emAmLeader) {
    code = EMBER_COAP_CODE_404_NOT_FOUND;
  } else if (! emIsGp16(info->remoteAddress.bytes, &senderId)) {
    code = EMBER_COAP_CODE_401_UNAUTHORIZED;
  } else if (emCommissioner != NULL_BUFFER) {
    finger = addStateTlv(finger, COMMISSION_FAILURE);
    finger = emAddTlv(finger,
                      COMMISSION_COMMISSIONER_ID_TLV,
                      emGetBufferPointer(emCommissioner),
                      emGetBufferLength(emCommissioner));
    code = EMBER_COAP_CODE_204_CHANGED;
  } else if (addCommissioner(id, idLength, senderId)) {
    idLength = emGetBufferLength(emCommissioner);
    finger = addStateTlv(finger, COMMISSION_SUCCESS);
    finger = emAddTlv(finger,
                      COMMISSION_COMMISSIONER_ID_TLV,
                      emGetBufferPointer(emCommissioner),
                      idLength);
    finger = emAddSessionIdTlv(finger);
    code = EMBER_COAP_CODE_204_CHANGED;
  } else {
    // no buffers - what to respond?
    code = EMBER_COAP_CODE_500_INTERNAL_SERVER_ERROR;
  }
  assert(finger - response <= sizeof(response));
  if (finger == response) {
    emberCoapRespondWithCode(info, code);
  } else {
    emberCoapRespondWithPayload(info, code, response, finger - response);
  }
}

// Max is three TLVs: the steering data plus two with two-byte values.
// Overhead of each TLV is two bytes.
#define MAX_COMM_GET_RESPONSE 256

#define TLV_PRESENT(message, tlv) \
  (((message)->tlvMask & COMM_TLV_BIT(tlv)) != 0)

static const uint32_t commGetTlvs =
  COMM_TLV_BIT(STEERING_DATA_TLV)
  | COMM_TLV_BIT(BORDER_ROUTER_LOCATOR_TLV)
  | COMM_TLV_BIT(SESSION_ID_TLV);
// To do: Add Joiner UDP Port to this list. The test harness currently does
// expect this to be present.

static void commGetDataHandler(CommissionMessage *message,
                               const uint8_t *payload,
                               uint16_t payloadLength,
                               Buffer header,
                               const EmberCoapRequestInfo *info)
{
  if (! emAmLeader) {
    emLogLine(COMMISSION, "got c/cg but am not leader");
    // should send some response code
    return;
  }

  uint32_t tlvsToSend = 0;
  if (message->getTlv == NULL) {
    tlvsToSend = commGetTlvs;
  } else {
    const uint8_t *readFinger = emNetworkDataPointer(message->getTlv);
    const uint8_t *end = readFinger + emNetworkDataSize(message->getTlv);
    for ( ; readFinger < end; readFinger++) {
      TlvData *data = getTlvData(*readFinger);
      if (data != NULL) {
        tlvsToSend |= BIT32(data->flagIndex);
      }
    }
  }
  uint8_t response[MAX_COMM_GET_RESPONSE];
  uint8_t *writeFinger = response;

  if (tlvsToSend & COMM_TLV_BIT(STEERING_DATA_TLV)) {
    writeFinger = emAddTlv(writeFinger, 
                           COMMISSION_STEERING_DATA_TLV,
                           steeringData,
                           steeringDataLength);
  }

  if (tlvsToSend & COMM_TLV_BIT(BORDER_ROUTER_LOCATOR_TLV)) {
    writeFinger = emAddInt16uTlv(writeFinger, 
                                 COMMISSION_BORDER_ROUTER_LOCATOR_TLV,
                                 emBorderRouterNodeId);
  }
        
  if (tlvsToSend & COMM_TLV_BIT(SESSION_ID_TLV)) {
    writeFinger = emAddInt16uTlv(writeFinger, 
                                 COMMISSION_SESSION_ID_TLV,
                                 emCommissionerSessionId);
  }

  if (tlvsToSend & COMM_TLV_BIT(JOINER_UDP_PORT_TLV)) {
    writeFinger = emAddInt16uTlv(writeFinger,
                                 COMMISSION_JOINER_UDP_PORT_TLV,
                                 emUdpCommissionPort);
  }

  assert(writeFinger - response <= sizeof(response));
  emberCoapRespondWithPayload(info,
                              EMBER_COAP_CODE_204_CHANGED, 
                              response, 
                              writeFinger - response);
}

static void failureResponse(const EmberCoapRequestInfo *info)
{
  emberCoapRespondWithPayload(info,
                              EMBER_COAP_CODE_204_CHANGED,
                              failureTlv,
                              sizeof(failureTlv));
}

void emSetSteeringData(const uint8_t *steering, uint8_t steeringLength)
{
  emLogLine(COMMISSION, "have steering data");
  steeringDataLength = steeringLength;
  MEMCOPY(steeringData, steering, steeringLength);
  sendCommissionerStatus();
#ifdef UNIX_HOST
  emberSetSteeringData(steering, steeringLength);
#endif
}

static void commSetDataHandler(CommissionMessage *message,
                               const uint8_t *payload,
                               uint16_t payloadLength,
                               Buffer header,
                               const EmberCoapRequestInfo *info)
{
  if ((! (emAmLeader
          && TLV_PRESENT(message, SESSION_ID_TLV)
          && message->sessionId == emCommissionerSessionId))
      || TLV_PRESENT(message, BORDER_ROUTER_LOCATOR_TLV)) { // Shouldn't be set
    failureResponse(info);
    return;
  }

  if (TLV_PRESENT(message, STEERING_DATA_TLV)) {
    emSetSteeringData(message->steeringDataTlv + 2, message->steeringDataTlv[1]);
  }

  if (TLV_PRESENT(message, COMMISSIONER_ID_TLV)) {
    emLogLine(COMMISSION,
              "got commissioner id %b",
              message->commissionerIdTlv + 2,
              message->commissionerIdTlv[1]);
    addCommissioner(message->commissionerIdTlv + 2,
                    message->commissionerIdTlv[1],
                    emBorderRouterNodeId);
  }

  if (TLV_PRESENT(message, COMMISSIONER_UDP_PORT_TLV)) {
    emLogLine(COMMISSION, "got UDP port %d", message->commissionerUdpPort);
    emUdpCommissionPort = message->commissionerUdpPort;
  }

  emberCoapRespondWithPayload(info,
                              EMBER_COAP_CODE_204_CHANGED, 
                              successTlv,
                              sizeof(successTlv));
}

//----------------------------------------------------------------
// Commissioner-initiated energy/active scans

static uint8_t scanResultDestination[16];
Buffer emComScanBuffer = NULL_BUFFER;   // only used for energy scans

// Active scan parameters
static uint32_t activeScanChannelMask;
static uint16_t activeScanPanId;
static uint32_t panIdConflictChannelMask;

// Energy scan parameters
static uint8_t energyScanCount;
static uint16_t energyScanPeriodMs;
static uint32_t energyScanChannelMask;
static uint8_t energyScanExponent;
static uint8_t energyScanResultCount;
static uint8_t energyScanNextChannel;

// Energy scan requests

#define SESSION_ID_TLV_LENGTH    4
#define CHANNEL_MASK_TLV_LENGTH  8
#define SCAN_COUNT_TLV_LENGTH    3
#define SCAN_PERIOD_TLV_LENGTH   4
#define SCAN_DURATION_TLV_LENGTH 4
#define CHANNEL_TLV_LENGTH       5
#define TIMESTAMP_TLV_LENGTH     10
#define PAN_ID_TLV_LENGTH        4
#define COMMISSION_PAN_ID_TLV_LENGTH 4
#define MAX_ENERGY_SCAN_DATA 100	// chosen arbitrarily

// As specified in 802.15.4, emberStartScan() takes a duration
// argument 'n' that is used as an exponent.  The scan duration is
// (2^n + 1) scan periods, where a scan period is 960 symbols and
// a symbol takes 16 microseconds.
// 
// (See stack/include/network-management.h::emberStartScan.)

static uint8_t scanMsToScanDuration(uint16_t scanDurationMs)
{
  // We multiply by 970 instead of 1000 to give to add 3% slop.
  // This avoids problems when the requester has done the arithmetic
  // slightly differently than we do.
  uint32_t scanDurationUs = scanDurationMs * 970;
  uint8_t n;
  for (n = 0; 
       n < 14 && (((1 << n) + 1) * (960 * 16)) < scanDurationUs;
       n++);
  return n;
}

static void comEnergyScanParamsInit(void)
{
  emComScanBuffer = NULL_BUFFER;
  energyScanCount = 0;
  energyScanPeriodMs = 0;
  energyScanChannelMask = 0;
  energyScanExponent = 0;
  energyScanResultCount = 0;
  energyScanNextChannel = 0;
  MEMSET(scanResultDestination, 0, 16);
}

static void scanEventHandler(Event *event);

static EventActions scanEventActions = {
  &emStackEventQueue,
  scanEventHandler,
  NULL,
  "com-scan"
};

Event emComScanEvent = { &scanEventActions, NULL };

static void scanEventHandler(Event *event)
{
  // Active scan uses this event to delay start by SCAN_DELAY_MS.
  if (activeScanChannelMask != 0 && activeScanPanId != 0) {
    emApiStartScan(EM_MGMT_ACTIVE_SCAN,
                   activeScanChannelMask & EMBER_ALL_802_15_4_CHANNELS_MASK, // Ensures a valid Channel Mask
                   DEFAULT_SCAN_DURATION);
    return;
  }

  uint8_t channel = energyScanNextChannel;
  while (0 < energyScanCount) {
    while (channel <= EMBER_MAX_802_15_4_CHANNEL_NUMBER
           && (BIT(channel) & energyScanChannelMask) == 0) {
      channel += 1;
    }
    if (channel <= EMBER_MAX_802_15_4_CHANNEL_NUMBER) {
      emLogLine(COAP, "scanning channel: %u", channel);
      energyScanNextChannel = channel + 1;
      emApiStartScan(EM_MGMT_ENERGY_SCAN, BIT(channel), energyScanExponent);
      return;
    } else {
      channel = EMBER_MIN_802_15_4_CHANNEL_NUMBER;
      energyScanCount -= 1;
    }
  }

  // No more scans needed - send the report.
  emLogLine(COMMISSION, "com energy scan complete, %u results.", 
            energyScanResultCount);

  uint8_t payload[CHANNEL_MASK_TLV_LENGTH + 2 + MAX_ENERGY_SCAN_DATA];
  uint8_t *finger = payload;
  finger = emAddChannelMaskTlv(finger, 0, energyScanChannelMask);
  *finger++ = COMMISSION_SCAN_ENERGY_LIST_TLV;
  *finger++ = energyScanResultCount;
  // We could pass emComScanBuffer to emSubmitCoapMessage() instead of
  // copying the data.  Doing that causes problems if the app code
  // doesn't expect linked payload buffers, so we copy the data.
  MEMCOPY(finger, emGetBufferPointer(emComScanBuffer), energyScanResultCount);
  finger += energyScanResultCount;
  assert(finger - payload <= sizeof(payload));

  CoapMessage message;
  emInitStackCoapMessage(&message, 
                         (EmberIpv6Address *) scanResultDestination,
                         payload, 
                         finger - payload);
  emSubmitCoapMessage(&message, MGMT_ENERGY_REPORT_URI, NULL_BUFFER);

  comEnergyScanParamsInit();
}

uint32_t emFetchChannelMask(const uint8_t *channelMaskTlv)
{
  // 0 = channnel page, 1 = length of channel mask, 2 = channel mask
  // Bit order is reversed in the TLV, swap it back.
  // Only channels 11-26 are valid.
  uint32_t v = emberFetchLowHighInt32u(channelMaskTlv + CHANNEL_MASK_INDEX);
  return (emReverseBitsInEachByte(v) & EMBER_ALL_802_15_4_CHANNELS_MASK);
}

static void energyScanHandler(CommissionMessage *message,
                              const uint8_t *payload,
                              uint16_t payloadLength,
                              Buffer header,
                              const EmberCoapRequestInfo *info)
{
  comEnergyScanParamsInit();
  emLogLine(COMMISSION,
            "Received request for an energy scan "
            "channelMask:0x%4X scanCount:%d scanPeriod:%d scanDuration:%d",
            emFetchChannelMask(message->channelMask),
            message->scanCount,
            message->scanPeriod,
            message->scanDuration);

  // TODO: we should wait until we know if we have started the scan
  // before replying.
  if (! emIsMulticastAddress(info->localAddress.bytes)) {
    emberCoapRespondWithCode(info, EMBER_COAP_CODE_204_CHANGED);
  }

  // We need to perform energy scan on a single channel at a time.  If the
  // channelMask specifies N channels, we need to do (scanCount * N) scans.

  uint8_t channelCount = 
    emBitCountInt32u(emFetchChannelMask(message->channelMask));
  uint8_t scanCount = message->scanCount;
  if (MAX_ENERGY_SCAN_DATA < scanCount * channelCount) {
    scanCount = MAX_ENERGY_SCAN_DATA / channelCount;
  }

  emComScanBuffer = emAllocateBuffer(scanCount * channelCount);

  if (emComScanBuffer != NULL_BUFFER) {
    energyScanChannelMask = emFetchChannelMask(message->channelMask);
    energyScanCount = scanCount;
    energyScanPeriodMs = message->scanPeriod;
    energyScanExponent = scanMsToScanDuration(message->scanDuration);
    energyScanResultCount = 0;
    MEMCOPY(scanResultDestination, info->remoteAddress.bytes, 16);
    energyScanNextChannel = EMBER_MIN_802_15_4_CHANNEL_NUMBER;
    emberEventSetDelayMs(&emComScanEvent, MS_TO_MSTICKS(SCAN_DELAY_MS));
  } else {
    emLogLine(COMMISSION, "Fatal error starting energy scan.");
  }
}

void emMgmtEnergyScanHandler(uint8_t channel, int8_t maxRssiValue)
{
  if (emComScanBuffer != NULL_BUFFER) {
    //emLogLine(COMMISSION, "%d\) channel: %d maxRssiValue: %d",
    //          energyScanResultCount,
    //          channel,
    //          maxRssiValue);
    ((uint8_t *) emGetBufferPointer(emComScanBuffer))[energyScanResultCount++]
        = maxRssiValue;
    emLogLine(COMMISSION, "Scan counter now: %u", energyScanResultCount);
  }
}

void emMgmtEnergyScanComplete(void)
{
  emberEventSetDelayMs(&emComScanEvent, energyScanPeriodMs);
}

static void energyReportHandler(CommissionMessage *message,
                                const uint8_t *payload,
                                uint16_t payloadLength,
                                Buffer header,
                                const EmberCoapRequestInfo *info)
{
  emLogLine(COMMISSION,
            "Received energy report for channelMask:0x%4X, list (%d): ",
            emFetchChannelMask(message->channelMask),
            message->scanEnergyListTlv[1]);
  uint8_t i;
  const int8_t *energyList = message->scanEnergyListTlv + 2;
  for (i = 0; i < message->scanEnergyListTlv[1]; i++) {
    emLog(COMMISSION, "%d ", energyList[i]);
  }
  emLogLine(COMMISSION, "\n");
  emberCoapRespondWithCode(info, EMBER_COAP_CODE_204_CHANGED);
}

//----------------------------------------------------------------
// Active scan requests

bool emComSendPanIdScanRequest(const int8u *destination,
                               uint32_t channelMask,
                               uint16_t panId)
{
  uint8_t payload[SESSION_ID_TLV_LENGTH
                  + CHANNEL_MASK_TLV_LENGTH
                  + PAN_ID_TLV_LENGTH];
  uint8_t *finger = payload;

  finger = emAddSessionIdTlv(finger);
  finger = emAddChannelMaskTlv(finger, 0, channelMask);
  finger = emAddInt16uTlv(finger, COMMISSION_PAN_ID_TLV, panId);

  assert(finger - payload <= sizeof(payload));
  CoapMessage message;
  emInitStackCoapMessage(&message, 
                         (EmberIpv6Address *) destination, 
                         payload, 
                         finger - payload);
  return (emSubmitCoapMessage(&message, MGMT_PANID_SCAN_URI, NULL_BUFFER)
          == EMBER_SUCCESS);
}

static void comActiveScanParamsInit(void)
{
  activeScanChannelMask = 0;
  activeScanPanId = 0;
  panIdConflictChannelMask = 0;
  MEMSET(scanResultDestination, 0, 16);
}

static void panIdScanHandler(CommissionMessage *message,
                             const uint8_t *payload,
                             uint16_t payloadLength,
                             Buffer header,
                             const EmberCoapRequestInfo *info)
{
  comActiveScanParamsInit();
  emLogLine(COMMISSION,
            "Received request for a pan ID scan panId:0x%2X channelMask:0x%4X",
            message->panId,
            emFetchChannelMask(message->channelMask));

  if (! emIsMulticastAddress(info->localAddress.bytes)) {
    emberCoapRespondWithCode(info, EMBER_COAP_CODE_204_CHANGED);
  }

  activeScanChannelMask = emFetchChannelMask(message->channelMask);
  activeScanPanId = message->panId;

  MEMCOPY(scanResultDestination,
          info->remoteAddress.bytes,
          16);

  emberEventSetDelayMs(&emComScanEvent, MS_TO_MSTICKS(SCAN_DELAY_MS));
}

void emMgmtActiveScanHandler(const EmberMacBeaconData *beaconData)
{
  if (beaconData->panId == activeScanPanId) {
    if (beaconData->channel == emGetActiveDataset()->channel[2]
        && beaconData->panId == emGetActiveDataset()->panId) {
      // Incoming beacon  is from a node on our own network. Ignore conflict.
      // So why do we bother with the scanning for conflicting PAN IDs
      // if we ignore any that conflict?
      // What we should be checking the extended PAN ID to see if this is
      // actually a beacon from our network.
    } else {
      panIdConflictChannelMask |= BIT(beaconData->channel);
    }
  }
}

void emMgmtActiveScanComplete(void)
{
  emLogLine(COMMISSION, "com active scan complete.");

  if (panIdConflictChannelMask != 0) {
    uint8_t payload[CHANNEL_MASK_TLV_LENGTH
                    + COMMISSION_PAN_ID_TLV_LENGTH];
    uint8_t *finger = payload;

    finger = emAddChannelMaskTlv(finger,
                                 0,
                                 panIdConflictChannelMask);
    finger = emAddInt16uTlv(finger,
                            COMMISSION_PAN_ID_TLV,
                            activeScanPanId);
    assert(finger - payload <= sizeof(payload));

    CoapMessage message;
    emInitStackCoapMessage(&message, 
                           (EmberIpv6Address *) scanResultDestination,
                           payload, 
                           finger - payload);
    emSubmitCoapMessage(&message, MGMT_PANID_CONFLICT_URI, NULL_BUFFER);
  }
  comActiveScanParamsInit();
}

// These should only be received by a commissioner, never by an ordinary

static void panIdConflictHandler(CommissionMessage *message,
                                 const uint8_t *payload,
                                 uint16_t payloadLength,
                                 Buffer header,
                                 const EmberCoapRequestInfo *info)
{
  emLogLine(COMMISSION,
            "Received notification of conflict for panId:0x%2X channelMask:0x%4X",
            message->panId,
            emFetchChannelMask(message->channelMask));
  emberCoapRespondWithCode(info, EMBER_COAP_CODE_204_CHANGED);
}

//----------------------------------------------------------------

static void relayRxHandler(CommissionMessage *message,
                           const uint8_t *payload,
                           uint16_t payloadLength,
                           Buffer header,
                           const EmberCoapRequestInfo *info)
{
  if (amCommissioner) {
    Ipv6Header ipHeader;
    MEMSET(&ipHeader, 0, sizeof(ipHeader));
    MEMCOPY(ipHeader.source + 8, message->joinerAddress, 8);
    ipHeader.sourcePort = message->joinerUdpPort;
    ipHeader.destinationPort = message->joinerRouterNodeId;
    ipHeader.transportPayload = 
      (uint8_t *) largeTlvData(message->joinerDtlsEncapTlv);
    ipHeader.transportPayloadLength =
      largeTlvLength(message->joinerDtlsEncapTlv);
    emIncomingJoinMessageHandler(header, &ipHeader, true);
  } else if (amGateway) {
    CoapMessage forward;
    MEMSET(&forward, 0, sizeof(forward));
    forward.type = COAP_TYPE_NON_CONFIRMABLE;
    forward.code = EMBER_COAP_CODE_POST;
    forward.transmitHandlerData =
      (void *)(unsigned long) commissionerConnectionHandle;
    forward.transmitHandler = &emCoapDtlsTransmitHandler;
    MEMMOVE(emGetBufferPointer(header), payload, payloadLength);
    emSetBufferLength(header, payloadLength);
    emSubmitCoapMessage(&forward, DTLS_RELAY_RECEIVE_URI, header);
  } else {
    // send 404?
  }
}

static void relayTxHandler(CommissionMessage *message,
                           const uint8_t *payload,
                           uint16_t payloadLength,
                           Buffer header,
                           const EmberCoapRequestInfo *info)
{
  uint8_t kek[16];
  DtlsConnection connection;

  // Save these because we are about to clobber the buffer holding them.
  if (message->tlvMask & COMM_TLV_BIT(JOINER_ROUTER_KEK_TLV)) {
    MEMCOPY(kek, message->joinerRouterKek, 16);
  }
  MEMCOPY(connection.udpData.remoteAddress, emFe8Prefix.contents, 8);
  MEMCOPY(connection.udpData.remoteAddress + 8, message->joinerAddress , 8);
  connection.udpData.remotePort = message->joinerUdpPort;

  if (message->tlvMask & COMM_TLV_BIT(JOINER_DTLS_ENCAP_TLV)) {
    uint16_t length = largeTlvLength(message->joinerDtlsEncapTlv);
    MEMMOVE(emGetBufferPointer(header),
            largeTlvData(message->joinerDtlsEncapTlv),
            length);
    emSetBufferLength(header, length);
    connection.udpData.flags = UDP_DTLS_JOIN;
    connection.udpData.localPort = emUdpJoinPort;
    connection.udpData.remotePort = message->joinerUdpPort;

    emSubmitDtlsPayload(&connection, header);
  }

  if (message->tlvMask & COMM_TLV_BIT(JOINER_ROUTER_KEK_TLV)) {
    emSendJoinerEntrust(&connection, kek);
  }
}

static void keepAliveHandler(CommissionMessage *message,
                             const uint8_t *payload,
                             uint16_t payloadLength,
                             Buffer header,
                             const EmberCoapRequestInfo *info)
{
  if (! (emAmLeader
         && message->sessionId == emCommissionerSessionId)) {
    return;
  }

  if (message->state == COMMISSION_FAILURE) {
    emRemoveCommissioner();
    sendCommissionerStatus();
  } else {
    commissionerKeepalive();
  }

  uint8_t response[MAX_LEADER_KEEPALIVE_RESPONSE];
  uint8_t *finger = response;
  uint8_t status = message->state;
  finger = addStateTlv(finger, status);

  assert(finger - response <= sizeof(response));
  emberCoapRespondWithPayload(info,
                              EMBER_COAP_CODE_204_CHANGED,
                              response, 
                              finger - response);
}

static void joinFinalHandler(CommissionMessage *message,
                             const uint8_t *payload,
                             uint16_t payloadLength,
                             Buffer header,
                             const EmberCoapRequestInfo *info)
{
  UdpConnectionData *data =
    emFindConnectionFromHandle((unsigned long) info->transmitHandlerData);
  if (data == NULL) {
    emberCoapRespondWithCode(info, EMBER_COAP_CODE_401_UNAUTHORIZED);
  } else {
    bool rejectJoinUrl = false;
    const int8u *provisioningUrlTlv = message->provisioningUrlTlv;
    if (emProvisioningUrlLength != 0) { // Want to filter on prov. url
      if (provisioningUrlTlv == NULL
          || (provisioningUrlTlv[0] != COMMISSION_PROVISIONING_URL_TLV)
          || (memcmp(emProvisioningUrl,
                     provisioningUrlTlv + 2,         // URL
                     provisioningUrlTlv[1]) != 0)) { // length
        rejectJoinUrl = true;
      }
    }
    data->flags |= UDP_DTLS_JOIN_KEK;
    if (rejectJoinUrl) {
      failureResponse(info);
    } else {
      emberCoapRespondWithPayload(info,
                                  EMBER_COAP_CODE_204_CHANGED,
                                  successTlv,
                                  sizeof(successTlv));
    }
    data->flags &= ~UDP_DTLS_JOIN_KEK;
  }
}

// This is not called by 'make test'.

static void joinAppHandler(CommissionMessage *message,
                           const uint8_t *payload,
                           uint16_t payloadLength,
                           Buffer header,
                           const EmberCoapRequestInfo *info)
{
  if (! emAmThreadCommissioner()) {
    emLogLine(COMMISSION, "got c/ja but am not commissioner");
    return;    
  }

  const int8u *provisioningUrlTlv = message->provisioningUrlTlv;
  if (emProvisioningUrlLength == 0
      || (provisioningUrlTlv != NULL
          && provisioningUrlTlv[1] == emProvisioningUrlLength
          && (MEMCOMPARE(emProvisioningUrl,
                         provisioningUrlTlv + 2,         // URL
                         emProvisioningUrlLength)
              == 0))) {
    emberCoapRespondWithPayload(info, 
                                EMBER_COAP_CODE_204_CHANGED,
                                successTlv,
                                sizeof(successTlv));
  } else {
    failureResponse(info);
  }
}

typedef void (*CoapHandler)(CommissionMessage *message,
                            const uint8_t *payload,
                            uint16_t payloadLength,
                            Buffer header,
                            const EmberCoapRequestInfo *info);

static UriHandler joinFinalUriHandler[] = {
  { JOIN_FINAL_URI, JOIN_FINAL_TLVS, (Func)joinFinalHandler },
  { NULL, 0, NULL }     // terminator
};

static UriHandler uriHandlers[] = {
  // Received by any node
  { MGMT_PANID_SCAN_URI,     PANID_SCAN_TLVS,     (Func)panIdScanHandler },
  { MGMT_ENERGY_SCAN_URI,    ENERGY_SCAN_TLVS,    (Func)energyScanHandler },

  // Received by leader
  { LEADER_PETITION_URI,     PETITION_TLVS,       (Func)petitionHandler },
  { LEADER_KEEP_ALIVE_URI,   KEEP_ALIVE_TLVS,     (Func)keepAliveHandler },
  { COMMISSIONER_GET_URI,    GET_DATA_TLVS,       (Func)commGetDataHandler },
  { COMMISSIONER_SET_URI,    SET_DATA_TLVS,       (Func)commSetDataHandler },
  
  // Received by joiner router
  { DTLS_RELAY_TRANSMIT_URI, RELAY_TX_TLVS,       (Func)relayTxHandler },

  // Received by border router and on-mesh commissioner
  { DTLS_RELAY_RECEIVE_URI,  RELAY_RX_TLVS,       (Func)relayRxHandler },

  // Received by commissioner
  { JOIN_APP_URI,            JOIN_APP_TLVS,       (Func)joinAppHandler },
  { MGMT_PANID_CONFLICT_URI, PANID_CONFLICT_TLVS, (Func)panIdConflictHandler },
  { MGMT_ENERGY_REPORT_URI,  ENERGY_REPORT_TLVS,  (Func)energyReportHandler },

  { NULL, 0, NULL }     // terminator
};

// BUG: We need to check that these messages originate from within the mesh.

static bool handleCommissionPost(const uint8_t *uri,
                                 const uint8_t *payload,
                                 uint16_t payloadLength,
                                 const EmberCoapRequestInfo *info,
                                 Buffer header,
                                 const UriHandler *handlers)
{
  UriHandler *handler = emLookupUriHandler(uri, handlers);
  if (handler == NULL) {
    return false;
  }
  
  CommissionMessage message;
  MEMSET(&message, 0, sizeof(message));
  if (parseCommissionMessage(&message, payload, payloadLength)
      && ((message.tlvMask & handler->tlvMask)
          == handler->tlvMask)) {
    ((CoapHandler) handler->handler)(&message, payload, payloadLength, header, info);
  } else {
    emLogLine(COMMISSION,
              "failed to parse commission message, "
              "we want: %u TLVs but they gave: %u, match: %u",
              handler->tlvMask,
              message.tlvMask,
              ((message.tlvMask & handler->tlvMask)
               == handler->tlvMask));
    emberCoapRespondWithCode(info, EMBER_COAP_CODE_400_BAD_REQUEST);
  }
  return true;
}

static void logCommissionMessage(bool tx,
                                 const char *handler,
                                 CoapType type,
                                 EmberCoapCode code,
                                 const uint8_t *uri)
{
  emLogLine(COMMISSION, "%s:%s %s %s %s",
            handler,
            tx ? "tx" : "rx",
            emGetCoapTypeName(type),
            uri,
            emGetCoapCodeName(code));
}

static bool joinFinalMessageHandler(EmberCoapCode code,
                                    const uint8_t *uri,
                                    EmberCoapReadOptions *options,
                                    uint8_t *payload,
                                    uint16_t payloadLength,
                                    const EmberCoapRequestInfo *info,
                                    Buffer header)
{
  return (code == EMBER_COAP_CODE_POST
          && handleCommissionPost(uri,
                                  payload,
                                  payloadLength,
                                  info,
                                  header,
                                  joinFinalUriHandler));
}

bool emCoapRequestHandler(EmberCoapCode code,
                          const uint8_t *uri,
                          EmberCoapReadOptions *options,
                          const uint8_t *payload,
                          uint16_t payloadLength,
                          const EmberCoapRequestInfo *info,
                          Buffer header)
{
  uint32_t contentFormat;
  if (emberReadIntegerOption(options,
                             EMBER_COAP_OPTION_CONTENT_FORMAT,
                             &contentFormat)
      && contentFormat != EMBER_COAP_CONTENT_FORMAT_OCTET_STREAM) {
    emLogLine(COAP,
              "Incoming message has invalid content format: %u",
              contentFormat);
    return false;
  }

  emLogBytes(COAP,
             "[Stack CoAP RX | %s/%s | from:",
             info->remoteAddress.bytes,
             16,
             emGetCoapTypeName(COAP_TYPE_CONFIRMABLE),
             emGetCoapCodeName(code));
  emLogBytes(COAP,
             " | uri: %s | port: %d | token:",
             info->token,
             info->tokenLength,
             uri,
             info->remotePort);
  emLogBytes(COAP,
             " | length: %d | payload:",
             payload,
             payloadLength,
             payloadLength);
  emLog(COAP, "]\n");

  // We don't actually know it is CONFIRMABLE, but why should we care?
  logCommissionMessage(false, "stack", COAP_TYPE_CONFIRMABLE, code, uri);
  return (code == EMBER_COAP_CODE_POST
          && (emHandleThreadDiagnosticMessage(uri, payload, payloadLength, info)
              || handleCommissionPost(uri, payload, payloadLength, info, header, uriHandlers)
              || emHandleAddressManagementPost(uri, payload, payloadLength, info)
              || emHandleCommissionDatasetPost(uri, payload, payloadLength, info)));
}

#define JOINER_ENTRUST_TLV_MASK \
  (COMM_TLV_BIT(NETWORK_MASTER_KEY_TLV) \
   | COMM_TLV_BIT(NETWORK_KEY_SEQUENCE_TLV) \
   | COMM_TLV_BIT(MESH_LOCAL_ULA_TLV) \
   | COMM_TLV_BIT(EXTENDED_PAN_ID_TLV) \
   | COMM_TLV_BIT(NETWORK_NAME_TLV))

void emCoapCommissionRequestHandler(EmberCoapCode code,
                                    uint8_t *uri,
                                    EmberCoapReadOptions *options,
                                    const uint8_t *payload,
                                    uint16_t payloadLength,
                                    const EmberCoapRequestInfo *info)
{
  CommissionMessage message;
  MEMSET(&message, 0, sizeof(message));

  logCommissionMessage(false, "KEK", COAP_TYPE_CONFIRMABLE, code, uri);

  if (strncmp((char *) uri, JOINER_ENTRUST_URI, 8) == 0
      && parseCommissionMessage(&message, payload, payloadLength)
      && ((message.tlvMask & JOINER_ENTRUST_TLV_MASK)
          == JOINER_ENTRUST_TLV_MASK)) {
    emLogLine(COMMISSION, "have active commissioner data");
    emCancelCommissioningKey();   // only one commissioning message is allowed
    emSetNetworkMasterKey(message.masterKey, message.keySequence);
    emSetDefaultGlobalPrefix(message.meshLocalUla);
    emSetExtendedPanId(message.extendedPanId);
    emJoinCommissionCompleteHandler(true);
  }
}

//----------------------------------------------------------------
// Sending commissioning commands

static bool sendToLeaderWithHandlerData(const char *uri,
                                        EmberCoapResponseHandler handler,
                                        const uint8_t *data,
                                        uint16_t dataLength,
                                        const uint8_t *payload,
                                        uint16_t payloadLength)
{
  EmberCoapSendInfo info;
  MEMSET(&info, 0, sizeof(info));
  info.localPort = emStackCoapPort;
  info.remotePort = emStackCoapPort;
  info.responseTimeoutMs = 12000;    // allow two retries
  info.responseAppData = data;
  info.responseAppDataLength = dataLength;

  EmberIpv6Address dest;
  if (emNodeType == EMBER_COMMISSIONER) {
    info.transmitHandlerData = (void *)(unsigned long) emParentConnectionHandle;
    info.transmitHandler = &emCoapDtlsTransmitHandler;
    MEMSET(&dest, 0, sizeof(dest));
  } else {
    emStoreGp16(LEADER_ANYCAST_ADDRESS, dest.bytes);
  }

  logCommissionMessage(true, 
                       "stack", 
                       COAP_TYPE_CONFIRMABLE,
                       EMBER_COAP_CODE_POST,
                       (const uint8_t *) uri);
  return (emberCoapPost(&dest, uri, payload, payloadLength, handler, &info)
          == EMBER_SUCCESS);
}

#define sendToLeader(uri, handler, payload, length) \
  (sendToLeaderWithHandlerData((uri), (handler), NULL, 0, (payload), (length)))

//----------------------------------------------------------------
// Messages received by a border router from an external commissioner.

typedef struct {
  uint8_t localSessionId;
  uint8_t tokenLength;
  uint8_t token[COAP_MAX_TOKEN_LENGTH];
  uint8_t encodedUri[10]; // this is just used for the test harness
  uint8_t encodedUriLength;
} RelayResponse;

static void forwardResponseToCommissioner(EmberCoapStatus status,
                                          EmberCoapCode code,
                                          EmberCoapReadOptions *options,
                                          uint8_t *payload,
                                          uint16_t payloadLength,
                                          EmberCoapResponseInfo *info)
{
  CommissionMessage message;
  MEMSET(&message, 0, sizeof(message));

  if (status != EMBER_COAP_MESSAGE_RESPONSE) {
    emLogLine(COMMISSION, "failure status for relayed message: %d", status);
  } else {
    RelayResponse *relayResponse = (RelayResponse *) info->applicationData;
    // TODO: check ->localSessionId
    CoapMessage forward;
    MEMSET(&forward, 0, sizeof(forward));
    forward.type = COAP_TYPE_CONFIRMABLE;
    forward.code = code;
    forward.payload = payload;
    forward.payloadLength = payloadLength;
    forward.transmitHandlerData =
      (void *)(unsigned long) commissionerConnectionHandle;
    forward.transmitHandler = &emCoapDtlsTransmitHandler;
    MEMCOPY(forward.token,
            relayResponse->token,
            relayResponse->tokenLength);
    forward.tokenLength = relayResponse->tokenLength;
    emSubmitCoapMessage(&forward, NULL, NULL_BUFFER);
  }
}

static void forwardToLeader(const uint8_t *payload,
                            uint16_t payloadLength,
                            const EmberCoapRequestInfo *info,
                            const char *uri)
{
  RelayResponse relayResponse;
  MEMSET(&relayResponse, 0, sizeof(relayResponse));
  relayResponse.localSessionId = 0; // TODO add localSessionId
  relayResponse.tokenLength = info->tokenLength;
  MEMCOPY(relayResponse.token, info->token, relayResponse.tokenLength);
  sendToLeaderWithHandlerData(uri,
                              &forwardResponseToCommissioner,
                              (uint8_t *) &relayResponse,
                              sizeof(relayResponse),
                              payload,
                              payloadLength);
}

static void brPetitionHandler(CommissionMessage *message,
                              const uint8_t *payload,
                              uint16_t payloadLength,
                              Buffer header,
                              const EmberCoapRequestInfo *info)
{
  const uint8_t* id = message->commissionerIdTlv + 2;
  uint8_t idLength = message->commissionerIdTlv[1];

  if (MAX_COMMISSIONER_ID < idLength) {
    idLength = MAX_COMMISSIONER_ID;
  }

  if (emAmLeader) {
    uint8_t response[MAX_PETITION_RESPONSE];
    uint8_t *finger = response;
    uint8_t status = COMMISSION_FAILURE;
    
    if (addCommissioner(id, idLength, emberGetNodeId())) {
      if (amGateway) {
        commissionerConnectionHandle = 
          (unsigned long) info->transmitHandlerData;
      }
      idLength = emGetBufferLength(emCommissioner);
      finger = emAddTlv(finger,
                        COMMISSION_COMMISSIONER_ID_TLV,
                        emGetBufferPointer(emCommissioner),
                        idLength);
      finger = emAddSessionIdTlv(finger);
      status = COMMISSION_SUCCESS;
    }

    finger = addStateTlv(finger, status);

    assert(finger - response <= sizeof(response));
    emberCoapRespondWithPayload(info, 
                                EMBER_COAP_CODE_204_CHANGED,
                                response,
                                finger - response);
  } else {
    forwardToLeader(payload, payloadLength, info, LEADER_PETITION_URI);
  }
}

static void brKeepAliveHandler(CommissionMessage *message,
                               const uint8_t *payload,
                               uint16_t payloadLength,
                               Buffer header,
                               const EmberCoapRequestInfo *info)
{
  if (emAmLeader) {
    uint8_t response[3]; // TODO: right size? make a define.
    uint8_t *finger = response;
    uint8_t status = COMMISSION_FAILURE;

    if (message->sessionId == emCommissionerSessionId) {
      if (message->state == COMMISSION_FAILURE) {
        emRemoveCommissioner();
        sendCommissionerStatus();
      } else {
        commissionerKeepalive();
      }
      status = message->state;
    }

    finger = addStateTlv(finger, status);

    assert(finger - response <= sizeof(response));
    emberCoapRespondWithPayload(info,
                                EMBER_COAP_CODE_204_CHANGED,
                                response,
                                finger - response);
  } else {
    forwardToLeader(payload, payloadLength, info, BORDER_KEEP_ALIVE_URI);
  }
}

// Thread 1.1 code.

static void brCommGetHandler(CommissionMessage *message,
                             const uint8_t *payload,
                             uint16_t payloadLength,
                             Buffer header,
                             const EmberCoapRequestInfo *info)
{
  if (emAmLeader) {
    commGetDataHandler(message, payload, payloadLength, header, info);
  } else {
    forwardToLeader(payload, payloadLength, info, COMMISSIONER_GET_URI);
  }
}

static void brCommSetHandler(CommissionMessage *message,
                             const uint8_t *payload,
                             uint16_t payloadLength,
                             Buffer header,
                             const EmberCoapRequestInfo *info)
{
  if (emAmLeader) {
    commSetDataHandler(message, payload, payloadLength, header, info);
  } else {
    forwardToLeader(payload, payloadLength, info, COMMISSIONER_SET_URI);
  }
}

static void brRelayTxHandler(CommissionMessage *message,
                             const uint8_t *payload,
                             uint16_t payloadLength,
                             Buffer header,
                             const EmberCoapRequestInfo *info)
{
  if (message->joinerRouterNodeId == emberGetNodeId()) {
    relayTxHandler(message, payload, payloadLength, header, info);
  } else {
    CoapMessage forward;
    emInitStackMl16CoapMessage(&forward, message->joinerRouterNodeId, NULL, 0);
    emStoreGp16(emberGetNodeId(), forward.localAddress.bytes);
    MEMMOVE(emGetBufferPointer(header), payload, payloadLength);
    emSetBufferLength(header, payloadLength);
    emSubmitCoapMessage(&forward, DTLS_RELAY_TRANSMIT_URI, header);
  }
}

static const UriHandler borderRouterHandlers[] = {
  { BORDER_PETITION_URI,     PETITION_TLVS,   (Func)brPetitionHandler },
  { BORDER_KEEP_ALIVE_URI,   KEEP_ALIVE_TLVS, (Func)brKeepAliveHandler },
  { COMMISSIONER_GET_URI,    GET_DATA_TLVS,   (Func)brCommGetHandler },
  { COMMISSIONER_SET_URI,    NO_TLVS,         (Func)brCommSetHandler },
  { DTLS_RELAY_TRANSMIT_URI, RELAY_TX_TLVS,   (Func)brRelayTxHandler },
  { NULL, 0, NULL }     // terminator
};

bool emBorderRouterMessageHandler(EmberCoapCode code,
                                  const uint8_t *uri,
                                  EmberCoapReadOptions *options,
                                  uint8_t *payload,
                                  uint16_t payloadLength,
                                  const EmberCoapRequestInfo *info,
                                  Buffer header)
{
  logCommissionMessage(false, "border", COAP_TYPE_CONFIRMABLE, code, uri);
  return (code == EMBER_COAP_CODE_POST
          && (handleCommissionPost(uri,
                                   payload,
                                   payloadLength,
                                   info,
                                   header,
                                   borderRouterHandlers)
              || emHandleCommissionDatasetPost(uri, payload, payloadLength, info)));
}

static const UriHandler commissionerHandlers[] = {
  { DTLS_RELAY_RECEIVE_URI, RELAY_RX_TLVS, (Func)relayRxHandler },
// Not needed, 'make test'.  If it is needed we need to add a test for it.
//  { JOIN_APP_URI,           0,           joinAppHandler },
  { NULL, 0, NULL }     // terminator
};

bool emCommissionerMessageHandler(EmberCoapCode code,
                                  const uint8_t *uri,
                                  EmberCoapReadOptions *options,
                                  uint8_t *payload,
                                  uint16_t payloadLength,
                                  const EmberCoapRequestInfo *info,
                                  Buffer header)
{
  logCommissionMessage(false, "commissioner", COAP_TYPE_CONFIRMABLE, code, uri);
  return (code == EMBER_COAP_CODE_POST
          && handleCommissionPost(uri,
                                  payload,
                                  payloadLength,
                                  info,
                                  header,
                                  commissionerHandlers));
}

// This passes to the CoAP handler messages that arrive over a DTLS
// connection.

void emHandleJoinDtlsMessage(EmberUdpConnectionData *connection,
                             uint8_t *packet,
                             uint16_t length,
                             Buffer buffer)
{
  Ipv6Header ipHeader;
  CoapRequestHandler handler;
  MEMSET(&ipHeader, 0, sizeof(ipHeader));
  ipHeader.transportPayload = packet;
  ipHeader.transportPayloadLength = length;
  // These should not be needed
  MEMCOPY(ipHeader.destination, connection->localAddress, 16);
  MEMCOPY(ipHeader.source, connection->remoteAddress, 16);
  ipHeader.destinationPort = connection->localPort;
  ipHeader.sourcePort = connection->remotePort;

  if (emNodeType == EMBER_COMMISSIONER
      && connection->localPort == emUdpCommissionPort) {
    // The only message we get here in 'make test' is c/rx.
    handler = &emCommissionerMessageHandler;
  } else if (amGateway
             && connection->localPort == emUdpCommissionPort) {
    // 'make test' gets c/cp, c/ca. c/cs, and c/tx messages here.
    handler = &emBorderRouterMessageHandler;
  } else if (connection->localPort == emUdpJoinPort) {
    // The only message we should be getting here is the Join Final.
    handler = &joinFinalMessageHandler;
  } else {
    // These are the data packets that are relayed from the joiner router,
    // where the 'port' is the joiner router's node ID.  There is a bug
    // here, because it is possible for node IDs and ports to collide.
    // These should also only get Join Final messages.
    handler = &joinFinalMessageHandler;
  }

  emCoapStackIncomingMessageHandler(buffer,
                                    &ipHeader,
                                    &emCoapDtlsTransmitHandler,
                                    ((void *) (unsigned long)
                                     connection->connection),
                                    handler);
}

//----------------------------------------------------------------

static void forwardJoinerDtlsEncap(const uint8_t *uriPath,
                                   bool tx,
                                   EmberNodeId destination,
                                   EmberNodeId joinerRouterId,
                                   const uint8_t *joinerIid,
                                   uint16_t joinerPort,
                                   const uint8_t *kek,
                                   Buffer payload)
{
  uint8_t tlvs[(2 + 2) + (2 + 8) + (2 + 2) + (2 + 16) + 4];
  uint8_t *finger = tlvs;
  uint16_t payloadLength = emTotalPayloadLength(payload);

  finger = emAddInt16uTlv(finger, COMMISSION_JOINER_UDP_PORT_TLV, joinerPort);
  finger = emAddInt16uTlv(finger,
                          COMMISSION_JOINER_ROUTER_LOCATOR_TLV,
                          joinerRouterId);
  finger = emAddTlv(finger, COMMISSION_JOINER_ADDRESS_TLV, joinerIid, 8);

  if (kek != NULL) {
    finger = emAddTlv(finger, COMMISSION_JOINER_ROUTER_KEK_TLV, kek, 16);
  }

  if (0 < payloadLength) {
    *finger++ = COMMISSION_JOINER_DTLS_ENCAP_TLV;
    if (payloadLength < 255) {
      *finger++ = payloadLength;
    } else {
      *finger++ = 255;
      *finger++ = HIGH_BYTE(payloadLength);
      *finger++ = LOW_BYTE(payloadLength);
    }
  }

  assert(finger - tlvs <= sizeof(tlvs));

  CoapMessage message;
  if (tx && emNodeType == EMBER_COMMISSIONER) {
    emInitStackCoapMessage(&message, NULL, tlvs, finger - tlvs);
    message.transmitHandlerData =
      (void *) (unsigned long) emParentConnectionHandle;
    message.transmitHandler = &emCoapDtlsTransmitHandler;
  } else if (! tx && amGateway) {
    emInitStackCoapMessage(&message, NULL, tlvs, finger - tlvs);
    message.transmitHandlerData =
      (void *) (unsigned long) commissionerConnectionHandle;
    message.transmitHandler = &emCoapDtlsTransmitHandler;
  } else {
    emInitStackMl16CoapMessage(&message, destination, tlvs, finger - tlvs);
  }
  message.type = COAP_TYPE_NON_CONFIRMABLE;
  emSubmitCoapMessage(&message, uriPath, payload);
}

void emForwardToCommissioner(PacketHeader header, Ipv6Header *ipHeader)
{
  if (emBorderRouterNodeId == 0xFFFE) {
    return;
  }

  if (joinerRelayCount >= JOINER_RELAY_RATE_LIMIT_COUNT) {
    return;
  } else if (joinerRelayCount == 0) {
    emberEventSetDelayMs(&joinerRelayRateLimitEvent, 
                         JOINER_RELAY_RATE_LIMIT_INTERVAL_MS);
  }
  joinerRelayCount += 1;

  // Get rid of the IPv6 header.
  MEMMOVE(emGetBufferPointer(header),
          ipHeader->transportPayload,
          ipHeader->transportPayloadLength);
  emSetBufferLength(header, ipHeader->transportPayloadLength);
  forwardJoinerDtlsEncap(DTLS_RELAY_RECEIVE_URI,
                         false,
                         emBorderRouterNodeId,
                         emberGetNodeId(),
                         ipHeader->source + 8,
                         ipHeader->sourcePort,
                         NULL,
                         header);
}

void emForwardToJoiner(const uint8_t *joinerIid,
                       uint16_t joinerPort,
                       EmberNodeId joinerRouterNodeId,
                       const uint8_t *kek,
                       Buffer payload)
{
  forwardJoinerDtlsEncap(DTLS_RELAY_TRANSMIT_URI,
                         true,
                         joinerRouterNodeId,
                         joinerRouterNodeId,
                         joinerIid,
                         joinerPort,
                         kek,
                         payload);
}

static void joinFinResponseHandler(EmberCoapStatus status,
                                   EmberCoapCode code,
                                   EmberCoapReadOptions *options,
                                   uint8_t *payload,
                                   uint16_t payloadLength,
                                   EmberCoapResponseInfo *info)
{
  // There may not be agreement on the response code.  Thread originally
  // specified 205, but the CoAP RFC says that it should be 204.  So we
  // accept both.

  if (status != EMBER_COAP_MESSAGE_RESPONSE) {
    emLogLine(COAP, "join fin reply: failure: %d", status);
  } else if (code == EMBER_COAP_CODE_204_CHANGED
             || code == EMBER_COAP_CODE_205_CONTENT) {
    CommissionMessage message;
    MEMSET(&message, 0, sizeof(message));
    if (! parseCommissionMessage(&message, payload, payloadLength)) {
      emLogLine(COMMISSION, "failed to parse commission message response");
    } else {
      bool success = false;

      // Anything that is not failure is success.
      if ((message.tlvMask & COMM_TLV_BIT(STATE_TLV)) == 0
          || message.state != COMMISSION_FAILURE) {
        success = true;
      }

      emAppCommissionCompleteHandler(success);
    }
  }
}

uint8_t emProvisioningUrl[64] = { 0 };
uint8_t emProvisioningUrlLength = 0;

bool emStartAppCommissioning(bool joinerSession)
{
  CoapMessage message;
  MEMSET(&message, 0, sizeof(message));
  uint8_t tlvs[6 * 2 + 1 + 14 + 2 + 6 + 7 + 6
               + 2 + 64]; // Provisioning URL
  uint8_t *finger = tlvs;

  if (joinerSession) {
    uint8_t state = COMMISSION_SUCCESS;              // accept
    finger = addStateTlv(finger, state);
  }

  // Include the nuls at the end to make debugging easier.
  finger = emAddTlv(finger, COMMISSION_VENDOR_NAME_TLV,       "", 1);
  finger = emAddTlv(finger, COMMISSION_VENDOR_MODEL_TLV,      "", 1);
  finger = emAddTlv(finger, COMMISSION_VENDOR_SW_VERSION_TLV, "", 1);
  finger = emAddTlv(finger, COMMISSION_VENDOR_DATA_TLV,       "", 1);
  // Fixed length.
  finger = emAddTlv(finger, COMMISSION_VENDOR_STACK_VERSION_TLV, "      ", 6);
  if (emProvisioningUrlLength != 0) {
    finger = emAddTlv(finger,
                      COMMISSION_PROVISIONING_URL_TLV,
                      emProvisioningUrl,
                      emProvisioningUrlLength);
  }

  assert(finger <= tlvs + sizeof(tlvs));
  if (joinerSession) {
    // Can't use Ml16 version until we get the mesh-local address after joining.
    emInitStackCoapMessage(&message, NULL, tlvs, finger - tlvs);
    message.transmitHandlerData = 
      (void *) (unsigned long) emParentConnectionHandle;
    message.transmitHandler = &emCoapDtlsTransmitHandler;
  } else {
    emInitStackMl16CoapMessage(&message,
                               COMMISSIONER_ANYCAST_ADDRESS,
                               tlvs, 
                               finger - tlvs);
  }
  // We don't have any special app commissioning, so use the same response
  // handler for both JOIN_FINAL and JOIN_APP for now.
  message.responseHandler = &joinFinResponseHandler;
  message.responseTimeoutMs = 12000;    // allow two retries
                                                        
  return (emSubmitCoapMessage(&message,
                              (joinerSession
                               ? JOIN_FINAL_URI
                               : JOIN_APP_URI),
                              NULL_BUFFER)
          == EMBER_SUCCESS);
}

bool emAmThreadCommissioner(void)
{
  return amCommissioner;
}

//----------------------------------------------------------------
// Discovery requests and responses

void emProcessIncomingDiscoveryResponse(const uint8_t *mleTlv,
                                        const uint8_t *senderLongId,
                                        PacketHeader header)
{
  CommissionMessage message;
  MEMSET(&message, 0, sizeof(message));
  emLogBytesLine(COAP, "response", emNetworkDataPointer(mleTlv),
                           emNetworkDataSize(mleTlv));
  if (! (emParseTlvMessage(&message,
                           &message.tlvMask,
                           emNetworkDataPointer(mleTlv),
                           emNetworkDataSize(mleTlv),
                           &getTlvData)
         && message.tlvMask & COMM_TLV_BIT(DISCOVERY_RESPONSE_TLV)
         // don't try to process earlier versions, not that there are any
         && (THREAD_1_1_PROTOCOL_VERSION 
             <= (message.discoveryResponseFlags >> 4)))) {
    loseVoid(MLE);
    return;
  }

  MacBeaconData fullBeaconData;
  MEMSET(&fullBeaconData, 0, sizeof(MacBeaconData));
  EmberMacBeaconData *beaconData = &fullBeaconData.apiBeaconData;

  beaconData->protocolId = THREAD_PROTOCOL_ID;
  beaconData->version = message.discoveryResponseFlags >> 4;

  MEMCOPY(beaconData->longId, senderLongId, EUI64_SIZE);
  MEMCOPY(beaconData->networkId, 
          message.networkNameTlv + 2,
          message.networkNameTlv[1]);
  MEMCOPY(beaconData->extendedPanId, message.extendedPanId, EXTENDED_PAN_ID_SIZE);

  beaconData->channel = emHeaderGetInt8u(header, QUEUE_STORAGE_CHANNEL_INDEX);
  beaconData->lqi = emHeaderGetInt8u(header, QUEUE_STORAGE_LQI_INDEX);
  beaconData->rssi = (int8_t) emHeaderGetInt8u(header, QUEUE_STORAGE_RSSI_INDEX);
  beaconData->panId = emMacSourcePanId(header);

  if (message.discoveryResponseFlags 
      & DISCOVERY_RESPONSE_NATIVE_COMMISSIONER_FLAG) {
    fullBeaconData.allowingCommission = true;
    fullBeaconData.commissionPort = message.commissionerUdpPort;
  }

  if (message.steeringDataTlv != NULL) {
    // If a steering data TLV is included, then check for the joiner
    // and commissioner UDP ports in the discovery response.
    beaconData->allowingJoin = true;
    fullBeaconData.joinPort = message.joinerUdpPort;
    beaconData->steeringDataLength = message.steeringDataTlv[1];
    MEMCOPY(beaconData->steeringData,
            message.steeringDataTlv + 2,
            beaconData->steeringDataLength);
  }
  emProcessIncomingJoinBeacon(&fullBeaconData);
}

//----------------------------------------------------------------
// Application Interface

void emApiGetCommissioner(void)
{
  uint8_t *tlv = emFindNetworkDataTlv(NWK_DATA_COMMISSION);
  if (tlv == NULL) {
    emApiCommissionerStatusHandler(EMBER_NO_COMMISSIONER, NULL, 0);
  } else {
    CommissionMessage message;
    MEMSET(&message, 0, sizeof(message));
    assert(emParseTlvMessage(&message,
                             &message.tlvMask,
                             emNetworkDataPointer(tlv),
                             emNetworkDataSize(tlv),
                             &getTlvData));
    if ((message.tlvMask & (COMM_TLV_BIT(BORDER_ROUTER_LOCATOR_TLV)
                            | COMM_TLV_BIT(SESSION_ID_TLV)))
        == (COMM_TLV_BIT(BORDER_ROUTER_LOCATOR_TLV)
            | COMM_TLV_BIT(SESSION_ID_TLV))) {
      // Should check this against our local expectations.
      uint16_t flags = EMBER_HAVE_COMMISSIONER;
      if ((emberGetNodeId() == message.borderRouterNodeId)
          && ! amGateway
          && amCommissioner) {
        flags |= EMBER_AM_COMMISSIONER;
      }
      uint8_t steeringDataLength = (message.steeringDataTlv == NULL
                                    ? 0
                                    : emNetworkDataSize(message.steeringDataTlv));
      const uint8_t *steeringData = NULL;
      if (steeringDataLength != 0) {
        steeringData = emNetworkDataPointer(message.steeringDataTlv);
        if (steeringData != NULL && !emIsMemoryZero(steeringData, steeringDataLength)) {
          // nonzero length and data not all zeroes -> joining enabled
          flags |= EMBER_JOINING_ENABLED;
          if (!emMemoryByteCompare(steeringData, steeringDataLength, 0xFF)) {
            // data not all ones -> joining restricted by steering
            flags |= EMBER_JOINING_WITH_EUI_STEERING;
          }
        }
      }
      emApiCommissionerStatusHandler(flags, NULL, 0);
    } else {
      emApiCommissionerStatusHandler(EMBER_NO_COMMISSIONER, NULL, 0);
    }
  }
}

// Not static only because thread test app calls it.

void emRemoveCommissioner(void)
{
  if (emCommissioner != NULL_BUFFER) {
    emCommissioner = NULL_BUFFER;
    amCommissioner = false;
    emberEventSetInactive(&commissionerTimeoutEvent);
    emCommissionerSessionId = nextSessionId++;
  }
}

//----------------------------------------------------------------
// From here on down is code that is only used on a commissioner.
//----------------------------------------------------------------

void emApiSetJoiningMode(EmberJoiningMode mode, uint8_t length)
{
  // Default to disallowing joining.
  if (mode == EMBER_NO_JOINING
      || EMBER_JOINING_ALLOW_EUI_STEERING < mode) {
    steeringDataLength = 0;
  } else if (mode == EMBER_JOINING_ALLOW_ALL_STEERING) {
    steeringDataLength = 1;  // no point in sending more than one FF byte
    steeringData[0] = 0xFF;
  } else {
    if (COMMISSION_MAX_STEERING_DATA_SIZE < length) {
      length = COMMISSION_MAX_STEERING_DATA_SIZE;
    }
    steeringDataLength = length;
    MEMSET(steeringData, 0, sizeof(steeringData));
  }
}

void emApiAddSteeringEui64(const EmberEui64 *eui64)
{
  if (amCommissioner) {
    addToSteeringData(eui64->bytes);
  }
}

static void genericResponseHandler(EmberCoapStatus status,
                                   EmberCoapCode code,
                                   EmberCoapReadOptions *options,
                                   uint8_t *payload,
                                   uint16_t payloadLength,
                                   EmberCoapResponseInfo *info)
{
  if (status == EMBER_COAP_MESSAGE_RESPONSE) {
    emLogLine(COMMISSION, "command reply: success");
  } else {
    emLogLine(COMMISSION, "command reply: failure: %d", status);
  }
}

void emApiSendSteeringData(void)
{
  EmberStatus status = EMBER_ERR_FATAL;

  if (! amCommissioner) {
    // do nothing
  } else if (! emAmLeader) {
    // Could check that at least we think we are the commissioner.
    uint8_t payload[300];
    uint8_t *finger = payload;

    finger = emAddSessionIdTlv(finger);
    finger = emAddTlv(finger,
                    COMMISSION_STEERING_DATA_TLV,
                    steeringData,
                    steeringDataLength);
    if (sendToLeader(COMMISSIONER_SET_URI,
                     &genericResponseHandler,
                     payload,
                     finger - payload)) {
      status = EMBER_SUCCESS;
    }
  } else {
    status = EMBER_SUCCESS;
    // Could check that this is actually a change.
    sendCommissionerStatus();
  }
  emApiSendSteeringDataReturn(status);
}

static void sendKeepalive(uint8_t status)
{
  uint8_t payload[7];
  uint8_t *finger = payload;
  finger = emAddSessionIdTlv(finger);
  finger = addStateTlv(finger, status);
  assert(finger - payload <= sizeof(payload));
  sendToLeader(LEADER_KEEP_ALIVE_URI,
               // Should check status of the response.
               &genericResponseHandler,
               payload,
               finger - payload);
}

static void petitionResponseHandler(EmberCoapStatus status,
                                    EmberCoapCode code,
                                    EmberCoapReadOptions *options,
                                    uint8_t *payload,
                                    uint16_t payloadLength,
                                    EmberCoapResponseInfo *info)
{
  CommissionMessage message;
  MEMSET(&message, 0, sizeof(message));

  if (status != EMBER_COAP_MESSAGE_RESPONSE) {
    emLogLine(COMMISSION, "failure status for petition: %d", status);
  } else if (! parseCommissionMessage(&message, payload, payloadLength)) {
    emLogLine(COMMISSION, "failed to parse commission message response");
  } else if (! (message.tlvMask & COMM_TLV_BIT(STATE_TLV))) {
    emLogLine(COMMISSION, "tn/mc/lp has no state");
  } else if (amGateway) {
    RelayResponse *relayResponse = (RelayResponse *) info->applicationData;
    // TODO: check ->localSessionId
    CoapMessage forward;
    MEMSET(&forward, 0, sizeof(forward));
    forward.type = COAP_TYPE_CONFIRMABLE;
    forward.code = code;
    forward.payload = payload;
    forward.payloadLength = payloadLength;
    forward.transmitHandlerData =
      (void *) (unsigned long) commissionerConnectionHandle;
    forward.transmitHandler = &emCoapDtlsTransmitHandler;
    MEMCOPY(forward.token,
            relayResponse->token,
            relayResponse->tokenLength);
    forward.tokenLength = relayResponse->tokenLength;
    emSubmitCoapMessage(&forward, NULL, NULL_BUFFER);
  } else if (message.state == COMMISSION_SUCCESS) {
    emCommissionerSessionId = message.sessionId;  // BUG: check that TLV is present
    emLogLine(COMMISSION, "am commissioner");
    amCommissioner = true;
    if (! emberEventIsScheduled(&commissionerTimeoutEvent)) {
      emberEventSetDelayMs(&commissionerTimeoutEvent,
                           COMMISSIONER_INITIAL_KEEPALIVE_INTERVAL_MS);
    }
    printCommissionMessage(&message);
  } else {
    emLogLine(JOIN, "am not commissioner");
    printCommissionMessage(&message);
  }
}

void emApiBecomeCommissioner(const uint8_t *id, uint8_t idLength)
{
  bool success = false;
  if (! emAmLeader) {
    uint8_t payload[2 + 256];

    emAddTlv(payload, COMMISSION_COMMISSIONER_ID_TLV, id, idLength);
    success = sendToLeader(LEADER_PETITION_URI,
                           &petitionResponseHandler,
                           payload,
                           2 + idLength);
  } else {
    success = addCommissioner(id, idLength, emberGetNodeId());
    amCommissioner = success;
  }
  emApiBecomeCommissionerReturn(success ? EMBER_SUCCESS : EMBER_ERR_FATAL);
}

// We have a race condition with the previous call.  We need a local copy
// of what we think the state, as well as the state from the network data.
// There is currently no 'stop being commissioner' message.  All we can
// do is stop sending the keepalive (which we don't send anyway).

void emApiStopCommissioning(void)
{
  if (amCommissioner) {
    emRemoveCommissioner();
    if (emNodeType == EMBER_COMMISSIONER) {
      emExternalCommissionerKeepAlive(COMMISSION_FAILURE);
    } else {
      sendKeepalive(COMMISSION_FAILURE);
    }
    sendCommissionerStatus();
  }
}

// Test function for commission-tester
void emSendGetData(const uint8_t *tlvs,
                   uint8_t count,
                   EmberCoapResponseHandler handler)
{
  uint8_t payload[2 + 8];

  if (8 < count) {
    count = 8;
  }
  emAddTlv(payload, COMMISSION_GET_TLV, tlvs, count);
  sendToLeader(COMMISSIONER_GET_URI, handler, payload, 2 + count);
}

bool emComSendEnergyScanRequest(const int8u *destination,
                                uint32_t channelMask,
                                uint8_t scanCount,
                                uint16_t scanPeriod,
                                uint16_t scanDuration)
{
  uint8_t payload[SESSION_ID_TLV_LENGTH
                  + CHANNEL_MASK_TLV_LENGTH
                  + SCAN_COUNT_TLV_LENGTH
                  + SCAN_PERIOD_TLV_LENGTH
                  + SCAN_DURATION_TLV_LENGTH];
  uint8_t *finger = payload;

  finger = emAddSessionIdTlv(finger);
  finger = emAddChannelMaskTlv(finger, 0, channelMask);
  finger = emAddTlv(finger, COMMISSION_SCAN_COUNT_TLV, &scanCount, 1);
  finger = emAddInt16uTlv(finger, COMMISSION_SCAN_PERIOD_TLV, scanPeriod);
  finger = emAddInt16uTlv(finger, COMMISSION_SCAN_DURATION_TLV, scanDuration);

  assert(finger - payload <= sizeof(payload));
  CoapMessage message;
  emInitStackCoapMessage(&message, 
                         (EmberIpv6Address *) destination, 
                         payload, 
                         finger - payload);
  return (emSubmitCoapMessage(&message, MGMT_ENERGY_SCAN_URI, NULL_BUFFER)
          == EMBER_SUCCESS);
}

//----------------------------------------------------------------
// Messages sent by an external commissioner.

static void commissionerPetitionResponseHandler(EmberCoapStatus status,
                                                EmberCoapCode code,
                                                EmberCoapReadOptions *options,
                                                uint8_t *payload,
                                                uint16_t payloadLength,
                                                EmberCoapResponseInfo *info)
{
  if (status != EMBER_COAP_MESSAGE_RESPONSE) {
    emLogLine(COMMISSION, "failure status for petition: %d", status);
  } else if (code == EMBER_COAP_CODE_204_CHANGED
             || code == EMBER_COAP_CODE_205_CONTENT) {
    // There may not be agreement on the response code.  Thread originally
    // specified 205, but the CoAP RFC says that it should be 204.  So we
    // accept both.
    CommissionMessage message;
    MEMSET(&message, 0, sizeof(message));
    if (! parseCommissionMessage(&message, payload, payloadLength)) {
      emLogLine(COMMISSION, "failed to parse commission message response");
    } else if (! (message.tlvMask & COMM_TLV_BIT(STATE_TLV))) {
      emLogLine(COMMISSION, "tn/mc/lp has no state");
    } else if (message.state == COMMISSION_SUCCESS) {
      emCommissionerSessionId = message.sessionId;  // BUG: check that TLV is present
      emLogLine(COMMISSION, "am commissioner");
      amCommissioner = true;
      if (! emberEventIsScheduled(&commissionerTimeoutEvent)) {
        emberEventSetDelayMs(&commissionerTimeoutEvent,
                             COMMISSIONER_KEEPALIVE_INTERVAL_MS);
      }
      printCommissionMessage(&message);
    } else {
      emLogLine(JOIN, "am not commissioner");
      printCommissionMessage(&message);
    }
  }
}

void emBecomeExternalCommissioner(const uint8_t *id, uint8_t idLength)
{
  uint8_t payload[2 + 256];
  uint8_t *finger = payload;

  // BUG: This should be done when we are told to allow joining.
  emSetThreadPermitJoin(true);
  finger = emAddTlv(finger, COMMISSION_COMMISSIONER_ID_TLV, id, idLength);
  sendToLeader(BORDER_PETITION_URI,
               &commissionerPetitionResponseHandler,
               payload,
               finger - payload);
}

void emExternalCommissionerKeepAlive(bool accept)
{
  uint8_t payload[(2 + 1) + (2 + 2)];
  uint8_t *finger = payload;

  uint8_t state = (accept ? COMMISSION_SUCCESS : COMMISSION_FAILURE);
  finger = addStateTlv(finger, state);
  finger = emAddSessionIdTlv(finger);
  sendToLeader(BORDER_KEEP_ALIVE_URI,
               &commissionerPetitionResponseHandler,
               payload,
               finger - payload);
}
//----------------------------------------------------------------
// Glue code required to deliver external commissioner messages via
// the host (which acts as the proxy) to the stack for processing.

void emProcessCommissionerProxyMessage(Buffer buffer, 
                                       uint8_t *payload, 
                                       uint16_t length)
{
  Ipv6Header ipHeader;
  uint8_t handle = payload[0];
  if (length < 2) {
    return;
  }
  MEMSET(&ipHeader, 0, sizeof(ipHeader));
  ipHeader.transportPayload = payload + 1;
  ipHeader.transportPayloadLength = length - 1;
  emCoapStackIncomingMessageHandler(buffer,
                                    &ipHeader,
                                    &emCoapDtlsTransmitHandler,
                                    (void *) (unsigned long) handle,
                                    &emBorderRouterMessageHandler);
}

//----------------------------------------------------------------

#ifdef EMBER_TEST

static const char *commissionTlvNames[] = {
  "channel",
  "pan_id",
  "extended_pan_id",
  "network_name",
  "commissioning_credential",
  "network_master_key",
  "network_key_sequence",
  "mesh_local_ula",
  "steering_data",
  "border_router_locator",
  "commissioner_id",
  "session_id",
  "security_policy",
  "get",
  "timestamp",
  "0F?",
  "state",
  "joiner_dtls_encap",
  "joiner_udp_port",
  "joiner_address",
  "joiner_router_locator",
  "joiner_router_kek",
  "16?", "17?", "18?", "19?", "1A?", "1B?", "1C?", "1D?", "1E?", "1F?",
  "provisioning_url",
  "vendor_name",
  "vendor_model",
  "vendor_sw_version",
  "vendor_data",
  "vendor_stack_version",
  "26?", "27?", "28?", "29?", "2A?", "2B?", "2C?", "2D?", "2E?", "2F?",
  "udp_encap",
  "ipv6_address"
};

const char *emCommissionTlvName(uint8_t type)
{
  if (type <= MAX_TLV_TYPE) {
    return commissionTlvNames[type];
  } else {
    return "out_of_range";
  }
}

#endif
