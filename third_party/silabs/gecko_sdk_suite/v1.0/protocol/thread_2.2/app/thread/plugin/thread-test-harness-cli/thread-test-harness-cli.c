// Copyright 2017 Silicon Laboratories, Inc.

// This plugin provides all of the CLI definitions needed for
// to test against the GRL Thread Test Harness.

#include PLATFORM_HEADER
#include CONFIGURATION_HEADER
#include EMBER_AF_API_STACK
#include EMBER_AF_API_DEBUG_PRINT
#include EMBER_AF_API_HAL
#include EMBER_AF_API_COMMAND_INTERPRETER2

#include "app/coap/coap.h"
#include "stack/core/ember-stack.h"
#include "stack/ip/zigbee/join.h"
#include "stack/ip/zigbee/child-data.h"
#include "stack/ip/zigbee/key-management.h"
#include "stack/routing/neighbor/neighbor.h"
#include "stack/ip/ip-address.h"
#include "stack/ip/network-data.h"
#include "stack/ip/mle.h"
#include "stack/ip/tls/dtls-join.h"
#include "stack/ip/commission.h"
#include "stack/mac/802.15.4/802-15-4-ccm.h"
#include "stack/ip/address-management.h"
#include "stack/ip/rip.h"
#include "stack/ip/network-fragmentation.h"
#include "stack/ip/local-server-data.h"
#include "stack/ip/commission-dataset.h"
#include "stack/ip/association.h"
#include "stack/core/ember-stack.h"

// Macros
// ============================================================================
// Can add up to 5 short id and eui64 filters each.
#define MAX_FILTERS 32
#define IS_PENDING 0x01
#define IS_RAW     0x02

// Globals
// ============================================================================
extern uint8_t emRouterUpgradeThreshold;
extern uint8_t emRouterDowngradeThreshold;
extern uint8_t emMinDowngradeNeighbors;
extern uint16_t joinStartTimeQs;
extern uint32_t emForceChildTimeoutSec;
extern bool emForceSlaacAddress;
extern bool emForceDark;
extern bool emForceRejectCoapSolicit;

extern uint32_t emRouterSelectionJitterMs;

extern EmberNodeType emNodeType;

uint8_t emForceLinkQuality[8];
uint8_t emForcedSlaacAddress[16] = {0};
bool emForceSlaacAddress = false;
static uint32_t dataPollPeriod = 0;

// These are initialized at startup by calling clearFilters()
static EmberEui64 euiBlacklist[MAX_FILTERS];
static EmberEui64 euiWhitelist[MAX_FILTERS];

EmberEventControl dataPollEvent;

// Forward declarations
// ============================================================================
static void printOk(void);
static void printFail(void);
static void clearFilters(void);
static bool addEui64Blacklist(EmberEui64 eui64);
static bool addEui64Whitelist(const EmberEui64 *eui64);
static const char *booleanToString(int value);
static void getDataset(const uint8_t *uri, uint8_t flags);
static void setDataset(const uint8_t *uri);
static void scanRequest(uint32_t channelMask);
static uint32_t convertChannelToMask(uint32_t channel);
static void energyRequest(uint32_t channelMask);
static bool readCoapDest(CoapMessage *coap);
static void printDatasetTlvs(const uint8_t *tlvs, const uint8_t *limit);
static void printDataset(uint8_t options, bool isActive, bool raw);
void dataPollEventHandler(void);

extern bool emForm(uint8_t channel,
                   int8_t power,
                   uint16_t panId,
                   const uint8_t *networkId,
                   uint8_t networkIdLength,
                   uint16_t nodeId,
                   const uint8_t *prefix,
                   const uint8_t *legacyUla,
                   EmberNodeType type);

extern void printAllAddresses(void);
extern void printEui(const EmberEui64 *eui);
extern void printNetworkState(void);
extern bool emParseDiagnosticData(EmberDiagnosticData *data,
                           const uint8_t *payload,
                           uint16_t length);
extern uint8_t emSetPhyRadioChannel(uint8_t radioChannel);

// Utility Functions
// ============================================================================
// print functions
static void printOk(void)
{
  emberAfAppPrintln("OK.");
}

static void printFail(void)
{
  emberAfAppPrintln("Failed.");
}

// EUI Functions
static void clearFilters(void)
{
  uint8_t i;
  for (i = 0; i < MAX_FILTERS; i++) {
    MEMSET(euiBlacklist[i].bytes, 0, EUI64_SIZE);
    MEMSET(euiWhitelist[i].bytes, 0, EUI64_SIZE);
  }
}

static bool addEui64Blacklist(EmberEui64 eui64)
{
  uint8_t i;
  for (i = 0; i < MAX_FILTERS; i++) {
    if (isNullEui64(euiBlacklist[i].bytes)
        || MEMCOMPARE(euiBlacklist[i].bytes,
                      eui64.bytes,
                      EUI64_SIZE) == 0) {
      MEMCOPY(euiBlacklist[i].bytes, eui64.bytes, EUI64_SIZE);
      return true;
    }
  }
  return false;
}

static bool addEui64Whitelist(const EmberEui64 *eui64)
{
  uint8_t i;
  for (i = 0; i < MAX_FILTERS; i++) {
    if (isNullEui64(euiWhitelist[i].bytes)
        || MEMCOMPARE(euiWhitelist[i].bytes,
                      eui64->bytes,
                      EUI64_SIZE) == 0) {
      MEMCOPY(euiWhitelist[i].bytes, eui64->bytes, EUI64_SIZE);
      return true;
    }
  }
  return false;
}

// Boolean to string
static const char *booleanToString(int value)
{
  return (value == 0 ? "no" : "yes");
}

static void getDataResponseHandler(EmberCoapStatus status,
                                   EmberCoapCode code,
                                   EmberCoapReadOptions *options,
                                   uint8_t *payload,
                                   uint16_t payloadLength,
                                   EmberCoapResponseInfo *info)
{
  if (status != EMBER_COAP_MESSAGE_RESPONSE) {
    return;
  }

  uint8_t flags = ((uint8_t *) info->applicationData)[0];
  const uint8_t *finger = NULL;
  const uint8_t *limit = payload + payloadLength;

  emberAfAppPrintln("MGMT_GET_%s got %u %sbytes:",
                    (flags & IS_PENDING) ? "PENDING" : "ACTIVE",
                    payloadLength,
                    (flags & IS_RAW) ? "raw " : "");

  if (flags & IS_RAW) {
    emberAfAppPrintBuffer(payload, payloadLength, false);
    emberAfAppPrintln("");
  } else {
    for (finger = payload; finger < limit; finger += (finger[1] + 2)) {
      emberAfAppPrint(" %X: [", finger[0]);
      emberAfAppPrintBuffer((uint8_t *)finger + 2, finger[1], false);
      emberAfAppPrintln("]");
    }
  }
}

// Called by the getData commands
static void getDataset(const uint8_t *uri, uint8_t flags)
{
  EmberIpv6Address destination;

  if (!emberGetIpArgument(0, destination.bytes)) {
    emberAfAppPrintln("Can't parse IP address");
    return;
  }

  uint8_t length = 0;
  const uint8_t *tlvs = emberStringCommandArgument(1, &length);

  uint8_t payload[2 + 32];
  if (0 < length) {
    if (32 < length) {
      length = 32;
    }
    payload[0] = DATASET_GET_TLV;
    payload[1] = length;
    MEMCOPY(payload + 2, tlvs, length);
  }

  EmberCoapSendInfo info;
  MEMSET(&info, 0, sizeof(info));
  info.responseAppData = &flags;
  info.responseAppDataLength = 1;
  info.localPort = emStackCoapPort;
  info.remotePort = emStackCoapPort;

  if (emberCoapPost(&destination,
                    uri,
                    payload,
                    (length == 0 ? 0 : length + 2),
                    getDataResponseHandler,
                    &info)
      == EMBER_SUCCESS) {
    printOk();
  } else {
    printFail();
  }
}

// called by the dataset setting commands
static void setDataset(const uint8_t *uri)
{
  uint8_t length = 0;
  uint8_t *tlvs = emberStringCommandArgument(0, &length);
  uint16_t shortDest = emGetLeaderNodeId();
  if (emberCommandArgumentCount() > 1) {
    shortDest = emberUnsignedCommandArgument(1);
  }

  // In case the session ID TLV was not passed in and we're a commissioner,
  // make sure to include it.  This fixes a test harness issue.
  // TODO:  We need APIs for the set/get active dataset calls. Continuing to
  // use CLI commands this way is not sustainable and also error prone.

  bool haveSessionId = false;
  uint8_t *finger = tlvs;
  uint8_t i = 0;
  while (i < length) {
    if (*finger == DATASET_SESSION_ID_TLV) {
      haveSessionId = true;
      break;
    } else {
      uint8_t tlvLength = finger[1];
      i += 2 + tlvLength;
      finger = tlvs + i;
    }
  }

  if (!haveSessionId && emAmThreadCommissioner()) {
    emberAfAppPrintln("Adding missing TLV for session ID: %d",
                      emCommissionerSessionId);
    finger = emAddSessionIdTlv(finger);
    length += 4;
  }

  bool result;

  if (emAmLeader) {
    EmberCoapRequestInfo info;
    MEMSET(&info, 0, sizeof(info));
    result = emHandleCommissionDatasetPost(uri, tlvs, length, &info);
  } else {
    CoapMessage message;
    emInitStackMl16CoapMessage(&message, shortDest, NULL, 0);
    Buffer bytes = emFillBuffer(tlvs, length);

    if (bytes == NULL_BUFFER) {
      emberAfAppPrintln("OOM");
      return;
    }
    result = (emSubmitCoapMessage(&message, uri, bytes) == EMBER_SUCCESS);
  }
  
  if (result) {
    printOk();
  } else {
    printFail();
  }
}

// called by the dataset print commands
static void printDataset(uint8_t options, bool isActive, bool raw)
{
  uint8_t payload[254];
  MEMSET(payload, 0, sizeof(payload));
  uint8_t *finger = payload;

  if (!emCopyDatasetTlvs(&finger, payload + sizeof(payload), options)) {
    printFail();
    return;
  }

  emberAfAppPrint("%s", isActive ? "Active Dataset: " : "Pending Dataset: ");
  if (raw) {
    emberAfAppPrintBuffer(payload, finger - payload, false);
  } else {
    printDatasetTlvs(payload, finger);
  }
  printOk();
}

static void printDatasetTlvs(const uint8_t *tlvs, const uint8_t *limit)
{
  const uint8_t *finger;

  for (finger = tlvs; finger < limit; finger += (finger[1] + 2)) {
    if (finger + 2 > limit) {
      emberAfAppPrintln("Hit unexpected EOL");
      break;
    }

    uint16_t length = finger[1];

    emberAfAppPrint("TLV: %u | length: %u | bytes: ",
                    finger[0],
                    length);
    emberAfAppPrintBuffer(finger + 2, length, false);
    emberAfAppPrintln("");
  }

  emberAfAppPrintln("done");
}

// Used in all of the scan request commands
static void scanRequest(uint32_t channelMask)
{
  EmberIpv6Address destination;

  if (!emberGetIpArgument(0, destination.bytes)) {
    emberAfAppPrintln("Can't parse IP address");
    return;
  }

  uint8_t channelInput = emberUnsignedCommandArgument(1);
  uint16_t panId = emberUnsignedCommandArgument(2);

  emberAfAppPrintln("Sending PAN ID scan request, "
                     "channel mask: 0x%4X, panId: 0x%2X",
                     channelMask,
                     panId);

  emComSendPanIdScanRequest(destination.bytes, channelMask, panId);
  printOk();
}

// Convert a channel to a bit mask
static uint32_t convertChannelToMask(uint32_t channel)
{
  return (channel == 0
          ? EMBER_ALL_802_15_4_CHANNELS_MASK
          : BIT(channel));
}

// Used in the energy request commands
static void energyRequest(uint32_t channelMask)
{
  EmberIpv6Address destination;

  if (!emberGetIpArgument(0, destination.bytes)) {
    emberAfAppPrintln("Can't parse IP address");
    return;
  }

  uint8_t scanCount = emberUnsignedCommandArgument(2);
  uint16_t scanPeriod = emberUnsignedCommandArgument(3);
  uint16_t scanDuration = emberUnsignedCommandArgument(4);

  emComSendEnergyScanRequest(destination.bytes,
                             channelMask,
                             scanCount,
                             scanPeriod,
                             scanDuration);
  printOk();
}

// Callbacks/Handlers
// ============================================================================
void emberDiagnosticAnswerHandler(EmberStatus status,
                                  const EmberIpv6Address *remoteAddress,
                                  const uint8_t *payload,
                                  uint8_t payloadLength)
{
  if (status != EMBER_SUCCESS) {
    emberAfAppPrintln("Error: can't send diagnostic request, status: %u",
                       status);
    return;
  }

  EmberDiagnosticData diagnosticData;

  if (!emParseDiagnosticData(&diagnosticData, payload, payloadLength)) {
    emberAfAppPrintln("Can't parse diagnostic data");
    return;
  }

  emberAfAppPrint("[Received diagnostic response");

  if (emberDiagnosticDataHasTlv(&diagnosticData, DIAGNOSTIC_MAC_EXTENDED_ADDRESS)) {
    emberAfAppPrint("\nMac extended address: ");
    emberAfAppPrintBuffer(diagnosticData.macExtendedAddress, 8, false);
    emberAfAppPrintln("");
  }

  if (emberDiagnosticDataHasTlv(&diagnosticData, DIAGNOSTIC_ADDRESS_16)) {
    emberAfAppPrintln("Address16: 0x%2X", diagnosticData.address16);
  }

  if (emberDiagnosticDataHasTlv(&diagnosticData, DIAGNOSTIC_MODE)) {
    emberAfAppPrintln("Mode: %u", diagnosticData.mode);
  }

  if (emberDiagnosticDataHasTlv(&diagnosticData, DIAGNOSTIC_TIMEOUT)) {
    emberAfAppPrintln("Timeout: %u", diagnosticData.timeout);
  }

  if (emberDiagnosticDataHasTlv(&diagnosticData, DIAGNOSTIC_CONNECTIVITY)) {
    emberAfAppPrint("connectivity: ");
    emberAfAppPrintBuffer(diagnosticData.connectivity + 1,
                          diagnosticData.connectivity[0],
                          false);
    emberAfAppPrintln("");
  }

  if (emberDiagnosticDataHasTlv(&diagnosticData, DIAGNOSTIC_ROUTING_TABLE)) {
    emberAfAppPrint("Routing table: ");
    emberAfAppPrintBuffer(diagnosticData.routingTable + 1,
                          diagnosticData.routingTable[0],
                          false);
    emberAfAppPrintln("");
  }

  if (emberDiagnosticDataHasTlv(&diagnosticData, DIAGNOSTIC_LEADER_DATA)) {
    emberAfAppPrint("Leader data: ");
    emberAfAppPrintBuffer(diagnosticData.leaderData + 1,
                          diagnosticData.leaderData[0],
                          false);
    emberAfAppPrintln("");
  }

  if (emberDiagnosticDataHasTlv(&diagnosticData, DIAGNOSTIC_NETWORK_DATA)) {
    emberAfAppPrint("Network data: ");
    emberAfAppPrintBuffer(diagnosticData.networkData + 1,
                          diagnosticData.networkData[0],
                          false);
    emberAfAppPrintln("");
  }

  if (emberDiagnosticDataHasTlv(&diagnosticData, DIAGNOSTIC_IPV6_ADDRESS_LIST)) {
    emberAfAppPrint("IPv6 address list: ");
    emberAfAppPrintBuffer(diagnosticData.ipv6AddressList + 1,
                          diagnosticData.ipv6AddressList[0],
                          false);
    emberAfAppPrintln("");
  }

  if (emberDiagnosticDataHasTlv(&diagnosticData, DIAGNOSTIC_MAC_COUNTERS)) {
    emberAfAppPrint("Mac counters list: ");
    emberAfAppPrintBuffer(diagnosticData.macCounters + 1,
                          diagnosticData.macCounters[0],
                          false);
    emberAfAppPrintln("");
  }

  if (emberDiagnosticDataHasTlv(&diagnosticData, DIAGNOSTIC_BATTERY_LEVEL)) {
    emberAfAppPrintln("Battery level: %u", diagnosticData.batteryLevel);
  }

  if (emberDiagnosticDataHasTlv(&diagnosticData, DIAGNOSTIC_VOLTAGE)) {
    emberAfAppPrintln("Voltage: %u", diagnosticData.voltage);
  }

  if (emberDiagnosticDataHasTlv(&diagnosticData, DIAGNOSTIC_CHILD_TABLE)) {
    emberAfAppPrint("Child table: ");
    emberAfAppPrintBuffer(diagnosticData.childTable + 1,
                          diagnosticData.childTable[0],
                          false);
    emberAfAppPrintln("");
  }

  if (emberDiagnosticDataHasTlv(&diagnosticData, DIAGNOSTIC_CHANNEL_PAGES)) {
    emberAfAppPrint("Channel pages: ");
    emberAfAppPrintBuffer(diagnosticData.channelPages + 1,
                          diagnosticData.channelPages[0],
                          false);
  }

  emberAfAppPrintln("]");
}

static bool readCoapDest(CoapMessage *coap)
{
  if (emberCommandArgumentCount() == 2) {
    EmberIpv6Address destination;
    if (emberGetIpArgument(1, destination.bytes)) {
      emInitStackCoapMessage(coap, &destination, NULL, 0);
    } else {
      emberAfAppPrintln("Bad IP argument.");
      return false;
    }
  } else {
    emInitStackMl16CoapMessage(coap, 
                               emGetLeaderNodeId(), 
                               NULL, 
                               0);
  }
  return true;
}

// Test commands
// ============================================================================
// print_child
void printChildTableCommand(void)
{
  uint8_t i;
  emberAfAppPrintln("\nChild table (%u of %u)",
                     emChildCount(),
                     emberChildTableSize);
  for (i = 0; i < emberChildTableSize; i++) {
    if (emChildIdTable[i] != 0xFFFF) {
      emberAfAppPrint("%u. 0x%2x ", i, emChildIdTable[i]);
      printEui((EmberEui64 *)(emChildLongIdTable + (i << 3)));
      emberAfAppPrintln(" status:0x%2x t:%lu timeout:%lu fc:%lu clt:%lu",
                        emChildStatus[i],
                        emChildTimers[i],
                        emChildTimeouts[i],
                        emChildFrameCounters[i],
                        emChildSecondsSinceLastTransaction(i));
    }
  }
  emberAfAppPrintln("Child table finished.");
}

// force_child_timeout
void forceChildTimeoutCommand(void)
{
  emForceChildTimeoutSec = emberUnsignedCommandArgument(0);
  emNoteSuccessfulPoll();
  printOk();
}

// get_child_timeout
void getChildTimeoutCommand(void)
{
  if (emNodeType == EMBER_END_DEVICE
      || emNodeType == EMBER_SLEEPY_END_DEVICE) {
    emberAfAppPrintln("Timeout: %u", emGetPollTimeout());
  }
  else {
    emberAfAppPrintln("Error: Not an End Device");
  }
}

// request_router_id
// This forces the node to request a specific router/short ID in the address
// solicit to the parent.  For interop testing ONLY.
void requestRouterIdCommand(void)
{
  uint8_t routerId = emberUnsignedCommandArgument(0);
  // Setting emPreviousNodeId to this value makes the node think it
  // used to have this node ID, effectively leading it to request this
  // short ID.
  emPreviousNodeId = emRipIdToShortMacId(routerId);
  printOk();
}

// blacklist_eui
void blacklistEuiCommand(void)
{
  uint8_t i, count = emberCommandArgumentCount();
  for (i = 0; i < count; i++) {
    EmberEui64 eui64;
    emberGetEui64Argument(i, &eui64);
    if (addEui64Blacklist(eui64)) {
      emberAfAppPrint("Messages from eui ");
      printEui(&eui64);
      emberAfAppPrintln(" will be dropped");
    } else {
      emberAfAppPrintln("Can only add up to %d eui filters", MAX_FILTERS);
    }
  }
}

// whitelist_eui
void whitelistEuiCommand(void)
{
  uint8_t i, count = emberCommandArgumentCount();
  bool setAnEui = false;

  for (i = 0; i < count; i++) {
    EmberEui64 eui64;
    emberGetEui64Argument(i, &eui64);
    if (addEui64Whitelist(&eui64)) {
      emberAfAppPrint("Messages from eui ");
      printEui(&eui64);
      emberAfAppPrintln(" will be allowed");
      setAnEui = true;
    } else {
      emberAfAppPrintln("Can only add up to %d eui filters",
                        MAX_FILTERS);
    }
  }

  if (setAnEui) {
    emberAfAppPrintln("All other euis will be dropped.");
  }
}

// clear_filters
void clearFiltersCommand(void)
{
  clearFilters();
  emberAfAppPrintln("OK.");
}

// print_filters
void printFiltersCommand(void)
{
  uint8_t i;
  bool printed = false;
  emberAfAppPrintln("Blacklisted euis (these take priority): ");
  for (i = 0; i < MAX_FILTERS; i++) {
    if (!isNullEui64(&euiBlacklist[i])) {
      emberAfAppPrint("%d. ", (i+1));
      printEui(&euiBlacklist[i]);
      emberAfAppPrintln("");
      printed = true;
    }
  }
  if (!printed) {
    emberAfAppPrintln("none.");
  } else {
    printed = false;
  }

  emberAfAppPrintln("Whitelisted euis: ");
  for (i = 0; i < MAX_FILTERS; i++) {
    if (!isNullEui64(&euiWhitelist[i])) {
      emberAfAppPrint("%d. ", (i+1));
      printEui(&euiWhitelist[i]);
      emberAfAppPrintln("");
      printed = true;
    }
  }

  if (!printed) {
    emberAfAppPrintln("none.");
  } else {
    printed = false;
  }
}

// set_lq
void setLinkQualityCommand(void)
{
  EmberEui64 eui64;
  emberGetEui64Argument(0, &eui64);
  uint8_t linkQuality = emberUnsignedCommandArgument(1);
  if (linkQuality > 3) {
    emberAfAppPrintln("Incorrect link quality. should be 0-3.");
    return;
  }
  uint8_t index = emRouterIndexByLongId(eui64.bytes);

  if (index == 0xFF) {
    emForceLinkQualityPointer = emForceLinkQuality;
    MEMCOPY(emForceLinkQuality, eui64.bytes, 8);
    emForceResponseLinkQuality = linkQuality;
    emberAfAppPrint("Forcing link quality to be: %u for: ",
                     emForceResponseLinkQuality);
    emberAfAppPrintBuffer(emForceLinkQuality, 8, false);
    emberAfAppPrintln("\nOK.");
    return;
  }

  uint8_t routerId = emRipTable[index].routerId;
  if (routerId > 4) {
    emberAfAppPrintln("Looked up router id %d.  must be < 5.", routerId);
  }
  emCustomLinkQualities[routerId] = linkQuality;
  emberAfAppPrintln("OK.");
}

// jpake_port
void jpakePortCommand(void)
{
  emUdpJoinPort = emberUnsignedCommandArgument(0);
  emberAfAppPrintln("OK.");
}

// remove_router_by_short_id
void removeRouterByShortIdCommand(void)
{
  uint16_t shortId = (uint16_t) emberUnsignedCommandArgument(0);
  uint8_t *eui64 = (uint8_t *) emNeighborLookupLongId(shortId);

  if (eui64 == NULL) {
    emberAfAppPrintln("Can't find EUI64 for short ID: %d",
                       shortId);
  } else {
    emUnassignRipId(eui64);
    printOk();
  }
}

// remove_prefix
void removePrefixCommand(void)
{
  uint8_t prefixLengthBytes;
  const uint8_t *prefix = emberStringCommandArgument(0, &prefixLengthBytes);
  if (prefixLengthBytes > 16) {
    prefixLengthBytes = 16;
  }
  emRemovePrefixTlv(prefix, prefixLengthBytes*8);
  printOk();
}

// version
void versionCommand(void)
{
  emberAfAppPrintln("thread-test-app version %u.%u.%u build %u, change %lu",
                     emberVersion.major,
                     emberVersion.minor,
                     emberVersion.patch,
                     emberVersion.build,
                     emberVersion.change);
}

// status
void statusCommand(void)
{
  uint8_t *identifier = (uint8_t *) emLocalIslandId();
  emberAfAppPrint("channel: %u | "
                  "pan id: 0x%2x | "
                  "short id: 0x%2x | "
                  "parent id: 0x%2x | "
                  "parent long id: %x | "
                  "leader: %s | "
                  "full router: %s | "
                  "network fragment identifier: ",
                  emberGetRadioChannel(),
                  emberGetPanId(),
                  emberGetNodeId(),
                  emParentId,
                  emParentLongId,
                  8,
                  booleanToString(emAmLeader),
                  booleanToString(emCheckStackConfig(STACK_CONFIG_FULL_ROUTER)));
  if (identifier == NULL) {
    emberAfAppPrintln("(null)");
  } else {
    emberAfAppPrintln("%x-%x-%x-%x-%x",
                       identifier[0],
                       identifier[1],
                       identifier[2],
                       identifier[3],
                       identifier[4]);
  }
  emberGetRadioPower(); // emberGetRadioPowerReturn can output the power.
}

// set_sequence
void setSequenceCommand(void)
{
  uint32_t sequenceNumber = emberUnsignedCommandArgument(0);
  emSetNetworkKeySequence(sequenceNumber);
  printOk();
}

// com_remove
void removeCommissionerCommand(void)
{
  emberStopCommissioning();
  emberAfAppPrintln("OK. No commissioner");
}

// native_petition
void petitionCommand(void)
{
  emBecomeExternalCommissioner("My name", 7);
}

// set_prov_url
void setProvisioningUrlCommand(void)
{
  emProvisioningUrlLength = emberGetStringArgument(0,
                                                   emProvisioningUrl,
                                                   sizeof(emProvisioningUrl),
                                                   false);
  emberAfAppPrintln("OK.");
}

// need_all_network_data
void needAllNetworkDataCommand(void)
{
  emNeedAllNetworkData(emberUnsignedCommandArgument(0) == true);
  printOk();
}

// set_fragment_timeout
void setFragmentTimeoutCommand(void)
{
  emLeaderTimeoutMs = emberUnsignedCommandArgument(0);
  emberAfAppPrintln("Set leader timeout to %u ms.", emLeaderTimeoutMs);
  emStartLeaderTimeout();
  printOk();
}

// get_coap_diagnostics
void getCoapDiagnosticsCommand(void)
{
  uint8_t tlvs[100];
  uint8_t length = emberGetStringArgument(1,
                                          tlvs,
                                          sizeof(tlvs),
                                          false);
  EmberIpv6Address destination;
  assert(length <= sizeof(tlvs));

  if (!emberGetIpArgument(0, destination.bytes)) {
    emberAfAppPrintln("Can't parse IP address");
    return;
  }

  emberSendDiagnosticGet(&destination, tlvs, length);
  printOk();
}

// reset_coap_diagnostics
void resetCoapDiagnosticsCommand(void)
{
  uint8_t tlvs[100];
  uint8_t length = emberGetStringArgument(1,
                                          tlvs,
                                          sizeof(tlvs),
                                          false);
  EmberIpv6Address destination;
  assert(length <= sizeof(tlvs));

  if (!emberGetIpArgument(0, destination.bytes)) {
    emberAfAppPrintln("Can't parse IP address");
    return;
  }

  emberSendDiagnosticReset(&destination, tlvs, length);
  printOk();
}

// get_active_dataset
void getActiveDatasetCommand(void)
{
  getDataset(MANAGEMENT_ACTIVE_GET_URI, 0);
}

// get_pending_dataset
void getPendingDatasetCommand(void)
{
  getDataset(MANAGEMENT_PENDING_GET_URI, IS_PENDING);
}


// set_active_dataset
void setActiveDatasetCommand(void)
{
  setDataset(MANAGEMENT_ACTIVE_SET_URI);
}

// set_pending_dataset
void setPendingDatasetCommand(void)
{
  setDataset(MANAGEMENT_PENDING_SET_URI);
}

// print_active_dataset
void printActiveDatasetCommand(void)
{
  printDataset(COMPLETE_ACTIVE_DATASET, true, false);
}

// print_pending_datset
void printPendingDatasetCommand(void)
{
  printDataset(COMPLETE_PENDING_DATASET, false, false);
}

// set_active_dataset_bytes
void setActiveDatasetBytesCommand(void)
{
  uint8_t length;
  uint8_t *tlvs = emberStringCommandArgument(0, &length);
  OperationalDatasetTlvs dataset;
  emGetActiveDatasetTlvs(&dataset);
  if (emMergeTlvs(&dataset, tlvs, length)) {
    emSetActiveDataset(&dataset);
    printOk();
  } else {
    printFail();
  }
}

// send_pan_id_scan_request
void sendPanIdScanRequestCommand(void)
{
  scanRequest(emberUnsignedCommandArgument(1));
}

// send_energy_scan_request
void sendEnergyScanRequestCommand(void)
{
  energyRequest(emberUnsignedCommandArgument(1));
}

// increment_sequence
void incrementSequenceCommand(void)
{
  uint32_t sequence;
  if (emGetNetworkKeySequence(&sequence)) {
    emSetNetworkKeySequence(sequence + 1);
    printOk();
  } else {
    printFail();
  }
}

// use_mle_discovery
void useMleDiscoveryCommand(void)
{
  emUseMleDiscovery = (emberCommandArgumentCount() > 0
                       ? (bool) emberUnsignedCommandArgument(0)
                       : true);

  uint8_t *securityPolicy = (uint8_t *) emGetActiveDataset()->securityPolicy;
  if (emUseMleDiscovery) {
    securityPolicy[2] |= BEACONS_ENABLED;
  } else {
    securityPolicy[2] &= ~BEACONS_ENABLED;
  }

  OperationalDatasetTlvs dataset;
  emGetActiveDatasetTlvs(&dataset);
  uint8_t securityPolicyTlv[5] = {0};
  securityPolicyTlv[0] = COMMISSION_SECURITY_POLICY_TLV;
  securityPolicyTlv[1] = 3;
  MEMCOPY(securityPolicyTlv + 2, securityPolicy, 3);
  if (emMergeTlvs(&dataset, securityPolicyTlv, 5)) {
    emSetActiveDataset(&dataset);
    printOk();
  } else {
    emberAfAppPrintln("Fatal error.");
  }
}

// randomize_mac_extended_id
void randomizeMacExtendedIdCommand(void)
{
  emRandomizeMacExtendedId = (bool)emberUnsignedCommandArgument(0);
  printOk();
}

// set_island_id
void setIslandIdCommand(void)
{
  if (emberCommandArgumentCount() > 0) {
    uint8_t islandIdLength;
    uint8_t *islandId = emberStringCommandArgument(0, &islandIdLength);

    if (islandIdLength != ISLAND_ID_SIZE) {
      emberAfAppPrintln("Island ID needs to be %u bytes",
                         ISLAND_ID_SIZE);
      return;
    }
    emForceIslandId = true;
    MEMCOPY(emForcedIslandId, islandId, 5);
  }

  printOk();
}

// set_router_selection_parameters
void setRouterSelectionParametersCommand(void)
{
  emRouterUpgradeThreshold = emberUnsignedCommandArgument(0);
  emRouterDowngradeThreshold = emberUnsignedCommandArgument(1);
  emMinDowngradeNeighbors = emberUnsignedCommandArgument(2);
  printOk();
}

// send_announce_begin
void sendAnnounceBeginCommand(void)
{
  EmberIpv6Address destination;

  if (!emberGetIpArgument(0, destination.bytes)) {
    return;
  }

  uint32_t mask =
    convertChannelToMask(emberUnsignedCommandArgument(2));

  if (emSendManagementAnnounceBegin
      (&destination,
       emberUnsignedCommandArgument(1),      // channel page
       mask,                                 // channel mask
       emberUnsignedCommandArgument(3),      // count
       emberUnsignedCommandArgument(4))      // period
      == EMBER_SUCCESS) {
    printOk();
  } else {
    printFail();
  }
}

// force_slaac_address
void forceSlaacAddressCommand(void)
{
  uint8_t ipAddress[16];

  if (!emberGetIpArgument(0, ipAddress)) {
    printFail();
    return;
  }

  emForceSlaacAddress = true;
  MEMCOPY(emForcedSlaacAddress, ipAddress, 16);
  printOk();
}

// com_get
void comGetCommand(void)
{
  CoapMessage coap;
  uint8_t byteCount;
  uint8_t *getBytes = emberStringCommandArgument(0, &byteCount);
  Buffer getBuffer = NULL_BUFFER;

  if (! readCoapDest(&coap)) {
    return;
  }

  // create the GET TLV
  if (byteCount > 0) {
    getBuffer = emAllocateBuffer(byteCount + 2);
    uint8_t *finger = emGetBufferPointer(getBuffer);
    *finger++ = COMMISSION_GET_TLV;
    *finger++ = byteCount;
    MEMCOPY(finger, getBytes, byteCount);
  }

  if (emSubmitCoapMessage(&coap, COMMISSIONER_GET_URI, getBuffer)
      == EMBER_SUCCESS) {
    printOk();
  } else {
    printFail();
  }
}

// com_set
void comSetCommand(void)
{
  CoapMessage coap;

  if (! readCoapDest(&coap)) {
    return;
  }

  uint16_t length;
  const uint8_t *tlvs = emberLongStringCommandArgument(0, &length);
  Buffer setBuffer = emFillBuffer(tlvs, length);
  
  if (emSubmitCoapMessage(&coap, COMMISSIONER_SET_URI, setBuffer)
      == EMBER_SUCCESS) {
    printOk();
  } else {
    printFail();
  }
}

// print_session_id
void printSessionIdCommand(void)
{
  if (emAmThreadCommissioner()) {
    emberAfAppPrintln("session ID: 0x%2X", emCommissionerSessionId);
  } else {
    emberAfAppPrintln("No session ID is available for you at this time.");
  }
}

// set_min_delay_timer
void forceDelayTimerCommand(void)
{
  emForceDelayTimerMs = emberUnsignedCommandArgument(0);
  printOk();
}


// stretch_pskc
void stretchPskcCommand(void)
{
  uint8_t stretched[16];
  uint8_t length;
  uint8_t *passphrase = emberStringCommandArgument(0, &length);
  emDerivePskc(passphrase,
               length,
               emExtendedPanId,
               emNetworkId,
               stretched);

  uint8_t pskcTlv[18];
  uint8_t *finger = pskcTlv;
  *finger++ = DATASET_PSKC_TLV;
  *finger++ = 16;
  MEMCOPY(finger, stretched, 16);
  finger += 16;

  emberAfAppPrint("Stretched PSKC TLV: ");
  emberAfAppPrintBuffer(stretched, 16, false);
  emberAfAppPrintln("");
  printOk();
}

// set_router_selection_jitter_ms
void setRouterSelectionJitterMsCommand(void)
{
  emRouterSelectionJitterMs = emberUnsignedCommandArgument(0);
  printOk();
}

// form_pan
void formPanCommand(void)
{
  uint8_t channel = emberUnsignedCommandArgument(0);
  int8_t power = emberSignedCommandArgument(1);
  uint16_t panId = emberUnsignedCommandArgument(3);
  uint8_t networkIdLength;
  uint8_t *networkId = emberStringCommandArgument(4, &networkIdLength);
  uint8_t type = emberUnsignedCommandArgument(2);
  uint8_t prefix[16];

  if ((type != EMBER_ROUTER) && (type != EMBER_LURKER)) {
    emberAfAppPrintln("Node type must be router (%d) or lurker (%d)",
                      EMBER_ROUTER,
                      EMBER_LURKER);
    return;
  }

  if (networkIdLength > 16) {
    networkIdLength = 16;

    // print out a warning
    uint8_t networkIdCopy[17];
    MEMCOPY(networkIdCopy, networkId, 16);
    networkIdCopy[16] = 0;

    emberAfAppPrintln("Warning: shortened network ID '%s' to 16 characters: '%s'",
                      networkId,
                      networkIdCopy);
  }

  if (emberGetIpArgument(5, prefix)) {
    if (emberCommandArgumentCount() > 6) {
      EmberNetworkParameters params;
      emberGetStringArgument(6, params.extendedPanId, EXTENDED_PAN_ID_SIZE, false);
      emApiConfigureNetwork(&params, EMBER_EXTENDED_PAN_ID_OPTION);
    }

    if (!emForm(channel,
                power,
                panId,
                networkId,
                networkIdLength,
                0,
                prefix,
                prefix,
                type)) {
      emberAfAppPrintln("WARNING: node already on a network");
    }
  }
}

// set_eui
void setEuiCommand(void)
{
  EmberEui64 eui64;
  emberGetEui64Argument(0, &eui64);
  emSetEui64(&eui64);
  emberAfAppPrint("set eui ");
  printEui((EmberEui64 *)emMacExtendedId);
  emberAfAppPrintln("");
}

// print_eui
void printEuiCommand(void)
{
  printEui((EmberEui64 *)emMacExtendedId);
  emberAfAppPrintln("");
  printOk();
}

// set_channel
void setChannelCommand(void)
{
  uint8_t channel = emberUnsignedCommandArgument(0);
  EmberStatus status = emSetPhyRadioChannel(channel);
  emberAfAppPrintln("channel %u (0x%x), status 0x%x", channel, channel, status);
}

// set_pan_id
void setPanIdCommand(void)
{
  emSetPanId(emberUnsignedCommandArgument(0));
  emberAfAppPrintln("pan id 0x%2x", emberGetPanId());
}

// print_rip
void printRipCommand(void)
{
  emPrintRipTable(APP_SERIAL);
  emberAfAppPrintln("");
  printOk();
}

// network_state
void networkStateCommand(void)
{
  printNetworkState();
  emberAfAppPrintln("");
  printOk();
}

// com_add_steering
void addSteeringDataCommand(void)
{
  EmberEui64 eui64;
  emberGetEui64Argument(0, &eui64);
  emberAddSteeringEui64(&eui64);
  printOk();
}

// com_petition
void commissionerPetitionCommand(void)
{
  uint8_t commissionerIdLength;
  uint8_t *commissionerId = emberStringCommandArgument(0, &commissionerIdLength);
  emberBecomeCommissioner(commissionerId, commissionerIdLength);
}

// com_steering
void sendSteeringDataCommand(void)
{
  emberSendSteeringData();
  printOk();
}

// com_join_mode
void setJoiningModeCommand(void)
{
  emberSetJoiningMode((EmberJoiningMode )emberUnsignedCommandArgument(0),
                      emberUnsignedCommandArgument(1));
  printOk();
}

// set_key
void setKeyCommand(void)
{
  uint8_t length;
  uint8_t key[EMBER_ENCRYPTION_KEY_SIZE];
  uint8_t *bytes = emberStringCommandArgument(0, &length);
  uint8_t keySequence = emberUnsignedCommandArgument(1);
  if (length >= EMBER_ENCRYPTION_KEY_SIZE) {
    MEMSET(key, 0, EMBER_ENCRYPTION_KEY_SIZE);
    MEMCOPY(key, bytes, EMBER_ENCRYPTION_KEY_SIZE);
  }
  emSetNetworkMasterKey(key, keySequence);
  emberAfAppPrint("key: [");
  emberAfAppPrintBuffer(key, EMBER_ENCRYPTION_KEY_SIZE, false);
  emberAfAppPrintln("], sequence: %d", emberUnsignedCommandArgument(1));
}

// print_ip_addresses
void printIpAddressesCommand(void)
{
  printAllAddresses();
}

// print_global_addresses
void globalAddressTableCommand(void)
{
  emberAfAppPrintln("printing available global addresses...");
  if (emberCommandArgumentCount() == 0) {
    emberGetGlobalAddresses(NULL, 0);
  } else {
    uint8_t prefixLengthBytes;
    const uint8_t *prefix = emberStringCommandArgument(0, &prefixLengthBytes);
    emberGetGlobalAddresses(prefix, prefixLengthBytes*8);
  }
}

// external_route
void configureExternalRouteCommand(void)
{
  uint8_t flags = emberUnsignedCommandArgument(0);
  uint8_t prefixLengthBytes;
  const uint8_t *prefix = emberStringCommandArgument(1, &prefixLengthBytes);
  if (prefixLengthBytes > 8) {
    prefixLengthBytes = 8;
  }

  emberConfigureExternalRoute(flags,
                              prefix,
                              prefixLengthBytes*8,
                              0); // We just use a default domain id = 0.
  printOk();
}

// allow_commissioner
void allowCommissionerCommand(void)
{
  emberAllowNativeCommissioner(emberUnsignedCommandArgument(0) == true);
}

// configure
void configureCommand(void)
{
  uint8_t networkIdLength;
  const uint8_t *networkId = emberStringCommandArgument(2, &networkIdLength);

  if (EMBER_NETWORK_ID_SIZE < networkIdLength) {
    networkIdLength = EMBER_NETWORK_ID_SIZE;
  }

  uint8_t ulaPrefix[16];
  uint8_t extendedPanIdLength;
  uint8_t *extendedPanId = emberStringCommandArgument(4, &extendedPanIdLength);
  uint8_t keyLength;
  const EmberKeyData *key =
    (EmberKeyData *) emberStringCommandArgument(5, &keyLength);
  uint16_t panId = 0xFFFF;
  uint32_t keySequence = 0;
  if (emberCommandArgumentCount() > 6) {
    panId = emberUnsignedCommandArgument(6);
    if (emberCommandArgumentCount() > 7) {
      keySequence = emberUnsignedCommandArgument(7);
    }
  }

  if (keyLength == EMBER_ENCRYPTION_KEY_SIZE
      && emberGetIpArgument(3, ulaPrefix)) {
    emberCommissionNetwork(emberUnsignedCommandArgument(0), // preferred channel
                           emberUnsignedCommandArgument(1), // fallback channel mask
                           networkId,
                           networkIdLength,
                           panId,
                           ulaPrefix,
                           extendedPanId,
                           key,
                           keySequence);
  } else {
    emberAfAppPrintln("size(s) is invalid");
  }
}

// gateway
void configureGatewayCommand(void)
{
  uint8_t borderRouterFlags = emberUnsignedCommandArgument(0);

  bool isStable = emberUnsignedCommandArgument(1);
  uint8_t prefixLengthBytes;
  const uint8_t *prefix = emberStringCommandArgument(2, &prefixLengthBytes);
  if (prefixLengthBytes > 16) {
    prefixLengthBytes = 16;
  }
  uint32_t preferredLifetime = emberUnsignedCommandArgument(3);
  uint32_t validLifetime = emberUnsignedCommandArgument(4);

  emberConfigureGateway(borderRouterFlags,
                        isStable,
                        (prefixLengthBytes == 0)
                         ? NULL
                         : prefix,
                        prefixLengthBytes*8,
                        0, // We just use a default domain id = 0.
                        preferredLifetime,
                        validLifetime);
  printOk();
}

// data polling
void dataPollEventHandler(void)
{
  if (dataPollPeriod == 0) {
    emberEventControlSetInactive(dataPollEvent);
  } else {
    if (emberNetworkStatus() == EMBER_JOINED_NETWORK_ATTACHED) {
      emberPollForData();
    }
    emberEventControlSetDelayMS(dataPollEvent, dataPollPeriod);
  }
}

void emberPollForDataReturn(EmberStatus status)
{
  if (status != EMBER_SUCCESS) {
    emberAfAppPrintln("poll failed, status:0x%x", status);
  }
}

// data_poll
void dataPollCommand(void)
{
  dataPollPeriod = emberUnsignedCommandArgument(0);
  bool goToDeepSleep = (emberCommandArgumentCount() > 1)
                           && emberUnsignedCommandArgument(1);
  if (dataPollPeriod != 0) {
    dataPollEventHandler();
    emberAfAppPrintln("polling period %d ms", dataPollPeriod);
  } else {
    emberEventControlSetInactive(dataPollEvent);
    emberAfAppPrintln("polling off");
  }

  // Also go to deep sleep?
  if (goToDeepSleep) {
    emberDeepSleep(true);
  }
}

// Network Management CLI Commands
void joinCommand(void)
{
  EmberNetworkParameters params;
  uint8_t zeroes[16] = {0};
  uint16_t options = EMBER_NODE_TYPE_OPTION | EMBER_TX_POWER_OPTION;
  uint8_t channel = emberUnsignedCommandArgument(0);
  uint32_t channelMask = (channel == 0
                        ? EMBER_ALL_802_15_4_CHANNELS_MASK
                        : BIT32(channel));

  MEMSET((uint8_t *)&params, 0, sizeof(params));
  params.channel = channel;
  params.radioTxPower = emberSignedCommandArgument(1);
  params.nodeType = emberUnsignedCommandArgument(2);
  emberGetStringArgument(3, params.networkId, EMBER_NETWORK_ID_SIZE, false);
  if (MEMCOMPARE(zeroes, params.networkId, EMBER_NETWORK_ID_SIZE) != 0) {
    options |= EMBER_NETWORK_ID_OPTION;
  }
  emberGetStringArgument(4, params.extendedPanId, EXTENDED_PAN_ID_SIZE, false);
  if (MEMCOMPARE(zeroes, params.extendedPanId, EXTENDED_PAN_ID_SIZE) != 0) {
    options |= EMBER_EXTENDED_PAN_ID_OPTION;
  }
  params.panId = emberUnsignedCommandArgument(5);
  if (params.panId != 0xFFFF) {
    options |= EMBER_PAN_ID_OPTION;
  }

  uint8_t joinKey[17]; // 16 + 0

  params.joinKeyLength =
    emberGetStringArgument(6, joinKey, sizeof(joinKey), false);

  if (params.joinKeyLength != 0) {
    options |= EMBER_JOIN_KEY_OPTION;
    MEMCOPY(params.joinKey, joinKey, params.joinKeyLength);
  }

  joinStartTimeQs = halCommonGetInt16uQuarterSecondTick();
  emberJoinNetwork(&params, options, channelMask);
}

// set_join_key
void setJoinKeyCommand(void)
{
  uint8_t key[17]; // 16 + 0
  uint8_t keyLength = emberGetStringArgument(0, key, sizeof(key), false);

  EmberEui64 eui64;
  EmberEui64 *eui64Pointer = NULL;

  if (emberCommandArgumentCount() == 2) {
    emberGetEui64Argument(1, &eui64);
    eui64Pointer = &eui64;
  }

// BUG: this breaks some joining
//  emDtlsJoinRequireCookie = true;       // Thread wants use to use DTLS cookies.
  emberSetJoinKey(eui64Pointer, key, keyLength);
  printOk();
}

// join_commissioned
void joinCommissionedCommand(void)
{
  joinStartTimeQs = halCommonGetInt16uQuarterSecondTick();
  bool requireConnectivity = (emberCommandArgumentCount() > 2)
                                 ? emberUnsignedCommandArgument(2)
                                 : false;
  emberJoinCommissioned(emberSignedCommandArgument(0),    // power
                        emberUnsignedCommandArgument(1),  // type
                        requireConnectivity);
  printOk();
}

// resume
void resumeNetworkCommand(void)
{
  joinStartTimeQs = halCommonGetInt16uQuarterSecondTick();
  emberResumeNetwork();
}

// reset_network
void resetNetworkCommand(void)
{
  emberResetNetworkState();
}

// ping
void pingCommand(void)
{
  uint8_t destIp[16];
  uint16_t id = 0xABCD;
  uint16_t sequence = 0x1234;
  uint16_t length = 0;
  uint16_t hopLimit = 0;
  if (!emberGetIpArgument(0, destIp)) {
    emberAfAppPrintln("Failure while parsing IP address");
    return;
  }
if (emberCommandArgumentCount() > 1) {
    if (emberCommandArgumentCount() != 5) {
      emberAfAppPrintln("Expected arguments: id, sequence, length, hop limit");
      return;
    }
    id = emberUnsignedCommandArgument(1);
    sequence = emberUnsignedCommandArgument(2);
    length = emberUnsignedCommandArgument(3);
    hopLimit = emberUnsignedCommandArgument(4);
  }
  if (!emberIpPing(destIp, id, sequence, length, hopLimit)) {
    emberAfAppPrintln("Error: ping send failed");
  }
}

// option 
// typedef struct { uint8_t *name; } commandOption;

char const *optionTable[] = {
  "force_dark",
  "reject_coap_solicit",
  NULL
};

bool * const optionValues[] = {
  &emForceDark,
  &emForceRejectCoapSolicit,
};

void setOptionCommand(void)
{
  uint8_t commandLength;
  uint8_t *commandName = emberStringCommandArgument(-1, &commandLength);
  uint16_t i;

  for (i = 0; ; i++) {
    const char *next = optionTable[i];
    if (next == NULL) {
      break;
    } else if (emStrlen((const uint8_t *)next) == commandLength
               && MEMCOMPARE(next, commandName, commandLength) == 0) {
      bool value = (bool) emberUnsignedCommandArgument(0);
      if (i < sizeof(optionValues) / sizeof(bool *)) {
        *optionValues[i] = value;
      } else {
        uint16_t mask = 1 << (i - sizeof(optionValues) / sizeof(bool *));
        if (value) {
          emStackConfiguration |= mask;
        } else {
          emStackConfiguration &= ~mask;
        }
      }
      emberAfAppPrintln("%p: %p", next, (value ? "on" : "off"));
      printOk();
      break;
    }
  }
}

// reboot
void resetMicroCommand(void)
{
  emberResetMicro();
}

// send_address_solicit
void sendAddressSolicitCommand(void)
{
  if (emSendAddressSolicit((AddressManagementStatus) emberUnsignedCommandArgument(0))) {
    printOk();
  } else {
    printFail();
  }
}
