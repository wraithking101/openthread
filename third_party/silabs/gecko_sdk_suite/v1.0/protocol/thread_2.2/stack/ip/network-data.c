/*
 * File: network-data.c
 * Description: Shared network data, distributed via MLE.
 *
 * Copyright 2013 by Silicon Laboratories. All rights reserved.             *80*
 */

#include <string.h>
#include "core/ember-stack.h"
#include "hal/hal.h"
#include "framework/buffer-management.h"
#include "framework/event-queue.h"
#include "jit.h"
#include "address-management.h"
#include "rip.h"
#include "mle.h"
#include "ip-address.h"
#include "dhcp.h"
#include "dispatch.h"
#include "ip-header.h"
#include "context-table.h"
#include "local-server-data.h"
#include "tls/debug.h"
#include "zigbee/join.h"
#include "zigbee/child-data.h"
#include "routing/neighbor/neighbor.h"
#include "phy/phy.h"
#include "routing/util/retry.h"
#include "address-cache.h"
#include "network-fragmentation.h"
#include "commission-dataset.h"
#include "commission.h"

#include "network-data.h"

// A struct holding all of the information about a change to the
// network data.

typedef struct {
  uint8_t *writeFinger;       // write new data here
  uint8_t *writeLimit;        // but don't go past here
  uint16_t contextIdMask;     // assigned context IDs
  EmberNodeId server;         // the node whose data is being changed
  bool removeChildren;        // remove the children of the server as well
  bool stableChange;          // has the stable data changed
  bool temporaryChange;       // has the temporary data changed
} UpdateData;

//----------------------------------------------------------------
// forward declarations

static void networkDataEventHandler(Event *event);
HIDDEN NetworkDataStatus compareLeaderData(const uint8_t *mleLeaderTlv);
HIDDEN bool updateServerData(EmberNodeId server,
                             uint8_t *newTlvs,
                             uint8_t newTlvsLength,
                             bool removeChildren);
static uint8_t *findMatchingPrefixTlv(const uint8_t *prefix, 
                                      uint8_t lengthInBits,
                                      uint8_t *tlvs,
                                      uint16_t tlvsLength);
static void cleanUpPrefixTlv(uint8_t *prefixTlv, UpdateData *updateData, bool isNew);
static uint8_t *removeNodeId(uint8_t *tlv,
                             uint8_t *writeFinger,
                             UpdateData *updateData);
static uint8_t findNextId(uint16_t *maskLoc);
static uint8_t *writeContextData(uint8_t *finger,
                                 uint8_t id,
                                 bool stable,
                                 uint8_t length);

//----------------------------------------------------------------
// Holds the contents of the newest NWK_DATA_TLV received.
// Must be copied when sending so that changes, such timeout changes
// for the 6LoWPAN context values, don't invalidate the UDP checksum.
// Because the network data may be empty, we need a separate booelan
// to see if we actually have it.
static bool haveNetworkData = false;
Buffer emNetworkData = NULL_BUFFER;

// Handy utility for getting a pointer to the network data.
uint8_t *emGetNetworkData(uint16_t *lengthLoc)
{
  if (! haveNetworkData) {
    *lengthLoc = 0xFFFF;
    return NULL;
  } else if (emNetworkData == NULL_BUFFER) {
    *lengthLoc = 0;
    return NULL;
  } else {
    *lengthLoc = emGetBufferLength(emNetworkData);
    return emGetBufferPointer(emNetworkData);
  }
}

static uint8_t *copySubTlv(uint8_t *to, uint8_t *from, EmberNodeId anycast)
{
  uint8_t size = emNetworkDataTlvSize(from);
  uint8_t typeByte = emNetworkDataTypeByte(from);
  if (typeByte & NWK_DATA_STABLE_FLAG) {
    MEMCOPY(to, from, size);
    uint8_t *end = to + size;
    if (typeByte == emNetworkDataStableType(NWK_DATA_HAS_ROUTE)
        || typeByte == emNetworkDataStableType(NWK_DATA_BORDER_ROUTER)) {
      uint8_t dataSize = (typeByte == emNetworkDataStableType(NWK_DATA_HAS_ROUTE)
                        ? 3
                        : 4);
      for (to = emNetworkDataPointer(to); to < end; to += dataSize) {
        emberStoreHighLowInt16u(to, anycast);
      }
    }
    to = end;
  }
  return to;
}

#define BIG_SIZE_DELIMITER 0xFF

uint8_t emNetworkDataTlvOverhead(const uint8_t *finger)
{
  return (finger[1] == BIG_SIZE_DELIMITER
          ? 4
          : 2);
}

uint8_t emNetworkDataLengthOverhead(uint16_t length)
{
  return (length < BIG_SIZE_DELIMITER
          ? 2
          : 4);
}

uint16_t emNetworkDataSize(const uint8_t *finger)
{
  return (finger[1] == BIG_SIZE_DELIMITER
          ? emberFetchHighLowInt16u(finger + 2)
          : finger[1]);
}

void emSetNetworkDataSize(uint8_t *finger, uint16_t length)
{
  if (length < BIG_SIZE_DELIMITER) {
    finger[1] = length;
  } else {
    finger[1] = BIG_SIZE_DELIMITER;
    emberStoreHighLowInt16u(finger + 2, length);
  }
}

uint16_t emNetworkDataTlvSize(const uint8_t *finger)
{
  return emNetworkDataSize(finger) + emNetworkDataTlvOverhead(finger);
}

uint16_t emCopyNetworkData(uint8_t *to, bool stableOnly)
{
  if (emNetworkData == NULL_BUFFER) {
    return 0;
  } else if (! stableOnly) {
    uint16_t length = emGetBufferLength(emNetworkData);
    MEMCOPY(to, emGetBufferPointer(emNetworkData), length);
    return length;
  } else {
    uint8_t *readFinger = emGetBufferPointer(emNetworkData);
    uint8_t *end = readFinger + emGetBufferLength(emNetworkData);
    uint8_t *writeFinger = to;
    
    while (readFinger < end) {
      uint8_t size = emNetworkDataTlvSize(readFinger);
      switch (emNetworkDataTypeByte(readFinger)) {

      case emNetworkDataStableType(NWK_DATA_PREFIX): {
        uint8_t *newTlv = writeFinger;
        uint8_t *read = readFinger;
        uint8_t preludeSize = prefixTlvHeaderSize(read);
        MEMCOPY(writeFinger, read, preludeSize);
        EmberNodeId anycast;
        uint8_t *contextTlv = emFindPrefixSubTlv(read, NWK_DATA_6LOWPAN_ID);
        uint8_t *borderRouterTlv = emFindPrefixSubTlv(read, NWK_DATA_BORDER_ROUTER);
        uint8_t borderRouterFlags = 0;
        if (borderRouterTlv != NULL) {
          borderRouterFlags = borderRouterTlv[4];
        }
        if (contextTlv == NULL
            || (borderRouterFlags & EMBER_BORDER_ROUTER_SLAAC_FLAG)) {
          anycast = 0xFFFE;
        } else {
          anycast = (0xFC00 | (contextTlv[NWK_DATA_6LOWPAN_ID_ID_OFFSET]
                               & PREFIX_6CO_ID_MASK));
        }
        read += preludeSize;
        writeFinger += preludeSize;
        while (read < readFinger + size) {
          writeFinger = copySubTlv(writeFinger, read, anycast);
          read += emNetworkDataTlvSize(read);
        }
        uint8_t newTlvSize = writeFinger - emNetworkDataPointer(newTlv);
        if (newTlvSize + 2 == preludeSize) {
          writeFinger = newTlv;         // newTlv was empty, ignore it
        } else {
          emSetNetworkDataSize(newTlv, newTlvSize);
        }
        break;
      }
      }
      readFinger += size;
    }
    return writeFinger - to;
  }
}

// Cached to save looking it up every time.
bool emAmLeader = false;

// Leader data
bool emHaveLeader = false;
HIDDEN uint8_t leaderData[EMBER_NETWORK_DATA_LEADER_SIZE];

void emAddLeaderData(uint8_t **toLoc)
{
  assert(emHaveLeader);
  MEMCOPY(*toLoc, leaderData, EMBER_NETWORK_DATA_LEADER_SIZE);
  *toLoc += EMBER_NETWORK_DATA_LEADER_SIZE;
}

void emSetLeaderData(const uint8_t *data)
{
  if (! emHaveLeader
      || MEMCOMPARE(leaderData, data, EMBER_NETWORK_DATA_LEADER_SIZE) != 0) {
    MEMCOPY(leaderData, data, EMBER_NETWORK_DATA_LEADER_SIZE);
    emHaveLeader = true;
    emApiLeaderDataHandler(leaderData);
  }
}

//----------------------------------------------------------------
// Keeping our copy of the network data up-to-date.

enum {
  DATA_IN_SYNC,         // No need to do anything.
  DATA_REQUESTED,       // We have sent a request to a neighbor.
  DATA_TX,              // Waiting to broadcast newly received data.
};

HIDDEN uint8_t dataState = DATA_IN_SYNC;

static EventActions networkDataEventActions = {
  &emStackEventQueue,
  networkDataEventHandler,
  NULL,         // no marking function is needed
  "network data"
};

static Event networkDataEvent = { &networkDataEventActions, NULL };

void emNoteNetworkDataVersion(uint8_t *ipSource,
                              uint8_t neighborIndex,
                              EmberNodeId macShortSource,
                              const uint8_t *leaderData)
{
  if (emNodeType != EMBER_LURKER
      && dataState != DATA_REQUESTED
      && compareLeaderData(leaderData) == NWK_DATA_NEWER
      && neighborIndex != 0xFF // does parent count?
      && emLinkIsEstablished(neighborIndex)) {
    emLogLine(POLL, "received newer network data version");
    // Up to one second of jitter because we are responding
    // to a broadcast, then 500ms for the data to arrive.
    uint16_t delay = expRandom(10);
    emSendNetworkDataRequest(ipSource, false, delay);
    emberEventSetDelayMs(&networkDataEvent, delay + 500);
    dataState = DATA_REQUESTED;
  }
}

void emAnnounceNewNetworkData(bool stableDataChanged)
{
  if (emAmRouter() && dataState != DATA_TX) {
    dataState = DATA_TX;

    if (emAmLeader) {
//      simPrint("incrementing version, stable %d", stableDataChanged);
      leaderData[NWK_DATA_VERSION_OFFSET] += 1;
      if (stableDataChanged) {
        leaderData[NWK_DATA_STABLE_VERSION_OFFSET] += 1;
      }
    }

    if (stableDataChanged) {
      emSetAllSleepyChildFlags(CHILD_HAS_OLD_NETWORK_DATA, true);
    }

    // simPrint("-> DATA_TX");
    // On the leader, add a brief delay in case there are other, related
    // network data changes.  On other nodes, we are likely responding
    // to a broadcast, so add 1<<9ms (about 1/2 second) of jitter.
    emberEventSetDelayMs(&networkDataEvent,
                         (emAmLeader
                          ? 100
                          : expRandom(9)));
  }
}

static void noteNetworkDataChange(bool stableDataChanged)
{
  if (emNodeType == EMBER_LURKER) {
    return;
  }

//  simPrint("noteNetworkDataChange()");
  emVerifyLocalServerData();

  uint8_t *finger = emGetBufferPointer(emNetworkData);
  uint8_t *end = finger + emGetBufferLength(emNetworkData);
  uint8_t *temporaryCommissionTlv = NULL;

  while (finger < end) {
    uint8_t type = emNetworkDataType(finger);
    uint16_t length = emNetworkDataSize(finger);
    uint8_t *data = emNetworkDataPointer(finger);
    if (type == NWK_DATA_PREFIX) {
      uint8_t *prefix = finger + NWK_DATA_PREFIX_BITS_OFFSET;
      uint8_t prefixLengthInBits = finger[NWK_DATA_PREFIX_LENGTH_OFFSET];
      uint8_t *addressServers = emFindPrefixSubTlv(finger, NWK_DATA_BORDER_ROUTER);
      uint8_t domainId = finger[NWK_DATA_PREFIX_DOMAIN_OFFSET];
      if (addressServers != NULL) {
        emNoteGlobalPrefix(prefix,
                           prefixLengthInBits,
                           addressServers[4],
                           domainId);
      } else {
        GlobalAddressEntry *entry 
          = emGetPrefixAddressEntry(prefix, prefixLengthInBits, true);
        if (entry != NULL) {
          LocalServerFlag localConfigurationFlags =
            entry->localConfigurationFlags;

          // delete it
          emLogLine(NETWORK_DATA, "delete");
          emDeleteGlobalAddressEntry(prefix, prefixLengthInBits);

          // notify the application layer
          if ((localConfigurationFlags & EMBER_GLOBAL_ADDRESS_SLAAC)
              == EMBER_GLOBAL_ADDRESS_SLAAC) {
            emApiSlaacServerChangeHandler(prefix, prefixLengthInBits, false);
          } else if ((localConfigurationFlags & EMBER_GLOBAL_ADDRESS_DHCP)
                     == EMBER_GLOBAL_ADDRESS_DHCP) {
            emApiDhcpServerChangeHandler(prefix, prefixLengthInBits, false);
          } else {
            // what to do?
          }

          // log it
          emLogBytesLine(NETWORK_DATA, 
                         "prefix no longer exists: ", 
                         prefix, 
                         EMBER_BITS_TO_BYTES(prefixLengthInBits));
        }
      }
    } else if (type == NWK_DATA_COMMISSION) {
      if (! emNetworkDataIsStable(finger)) {
        temporaryCommissionTlv = finger;
      }
    }
    finger = data + length;
  }

  emApiNetworkDataChangeHandler(emGetBufferPointer(emNetworkData),
                                emGetBufferLength(emNetworkData));
  emAnnounceNewNetworkData(stableDataChanged);

  // Disabled because it is not in Thread, but something like it needs to be.
  // } else if (emNodeTypeIsSleepy()
  //            && emJoinState == JOIN_COMPLETE) {
  //   // If we're a sleepy end device, our parent sent us a network data
  //   // update.  Send it back to them immediately so they know we have the
  //   // correct data.
  //   Disabled for Thread interop.
  //   uint8_t parentAddress[16];
  //   emGetParentIpv6Address((EmberIpv6Address *) parentAddress);
  //   emSendNetworkData(parentAddress, NO_OPTIONS);

  emSetCommissionState(temporaryCommissionTlv);

  emLogNetworkData(NETWORK_DATA);
}

void emStopNetworkData(void)
{
  emberEventSetInactive(&networkDataEvent);
}

static void networkDataEventHandler(Event *event)
{
  switch (dataState) {
  case DATA_IN_SYNC:
    // all done
    break;
  case DATA_REQUESTED:
    dataState = DATA_IN_SYNC;
    break;
  case DATA_TX:
    emSendNetworkData(emFf02AllNodesMulticastAddress.contents, NO_OPTIONS);
    // simPrint("DATA_TX -> DATA_IN_SYNC");
    dataState = DATA_IN_SYNC;
    break;
  }
}

void emApiGetNetworkDataTlv(uint8_t type, uint8_t index)
{
  uint8_t count = index;
  uint8_t *tlv = emFindNextNetworkDataTlv(type, NULL);
  while (count-- > 0 && tlv != NULL) {
    tlv = emFindNextNetworkDataTlv(type, tlv);
  }
  uint8_t version = emGetCompleteNetworkData()
                    ? leaderData[NWK_DATA_VERSION_OFFSET]
                    : leaderData[NWK_DATA_STABLE_VERSION_OFFSET];
  uint8_t tlvLength = ((tlv == NULL)
                       ? 0
                       : (emNetworkDataSize(tlv) + emNetworkDataTlvOverhead(tlv)));
  emApiGetNetworkDataTlvReturn(type, index, version, tlv, tlvLength);
}

void emNetworkDataInit(void)
{
  dataState = DATA_IN_SYNC;
  haveNetworkData = false;
  emNetworkData = NULL_BUFFER;
}

//----------------------------------------------------------------
// Utilities

// Returns a pointer to the next TLV.

static uint8_t *networkDataNextTlv(uint8_t *tlv)
{
  return emNetworkDataPointer(tlv) + emNetworkDataSize(tlv);
}

uint8_t *emFindTlv(uint8_t type, uint8_t *finger, const uint8_t *end)
{
  // fprintf(stderr, "[emFindTlv(%d, ...)]\n", type);
  // dump("data", finger, end - finger);
  while (finger < end) {
    // fprintf(stderr, "  [have %d]\n", emNetworkDataType(finger));
    if (type == emNetworkDataType(finger)) {
      return finger;
    }
    finger = networkDataNextTlv(finger);
  }
  return NULL;
}

uint8_t *emFindNetworkDataSubTlv(uint8_t *outerTlv,
                                 uint8_t outerHeaderSize,
                                 uint8_t innerType)
{
  return emFindTlv(innerType,
                   emNetworkDataPointer(outerTlv) + outerHeaderSize,
                   networkDataNextTlv(outerTlv));
}

uint8_t *emFindPrefixSubTlv(uint8_t *prefixTlv, uint8_t innerType)
{
  return emFindNetworkDataSubTlv(prefixTlv,
                                 (prefixTlvHeaderSize(prefixTlv)
                                  - emNetworkDataTlvOverhead(prefixTlv)),
                                 innerType);
}

uint8_t *emFindNextNetworkDataTlv(uint8_t type, uint8_t *start)
{
  if (emNetworkData == NULL_BUFFER) {
    return NULL;
  } else {
    uint8_t *contents = emGetBufferPointer(emNetworkData); 
    if (start == NULL) {
      start = contents;
    } else {
      start = networkDataNextTlv(start);
    }
    return emFindTlv(type, start, contents + emGetBufferLength(emNetworkData));
  }
}

uint8_t *emFindTlvWithTypeByte(uint8_t typeByte)
{
  if (emNetworkData == NULL_BUFFER) {
    return NULL;
  } else {
    uint8_t *finger = emGetBufferPointer(emNetworkData);     
    uint8_t *end = finger + emGetBufferLength(emNetworkData);
    while (finger < end) {
      if (typeByte == emNetworkDataTypeByte(finger)) {
        return finger;
      }
      finger = networkDataNextTlv(finger);
    }
    return NULL;
  }
}

EmberNodeId emGetLeaderNodeId(void)
{
  if (emHaveLeader) {
    return emRipIdToShortMacId(leaderData[NWK_DATA_LEADER_RIP_OFFSET]);
  } else {
    return 0xFFFE;
  }
}

//----------------------------------------------------------------
// Leader data.

// Verify that the leader data matches what we have.

bool emLeaderDataMatches(const uint8_t *mleLeaderData)
{
  return (emHaveLeader
          && (MEMCOMPARE(leaderData + NWK_DATA_ISLAND_ID_OFFSET,
                         mleLeaderData + NWK_DATA_ISLAND_ID_OFFSET,
                         ISLAND_ID_SIZE)
               == 0));
}

// Initializing the leader's data.  Make the standard prefix, putting
// in the local long and short IDs as the leader's, and add gateway
// information if we are a gateway.

void emInitializeLeaderNetworkData(void)
{
  emFillIslandId(leaderData + NWK_DATA_ISLAND_ID_OFFSET, 64);
  leaderData[NWK_DATA_LEADER_RIP_OFFSET] = emLocalRipId();
  leaderData[NWK_DATA_VERSION_OFFSET]        = halCommonGetRandom();
  leaderData[NWK_DATA_STABLE_VERSION_OFFSET] = halCommonGetRandom();
  emSetIslandId(leaderData + NWK_DATA_ISLAND_ID_OFFSET);

  emAmLeader = true;
  emHaveLeader = true;
  haveNetworkData = true;
  emInitializeLeaderServerData();

  // We either just started a network or just started our own fragment.
  // In the latter case we may have children that need the new network data.
  if (emChildCount() != 0) {
    emAnnounceNewNetworkData(true);
  }
}

// Called only if there is no existing commission TLV.
void emAddCommissionTlv(uint8_t *tlv) 
{
  uint8_t tlvLength = emNetworkDataTlvSize(tlv);
  uint16_t oldLength = emGetBufferLength(emNetworkData);
  uint16_t newLength = oldLength + tlvLength;
  Buffer dataBuffer = emAllocateBuffer(newLength);
  if (dataBuffer == NULL_BUFFER) {
    return;
  }
  uint8_t *from = emGetBufferPointer(emNetworkData);
  uint8_t *to = emGetBufferPointer(dataBuffer);
  MEMCOPY(to, from, oldLength);
  MEMCOPY(to + oldLength, tlv, tlvLength);
  emNetworkData = dataBuffer;
  haveNetworkData = true;
  noteNetworkDataChange(false);
}

//----------------------------------------------------------------
// Returns the added/adjusted TLV, or NULL if it could not be made.
// If tlvDataLength is zero, then the tlv is removed and NULL is
// returned (setting the size to zero cannot fail).
//
// If the new length is no longer than the old length, then the TLV is
// shortened in place.  Otherwise a new, longer buffer is allocated
// and the TLV is given the new space.

uint8_t *emSetNetworkDataTlvLength(uint8_t *tlv, uint16_t tlvDataLength)
{
  uint16_t oldTlvLength = emNetworkDataTlvSize(tlv);
  uint16_t newTlvLength = (tlvDataLength == 0xFFFF
                           ? 0
                           : tlvDataLength + emNetworkDataTlvOverhead(tlv));
  uint16_t oldLength = emGetBufferLength(emNetworkData);
  uint16_t newLength = (oldLength + newTlvLength) - oldTlvLength;
  bool needNewBuffer = oldLength < newLength;
  Buffer dataBuffer;
  // fprintf(stderr, "[new length (%d + %d) - %d = %d]\n",
  //         oldLength, newTlvLength, oldTlvLength, newLength);

  if (newLength == 0) {
    emNetworkData = NULL_BUFFER;
    return NULL;
  } else if (needNewBuffer) {
    dataBuffer = emAllocateBuffer(newLength);
    if (dataBuffer == NULL_BUFFER) {
      return NULL;
    }
  } else if (newTlvLength == oldTlvLength) {
    return tlv;
  } else {
    dataBuffer = emNetworkData;
  }

  uint8_t *from = emGetBufferPointer(emNetworkData);
  uint8_t *to = emGetBufferPointer(dataBuffer);
  uint8_t *result = NULL;
  uint16_t prefixLength = tlv - from;

  if (needNewBuffer) {
    // Copy prefix and TLV to their new location.
    MEMMOVE(to,
            from,
            (prefixLength
             + (oldTlvLength < newTlvLength
                ? oldTlvLength
                : newTlvLength)));
  }

  to += prefixLength;
  if (newTlvLength != 0) {
    result = to;
  }

  // Copy everything after tlv to its new location.
  MEMMOVE(to + newTlvLength,
          from + (prefixLength + oldTlvLength),
          oldLength - (prefixLength + oldTlvLength));

  if (newLength < oldLength) {
    emSetBufferLength(dataBuffer, newLength);
  }
  if (result != NULL) {
    emSetNetworkDataSize(result, tlvDataLength);
  }
  emNetworkData = dataBuffer;
  haveNetworkData = true;
  return result;
}

// Also removes the outer TLV is it has no other values.

void emRemoveNetworkDataSubTlv(uint8_t *outerTlv, uint8_t *innerTlv)
{
  uint8_t *data = emNetworkDataPointer(outerTlv);
  uint8_t *outerEnd = data + emNetworkDataSize(outerTlv);
  uint8_t *innerEnd = networkDataNextTlv(innerTlv);
  uint16_t newSize = (emNetworkDataSize(outerTlv)
                      - emNetworkDataTlvSize(innerTlv));
  MEMMOVE(innerTlv, innerEnd, outerEnd - innerEnd);
  emSetNetworkDataTlvLength(outerTlv, newSize);
}

// Remove any data from the old ID and add it for the new.
// 
// TODO (maybe): There may be cases where it would be better to
// replace the old ID with the new one before applying the new data.
// For example, it might keep 6LoWPAN IDs from changing.  But it
// would add code and so might not be worth it.

void emProcessNetworkDataChangeRequest(EmberNodeId serverNodeId,
                                       EmberNodeId oldServerNodeId,
                                       uint8_t *data,
                                       uint16_t dataLength,
                                       bool removeChildren)
{
  if (emAmLeader) {
    emLogBytesLine(NETWORK_DATA, "ND change request from %u:",
                   data, dataLength, serverNodeId);
    if (oldServerNodeId != 0xFFFE) {
      updateServerData(oldServerNodeId, NULL, 0, false);
    }
    updateServerData(serverNodeId, data, dataLength, removeChildren);
  }
}

//----------------------------------------------------------------
// Process (possibly) new incoming network data.
//
// Returns one of the following:
//  NWK_DATA_NEWER
//  NWK_DATA_OLDER
//  NWK_DATA_OTHER_FRAGMENT

// This relies on MLE and network data using the same data format, so that the
// MLE TLV has the same format as the network-data leader TLV.

HIDDEN NetworkDataStatus compareLeaderData(const uint8_t *mleLeaderData)
{
  if (mleLeaderData == NULL) {
    emLogLine(NETWORK_DATA, "older leader data (it's NULL)");
    // not there, ignore it
    // fprintf(stderr, "[no data]\n");
    return NWK_DATA_OLDER;
  } else if (! (emHaveLeader && haveNetworkData)) {
    emLogLine(NETWORK_DATA, "newer leader data (we don't have any)");
    // any data is better than nothing - what if we are a rebooted leader?
    // fprintf(stderr, "[better than nothing]\n");
    return NWK_DATA_NEWER;
  } else {
    if (MEMCOMPARE(mleLeaderData + NWK_DATA_ISLAND_ID_OFFSET,
                   leaderData + NWK_DATA_ISLAND_ID_OFFSET,
                   ISLAND_ID_SIZE)
        != 0) {
      // From a different fragment - will need to check fragment
      // priority and switch if necessary.
      // fprintf(stderr, "[other fragment]\n");
      return NWK_DATA_OTHER_FRAGMENT;
    } else if (timeGTorEqualInt8u(leaderData[NWK_DATA_VERSION_OFFSET],
                                  mleLeaderData[NWK_DATA_VERSION_OFFSET])) {
      // emLogLine(NETWORK_DATA,
      //           "older leader data, our version: %u, theirs: %u",
      //           leaderData[NWK_DATA_VERSION_OFFSET],
      //           mleLeaderData[NWK_DATA_VERSION_OFFSET]);
      // fprintf(stderr, "[older]\n");
      return NWK_DATA_OLDER;
    } else {
      emLogLine(NETWORK_DATA, "newer leader data");
      // fprintf(stderr, "[newer]\n");
      return NWK_DATA_NEWER;
    }
  }   
}  

// This relies on MLE and network data using the same TLV format.
void emUpdateNetworkData(Buffer mleMessage,
                         uint8_t *mleLeaderData,
                         uint8_t *mleNetworkDataTlv,
                         uint8_t *ipSource)
{
  if (compareLeaderData(mleLeaderData) == NWK_DATA_NEWER) {
//    simPrint("updating network data");
    emLogLine(NETWORK_DATA, "updating network data");

    // verify that the island ID matches
    assert(emAttachState == ATTACH_ROUTER_REBOOT
           || emLeaderDataMatches(mleLeaderData));

    uint16_t newLength = emNetworkDataSize(mleNetworkDataTlv);
    uint8_t *newData = emNetworkDataPointer(mleNetworkDataTlv);

    bool stableDataChanged = false;
    if (timeGTint8u(mleLeaderData[NWK_DATA_STABLE_VERSION_OFFSET],
                    leaderData[NWK_DATA_STABLE_VERSION_OFFSET])) {
      stableDataChanged = true;
    }
    // Avoid the problem of not being able to allocate a buffer by reusing
    // the incoming message.  On RTOS, incoming fragmented messages are stored
    // in pbufs as indirect buffers.  However long term storage of network
    // data should be in the stack heap, not the lwip heap.
    if (false // Temporary to fix weird fragmented child response bug in 5.6.1
        && mleMessage != NULL_BUFFER
        && emPointsIntoHeap(emGetBufferPointer(mleMessage))) {
      emNetworkData = mleMessage;
      MEMMOVE(emGetBufferPointer(emNetworkData), newData, newLength);
      if (newLength < emGetBufferLength(emNetworkData)) {
        emSetBufferLength(emNetworkData, newLength);
      }
    } else {
      Buffer temp = emFillBuffer(newData, newLength);
      if (temp == NULL_BUFFER) {
        emLogLine(DROP, "no heap space for network data");
        return;
      }
      emNetworkData = temp;
    }

    MEMCOPY(leaderData, mleLeaderData, EMBER_NETWORK_DATA_LEADER_SIZE);
    emHaveLeader = true;
    dataState = DATA_IN_SYNC;
    haveNetworkData = true;
    emLogBytesLine(NETWORK_DATA, "rx network data",
                   emGetBufferPointer(emNetworkData),
                   emGetBufferLength(emNetworkData));
    noteNetworkDataChange(stableDataChanged);
  } else {
    emLogLine(NETWORK_DATA, "network data is not newer");
  }
}

//----------------------------------------------------------------
// Looking up servers and gateways.

static EmberNodeId findNearestServer(uint8_t *tlv, uint8_t neededFlags)
{
  EmberNodeId server = 0xFFFE;

  if (tlv != NULL) {
    uint8_t cost = 0xFF;
    uint8_t *finger = tlv + emNetworkDataTlvOverhead(tlv);
    uint8_t *end = networkDataNextTlv(tlv);
    for ( ; finger < end; finger += 4) {
      EmberNodeId nextServer = emberFetchHighLowInt16u(finger);
      if ((finger[2] & neededFlags) == neededFlags) {
        if ((nextServer & 0xFF00) == 0xFC00) {
          // Immediately return an anycast address.
          // Sleepies should only ever receive this kind of address.
          return nextServer;
        }
        if (! emAmFullThreadDevice()) {
          // Minimal thread devices don't have a route table.  The
          // existence of a server is the best they can hope for.
          return nextServer;
        } else {
          // If the cost is not known, no server will be returned.
          // This is intentional.  Routers and powered end devices
          // that receive the full network data should all have routes.
          // If the route is temporarily down, then we shouldn't try
          // to reach that server.
          uint8_t nextCost = emGetRouteCost(nextServer);
          if (nextCost < cost) {
            server = nextServer;
            cost = nextCost;
          }
        }
      }
    }
  }

  return server;
}

const uint8_t *emNetworkDataTlvHasNode(const uint8_t* tlv, EmberNodeId node)
{
  const uint8_t *finger = emNetworkDataPointer(tlv);
  const uint8_t *end = finger + emNetworkDataSize(tlv);
  // only two choices
  uint8_t entrySize = (emNetworkDataType(tlv) == NWK_DATA_HAS_ROUTE
                       ? HAS_ROUTE_ENTRY_LENGTH 
                       : BORDER_ROUTER_ENTRY_LENGTH);
  for ( ; finger < end; finger += entrySize) {
    if (emberFetchHighLowInt16u(finger) == node) {
      return finger + 2;        // pointer to flags
    }
  }
  return NULL;
}

uint8_t *emFindPrefixTlv(const uint8_t *prefix,
                         uint8_t prefixLengthInBits,
                         uint8_t type,
                         bool exactMatch)
{
  uint8_t *prefixTlv;
  for (prefixTlv = emFindNetworkDataTlv(NWK_DATA_PREFIX);
       prefixTlv != NULL;
       prefixTlv = emFindNextNetworkDataTlv(NWK_DATA_PREFIX, prefixTlv)) {
    if ((! exactMatch
         || prefixTlv[NWK_DATA_PREFIX_LENGTH_OFFSET] == prefixLengthInBits)
        && emMatchingPrefixBitLength(prefix, 
                                     prefixLengthInBits, 
                                     prefixTlv + NWK_DATA_PREFIX_BITS_OFFSET, 
                                     prefixLengthInBits) 
           == prefixLengthInBits) {
      return emFindPrefixSubTlv(prefixTlv, type);
    }
  }
  return NULL;
}

uint8_t *emFindBorderRouterTlv(const uint8_t *prefix, 
                               uint8_t prefixLengthInBits,
                               bool match)
{
  return emFindPrefixTlv(prefix, 
                         prefixLengthInBits, 
                         NWK_DATA_BORDER_ROUTER, 
                         match);
}

static EmberNodeId findNearestAddressServer(const uint8_t *prefix,
                                            uint8_t prefixLengthInBits,
                                            uint8_t neededFlag,
                                            bool match)
{
  uint8_t *borderRouterTlv = emFindBorderRouterTlv(prefix, 
                                                   prefixLengthInBits, 
                                                   match);
  if (borderRouterTlv != NULL) {
    return findNearestServer(borderRouterTlv, neededFlag);
  } else {
    return 0xFFFE;
  }
}

EmberNodeId emFindNearestDhcpServer(const uint8_t *prefix, 
                                    uint8_t prefixLengthInBits, 
                                    bool match)
{
  return findNearestAddressServer(prefix, 
                                  prefixLengthInBits, 
                                  EMBER_BORDER_ROUTER_DHCP_FLAG, match);
}

bool emIsOnMeshPrefix(const uint8_t *prefix, uint8_t prefixLengthInBits)
{
  uint8_t *borderRouterTlv = emFindBorderRouterTlv(prefix, 
                                                   prefixLengthInBits, 
                                                   true);
  return (borderRouterTlv != NULL);
}

// In order of priority:
//  - length of prefix match (zero for default routes)
//  - route preference
//  - closeness within the mesh
// So the score has the prefix match in the top byte, the preference
// in the top two bits of the low byte, and the route cost in the
// low six bits of the low byte.
//
// The '^ 0x80' converts the two-bit signed cost (11, 00, 10) into an
// unsigned value while maintaining the same ordering (01, 10, 11).
// The two-bit cost is in the high two bits of the byte.

static uint16_t routeScore(uint8_t match,
                           uint8_t preference,
                           EmberNodeId router)
{
  uint8_t routeCost = RIP_INFINITY;
  if (router == emberGetNodeId()) {
    routeCost = 0;
  } else {
    uint8_t routerIndex = emRouterIndex(emShortMacIdToRipId(router));
    if (routerIndex == 0xFF) {
      // simPrint("no index");
      return 0;
    }
    if (emGetRoute(routerIndex, &routeCost) == 0xFF
        || routeCost == RIP_INFINITY) {
      // simPrint("no route for index %d", routerIndex);
      return 0;
    }
  }
  return ((match << 8)
          | (preference ^ 0x80)
          | (RIP_MAX_METRIC - routeCost));    // lower cost is better
}

static uint16_t checkScores(uint16_t bestScore,
                            EmberNodeId *routerLoc,
                            uint8_t *serverTlv,
                            uint8_t entryLength,                            
                            uint8_t matchLength,
                            uint8_t *borderRouterTlv)
{
  uint8_t *finger = serverTlv + emNetworkDataTlvOverhead(serverTlv);
  uint8_t *end = networkDataNextTlv(serverTlv);
  for ( ; finger < end; finger += entryLength) {
    EmberNodeId routerNodeId = emberFetchHighLowInt16u(finger);
    if (borderRouterTlv == NULL
        ? (0 < matchLength
           || (finger[2] & EMBER_BORDER_ROUTER_DEFAULT_ROUTE_FLAG))
        : emNetworkDataTlvHasNode(borderRouterTlv, routerNodeId) != NULL) {
      uint16_t score = routeScore(matchLength,
                                  finger[2] & EMBER_BORDER_ROUTER_PREFERENCE_MASK,
                                  routerNodeId);
      // simPrint("%d node %04X best %04X new %04X",
      //          borderRouterTlv == NULL, routerNodeId, bestScore, score);
      // fprintf(stderr, "[%d node %04X best %04X new %04X]\n",
      //         borderRouterTlv == NULL, routerNodeId, bestScore, score);
      if (bestScore < score) {
        bestScore = score;
        *routerLoc = routerNodeId;
      }
    }
  }
  return bestScore;
}

EmberNodeId emFindNearestGateway(const uint8_t *ipDestination,
                                 const uint8_t *ipSource)
{
  // 1. Find the border routers for the source prefix, if any.
  uint8_t *borderRouterTlv = emFindBorderRouterTlv(ipSource, 64, false);
  if (borderRouterTlv == NULL) {
    // fprintf(stderr, "[no source router]\n");
    // simPrint("no source router");
    return 0xFFFE;
  }

  EmberNodeId result = 0xFFFE;
  uint16_t bestScore = 0;

  // 2. Check the source-prefix border routers for regular or default routes.
  // We assume that a border router that offers a prefix also has a route to
  // that prefix, even if it doesn't offer a default route.
  uint8_t matchLength =
    emMatchingPrefixBitLength(ipSource, 64, ipDestination, 64);
  bestScore = checkScores(bestScore, 
                          &result, 
                          borderRouterTlv, 
                          BORDER_ROUTER_ENTRY_LENGTH,
                          matchLength, 
                          NULL);

  // 3. Look for other routes, but they must be from the source-prefix
  // border routers.
  uint8_t *prefixTlv;
  for (prefixTlv = emFindNetworkDataTlv(NWK_DATA_PREFIX);
       prefixTlv != NULL;
       prefixTlv = emFindNextNetworkDataTlv(NWK_DATA_PREFIX, prefixTlv)) {
    uint8_t *routeTlv = emFindPrefixSubTlv(prefixTlv, NWK_DATA_HAS_ROUTE);
    uint8_t *prefix = prefixTlv + NWK_DATA_PREFIX_BITS_OFFSET;
    uint8_t prefixLengthBits = prefixTlv[NWK_DATA_PREFIX_LENGTH_OFFSET];
    if (routeTlv != NULL) {
      matchLength =
        emMatchingPrefixBitLength(prefix, prefixLengthBits, ipDestination, 128);
      if (HIGH_BYTE(bestScore) < matchLength) { // don't bother if it can't win
        bestScore =
          checkScores(bestScore,
                      &result,
                      routeTlv,
                      HAS_ROUTE_ENTRY_LENGTH,
                      matchLength,
                      borderRouterTlv);
      }
    }
  }
  
  return result;
}

//----------------------------------------------------------------
// Merging.

// Debugging routine.
// static void dumpx(char *name, const uint8_t *data, int length)
// {
//   int i;
//   fprintf(stderr, "[%s %d", name, length);
//   for (i = 0; i < length; i++)
//     fprintf(stderr, " %02X", data[i]);
//   fprintf(stderr, "]\n");
// }

// During the update we keep track of stable and temporary changes.

static void noteChange(UpdateData *updateData, const int8u *tlv)
{
  if (emNetworkDataIsStable(tlv)) {
    updateData->stableChange = true;
  } else {
    updateData->temporaryChange = true;
  }
}

// Only used for Border Router and Has Route TLVs.

#define subTlvEntrySize(tlv) \
  (emNetworkDataType(tlv) == NWK_DATA_BORDER_ROUTER ? 4 : 3)

// Find the sub-TLV with the given type byte.

uint8_t *findPrefixSubTlvTypeByte(uint8_t *prefixTlv, uint8_t typeByte)
{
  uint8_t *finger = prefixTlv + prefixTlvHeaderSize(prefixTlv);
  uint8_t *end = networkDataNextTlv(prefixTlv);
  while (finger < end) {
    if (typeByte == emNetworkDataTypeByte(finger)) {
      return finger;
    }
    finger = networkDataNextTlv(finger);
  }
  return NULL;
}

// Find where 'nodeId' is in 'tlv', which is either a Border Router
// TLV or Has Route TLV.

static uint8_t *findInSubTlv(uint8_t *tlv, EmberNodeId nodeId)
{
  uint8_t *finger = emNetworkDataPointer(tlv);
  uint8_t *tlvEnd = networkDataNextTlv(tlv);
  uint8_t entrySize = subTlvEntrySize(tlv);

  for ( ; finger < tlvEnd; finger += entrySize) {
    if (emberFetchHighLowInt16u(finger) == nodeId) {
      return finger;
    }
  }
  return NULL;
}

// 'newSubTlv' is a Border Router or Has Route TLV that must be added to
// 'oldPrefixTlv'.  There are four possibilities:
//  1. 'oldPrefixTlv' does not already have a sub-TLV of the right type
//    -> copy 'newSubTlv' into 'oldPrefixTlv'
//  2. 'oldPrefixTlv' already has a sub-TLV of the right type but it does
//     not contain the new border router
//    -> make the sub-TLV bigger and copy the new data into it
//  3. 'oldPrefixTlv' already has a sub-TLV of the right type with the 
//    right node, but the data is different
//    -> copy in the new data
//  4. Everything is already in place.
//    -> change the node ID to 0xFFFF to show that the data is up-to-date

static void  addPrefixSubTlv(uint8_t *newSubTlv,
                             uint8_t *oldPrefixTlv,
                             UpdateData *updateData)
                            
{
  uint8_t newSubTlvSize = emNetworkDataTlvSize(newSubTlv);
  uint8_t *oldPrefixTlvEnd = networkDataNextTlv(oldPrefixTlv);
  uint8_t *oldSubTlv = 
    findPrefixSubTlvTypeByte(oldPrefixTlv,
                             emNetworkDataTypeByte(newSubTlv));
  uint8_t entrySize = subTlvEntrySize(newSubTlv);
  uint8_t *dataLocation = NULL;

  if (oldSubTlv == NULL) {
    MEMCOPY(oldPrefixTlvEnd, newSubTlv, 2);
    dataLocation = oldPrefixTlvEnd + 2;
    oldPrefixTlv[1] += newSubTlvSize;
  } else {
    dataLocation = findInSubTlv(oldSubTlv, updateData->server);

    if (dataLocation == NULL) {
      uint8_t *oldSubTlvEnd = networkDataNextTlv(oldSubTlv);
      uint8_t copySize = oldPrefixTlvEnd - oldSubTlvEnd;
      MEMMOVE(oldSubTlvEnd + entrySize, oldSubTlvEnd, copySize);
      dataLocation  = oldSubTlvEnd;
      oldPrefixTlv[1] += entrySize;
      oldSubTlv[1] += entrySize;
    } else if (MEMCOMPARE(newSubTlv + 2, dataLocation, entrySize) == 0) {
      emberStoreHighLowInt16u(dataLocation, 0xFFFF);
      return; // no change
    }
  }
  
  // Copy in the new data, but set the node ID to 0xFFFF to mark this as
  // a new entry, not an old one.
  assert(dataLocation != NULL);
  MEMCOPY(dataLocation, newSubTlv + 2, entrySize);
  emberStoreHighLowInt16u(dataLocation, 0xFFFF);
  noteChange(updateData, newSubTlv);
}

// Set 'server's network data to be 'newTlvs'.
// This is done in three stages:
//  1. Copy the existing network data TLVs, updating them whenever there
//     is a matching TLV in newTlvs and removing references to the server
//     where there is no matching new TLV.
//  2. Copy any remaining new TLVs which had no matching existing TLV,
//     adding contexts where needed.
//  3. If there were any changes, then replace the existing network data
//     with the new version.

HIDDEN bool updateServerData(EmberNodeId server,
                             uint8_t *newTlvs,
                             uint8_t newTlvsLength,
                             bool removeChildren)
{
  uint8_t *oldTlvs = emGetBufferPointer(emNetworkData);
  uint8_t *oldTlvsEnd = oldTlvs + emGetBufferLength(emNetworkData);

  // dumpx("have", oldTlvs, emGetBufferLength(emNetworkData));
  // dumpx("add",  newTlvs, newTlvsLength);
  // fprintf(stderr, "[from %04X remove children %d]\n", server, removeChildren);

  UpdateData updateData;
  uint8_t temp[MAX_NETWORK_DATA_SIZE];

  MEMSET(&updateData, 0, sizeof(updateData));
  updateData.writeFinger = temp;
  updateData.writeLimit = temp + sizeof(temp);
  updateData.server = server;
  updateData.removeChildren = removeChildren;
  updateData.contextIdMask = 1; // ID 0 is assigned to the mesh local prefix

  // Copy the old TLVs, updating them when necessary.
  for (; oldTlvs < oldTlvsEnd; oldTlvs = networkDataNextTlv(oldTlvs)) {
    // dumpx("B", oldTlvs, emNetworkDataTlvSize(oldTlvs));
    uint8_t size = emNetworkDataTlvSize(oldTlvs);
    uint8_t *oldTlv = updateData.writeFinger;
    MEMMOVE(oldTlv, oldTlvs, size);
    updateData.writeFinger += size;

    switch (emNetworkDataType(oldTlv)) {

    case NWK_DATA_PREFIX: {
      uint8_t * newTlv =
        findMatchingPrefixTlv(oldTlv + NWK_DATA_PREFIX_BITS_OFFSET,
                              oldTlv[NWK_DATA_PREFIX_LENGTH_OFFSET],
                              newTlvs,
                              newTlvsLength);
      if (newTlv != NULL) {
        uint16_t newTlvSize = emNetworkDataTlvSize(newTlv);
        uint8_t *newSubTlv = newTlv + prefixTlvHeaderSize(newTlv);
        for (; 
             newSubTlv < newTlv + newTlvSize;
             newSubTlv = networkDataNextTlv(newSubTlv)) {
          addPrefixSubTlv(newSubTlv, oldTlv, &updateData);
        }
        // Remove new TLV from 'newTlvs'.
        MEMMOVE(newTlv, 
                newTlv + newTlvSize, 
                (newTlvs + newTlvsLength) - (newTlv + newTlvSize));
        newTlvsLength -= newTlvSize;
      }
      cleanUpPrefixTlv(oldTlv, &updateData, false);
      break;
    }
      
    case NWK_DATA_COMMISSION:
      // do nothing
      break;

    case NWK_DATA_SERVICE:
      assert(false);
      break;

    default:
      assert(false);
      break;
    }
  }

  // Copy any remaining new TLVs, adding contexts where needed
  uint8_t *newTlvsEnd = newTlvs + newTlvsLength;
  updateData.server = 0xFFFF;   // don't remove references to the server
  for (; newTlvs < newTlvsEnd; newTlvs = networkDataNextTlv(newTlvs)) {
    // dumpx("D", newTlvs, emNetworkDataTlvSize(newTlvs));
    uint8_t size = emNetworkDataTlvSize(newTlvs);
    MEMCOPY(updateData.writeFinger, newTlvs, size);
    // fprintf(stderr, "[copy %d bytes of new TLV]\n", size);
    noteChange(&updateData, newTlvs);
    if (emNetworkDataType(newTlvs) == NWK_DATA_PREFIX) {
      cleanUpPrefixTlv(updateData.writeFinger, &updateData, true);
    } else {
      assert(false);
      updateData.writeFinger += size;
    }
  }

  if (updateData.stableChange || updateData.temporaryChange) {
    uint16_t oldDataSize = emGetBufferLength(emNetworkData);
    uint16_t newDataSize = updateData.writeFinger - temp;
    Buffer newBuffer = emAllocateBuffer(newDataSize);
    if (newBuffer == NULL_BUFFER) {
      return false;
    }
    emNetworkData = newBuffer;
    // dumpx("new", temp, newDataSize);
    MEMCOPY(emGetBufferPointer(emNetworkData), temp, newDataSize);
    if (newDataSize < oldDataSize) {
      emSetBufferLength(emNetworkData, newDataSize);
    }
    emAnnounceNewNetworkData(updateData.stableChange);
    noteNetworkDataChange(updateData.stableChange);
  }
  return true;
}

static uint8_t *findMatchingPrefixTlv(const uint8_t *prefix, 
                                      uint8_t lengthInBits,
                                      uint8_t *tlvs,
                                      uint16_t tlvsLength)
{
  uint8_t *end = tlvs + tlvsLength;
  for ( ; tlvs < end; tlvs = networkDataNextTlv(tlvs)) {
    if (emNetworkDataType(tlvs) == NWK_DATA_PREFIX
        && lengthInBits == tlvs[NWK_DATA_PREFIX_LENGTH_OFFSET]
        && (emMatchingPrefixBitLength(prefix, 
                                      lengthInBits, 
                                      tlvs + NWK_DATA_PREFIX_BITS_OFFSET, 
                                      lengthInBits) 
            == lengthInBits)) {
      return tlvs;
    }
  }
  return NULL;
}

// 1. Walk the sub-TLVs doing three things:
//   a. Find the 6LoWPAN ID, if any.
//   b. Remove old references to the server's node ID, which may result
//      in a sub-TLV being removed entirely.
//   c. Replace 0xFFFF, which marks a new reference, with the actual
//      node ID.
// 2. If there are servers, add a 6LoWPAN ID if there isn't already one.
//    If no servers and a 6LoWPAN ID, then turn off compression for the ID.
//    If no servers and no 6LoWPAN ID, then delete the prefix TLV.

static void cleanUpPrefixTlv(uint8_t *prefixTlv, 
                             UpdateData *updateData,
                             bool isNew)
{
  // dumpx("prefixTlv", prefixTlv, prefixTlv[1] + 2);
  // fprintf(stderr, "[prefix size %d/%d header size %d]\n", 
  //         prefixTlv[NWK_DATA_PREFIX_LENGTH_OFFSET],
  //         EMBER_BITS_TO_BYTES(prefixTlv[NWK_DATA_PREFIX_LENGTH_OFFSET]),
  //         prefixTlvHeaderSize(prefixTlv));
  uint8_t *subTlv = prefixTlv + prefixTlvHeaderSize(prefixTlv);
  uint8_t *writeFinger = subTlv;
  uint8_t *end = networkDataNextTlv(prefixTlv);
  // dumpx("subTlv", subTlv, end - subTlv);
  bool haveServer = false;
  uint8_t *contextTlv = NULL;
  emNetworkDataTypeByte(prefixTlv) &= ~ NWK_DATA_STABLE_FLAG;

  while (subTlv < end) {
    uint8_t *nextSubTlv = networkDataNextTlv(subTlv);
    switch (emNetworkDataType(subTlv)) {
    case NWK_DATA_6LOWPAN_ID:
      if (! isNew) {
        uint8_t size = emNetworkDataTlvSize(subTlv);
        if (writeFinger != subTlv) {
          MEMMOVE(writeFinger, subTlv, size);
        }
        contextTlv = writeFinger;
        emNetworkDataTypeByte(prefixTlv) |= 
          emNetworkDataTypeByte(writeFinger) & NWK_DATA_STABLE_FLAG;
        writeFinger += size;
      }
      break;
    case NWK_DATA_BORDER_ROUTER:
    case NWK_DATA_HAS_ROUTE: {
      uint8_t *savedWriteFinger = writeFinger;
      writeFinger = removeNodeId(subTlv, writeFinger, updateData);
      if (writeFinger != savedWriteFinger) {
        emNetworkDataTypeByte(prefixTlv) |= 
          emNetworkDataTypeByte(subTlv) & NWK_DATA_STABLE_FLAG;
        haveServer = true;
      } else if (! isNew) {
        noteChange(updateData, subTlv);
      }
    }
      break;
    default:
      assert(false);
    }
    subTlv = nextSubTlv;
  }

  if (contextTlv != NULL) {
    // fprintf(stderr, "[update context ID]\n");
    // Make the context TLV stable if the outer prefix TLV is.
    emNetworkDataTypeByte(contextTlv) =
      (emNetworkDataTypeByte(contextTlv) & ~NWK_DATA_STABLE_FLAG)
      | (emNetworkDataTypeByte(prefixTlv) & NWK_DATA_STABLE_FLAG);
    // Set the COMPRESS flag if we have a server, clear it otherwise
    if (haveServer) {
      contextTlv[NWK_DATA_6LOWPAN_ID_ID_OFFSET] |= PREFIX_6CO_COMPRESS_FLAG;
    } else {
      contextTlv[NWK_DATA_6LOWPAN_ID_ID_OFFSET] &= ~PREFIX_6CO_COMPRESS_FLAG;
    }
    updateData->contextIdMask |= BIT(contextTlv[NWK_DATA_6LOWPAN_ID_ID_OFFSET] 
                                     & PREFIX_6CO_ID_MASK);
  } else if (haveServer) {
    // fprintf(stderr, "[add context ID]\n");
    uint8_t id = findNextId(&updateData->contextIdMask);
    if (id < 16) {
      noteChange(updateData, prefixTlv);
      writeFinger = writeContextData(writeFinger,
                                     id,
                                     emNetworkDataIsStable(prefixTlv),
                                     prefixTlv[NWK_DATA_PREFIX_LENGTH_OFFSET]);
    }
  } else {
    // no server, no context - delete the whole thing
    updateData->writeFinger = prefixTlv;
    return;
  }

  prefixTlv[1] = writeFinger - (prefixTlv + 2);
  // dumpx("prefixTlv", prefixTlv, prefixTlv[1] + 2);
  updateData->writeFinger = writeFinger;
}

// Return the next unused ID, marking it as used in the mask.

static uint8_t findNextId(uint16_t *maskLoc)
{
  uint16_t mask = *maskLoc;
  uint8_t id;
  for (id = 0; id < 16 && (mask & BIT(id)); id++);
  if (id < 16) {
    *maskLoc |= BIT(id);
  }
  return id;
}

static uint8_t *writeContextData(uint8_t *finger,
                                 uint8_t id,
                                 bool stable,
                                 uint8_t length)
{
  uint8_t *contextTlv = finger;
  emNetworkDataTypeByte(contextTlv) =
    stable
    ? emNetworkDataStableType(NWK_DATA_6LOWPAN_ID)
    : emNetworkDataTemporaryType(NWK_DATA_6LOWPAN_ID);
  emSetNetworkDataSize(contextTlv,
                       (NWK_DATA_6LOWPAN_ID_SIZE
                        - (emNetworkDataLengthOverhead
                           (NWK_DATA_6LOWPAN_ID_SIZE))));
  contextTlv[NWK_DATA_6LOWPAN_ID_ID_OFFSET]
    = id | PREFIX_6CO_COMPRESS_FLAG;
  contextTlv[NWK_DATA_6LOWPAN_ID_LENGTH_OFFSET] = length;
  return finger + NWK_DATA_6LOWPAN_ID_SIZE;
}

// Remove the server's node ID, and possibly its children's node IDs, wherever
// the are found.  Replace 0xFFFF with the server's node ID.

static uint8_t *removeNodeId(uint8_t *tlv,
                             uint8_t *writeFinger,
                             UpdateData *updateData)
{ 
  // dumpx("before", tlv, emNetworkDataTlvSize(tlv));
  uint8_t *readFinger = emNetworkDataPointer(tlv);
  uint8_t *readEnd = networkDataNextTlv(tlv);
  uint8_t entrySize = subTlvEntrySize(tlv);
  uint8_t *tlvStart = writeFinger;
  uint16_t serverRipId = emShortMacIdToRipId(updateData->server);

  *writeFinger++ = emNetworkDataTypeByte(tlv);
  writeFinger += 1;         // skip over length location

  while (readFinger < readEnd) {
    EmberNodeId id = emberFetchHighLowInt16u(readFinger);
    if (id == updateData->server
        || (updateData->removeChildren
            && emShortMacIdToRipId(id) == serverRipId)) {
      noteChange(updateData, tlv);
    } else {
      if (writeFinger != readFinger) {
        MEMMOVE(writeFinger, readFinger, entrySize);
      }
      // Replace just-added entries with the server's real node ID.
      if (id == 0xFFFF) {
        emberStoreHighLowInt16u(writeFinger, updateData->server);
      }
      writeFinger += entrySize;
    }
   
    readFinger += entrySize;
  }
 
  if (writeFinger == tlvStart + 2) {    // skip over header + length
    return tlvStart;    // no data remaining, drop the TLV
  } else {
    tlvStart[1] = writeFinger - (tlvStart + 2);
    // dumpx("after", tlvStart, writeFinger - tlvStart);
    return writeFinger;
  }
}

//----------------------------------------------------------------
// Pretty printer to help with debugging.

static const char *tlvNames[] = {
  "has_route",
  "prefix",
  "border_router",
  "6lowpan_id",
  "commission",
  "service",
  "server"
};

static void reallyLogTlv(uint8_t logType,
                         const char *indent,
                         uint8_t *tlv,
                         uint8_t byteCount)
{
  uint8_t type = emNetworkDataType(tlv);
  uint8_t *data = emNetworkDataPointer(tlv);
  char isStable = emNetworkDataIsStable(tlv) ? 's' : 't';
  uint16_t i;

  emReallyEndLogLine(logType);
  emReallyStartLogLine(logType);

  if (type <= NWK_DATA_SERVER) {
    emReallyLog(logType, "%s%s_%c | ", indent, tlvNames[type], isStable);
  } else {
    emReallyLog(logType, "%s%X_%c | ", indent, type, isStable);
  }

  if (type == NWK_DATA_6LOWPAN_ID) {
    emReallyLog(logType, "compress: %s | ", (data[0] & 0x10) ? "yes" : "no");
  }

  emReallyLog(logType, "bytes:");

  for (i = 0; i < byteCount; i++) {
    emReallyLog(logType, " %X", data[i]);
  }
}

void emReallyLogNetworkData(uint8_t logType)
{
  if (! emReallyLogIsActive(logType)) {
    return;
  }

  if (emNetworkData == NULL_BUFFER) {
    emReallyLogLine(logType, "network data: ()");
    return;
  }
  
  uint8_t *start = emGetBufferPointer(emNetworkData);
  uint8_t *end = start + emGetBufferLength(emNetworkData);

  emReallyLogNetworkDataTlvs(logType, "network data:", start, end);
}

void emReallyLogNetworkDataTlvs(uint8_t logType, 
                                const char *title,
                                uint8_t *tlv,
                                const uint8_t *end)
{
  if (! emReallyLogIsActive(logType)) {
    return;
  }

  emReallyStartLogLine(logType);
  emReallyLog(logType, title);

  for ( ; tlv < end; tlv = networkDataNextTlv(tlv)) {
    uint16_t length = emNetworkDataSize(tlv);
    uint8_t *data = emNetworkDataPointer(tlv);
    uint16_t preludeLength = length;

    switch (emNetworkDataType(tlv)) {
    case NWK_DATA_PREFIX:
      // Subtract off the TLV type and length bytes.
      preludeLength = prefixTlvHeaderSize(tlv) - 2;
      break;
    case NWK_DATA_SERVICE:
      preludeLength = 0;
      break;
    }

    reallyLogTlv(logType, "  ", tlv, preludeLength);

    uint8_t *subTlv = data + preludeLength;
    uint8_t *tlvEnd = data + length;

    for ( ; subTlv < tlvEnd; subTlv = networkDataNextTlv(subTlv)) {
      reallyLogTlv(logType, "    ", subTlv, emNetworkDataSize(subTlv));
    }
  }
  
  emReallyEndLogLine(logType);
}

//----------------------------------------------------------------
// Returns the next prefix TLV that offers a route.

static uint8_t *findRouteTlv(uint8_t *tlv, const uint8_t *end)
{
  while (tlv != end) {
    tlv = emFindTlv(NWK_DATA_PREFIX, tlv, end);
    if (tlv == NULL) {
      return NULL;
    }
    if (emFindPrefixSubTlv(tlv, NWK_DATA_HAS_ROUTE) != NULL) {
      return tlv;
    }
    uint8_t *brTlv = emFindPrefixSubTlv(tlv, NWK_DATA_BORDER_ROUTER);
    if (brTlv != NULL) {
      uint8_t *finger = brTlv + emNetworkDataTlvOverhead(brTlv);
      uint8_t *end = networkDataNextTlv(brTlv);
      for ( ; finger < end; finger += 4) {
        if (finger[2] & EMBER_BORDER_ROUTER_DEFAULT_ROUTE_FLAG) {
          return tlv;
        }
      }
    }
    tlv = networkDataNextTlv(tlv);
  }
  return NULL;
}

//----------------------------------------------------------------
// Application API

void emApiGetNetworkData(uint8_t *buffer, uint16_t bufferLength)
{
  if (! haveNetworkData) {
    emApiGetNetworkDataReturn(EMBER_NETWORK_DOWN, NULL, 0);
  } else if (emNetworkData == NULL_BUFFER) {
    emApiGetNetworkDataReturn(EMBER_SUCCESS, NULL, 0);
  } else if (emGetBufferLength(emNetworkData) <= bufferLength) {
    uint16_t length = emGetBufferLength(emNetworkData);
    MEMCOPY(buffer, emGetBufferPointer(emNetworkData), length);
    emApiGetNetworkDataReturn(EMBER_SUCCESS, buffer, length);
  } else {
    emApiGetNetworkDataReturn(EMBER_BAD_ARGUMENT, NULL, 0);
  }
}
