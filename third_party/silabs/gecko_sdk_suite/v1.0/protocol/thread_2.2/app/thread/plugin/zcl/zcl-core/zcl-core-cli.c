// Copyright 2015 Silicon Laboratories, Inc.

#include PLATFORM_HEADER
#include CONFIGURATION_HEADER
#include EMBER_AF_API_STACK
#include EMBER_AF_API_COMMAND_INTERPRETER2
#ifdef EMBER_AF_API_DEBUG_PRINT
  #include EMBER_AF_API_DEBUG_PRINT
#endif
#include "stack/framework/buffer-management.h"
#include "zcl-core.h"
#include "zcl-core-well-known.h"
#define CLI_COMMAND_STRUCT_LENGTH 64

static EmberZclClusterSpec_t clusterSpec;
static EmberZclAttributeId_t attributeReadIds[32]; // TODO: find a good length for me!
static uint8_t attributeReadIdsCount = 0;
static uint8_t attributeWriteDataBuffer[EMBER_ZCL_ATTRIBUTE_MAX_SIZE];
static EmberZclAttributeWriteData_t attributeWriteData = {
  EMBER_ZCL_ATTRIBUTE_NULL,
  attributeWriteDataBuffer,
  0,
};

static EmberZclBindingEntry_t bindingEntry = {
  .destination.network.port = 0, // indicates not in use
};
static EmberZclBindingId_t bindingId = EMBER_ZCL_BINDING_NULL;

typedef struct {
  const EmberZclClusterSpec_t *clusterSpec;
  EmberZclCommandId_t commandId;
  const ZclipStructSpec *structSpec;

  uint8_t payloadStruct[CLI_COMMAND_STRUCT_LENGTH];
  char commandStrings[EMBER_COMMAND_BUFFER_LENGTH];

  EmZclCliRequestCommandFunction function;
} CurrentRequestCommand;
static CurrentRequestCommand currentRequestCommand = {
  .function = NULL,
};

static void getClusterSpecArguments(uint8_t index,
                                    EmberZclClusterSpec_t *clusterSpec);
static bool getUidArgument(uint8_t index, EmberZclUid_t *uid);
static void resetCliState(void);

static void attributeWriteResponseHandler(EmberZclMessageStatus_t status,
                                          const EmberZclAttributeContext_t *context,
                                          EmberZclStatus_t result);
static void attributeReadResponseHandler(EmberZclMessageStatus_t status,
                                         const EmberZclAttributeContext_t *context,
                                         const void *buffer,
                                         size_t bufferLength);
static void bindingResponseHandler(EmberZclMessageStatus_t status,
                                   const EmberZclBindingContext_t *context,
                                   const EmberZclBindingEntry_t *entry);
static void commandResponseHandler(EmberZclMessageStatus_t status,
                                   const EmberZclCommandContext_t *context,
                                   const void *response);
static void discoveryResponseHandler(EmberCoapStatus status,
                                     EmberCoapCode code,
                                     EmberCoapReadOptions *options,
                                     uint8_t *payload,
                                     uint16_t payloadLength,
                                     EmberCoapResponseInfo *info);
static void commandPrintInfoExtended(const EmberZclDestination_t *destination,
                                     const EmberZclClusterSpec_t *clusterSpec,
                                     EmberZclCommandId_t commandId,
                                     const ZclipStructSpec *structSpec,
                                     const uint8_t *theStruct,
                                     const char *prefix);
static void commandReallyPrintInfoExtended(const EmberZclApplicationDestination_t *destination,
                                           const EmberZclClusterSpec_t *clusterSpec,
                                           EmberZclCommandId_t commandId,
                                           const uint8_t *payload,
                                           uint16_t payloadLength,
                                           const char *prefix);

static EmberStatus addressWithBinding (EmberZclDestination_t *address);
static EmberStatus addressWithEndpoint(EmberZclDestination_t *address);
static EmberStatus addressWithGroup   (EmberZclDestination_t *address);

// These functions lives in app/coap/coap.c.
const uint8_t *emGetCoapCodeName(EmberCoapCode type);
const uint8_t *emGetCoapContentFormatTypeName(EmberCoapContentFormatType type);
const uint8_t *emGetCoapStatusName(EmberCoapStatus status);

// ----------------------------------------------------------------------------
// Commands

#define UID_DOTDOTDOT_SIZE 15 // 120 bits

// zcl info
void emZclCliInfoCommand(void)
{
  uint8_t result[EMBER_ZCL_UID_STRING_SIZE];
  emZclUidToString(&emZclUid, EMBER_ZCL_UID_BITS, result);
  emberAfAppPrintln("uid: %s", result);
}

// zcl attribute print
void emZclCliAttributePrintCommand(void)
{
  emberAfAppPrintln("");
  emberAfAppPrintln(" EE | R | MMMM | CCCC | AAAA | KKKK | Data ");
  emberAfAppPrintln("----+---+------+------+------+------+------");
  for (size_t i = 0; i < emZclEndpointCount; i++) {
    for (size_t j = 0; j < EM_ZCL_ATTRIBUTE_COUNT; j++) {
      uint8_t buffer[EMBER_ZCL_ATTRIBUTE_MAX_SIZE];
      if (emZclReadAttributeEntry(emZclEndpointTable[i].endpointId,
                                  &emZclAttributeTable[j],
                                  buffer,
                                  sizeof(buffer))
          == EMBER_ZCL_STATUS_SUCCESS) {
        halResetWatchdog();
        emberAfAppPrint(" %x | %c | %2x | %2x | %2x | %2x | ",
                        emZclEndpointTable[i].endpointId,
                        ((emZclAttributeTable[j].clusterSpec->role
                          == EMBER_ZCL_ROLE_CLIENT)
                         ? 'c'
                         : 's'),
                        emZclAttributeTable[j].clusterSpec->manufacturerCode,
                        emZclAttributeTable[j].clusterSpec->id,
                        emZclAttributeTable[j].attributeId,
                        emZclAttributeTable[j].mask);
        emberAfAppPrintBuffer(buffer, emZclAttributeTable[j].size, false);
        emberAfAppPrintln("");
      }
    }
  }
}

// zcl attribute reset <endpoint id:1>
void emZclCliAttributeResetCommand(void)
{
  EmberZclEndpointId_t endpointId
    = (EmberZclEndpointId_t)emberUnsignedCommandArgument(0);
  emberZclResetAttributes(endpointId);
}

// zcl attribute read <endpoint id:1> <role:1> <manufacturer code:2> <cluster id:2> <attribute id:2>
void emZclCliAttributeReadCommand(void)
{
  EmberZclEndpointId_t endpointId
    = (EmberZclEndpointId_t)emberUnsignedCommandArgument(0);
  EmberZclClusterSpec_t clusterSpec;
  getClusterSpecArguments(1, &clusterSpec);
  EmberZclAttributeId_t attributeId
    = (EmberZclAttributeId_t)emberUnsignedCommandArgument(4);

  const EmZclAttributeEntry_t *attribute
    = emZclFindAttribute(&clusterSpec, attributeId, false); // exclude remote
  uint8_t buffer[EMBER_ZCL_ATTRIBUTE_MAX_SIZE];
  EmberZclStatus_t status = emZclReadAttributeEntry(endpointId,
                                                    attribute,
                                                    buffer,
                                                    sizeof(buffer));
  emberAfAppPrint("%s 0x%x", "read", status);
  if (status == EMBER_ZCL_STATUS_SUCCESS) {
    emberAfAppPrint(": ");
    // SUCCESS implies that 'attribute' is non-NULL.
    //cstat !PTR-null-assign-pos
    emberAfAppPrintBuffer(buffer, attribute->size, false);
  }
  emberAfAppPrintln("");
}

// zcl attribute write <endpoint id:1> <role:1> <manufacturer code:2> <cluster id:2> <attribute id:2> <data:n>
void emZclCliAttributeWriteCommand(void)
{
  EmberZclEndpointId_t endpointId
    = (EmberZclEndpointId_t)emberUnsignedCommandArgument(0);
  EmberZclClusterSpec_t clusterSpec;
  getClusterSpecArguments(1, &clusterSpec);
  EmberZclAttributeId_t attributeId
    = (EmberZclAttributeId_t)emberUnsignedCommandArgument(4);
  uint8_t bufferLength;
  uint8_t *buffer = emberStringCommandArgument(5, &bufferLength);

  EmberZclStatus_t status = emberZclWriteAttribute(endpointId,
                                                   &clusterSpec,
                                                   attributeId,
                                                   buffer,
                                                   bufferLength);
  emberAfAppPrintln("%s 0x%x", "write", status);
}

// zcl attribute remote read <role:1> <manufacturer code:2> <cluster id:2> <attribute id:2> [<attribute id:2> ...]
void emZclCliAttributeRemoteReadCommand(void)
{
  resetCliState();

  getClusterSpecArguments(0, &clusterSpec);

  uint8_t commandArgumentCount = emberCommandArgumentCount();
  for (uint8_t i = 3; i < commandArgumentCount; i ++) {
    attributeReadIds[attributeReadIdsCount++]
      = (EmberZclAttributeId_t)emberUnsignedCommandArgument(i);
  }

  emberAfAppPrintln("%s 0x%x", "read", EMBER_ZCL_STATUS_SUCCESS);
}

// zcl attribute remote write <role:1> <manufacturer code:2> <cluster id:2> <attribute id:2> <data:n>
void emZclCliAttributeRemoteWriteCommand(void)
{
  resetCliState();

  getClusterSpecArguments(0, &clusterSpec);
  attributeWriteData.attributeId
    = (EmberZclAttributeId_t)emberUnsignedCommandArgument(3);
  uint8_t bufferLength;
  uint8_t *buffer = emberStringCommandArgument(4, &bufferLength);
  if (bufferLength <= sizeof(attributeWriteDataBuffer)) {
    MEMCOPY(attributeWriteDataBuffer, buffer, bufferLength);
    attributeWriteData.bufferLength = bufferLength;
    emberAfAppPrintln("%s 0x%x", "write", EMBER_ZCL_STATUS_SUCCESS);
  } else {
    emberAfAppPrintln("%s 0x%x", "write", EMBER_ZCL_STATUS_INSUFFICIENT_SPACE);
  }
}

// zcl binding clear
void emZclCliBindingClearCommand(void)
{
  emberZclRemoveAllBindings();
}

// zcl binding print
void emZclCliBindingPrintCommand(void)
{
  emberAfAppPrintln(" II | EE | R | MMMM | CCCC | RR |                       R                        ");
  emberAfAppPrintln("----+----+---+------+------+----+------------------------------------------------");
  for (EmberZclBindingId_t i = 0; i < EMBER_ZCL_BINDING_TABLE_SIZE; i++) {
    EmberZclBindingEntry_t entry;
    if (emberZclGetBinding(i, &entry)) {
      emberAfAppPrint(" %x | %x | %c | %2x | %2x | %x | ",
                      i,
                      entry.endpointId,
                      (entry.clusterSpec.role == EMBER_ZCL_ROLE_CLIENT
                       ? 'c'
                       : 's'),
                      entry.clusterSpec.manufacturerCode,
                      entry.clusterSpec.id,
                      entry.reportingConfigurationId);
      uint8_t uri[128];
      emZclDestinationToUri(&entry.destination, uri);
      emberAfAppPrintln("%s", uri);
    }
  }
}

// zcl binding add <endpoint id:1> <role:1> <manufacturer code:2> <cluster id:2> <secure:1> <destination address> <destination port:2> <destination endpoint id:1> <destination group id:2> <reporting configuration id:1>
// zcl binding set <endpoint id:1> <role:1> <manufacturer code:2> <cluster id:2> <secure:1> <destination address> <destination port:2> <destination endpoint id:1> <destination group id:2> <reporting configuration id:1> <binding id:1>
void emZclCliBindingAddSetCommand(void)
{
  resetCliState();

  bindingEntry.endpointId = (EmberZclEndpointId_t)emberUnsignedCommandArgument(0);
  getClusterSpecArguments(1, &bindingEntry.clusterSpec);
  bindingEntry.destination.network.scheme
    = ((bool)emberUnsignedCommandArgument(4)
       ? EMBER_ZCL_SCHEME_COAPS
       : EMBER_ZCL_SCHEME_COAP);
  if (!emberGetIpv6AddressArgument(5, &bindingEntry.destination.network.data.address)) {
    emberAfAppPrintln("%p: %p", "ERR", "invalid ip");
    return;
  }
  bindingEntry.destination.network.type = EMBER_ZCL_NETWORK_DESTINATION_TYPE_ADDRESS;
  bindingEntry.destination.network.port = (uint16_t)emberUnsignedCommandArgument(6);

  EmberZclEndpointId_t tempEndpointId = (EmberZclEndpointId_t)emberUnsignedCommandArgument(7);
  EmberZclGroupId_t tempGroupId = (EmberZclGroupId_t)emberUnsignedCommandArgument(8);
  if (tempGroupId != EMBER_ZCL_GROUP_NULL) {
    bindingEntry.destination.application.data.groupId = tempGroupId;
    bindingEntry.destination.application.type = EMBER_ZCL_APPLICATION_DESTINATION_TYPE_GROUP;
  } else {
    bindingEntry.destination.application.data.endpointId = tempEndpointId;
    bindingEntry.destination.application.type = EMBER_ZCL_APPLICATION_DESTINATION_TYPE_ENDPOINT;
  }

  bindingEntry.reportingConfigurationId = (EmberZclReportingConfigurationId_t)emberUnsignedCommandArgument(9);
  if (emberStringCommandArgument(-1, NULL)[0] == 'a') {
    bindingId = emberZclAddBinding(&bindingEntry);
    emberAfAppPrintln("%s 0x%x",
                      "add",
                      (bindingId == EMBER_ZCL_BINDING_NULL
                       ? EMBER_ZCL_STATUS_FAILURE
                       : EMBER_ZCL_STATUS_SUCCESS));
  } else {
    bindingId = (EmberZclBindingId_t)emberUnsignedCommandArgument(10);
    bool success = emberZclSetBinding(bindingId, &bindingEntry);
    emberAfAppPrintln("%s 0x%x",
                      "set",
                      (success
                       ? EMBER_ZCL_STATUS_SUCCESS
                       : EMBER_ZCL_STATUS_FAILURE));
  }
}

// zcl binding remove <binding id:1>
void emZclCliBindingRemoveCommand(void)
{
  resetCliState();

  bindingId = (EmberZclBindingId_t)emberUnsignedCommandArgument(0);
  bool success = emberZclRemoveBinding(bindingId);
  emberAfAppPrintln("%s 0x%x",
                    "remove",
                    (success
                     ? EMBER_ZCL_STATUS_SUCCESS
                     : EMBER_ZCL_STATUS_FAILURE));
}

// zcl binding remote add    <role:1> <manufacturer code:2> <cluster id:2> <secure:1> <destination address> <destination port:2> <destination endpoint id:1> <destination group id:2> <reporting configuration id:1>
// zcl binding remote update <role:1> <manufacturer code:2> <cluster id:2> <secure:1> <destination address> <destination port:2> <destination endpoint id:1> <destination group id:2> <reporting configuration id:1> <binding id:1>
void emZclCliBindingRemoteAddUpdateCommand(void)
{
  resetCliState();

  getClusterSpecArguments(0, &bindingEntry.clusterSpec);
  bindingEntry.destination.network.scheme
    = ((bool)emberUnsignedCommandArgument(3)
       ? EMBER_ZCL_SCHEME_COAPS
       : EMBER_ZCL_SCHEME_COAP);
  if (!emberGetIpv6AddressArgument(4, &bindingEntry.destination.network.data.address)) {
    emberAfAppPrintln("%p: %p", "ERR", "invalid ip");
    return;
  }
  bindingEntry.destination.network.port = (uint16_t)emberUnsignedCommandArgument(5);

  EmberZclEndpointId_t tempEndpointId = (EmberZclEndpointId_t)emberUnsignedCommandArgument(6);
  EmberZclGroupId_t tempGroupId = (EmberZclGroupId_t)emberUnsignedCommandArgument(7);
  if (tempGroupId != EMBER_ZCL_GROUP_NULL) {
    bindingEntry.destination.application.data.groupId = tempGroupId;
    bindingEntry.destination.application.type = EMBER_ZCL_APPLICATION_DESTINATION_TYPE_GROUP;
  } else {
    bindingEntry.destination.application.data.endpointId = tempEndpointId;
    bindingEntry.destination.application.type = EMBER_ZCL_APPLICATION_DESTINATION_TYPE_ENDPOINT;
  }

  bindingEntry.reportingConfigurationId = (EmberZclReportingConfigurationId_t)emberUnsignedCommandArgument(8);
  if (emberStringCommandArgument(-1, NULL)[0] == 'a') {
    bindingId = EMBER_ZCL_BINDING_NULL;
    emberAfAppPrintln("%s 0x%x", "add", EMBER_ZCL_STATUS_SUCCESS);
  } else {
    bindingId = (EmberZclBindingId_t)emberUnsignedCommandArgument(9);
    emberAfAppPrintln("%s 0x%x", "update", EMBER_ZCL_STATUS_SUCCESS);
  }
}

// zcl binding remote remove <role:1> <manufacturer code:2> <cluster id:2> <binding id:1>
void emZclCliBindingRemoteRemoveCommand(void)
{
  resetCliState();

  getClusterSpecArguments(0, &clusterSpec);
  bindingId = (EmberZclBindingId_t)emberUnsignedCommandArgument(3);
  emberAfAppPrintln("%s 0x%x", "remove", EMBER_ZCL_STATUS_SUCCESS);
}

// zcl send binding  <binding id:1>
// zcl send endpoint <address> <endpoint id:1>
// zcl send group    <address> <group id:2>
void emZclCliSendCommand(void)
{
  EmberStatus status;
  uint8_t *payloadStruct;

  EmberZclDestination_t destination;
  destination.network.type = EMBER_ZCL_NETWORK_DESTINATION_TYPE_ADDRESS;
  switch (emberStringCommandArgument(-1, NULL)[0]) {
  case 'b': // send to binding
    status = addressWithBinding(&destination);
    break;
  case 'e': // send to endpoint
    status = addressWithEndpoint(&destination);
    break;
  case 'g': // send to group
    status = addressWithGroup(&destination);
    break;
  default:
    assert(false); // uh...
    return;
  }

  if (status != EMBER_SUCCESS) {
    goto done;
  }

  if (attributeReadIdsCount != 0) {
    status = emberZclSendAttributeRead(&destination,
                                       &clusterSpec,
                                       attributeReadIds,
                                       attributeReadIdsCount,
                                       attributeReadResponseHandler);
  } else if (attributeWriteData.attributeId != EMBER_ZCL_ATTRIBUTE_NULL) {
    status = emberZclSendAttributeWrite(&destination,
                                        &clusterSpec,
                                        &attributeWriteData,
                                        1, // attribute write data count
                                        attributeWriteResponseHandler);
  } else if (bindingEntry.destination.network.port != 0
             && bindingId == EMBER_ZCL_BINDING_NULL) {
    status = emberZclSendAddBinding(&destination,
                                    &bindingEntry,
                                    bindingResponseHandler);
  } else if (bindingEntry.destination.network.port != 0
             && bindingId != EMBER_ZCL_BINDING_NULL) {
    status = emberZclSendUpdateBinding(&destination,
                                       &bindingEntry,
                                       bindingId,
                                       bindingResponseHandler);
  } else if (bindingEntry.destination.network.port == 0
             && bindingId != EMBER_ZCL_BINDING_NULL) {
    status = emberZclSendRemoveBinding(&destination,
                                       &clusterSpec,
                                       bindingId,
                                       bindingResponseHandler);
  } else if (currentRequestCommand.function != NULL) {
    // If the struct spec is NULL, then there is no payload, so we need to
    // set our payload pointer to NULL.
    payloadStruct = (currentRequestCommand.structSpec == NULL
                     ? NULL
                     : currentRequestCommand.payloadStruct);
    status = (*currentRequestCommand.function)(&destination,
                                               payloadStruct,
                                               commandResponseHandler);
  } else {
    status = EMBER_INVALID_CALL;
  }

  done:
  emberAfAppPrint("%s 0x%x", "send", status);
  if (currentRequestCommand.function != NULL) {
    commandPrintInfoExtended(&destination,
                             currentRequestCommand.clusterSpec,
                             currentRequestCommand.commandId,
                             currentRequestCommand.structSpec,
                             currentRequestCommand.payloadStruct,
                             " ");
  } else {
    emberAfAppPrintln("");
  }

  resetCliState();
}

// zcl ez-mode start <role:1> <manufacturer code:2> <cluster id:2>
void emZclCliEzModeStartCommand(void)
{
  EmberStatus status = emberZclStartEzMode();
  emberAfAppPrintln("%s 0x%x", "start", status);
}

// zcl discovery cluster <role:1> <manufacturer code:2> <cluster id:2>
void emZclCliDiscByClusterIdCommand(void)
{
  EmberZclClusterSpec_t spec;
  getClusterSpecArguments(0, &spec);
  emberZclDiscByClusterId(&spec, discoveryResponseHandler);
}

// zcl discovery endpoint <endpoint:1> <deviceId:2> / query to ze=urn:zcl:d.$DEVICE_ID.$ENDPOINT
void emZclCliDiscByEndpointCommand(void)
{
  EmberZclEndpointId_t endpoint = (EmberZclEndpointId_t) emberUnsignedCommandArgument(0);
  EmberZclDeviceId_t deviceId = (EmberZclDeviceId_t) emberUnsignedCommandArgument(1);
  emberZclDiscByEndpoint(endpoint, deviceId, discoveryResponseHandler);
}

// zcl discovery device-type <device type:2>
void emZclCliDiscByDeviceTypeCommand(void)
{
  EmberZclDeviceId_t deviceId = (EmberZclDeviceId_t) emberUnsignedCommandArgument(0);
  emberZclDiscByDeviceId(deviceId, discoveryResponseHandler);
}

static void discoveryResponseHandler(EmberCoapStatus status,
                                     EmberCoapCode code,
                                     EmberCoapReadOptions *options,
                                     uint8_t *payload,
                                     uint16_t payloadLength,
                                     EmberCoapResponseInfo *info)
{
  emberAfAppPrint("Discovery CLI:");

  emberAfAppPrint(" %s", emGetCoapStatusName(status));

  if (status != EMBER_COAP_MESSAGE_TIMED_OUT) {
    emberAfAppPrint(" %s", emGetCoapCodeName(code));
    if (payloadLength != 0) {
      uint32_t valueLoc;
      EmberCoapContentFormatType contentFormat
        = (emberReadIntegerOption(options,
                                  EMBER_COAP_OPTION_CONTENT_FORMAT,
                                  &valueLoc)
           ? (EmberCoapContentFormatType)valueLoc
           : EMBER_COAP_CONTENT_FORMAT_NONE);
      emberAfAppPrint(" f=%s p=",
                      emGetCoapContentFormatTypeName(contentFormat));
      if (contentFormat == EMBER_COAP_CONTENT_FORMAT_TEXT_PLAIN
          || contentFormat == EMBER_COAP_CONTENT_FORMAT_LINK_FORMAT) {
        emberAfAppDebugExec(emberAfPrintCharacters(EMBER_AF_PRINT_APP,
                                                   payload,
                                                   payloadLength));
      } else {
        emberAfAppPrintBuffer(payload, payloadLength, false);
      }
    }
  }

  emberAfAppPrintln("");
}

// zcl discovery uid <uid>
void emZclCliDiscByUidCommand(void)
{
  uint8_t *string = emberStringCommandArgument(0, NULL);
  emZclDiscByUidString(string, discoveryResponseHandler);
}

// zcl discovery resource-version <version:2>
void emZclCliDiscByResourceVersionCommand(void)
{
  EmberZclClusterRevision_t version = (EmberZclClusterRevision_t) emberUnsignedCommandArgument(0);
  emberZclDiscByResourceVersion(version, discoveryResponseHandler);
}

// zcl discovery cluster-version <version:2>
void emZclCliDiscByClusterRevisionCommand(void)
{
  EmberZclClusterRevision_t version = (EmberZclClusterRevision_t) emberUnsignedCommandArgument(0);
  emberZclDiscByClusterRev(version, discoveryResponseHandler);
}

// zcl discovery mode <mode:1>
// mode - 0: single query (default)
//        1: multiple query

void emZclCliDiscSetModeCommand(void)
{
  uint8_t mode = (uint8_t) emberUnsignedCommandArgument(0);
  bool status = emberZclDiscSetMode(mode);
  emberAfAppPrintln("%s 0x%x", "mode", status);
}

// zcl discovery init
void emZclCliDiscInitCommand(void)
{
  emberZclDiscInit();
}

// zcl discovery send
void emZclCliDiscSendCommand(void)
{
  emberAfAppPrintln("%s 0x%x", "send", emberZclDiscSend(discoveryResponseHandler));
}

// zcl cache clear
void emZclCliCacheClearCommand(void)
{
  emZclCacheRemoveAll();
}

// EmZclCacheScanPredicate to print each cache entry.
typedef struct {
  bool dotdotdot;
} CachePrintCriteria_t;
static bool printCacheEntry(const void *criteria,
                            const EmZclCacheEntry_t *entry)
{
  const EmberZclUid_t *uid = &entry->key.data.uid;
  const EmberIpv6Address *address = &entry->value.data.address;
  emberAfAppPrint(" 0x%2x |", entry->index);
  uint8_t result[EMBER_ZCL_UID_STRING_SIZE];
  emZclUidToString(uid,
                   (((CachePrintCriteria_t *)criteria)->dotdotdot
                    ? UID_DOTDOTDOT_SIZE
                    : EMBER_ZCL_UID_SIZE),
                   result);
  emberAfAppPrint(" uid: %s");
  if (((CachePrintCriteria_t *)criteria)->dotdotdot) {
    emberAfAppPrint("...");
  }
  emberAfAppPrint(" | ipv6: ");
  emberAfAppDebugExec(emberAfPrintIpv6Address(address));
  emberAfAppPrintln("");

  return false; // "not a match" -> continue through all cache entries
}

// zcl cache add uid <uid> ipv6 <ipv6address>
void emZclCliCacheAddCommand(void)
{
  EmberZclNetworkDestination_t key, value;
  EmZclCacheIndex_t index;

  key.type = EMBER_ZCL_NETWORK_DESTINATION_TYPE_UID;
  value.type = EMBER_ZCL_NETWORK_DESTINATION_TYPE_ADDRESS;

  if (!getUidArgument(0, &key.data.uid)) {
    emberAfAppPrintln("%p: %p", "ERR", "invalid uid");
    return;
  }

  if (!emberGetIpv6AddressArgument(1, &value.data.address)) {
    emberAfAppPrintln("%p: %p", "ERR", "invalid ip");
    return;
  }

  bool success = emZclCacheAdd(&key, &value, &index);
  emberAfAppPrint("%s 0x%x",
                    "added",
                    (success
                     ? EMBER_ZCL_STATUS_SUCCESS
                     : EMBER_ZCL_STATUS_FAILURE));
  if (success) {
    emberAfAppPrintln(" index 0x%2x", index);
  } else {
    emberAfAppPrintln("");
  }
}

// zcl cache print
void emZclCliCachePrintCommand(void)
{
  /*
  zcl cache print 1 (Full-length UID format)
   Index  | Identifier                                                            | Address
  --------+-----------------------------------------------------------------------+-----------------------------------------------
   0xIIII | uid: 00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff | ipv6: 0000:1111:2222:3333:4444:5555:6666:7777

  zcl cache print 0 (Abbreviated UID format; i.e., dotdotdot)
   Index  | Identifier                             | Address
  --------+----------------------------------------+-----------------------------------------------
   0xIIII | uid: 00112233445566778899aabbccddee... | ipv6: 0000:1111:2222:3333:4444:5555:6666:7777
  */

  CachePrintCriteria_t criteria = {
    .dotdotdot = (0 == emberUnsignedCommandArgument(0)),
  };
  emberAfAppPrintln("");
  emberAfAppPrint(" Index  | Identifier                             ");
  if (!criteria.dotdotdot) {
    emberAfAppPrint("                               ");
  }
  emberAfAppPrintln("| Address");
  emberAfAppPrint("--------+----------------------------------------");
  if (!criteria.dotdotdot) {
    emberAfAppPrint("-------------------------------");
  }
  emberAfAppPrintln("+-----------------------------------------------");
  emZclCacheScan(&criteria, printCacheEntry);
}

// zcl cache remove <index:2>
void emZclCliCacheRemoveCommand(void)
{
  EmberZclBindingId_t index
    = (EmberZclBindingId_t)emberUnsignedCommandArgument(0);
  bool success = emZclCacheRemoveByIndex(index);
  emberAfAppPrintln("%s 0x%x",
                    "remove",
                    (success
                     ? EMBER_ZCL_STATUS_SUCCESS
                     : EMBER_ZCL_STATUS_FAILURE));
}

// ----------------------------------------------------------------------------
// Utilities

void emZclCliSetCurrentRequestCommand(const EmberZclClusterSpec_t *clusterSpec,
                                      EmberZclCommandId_t commandId,
                                      const ZclipStructSpec *structSpec,
                                      EmZclCliRequestCommandFunction function,
                                      const char *cliFormat)
{
  resetCliState();

  ZclipStructData structData;
  ZclipFieldData fieldData;
  uint8_t *finger;
  uint8_t index = 0;
  char *string = currentRequestCommand.commandStrings;

  currentRequestCommand.clusterSpec = clusterSpec;
  currentRequestCommand.commandId   = commandId;
  currentRequestCommand.structSpec  = structSpec;
  currentRequestCommand.function    = function;

  if (!*cliFormat) {
    goto done;
  }

  if (!emExpandZclipStructData(currentRequestCommand.structSpec, &structData)){
    emberAfAppPrint("Struct data init failed.");
    goto done;
  }

  for (; *cliFormat; cliFormat ++, index ++) {
    assert(!emZclipFieldDataFinished(&structData));
    emGetNextZclipFieldData(&structData, &fieldData);
    finger = currentRequestCommand.payloadStruct + fieldData.valueOffset;
    switch (*cliFormat) {
    case '*':
      assert(0); // TODO: handle me.
      break;

    case 's':
      *((int8_t *)finger) = emberSignedCommandArgument(index) & 0x000000FF;
      break;
    case 'r':
      *((int16_t *)finger) = emberSignedCommandArgument(index) & 0x0000FFFF;
      break;
    case 'q':
      *((int32_t *)finger) = emberSignedCommandArgument(index);
      break;
    case 'u':
      *((uint8_t *)finger) = emberUnsignedCommandArgument(index) & 0x000000FF;
      break;
    case 'v':
      *((uint16_t *)finger) = emberUnsignedCommandArgument(index) & 0x0000FFFF;
      break;
    case 'w':
      *((uint32_t *)finger) = emberUnsignedCommandArgument(index);
      break;

    case 'b': {
      // We assume that "b" means a ZCL string!
      uint8_t length;
      uint8_t *tmp = emberStringCommandArgument(index, &length);
      MEMMOVE(finger, &string, sizeof(uint8_t **));
      finger += sizeof(uint8_t *);
      *string++ = length;
      MEMMOVE(string, tmp, length);
      string += length;
      break;
    }

    default:
      assert(0); // we should never get to a bad format char
    }

    assert(finger - currentRequestCommand.payloadStruct
           <= sizeof(currentRequestCommand.payloadStruct));
  } // end of for loop

  done:
  commandPrintInfoExtended(NULL, // destination address
                           currentRequestCommand.clusterSpec,
                           currentRequestCommand.commandId,
                           currentRequestCommand.structSpec,
                           currentRequestCommand.payloadStruct,
                           "buffer ");
}

static void getClusterSpecArguments(uint8_t index,
                               EmberZclClusterSpec_t *clusterSpec)
{
  clusterSpec->role = (EmberZclRole_t)emberUnsignedCommandArgument(index);
  clusterSpec->manufacturerCode
    = (EmberZclManufacturerCode_t)emberUnsignedCommandArgument(index + 1);
  clusterSpec->id
    = (EmberZclClusterId_t)emberUnsignedCommandArgument(index + 2);
}

static bool getUidArgument(uint8_t index, EmberZclUid_t *uid)
{
  uint8_t uidlen = 0;
  uint8_t *uidarg = emberStringCommandArgument(index, &uidlen);
  if (uidarg != NULL && uidlen == sizeof(EmberZclUid_t)) {
    MEMCOPY(uid, uidarg, sizeof(EmberZclUid_t));
    return true;
  }
  return false;
}

static void resetCliState(void)
{
  attributeReadIdsCount = 0;
  attributeWriteData.attributeId = EMBER_ZCL_ATTRIBUTE_NULL;
  bindingEntry.destination.network.port = 0;
  bindingId = EMBER_ZCL_BINDING_NULL;
  currentRequestCommand.function = NULL;
}

static void attributeReadResponseHandler(EmberZclMessageStatus_t status,
                                         const EmberZclAttributeContext_t *context,
                                         const void *buffer,
                                         size_t bufferLength)
{
  uint8_t uriPath[EMBER_ZCL_URI_PATH_MAX_LENGTH];
  EmberZclApplicationDestination_t destination = {{0}};
  if (context->groupId != EMBER_ZCL_GROUP_NULL) {
    destination.data.groupId = context->groupId;
    destination.type = EMBER_ZCL_APPLICATION_DESTINATION_TYPE_GROUP;
  } else {
    destination.data.endpointId = context->endpointId;
    destination.type = EMBER_ZCL_APPLICATION_DESTINATION_TYPE_ENDPOINT;
  }

  emZclAttributeIdToUriPath(&destination,
                            context->clusterSpec,
                            context->attributeId,
                            uriPath);
  emberAfAppPrint("response %s %s u=%s v=",
                  emZclGetMessageStatusName(status),
                  emGetCoapCodeName(context->code),
                  uriPath);
  emberAfAppPrintBuffer(buffer, bufferLength, false);
  emberAfAppPrintln("");
}

static void attributeWriteResponseHandler(EmberZclMessageStatus_t status,
                                          const EmberZclAttributeContext_t *context,
                                          EmberZclStatus_t result)
{
  uint8_t uriPath[EMBER_ZCL_URI_PATH_MAX_LENGTH];
  EmberZclApplicationDestination_t destination = {{0}};
  if (context->groupId != EMBER_ZCL_GROUP_NULL) {
    destination.data.groupId = context->groupId;
    destination.type = EMBER_ZCL_APPLICATION_DESTINATION_TYPE_GROUP;
  } else {
    destination.data.endpointId = context->endpointId;
    destination.type = EMBER_ZCL_APPLICATION_DESTINATION_TYPE_ENDPOINT;
  }

  emZclAttributeIdToUriPath(&destination,
                            context->clusterSpec,
                            context->attributeId,
                            uriPath);
  emberAfAppPrintln("response %s %s u=%s s=0x%x",
                    emZclGetMessageStatusName(status),
                    emGetCoapCodeName(context->code),
                    uriPath,
                    result);
}

static void bindingResponseHandler(EmberZclMessageStatus_t status,
                                   const EmberZclBindingContext_t *context,
                                   const EmberZclBindingEntry_t *entry)
{
  uint8_t uriPath[EMBER_ZCL_URI_PATH_MAX_LENGTH];
  EmberZclApplicationDestination_t destination = {{0}};
  if (context->groupId != EMBER_ZCL_GROUP_NULL) {
    destination.data.groupId = context->groupId;
    destination.type = EMBER_ZCL_APPLICATION_DESTINATION_TYPE_GROUP;
  } else {
    destination.data.endpointId = context->endpointId;
    destination.type = EMBER_ZCL_APPLICATION_DESTINATION_TYPE_ENDPOINT;
  }

  emZclBindingIdToUriPath(&destination,
                          context->clusterSpec,
                          context->bindingId,
                          uriPath);
  emberAfAppPrintln("response %s %s u=%s",
                    emZclGetMessageStatusName(status),
                    emGetCoapCodeName(context->code),
                    uriPath);
}

static void commandResponseHandler(EmberZclMessageStatus_t status,
                                   const EmberZclCommandContext_t *context,
                                   const void *responsePayload)
{
  EmberZclApplicationDestination_t destination = {{0}};
  if (context->groupId != EMBER_ZCL_GROUP_NULL) {
    destination.data.groupId = context->groupId;
    destination.type = EMBER_ZCL_APPLICATION_DESTINATION_TYPE_GROUP;
  } else {
    destination.data.endpointId = context->endpointId;
    destination.type = EMBER_ZCL_APPLICATION_DESTINATION_TYPE_ENDPOINT;
  }

  emberAfAppPrint("response %s %s",
                  emZclGetMessageStatusName(status),
                  emGetCoapCodeName(context->code));
  commandReallyPrintInfoExtended(&destination,
                                 context->clusterSpec,
                                 context->commandId,
                                 context->payload,
                                 context->payloadLength,
                                 " ");
}

static void commandPrintInfoExtended(const EmberZclDestination_t *destination,
                                     const EmberZclClusterSpec_t *clusterSpec,
                                     EmberZclCommandId_t commandId,
                                     const ZclipStructSpec *structSpec,
                                     const uint8_t *theStruct,
                                     const char *prefix)
{
  uint8_t cbor[128];
  uint16_t cborLen;

  if (structSpec != NULL) {
    cborLen = emCborEncodeOneStruct(cbor, sizeof(cbor), structSpec, theStruct);
  } else {
    cborLen = 0;
  }

  commandReallyPrintInfoExtended((destination == NULL
                                  ? NULL
                                  : &destination->application),
                                 clusterSpec,
                                 commandId,
                                 cbor,
                                 cborLen,
                                 prefix);
}

static void commandReallyPrintInfoExtended(const EmberZclApplicationDestination_t *destination,
                                           const EmberZclClusterSpec_t *clusterSpec,
                                           EmberZclCommandId_t commandId,
                                           const uint8_t *payload,
                                           uint16_t payloadLength,
                                           const char *prefix)
{
  uint8_t uriPath[EMBER_ZCL_URI_PATH_MAX_LENGTH];

  emZclCommandIdToUriPath(destination, clusterSpec, commandId, uriPath);
  emberAfAppPrint("%su=%s p=", prefix, uriPath);
  emberAfAppPrintBuffer(payload, payloadLength, false);
  emberAfAppPrintln("");
}

static EmberStatus addressWithBinding(EmberZclDestination_t *destination)
{
  EmberZclBindingId_t id = (EmberZclBindingId_t)emberUnsignedCommandArgument(0);
  EmberZclBindingEntry_t entry;

  if (emberZclGetBinding(id, &entry)) {
    *destination = entry.destination;
    return EMBER_SUCCESS;
  } else {
    return EMBER_INVALID_BINDING_INDEX;
  }
}

static EmberStatus addressWithEndpoint(EmberZclDestination_t *destination)
{
  uint8_t addressType = (uint8_t)emberUnsignedCommandArgument(0);
  switch(addressType) {
  case EMBER_ZCL_NETWORK_DESTINATION_TYPE_ADDRESS:
    if (!emberGetIpv6AddressArgument(1, &destination->network.data.address)) {
      return EMBER_BAD_ARGUMENT;
    }
    destination->network.type = EMBER_ZCL_NETWORK_DESTINATION_TYPE_ADDRESS;
    break;
  case EMBER_ZCL_NETWORK_DESTINATION_TYPE_UID:
    if (!getUidArgument(1, &destination->network.data.uid)) {
      return EMBER_BAD_ARGUMENT;
    }
    destination->network.type = EMBER_ZCL_NETWORK_DESTINATION_TYPE_UID;
    break;
  default:
    return EMBER_BAD_ARGUMENT;
  }

  EmberZclEndpointId_t endpointId
    = (EmberZclEndpointId_t)emberUnsignedCommandArgument(2);
  destination->application.data.endpointId = endpointId;
  destination->application.type = EMBER_ZCL_APPLICATION_DESTINATION_TYPE_ENDPOINT;

  return EMBER_SUCCESS;
}

static EmberStatus addressWithGroup(EmberZclDestination_t *destination)
{
  if (!emberGetIpv6AddressArgument(0, &destination->network.data.address)) {
    return EMBER_BAD_ARGUMENT;
  }

  EmberZclGroupId_t groupId
    = (EmberZclGroupId_t)emberUnsignedCommandArgument(1);
  destination->application.data.groupId = groupId;
  destination->application.type = EMBER_ZCL_APPLICATION_DESTINATION_TYPE_GROUP;

  return EMBER_SUCCESS;
}
