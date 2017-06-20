/*
 * File: global-prefix.c
 * Description: global prefix and address table
 * Author(s): Richard Kelsey, Matteo Paris
 *
 * Copyright 2013 Silicon Laboratories, Inc.                                *80*
 */

#include <string.h>
#include "core/ember-stack.h"
#include "hal/hal.h"
#include "phy/phy.h"
#include "framework/event-queue.h"
#include "commission.h"
#include "context-table.h"
#include "ip-address.h"
#include "network-data.h"
#include "dhcp.h"
#include "mle.h"

// The default prefix and a table of configured addresses.

// Thread uses a ULA as a network-wide shared prefix.  The ULA
// does not time out.  A end-device ML16 using the ULA is obtained
// from the parent at joining.  Routers can then get a ML16 with a
// router ID from the leader using DHCPv6.  An ML64 address is formed
// unilaterally.
//
// Each gateway registers its prefix with the leader, which
// distributes the prefix along with a 6LoWPAN context ID and the
// gateway's short ID in the network data.  Nodes can then use either
// DHCPv6 of SLAAC to configure a GP64, depending on the gateway.
// Those configured addresses are kept in the global address table in
// this file.

static uint8_t meshLocalUlaPrefix[8];
static bool haveMeshLocalUlaPrefix;
static uint8_t legacyUla[8];

bool emStoreDefaultGlobalPrefix(uint8_t *target)
{
  if (haveMeshLocalUlaPrefix) {
    MEMCOPY(target, meshLocalUlaPrefix, 8);
    return true;
  } else {
    return false;
  }
}

bool emIsDefaultGlobalPrefix(const uint8_t *prefix)
{
  return (haveMeshLocalUlaPrefix
          && MEMCOMPARE(prefix, meshLocalUlaPrefix, 8) == 0);
}

void emStoreLegacyUla(uint8_t *target)
{
  if (emOnLurkerNetwork()) {
    MEMCOPY(target, legacyUla, 8);
  }
}

bool emIsLegacyUla(const uint8_t *prefix)
{
  return (emOnLurkerNetwork()
          && memcmp(prefix, legacyUla, 8) == 0);
}

bool emHaveLegacyUla(void)
{
  return (emOnLurkerNetwork() 
          && ! emIsMemoryZero(legacyUla, 8));
}

//----------------------------------------------------------------

Buffer emGlobalAddressTable = NULL_BUFFER;

void emDefaultPrefixInit(void)
{
  haveMeshLocalUlaPrefix = false;
  emGlobalAddressTable = NULL_BUFFER;
  MEMSET(legacyUla, 0, 8);
}

void emGlobalAddressTableInit(void)
{
  emGlobalAddressTable = NULL_BUFFER;
}

//----------------------------------------------------------------
// Keeping our address up to date on our parent.  Checking that our parent
// correctly repeats them back to us is not yet implemented.

enum {
  ADDRESSES_IN_SYNC,            // No need to do anything.
  ADDRESSES_NEED_TO_SEND,       // Parent does not have our addresses.
  ADDRESSES_WAITING_FOR_ACK     // Addresses sent, no ack received.
};

HIDDEN uint8_t addressRegistrationState = ADDRESSES_IN_SYNC;

// We want to delay the address registration a little bit to avoid sending
// multiple registration messages when something causes two or more addresses
// to change.

static void registrationEventHandler(Event *event);

static EventActions registrationEventActions = {
  &emStackEventQueue,
  registrationEventHandler,
  NULL, // no marking function
  "registration"
};

static Event registrationEvent = {&registrationEventActions, NULL};

static void registrationEventHandler(Event *event)
{
  if (! emAmFullThreadDevice()
      && addressRegistrationState == ADDRESSES_NEED_TO_SEND) {
    EmberIpv6Address parentAddress;
    emStoreLongFe8Address(emParentLongId, parentAddress.bytes);
    
    if (emSendMleChildUpdate(&parentAddress, CHILD_UPDATE)) {
      // We don't have response checking set up yet, so we assume
      // that the parent gets the message.
      // addressRegistrationState = ADDRESSES_WAITING_FOR_ACK;
      addressRegistrationState = ADDRESSES_IN_SYNC;
    }
  }
}

void emNoteEndDeviceAddressChange(void)
{
  addressRegistrationState = ADDRESSES_NEED_TO_SEND;
  emberEventSetDelayMs(&registrationEvent, 100);         // wait 100ms
}

//----------------------------------------------------------------
// flags is non-zero -> return the first entry with those flags set
// prefix is non-NULL -> count is length of prefix (in bits), return matching 
// entry, otherwise -> return the count'th entry.
// match = true; exact prefix match.

static GlobalAddressEntry *findGlobalAddressEntry(const uint8_t *prefix,
                                                  uint8_t count,
                                                  uint8_t flags,
                                                  bool match)
{
  uint16_t i;
  Buffer finger;
  for (i = 1, finger = emBufferQueueHead(&emGlobalAddressTable);
       finger != NULL_BUFFER;
       i++, finger = emBufferQueueNext(&emGlobalAddressTable, finger)) {
    GlobalAddressEntry *entry =
      (GlobalAddressEntry *) emGetBufferPointer(finger);

    if (flags != 0
        ? (entry->localConfigurationFlags & flags) == flags
        : (prefix == NULL
           ? i == count
           : (emMatchingPrefixBitLength(prefix, count, entry->address, count) == count)
              && (! match || (count == entry->prefixLengthInBits)))) {
      return entry;
    }
  }
  return NULL;
}

void emDeleteGlobalAddressEntry(const uint8_t *prefix, 
                                uint8_t prefixLengthInBits)
{
  Buffer finger;
  for (finger = emBufferQueueHead(&emGlobalAddressTable);
       finger != NULL_BUFFER;
       finger = emBufferQueueNext(&emGlobalAddressTable, finger)) {
    GlobalAddressEntry *entry =
      (GlobalAddressEntry *) emGetBufferPointer(finger);
    if (emMatchingPrefixBitLength(prefix, 
                                  prefixLengthInBits, 
                                  entry->address, 
                                  prefixLengthInBits) == prefixLengthInBits) {
      emBufferQueueRemove(&emGlobalAddressTable, finger);
      return;
    }
  }
}

void emDeleteGatewayAddressEntry(const uint8_t *prefix, 
                                 uint8_t prefixLengthInBits)
{
  Buffer finger;
  for (finger = emBufferQueueHead(&emGlobalAddressTable);
       finger != NULL_BUFFER;
       finger = emBufferQueueNext(&emGlobalAddressTable, finger)) {
    GlobalAddressEntry *entry =
      (GlobalAddressEntry *) emGetBufferPointer(finger);

    if (((entry->localConfigurationFlags
          & EMBER_GLOBAL_ADDRESS_AM_GATEWAY)
         != 0)
        && emMatchingPrefixBitLength(prefix, 
                                     prefixLengthInBits, 
                                     entry->address, 
                                     prefixLengthInBits) 
           == prefixLengthInBits) {
      emBufferQueueRemove(&emGlobalAddressTable, finger);
      return;
    }
  }
}

// If we are a gateway this returns the entry for the prefix we supply.

GlobalAddressEntry *emGetGatewayAddressEntry(const uint8_t *prefix,
                                             uint8_t prefixLengthInBits,
                                             bool match)
{
  return findGlobalAddressEntry(prefix,
                                prefixLengthInBits,
                                EMBER_GLOBAL_ADDRESS_AM_GATEWAY,
                                match);
}

bool emGatewayHasDefaultRoute(const uint8_t *prefix, 
                              uint8_t prefixLengthInBits, 
                              bool match)
{
  GlobalAddressEntry *gatewayEntry = emGetGatewayAddressEntry(prefix,
                                                              prefixLengthInBits,
                                                              match);
  return (gatewayEntry != NULL
          && (gatewayEntry->borderRouterTlvFlags
              & EMBER_BORDER_ROUTER_DEFAULT_ROUTE_FLAG) != 0);
}

GlobalAddressEntry *emGetPrefixAddressEntry(const uint8_t *prefix,
                                            uint8_t prefixLengthInBits,
                                            bool match)
{
  return findGlobalAddressEntry(prefix, prefixLengthInBits, 0, match);
}

bool emIsOurDhcpServerPrefix(const uint8_t *prefix, 
                             uint8_t prefixLengthInBits, 
                             bool match)
{
  uint8_t dhcpFlags = (EMBER_GLOBAL_ADDRESS_AM_DHCP_SERVER | EMBER_GLOBAL_ADDRESS_AM_GATEWAY);
  GlobalAddressEntry *entry = emGetPrefixAddressEntry(prefix, 
                                                      prefixLengthInBits, 
                                                      false);
  return (entry != NULL
          && (entry->localConfigurationFlags & dhcpFlags) == dhcpFlags);
}

bool emIsOurSlaacServerPrefix(const uint8_t *prefix, 
                              uint8_t prefixLengthInBits, 
                              bool match)
{
  uint8_t slaacFlags = (EMBER_GLOBAL_ADDRESS_AM_SLAAC_SERVER | EMBER_GLOBAL_ADDRESS_AM_GATEWAY);
  GlobalAddressEntry *entry = emGetPrefixAddressEntry(prefix, 
                                                      prefixLengthInBits, 
                                                      false);
  return (entry != NULL
          && (entry->localConfigurationFlags & slaacFlags) == slaacFlags);
}

// Find the appropriate source address for the given destination address.
// Basically, just match the scope (link local, mesh local, global) and
// the IID (16-bit or EUI64).
//
// TODO: For globals there is a more in-depth procedure that we have
// not yet implemented.  See the Thread spec.

bool emStoreIpSourceAddress(uint8_t *source, const uint8_t *destination)
{
  if (emIsFe8Address(destination)) {
    emStoreLongFe8Address(emMacExtendedId, source);
  } else if (emIsDefaultGlobalPrefix(destination)) {
    if (emIsGp16(destination, NULL)) {
      emStoreGp16(emberGetNodeId(), source);
    } else {        
      emStoreLocalMl64(source);
    }
  } else if (destination[0] == 0xFF) {    // multicast
    if (destination[1] < 0x03               // less than subnet-local scope
        || destination[1] == 0x32) {        // link local all thread nodes
      emStoreLongFe8Address(emMacExtendedId, source);
    } else if (destination[1] <= 0x05       // less than site-local scope
               || destination[1] == 0x33) { // realm local all thread nodes
      return emStoreLocalMl64(source);
    } else {
      return false;                       // too wide a multicast scope
    }
  } else {
    GlobalAddressEntry *entry = emGetPrefixAddressEntry(destination, 64, false);
    if (entry == NULL
        || ! (entry->localConfigurationFlags
              & EMBER_GLOBAL_ADDRESS_CONFIGURED)) {
      entry = findGlobalAddressEntry(NULL, 0, EMBER_GLOBAL_ADDRESS_CONFIGURED, false);
    }
    if (entry == NULL) {
      return false;     // we have no appropriate address
    } else {
      MEMCOPY(source, entry->address, 16);
    }
  }
  return true;
}

GlobalAddressEntry *emAddGlobalAddress(uint8_t *address,
                                       uint8_t prefixLengthInBits,
                                       uint8_t domainId,
                                       uint32_t preferredLifetime,
                                       uint32_t validLifetime,
                                       const uint8_t *serverLongId,
                                       uint8_t localConfigurationFlags,
                                       uint8_t borderRouterTlvFlags,
                                       bool isStable)
{
  // Search for just the prefix; we only want one address per prefix.
  GlobalAddressEntry *entry = findGlobalAddressEntry(address, 
                                                     prefixLengthInBits,
                                                     0, 
                                                     true);
  if (entry == NULL) {
    uint16_t size = emBufferQueueLength(&emGlobalAddressTable);
    if (size >= EMBER_MAX_IPV6_GLOBAL_ADDRESS_COUNT) {
      // Enforce this for the host.
      return NULL;
    }
    Buffer buffer = emAllocateBuffer(sizeof(GlobalAddressEntry));
    if (buffer == NULL_BUFFER) {
      return NULL;
    }
    emBufferQueueAdd(&emGlobalAddressTable, buffer);
    entry = (GlobalAddressEntry *) emGetBufferPointer(buffer);
    MEMSET(entry, 0, sizeof(GlobalAddressEntry));
    entry->domainId = domainId;                     // Configure this once.
    entry->prefixLengthInBits = prefixLengthInBits; // Configure this once.
  }

  MEMMOVE(entry->address, address, 16);
  entry->requestTimeout = 0;
  entry->localConfigurationFlags = localConfigurationFlags;
  entry->borderRouterTlvFlags = borderRouterTlvFlags;
  entry->isStable = isStable;
  if (serverLongId != NULL) {
    MEMCOPY(entry->serverLongId, serverLongId, 8);
  }

  if (localConfigurationFlags & EMBER_GLOBAL_ADDRESS_CONFIGURED) {
    entry->preferredLifetime = preferredLifetime;
    entry->validLifetime = validLifetime;
    emLogBytesLine(MLE, "new address %d %d %d ", address, 16, localConfigurationFlags,
                   preferredLifetime,
                   validLifetime);
    // New address configuration.
    emApiAddressConfigurationChangeHandler((const EmberIpv6Address *) address,
                                           preferredLifetime,
                                           validLifetime,
                                           localConfigurationFlags);
    emNoteEndDeviceAddressChange();
  } else {
    emLogBytesLine(MLE, "new prefix %d ", address, 16, localConfigurationFlags);
  }

  return entry;
}

void emApiRequestSlaacAddress(const uint8_t *prefix, uint8_t prefixLengthInBits)
{
  // A prefixLength of 16 bytes means we are asking for an address for an 
  // existing prefix of length 8 bytes.  No, it's not pretty.
  GlobalAddressEntry *entry = emGetPrefixAddressEntry(prefix, 64, false);
  if (entry == NULL) {
    emApiRequestSlaacAddressReturn(EMBER_ERR_FATAL, prefix, prefixLengthInBits);
    return;
  }
  // Just choose a random iid.
  // TODO: Should we use emRadioGetRandomNumbers?
  if (prefixLengthInBits < 128) {
    emRadioGetRandomNumbers((uint16_t *)(void *)(entry->address + 8), 4);
  } else {
    MEMCOPY(entry->address, prefix, 16);
  }

  bool success =
    emAddGlobalAddress(entry->address,
                       64,
                       entry->domainId,
                       SLAAC_LIFETIME_DELAY_SEC,
                       SLAAC_LIFETIME_DELAY_SEC,
                       NULL,
                       (EMBER_GLOBAL_ADDRESS_CONFIGURED
                        | EMBER_GLOBAL_ADDRESS_SLAAC),
                       0,
                       true)
    != NULL;

  emApiRequestSlaacAddressReturn(success ? EMBER_SUCCESS : EMBER_ERR_FATAL,
                                 prefix, 
                                 prefixLengthInBits);
}

void emApiResignGlobalAddress(const EmberIpv6Address *address)
{
  GlobalAddressEntry *entry = emGetPrefixAddressEntry(address->bytes, 64, false);
  if (entry == NULL
      || ! (entry->localConfigurationFlags & EMBER_GLOBAL_ADDRESS_CONFIGURED)) {
    emApiResignGlobalAddressReturn(EMBER_BAD_ARGUMENT);
    return;
  }

  if (entry->localConfigurationFlags & EMBER_GLOBAL_ADDRESS_DHCP) {
    EmberNodeId dhcpServerId = emFindNearestDhcpServer(address->bytes, 128, false);
    if (dhcpServerId != 0xFFFE) {
      emSendDhcpAddressRelease(dhcpServerId, entry->serverLongId);
    }
  }
  emDeleteGlobalAddressEntry(address->bytes, 64);
  emApiResignGlobalAddressReturn(EMBER_SUCCESS);
}

void emNoteGlobalPrefix(const uint8_t *prefix,
                        uint8_t prefixLengthInBits,
                        uint8_t prefixFlags,
                        uint8_t domainId)
{
  GlobalAddressEntry *entry = emGetPrefixAddressEntry(prefix, 
                                                      prefixLengthInBits,
                                                      true);
  EmberNodeId dhcpServerId = emFindNearestDhcpServer(prefix,
                                                     prefixLengthInBits,
                                                     true);
  bool isSlaac = (prefixFlags & EMBER_BORDER_ROUTER_SLAAC_FLAG);
  uint8_t address[16];
  MEMCOPY(address, prefix, 8);

  if (entry == NULL) {
    if (dhcpServerId != 0xFFFE
        && dhcpServerId != emberGetNodeId()) {
      // We're going to request an address from the DHCP server.
      memset(address + 8, 0, 8);
      entry = emAddGlobalAddress(address,
                                 prefixLengthInBits,
                                 domainId,
                                 0,
                                 0,
                                 NULL,
                                 EMBER_GLOBAL_ADDRESS_DHCP,
                                 0,
                                 true);
      if (entry != NULL) {
        emApiDhcpServerChangeHandler(prefix, prefixLengthInBits, true);
      }
    } else if (isSlaac) {
      // The application will decide if we will assign a new SLAAC address.
      memset(address + 8, 0, 8);
      entry = emAddGlobalAddress(address,
                                 prefixLengthInBits,
                                 domainId,
                                 0,
                                 0,
                                 NULL,
                                 EMBER_GLOBAL_ADDRESS_SLAAC,
                                 0,
                                 true);
      if (entry != NULL) {
        emApiSlaacServerChangeHandler(prefix, prefixLengthInBits, true);
      }
    }
  } else {
    emNoteEndDeviceAddressChange(); // If we've updated our network data, let
                                    // the parent know about our addresses, even
                                    // if the parent had them at one point.
    if ((entry->localConfigurationFlags
        & (EMBER_GLOBAL_ADDRESS_SLAAC | EMBER_GLOBAL_ADDRESS_CONFIGURED))
        == (EMBER_GLOBAL_ADDRESS_SLAAC | EMBER_GLOBAL_ADDRESS_CONFIGURED)) {
      // BUG: need to walk the SLAAC addresses and remove those which are
      // no longer mentioned in the network data.
      if (! isSlaac) {
        entry->preferredLifetime = 0;
        entry->validLifetime = 0;
        entry->localConfigurationFlags = 0;
        emApiAddressConfigurationChangeHandler((const EmberIpv6Address *) entry->address,
                                               0,
                                               0,
                                               entry->localConfigurationFlags);
        // Not available anymore.
        emApiSlaacServerChangeHandler(prefix, prefixLengthInBits, false);
      }
    }
  } // BUG: should check that the new network data is consistent with
    // what entry has.

}

//----------------------------------------------------------------

static bool isLocalAnycastIid(const uint8_t *iid)
{
  EmberNodeId shortId;
  uint8_t prefix[16];
  if (emGleanShortIdFromInterfaceId(iid, &shortId)) {
    if (emAmLeader && shortId == LEADER_ANYCAST_ADDRESS) {
      return true;
    }
    if (emAmThreadCommissioner() && shortId == COMMISSIONER_ANYCAST_ADDRESS) {
      return true;
    }
    return ((shortId & 0xFFF0) == 0xFC00
            && 64 <= emUncompressContextPrefix(shortId & 0x000F, prefix)
            && emIsOurDhcpServerPrefix(prefix, 64, false));
  } else {
    return false;
  }
}

bool emIsGlobalIpAddress(const uint8_t *address)
{
  GlobalAddressEntry *entry = findGlobalAddressEntry(address, 128, 0, false);
  return (entry != NULL
          && (entry->localConfigurationFlags
              & EMBER_GLOBAL_ADDRESS_CONFIGURED));
}

bool emIsLocalIpAddress(const uint8_t *address)
{
  const uint8_t *iid = address + 8;

  if (emIsFe8Address(address)) {
    return (emIsLocalLl64InterfaceId(iid)
            || (emOnLurkerNetwork() 
                && emIsLocalShortInterfaceId(iid)));
  } else if (emOnLurkerNetwork() 
             && emIsLegacyUla(address)) {
    return (emIsLocalLl64InterfaceId(iid)
            || emIsLocalShortInterfaceId(iid)
            // Handle case where the legacy prefix is also the ML prefix
            || (emIsDefaultGlobalPrefix(address)
                && (emIsLocalMl64InterfaceId(iid)
                    || isLocalAnycastIid(iid))));
  } else if (emIsDefaultGlobalPrefix(address)) {
    return (emIsLocalMl64InterfaceId(iid)
            || emIsLocalShortInterfaceId(iid)
            || isLocalAnycastIid(iid));
  } else {
    return emIsGlobalIpAddress(address);
  }
}

#include "association.h"
#include "zigbee/join.h"

// RFC 4193 - Unique Local Address
// FC00::/7 prefix identifies as Local IPv6 unicast addresses.
// Bit 8 is 1 to show that it is locally defined.  Next 40 bits
// are chosen at random.  Last 16 bits are a subnet ID, which
// we leave as zero.

void emSetDefaultGlobalPrefix(const uint8_t *suppliedPrefix)
{
  if (suppliedPrefix == NULL) {
#if (! defined(UNIX_HOST) && ! defined(UNIX_HOST_SIM))
    meshLocalUlaPrefix[0] = 0xFD;
    meshLocalUlaPrefix[6] = 0;
    meshLocalUlaPrefix[7] = 0;
    MEMCOPY(meshLocalUlaPrefix + 1, emExtendedPanId, 5);
    haveMeshLocalUlaPrefix = true;
#endif //  (! defined(UNIX_HOST) && ! defined(UNIX_HOST_SIM))    
  } else {
    MEMCOPY(meshLocalUlaPrefix, suppliedPrefix, 8);
    haveMeshLocalUlaPrefix = true;
  }
  
  if (haveMeshLocalUlaPrefix) {
    emSetAllThreadNodesMulticastAddresses(meshLocalUlaPrefix);
  }
}

void emSetLegacyUla(const uint8_t *prefix)
{
  #ifdef EMBER_WAKEUP_STACK
  if (prefix == NULL) {
    legacyUla[0] = 0xFD;
    emberStoreHighLowInt32u(legacyUla + 1, halCommonGetRandom());
    legacyUla[5] = (uint8_t)halCommonGetRandom();
    legacyUla[6] = 0;
    legacyUla[7] = 0;
  } else if (memcmp(legacyUla, prefix, 8) != 0) {
    MEMCOPY(legacyUla, prefix, 8);
  }
  #endif
}

#ifdef EMBER_TEST
// For installing a global prefix for testing.
void emSetTestPrefix(const uint8_t *prefix)
{
  emSetDefaultGlobalPrefix(prefix);
}
#endif

