// Copyright 2015 Silicon Laboratories, Inc.

#ifndef __ZCL_CORE_H__
#define __ZCL_CORE_H__

#include PLATFORM_HEADER
#include EMBER_AF_API_STACK
#include "zclip-struct.h"
#include "cbor.h"
#include "zcl-core-types.h"

#ifndef EMBER_SCRIPTED_TEST
  #include "thread-zclip.h"
#endif

// ----------------------------------------------------------------------------
// Addresses.


#ifndef DOXYGEN_SHOULD_SKIP_THIS
extern EmberZclUid_t emZclUid;
size_t emZclCacheGetEntryCount(void);
bool emZclCacheAdd(const EmberZclNetworkDestination_t *key,
                   const EmberZclNetworkDestination_t *value,
                   EmZclCacheIndex_t *index);
bool emZclCacheGet(const EmberZclNetworkDestination_t *key,
                   EmberZclNetworkDestination_t *value);
bool emZclCacheGetByIndex(EmZclCacheIndex_t index,
                          EmberZclNetworkDestination_t *key,
                          EmberZclNetworkDestination_t *value);
bool emZclCacheGetFirstKeyForValue(const EmberZclNetworkDestination_t *value,
                                   EmberZclNetworkDestination_t *key);
bool emZclCacheGetIndex(const EmberZclNetworkDestination_t *key,
                        EmZclCacheIndex_t *index);
bool emZclCacheRemove(const EmberZclNetworkDestination_t *key);
bool emZclCacheRemoveByIndex(EmZclCacheIndex_t index);
void emZclCacheRemoveAll(void);
size_t emZclCacheRemoveAllByValue(const EmberZclNetworkDestination_t *value);
size_t emZclCacheRemoveAllByIpv6Prefix(const EmberIpv6Address *prefix,
                                       uint8_t prefixLengthInBits);
void emZclCacheScan(const void *criteria, EmZclCacheScanPredicate match);
#endif

// -----------------------------------------------------------------------------
// Endpoints.

EmberZclEndpointIndex_t emberZclEndpointIdToIndex(EmberZclEndpointId_t endpointId,
                                                  const EmberZclClusterSpec_t *clusterSpec);
EmberZclEndpointId_t emberZclEndpointIndexToId(EmberZclEndpointIndex_t index,
                                               const EmberZclClusterSpec_t *clusterSpec);

#ifndef DOXYGEN_SHOULD_SKIP_THIS
extern const EmZclEndpointEntry_t emZclEndpointTable[];
extern const size_t emZclEndpointCount;
const EmZclEndpointEntry_t *emZclFindEndpoint(EmberZclEndpointId_t endpointId);
bool emZclEndpointHasCluster(EmberZclEndpointId_t endpointId,
                             const EmberZclClusterSpec_t *clusterSpec);
bool emZclMultiEndpointDispatch(const EmZclContext_t *context,
                                EmZclMultiEndpointHandler handler,
                                CborState *state,
                                void *data);
void emZclUriEndpointHandler(EmZclContext_t *context);
void emZclUriEndpointIdHandler(EmZclContext_t *context);
#endif

// -----------------------------------------------------------------------------
// Groups.

bool emberZclIsEndpointInGroup(EmberZclEndpointId_t endpointId,
                               EmberZclGroupId_t groupId);
EmberZclStatus_t emberZclAddEndpointToGroup(EmberZclEndpointId_t endpointId,
                                            EmberZclGroupId_t groupId);
EmberZclStatus_t emberZclRemoveEndpointFromGroup(EmberZclEndpointId_t endpointId,
                                                 EmberZclGroupId_t groupId);
EmberZclStatus_t emberZclRemoveEndpointFromAllGroups(EmberZclEndpointId_t endpointId);
EmberZclStatus_t emberZclRemoveGroup(EmberZclGroupId_t groupId);
EmberZclStatus_t emberZclRemoveAllGroups(void);

#ifndef DOXYGEN_SHOULD_SKIP_THIS
bool emZclHasGroup(EmberZclGroupId_t groupId);
void emZclUriGroupHandler(EmZclContext_t *context);
void emZclUriGroupIdHandler(EmZclContext_t *context);
#endif

// -----------------------------------------------------------------------------
// Management.

EmberStatus emberZclStartEzMode(void);
void emberZclStopEzMode(void);
bool emberZclEzModeIsActive(void);

#ifndef DOXYGEN_SHOULD_SKIP_THIS
const EmZclCommandEntry_t *emZclManagementFindCommand(EmberZclCommandId_t commandId);
void emZclManagementHandler(EmZclContext_t *context);
void emZclManagementCommandHandler(EmZclContext_t *context);
void emZclManagementCommandIdHandler(EmZclContext_t *context);
#endif

// -----------------------------------------------------------------------------
// Clusters.

bool emberZclCompareClusterSpec(const EmberZclClusterSpec_t *s1,
                                const EmberZclClusterSpec_t *s2);
void emberZclReverseClusterSpec(const EmberZclClusterSpec_t *s1,
                                EmberZclClusterSpec_t *s2);

// -----------------------------------------------------------------------------
// Attributes.

void emberZclResetAttributes(EmberZclEndpointId_t endpointId);
EmberZclStatus_t emberZclReadAttribute(EmberZclEndpointId_t endpointId,
                                       const EmberZclClusterSpec_t *clusterSpec,
                                       EmberZclAttributeId_t attributeId,
                                       void *buffer,
                                       size_t bufferLength);
EmberZclStatus_t emberZclWriteAttribute(EmberZclEndpointId_t endpointId,
                                        const EmberZclClusterSpec_t *clusterSpec,
                                        EmberZclAttributeId_t attributeId,
                                        const void *buffer,
                                        size_t bufferLength);
EmberZclStatus_t emberZclExternalAttributeChanged(EmberZclEndpointId_t endpointId,
                                                  const EmberZclClusterSpec_t *clusterSpec,
                                                  EmberZclAttributeId_t attributeId,
                                                  const void *buffer,
                                                  size_t bufferLength);
EmberStatus emberZclSendAttributeRead(const EmberZclDestination_t *destination,
                                      const EmberZclClusterSpec_t *clusterSpec,
                                      const EmberZclAttributeId_t *attributeIds,
                                      size_t attributeIdsCount,
                                      const EmberZclReadAttributeResponseHandler handler);
EmberStatus emberZclSendAttributeWrite(const EmberZclDestination_t *destination,
                                       const EmberZclClusterSpec_t *clusterSpec,
                                       const EmberZclAttributeWriteData_t *attributeWriteData,
                                       size_t attributeWriteDataCount,
                                       const EmberZclWriteAttributeResponseHandler handler);

#ifndef DOXYGEN_SHOULD_SKIP_THIS
extern const uint8_t emZclAttributeDefaultMinMaxData[];
extern const size_t emZclAttributeDefaultMinMaxLookupTable[];
extern const EmZclAttributeEntry_t emZclAttributeTable[];
#define emZclIsAttributeLocal(attribute)                      \
  (READBITS((attribute)->mask, EM_ZCL_ATTRIBUTE_STORAGE_MASK) \
   != EM_ZCL_ATTRIBUTE_STORAGE_NONE)
#define emZclIsAttributeRemote(attribute)                     \
  (READBITS((attribute)->mask, EM_ZCL_ATTRIBUTE_STORAGE_MASK) \
   == EM_ZCL_ATTRIBUTE_STORAGE_NONE)
#define emZclIsAttributeExternal(attribute)                        \
  (READBITS((attribute)->mask, EM_ZCL_ATTRIBUTE_STORAGE_TYPE_MASK) \
   == EM_ZCL_ATTRIBUTE_STORAGE_TYPE_EXTERNAL)
#define emZclIsAttributeTokenized(attribute)                       \
  (READBITS((attribute)->mask, EM_ZCL_ATTRIBUTE_STORAGE_TYPE_MASK) \
   == EM_ZCL_ATTRIBUTE_STORAGE_TYPE_TOKEN)
#define emZclIsAttributeSingleton(attribute)                            \
  (READBITS((attribute)->mask, EM_ZCL_ATTRIBUTE_STORAGE_SINGLETON_MASK) \
   == EM_ZCL_ATTRIBUTE_STORAGE_SINGLETON_MASK)
#define emZclIsAttributeReadable(attribute)                      \
  (READBITS((attribute)->mask, EM_ZCL_ATTRIBUTE_ACCESS_READABLE) \
   == EM_ZCL_ATTRIBUTE_ACCESS_READABLE)
#define emZclIsAttributeWritable(attribute)                      \
  (READBITS((attribute)->mask, EM_ZCL_ATTRIBUTE_ACCESS_WRITABLE) \
   == EM_ZCL_ATTRIBUTE_ACCESS_WRITABLE)
#define emZclIsAttributeReportable(attribute)                      \
  (READBITS((attribute)->mask, EM_ZCL_ATTRIBUTE_ACCESS_REPORTABLE) \
   == EM_ZCL_ATTRIBUTE_ACCESS_REPORTABLE)
#define emZclIsAttributeBounded(attribute)                    \
  (READBITS((attribute)->mask, EM_ZCL_ATTRIBUTE_DATA_BOUNDED) \
   == EM_ZCL_ATTRIBUTE_DATA_BOUNDED)
// TODO: Keep track of analog vs. discrete data types.
#define emZclIsAttributeAnalog(attribute)   false
#define emZclIsAttributeDiscrete(attribute) true
const EmZclAttributeEntry_t *emZclFindAttribute(const EmberZclClusterSpec_t *clusterSpec,
                                                EmberZclAttributeId_t attributeId,
                                                bool includeRemote);
EmberZclStatus_t emZclReadAttributeEntry(EmberZclEndpointId_t endpointId,
                                         const EmZclAttributeEntry_t *attribute,
                                         void *buffer,
                                         size_t bufferLength);
EmberZclStatus_t emZclWriteAttributeEntry(EmberZclEndpointId_t endpointId,
                                          const EmZclAttributeEntry_t *attribute,
                                          const void *buffer,
                                          size_t bufferLength);
bool emZclReadEncodeAttributeKeyValue(CborState *state,
                                      EmberZclEndpointId_t endpointId,
                                      const EmZclAttributeEntry_t *attribute,
                                      void *buffer,
                                      size_t bufferLength);
bool emZclAttributeUriQueryFilterParse(EmZclContext_t *context,
                                       void *data,
                                       uint8_t depth);
bool emZclAttributeUriQueryUndividedParse(EmZclContext_t *context,
                                          void *data,
                                          uint8_t depth);
void emZclUriClusterAttributeHandler(EmZclContext_t *context);
void emZclUriClusterAttributeIdHandler(EmZclContext_t *context);
#endif

// -----------------------------------------------------------------------------
// Bindings.

bool emberZclHasBinding(EmberZclBindingId_t bindingId);
bool emberZclGetBinding(EmberZclBindingId_t bindingId,
                        EmberZclBindingEntry_t *entry);
bool emberZclSetBinding(EmberZclBindingId_t bindingId,
                        const EmberZclBindingEntry_t *entry);
EmberZclBindingId_t emberZclAddBinding(const EmberZclBindingEntry_t *entry);
bool emberZclRemoveBinding(EmberZclBindingId_t bindingId);
bool emberZclRemoveAllBindings(void);
EmberStatus emberZclSendAddBinding(const EmberZclDestination_t *destination,
                                   const EmberZclBindingEntry_t *entry,
                                   const EmberZclBindingResponseHandler handler);
EmberStatus emberZclSendUpdateBinding(const EmberZclDestination_t *destination,
                                      const EmberZclBindingEntry_t *entry,
                                      EmberZclBindingId_t bindingId,
                                      const EmberZclBindingResponseHandler handler);
EmberStatus emberZclSendRemoveBinding(const EmberZclDestination_t *destination,
                                      const EmberZclClusterSpec_t *clusterSpec,
                                      EmberZclBindingId_t bindingId,
                                      const EmberZclBindingResponseHandler handler);

#ifndef DOXYGEN_SHOULD_SKIP_THIS
size_t emZclGetBindingCount(void);
bool emZclHasBinding(const EmZclContext_t *context,
                     EmberZclBindingId_t bindingId);
void emZclUriClusterBindingHandler(EmZclContext_t *context);
void emZclUriClusterBindingIdHandler(EmZclContext_t *context);
#endif

// -----------------------------------------------------------------------------
// Commands.

EmberStatus emberZclSendDefaultResponse(const EmberZclCommandContext_t *context,
                                        EmberZclStatus_t status);

#ifndef DOXYGEN_SHOULD_SKIP_THIS
extern const EmZclCommandEntry_t emZclCommandTable[];
extern const size_t emZclCommandCount;
const EmZclCommandEntry_t *emZclFindCommand(const EmberZclClusterSpec_t *clusterSpec,
                                            EmberZclCommandId_t commandId);
EmberStatus emZclSendCommandRequest(const EmberZclDestination_t *destination,
                                    const EmberZclClusterSpec_t *clusterSpec,
                                    EmberZclCommandId_t commandId,
                                    const void *request,
                                    const ZclipStructSpec *requestSpec,
                                    const ZclipStructSpec *responseSpec,
                                    const EmZclResponseHandler handler);
EmberStatus emZclSendCommandResponse(const EmberZclCommandContext_t *context,
                                     const void *response,
                                     const ZclipStructSpec *responseSpec);
void emZclUriClusterCommandHandler(EmZclContext_t *context);
void emZclUriClusterCommandIdHandler(EmZclContext_t *context);
#endif

// -----------------------------------------------------------------------------
// Reporting.

#ifndef DOXYGEN_SHOULD_SKIP_THIS
bool emZclHasReportingConfiguration(EmberZclEndpointId_t endpointId,
                                    const EmberZclClusterSpec_t *clusterSpec,
                                    EmberZclReportingConfigurationId_t reportingConfigurationId);
void emZclUriClusterNotificationHandler(EmZclContext_t *context);
void emZclUriClusterReportingConfigurationHandler(EmZclContext_t *context);
void emZclUriClusterReportingConfigurationIdHandler(EmZclContext_t *context);
#endif

// -----------------------------------------------------------------------------
// Utilities.

#ifndef DOXYGEN_SHOULD_SKIP_THIS

const uint8_t *emZclGetMessageStatusName(EmberZclMessageStatus_t status);

// URI segment matching functions
bool emZclUriPathStringMatch       (EmZclContext_t *context, void *castString, uint8_t depth);
bool emZclUriQueryStringPrefixMatch(EmZclContext_t *context, void *castString, uint8_t depth);

EmberStatus emZclSend(const EmberZclNetworkDestination_t *destination,
                      EmberCoapCode code,
                      const uint8_t *uri,
                      const uint8_t *payload,
                      uint16_t payloadLength,
                      EmZclMessageResponseHandler handler,
                      void *applicationData,
                      uint16_t applicationDataLength);
EmberStatus emZclRespondNoPayload(EmberCoapCode code);
EmberStatus emZclRespond201Created(const uint8_t *locationPath);
EmberStatus emZclRespond202Deleted(void);
EmberStatus emZclRespond204Changed(void);
EmberStatus emZclRespond204ChangedCborState(const CborState *state);
EmberStatus emZclRespond205ContentCbor(const uint8_t *payload, uint16_t payloadLength);
EmberStatus emZclRespond205ContentCborState(const CborState *state);
EmberStatus emZclRespond205ContentLinkFormat(const uint8_t *payload, uint16_t payloadLength);
EmberStatus emZclRespond400BadRequest(void);
EmberStatus emZclRespond404NotFound(void);
EmberStatus emZclRespond405MethodNotAllowed(void);
EmberStatus emZclRespond413RequestEntityTooLarge(void);
EmberStatus emZclRespond500InternalServerError(void);

uint8_t emZclIntToHexString(uintmax_t value, size_t size, uint8_t *result);
bool emZclHexStringToInt(const uint8_t *chars, size_t length, uintmax_t *result);
size_t emZclClusterToString(const EmberZclClusterSpec_t *clusterSpec,
                            uint8_t *result);
bool emZclStringToCluster(const uint8_t *chars,
                          size_t length,
                          EmberZclClusterSpec_t *clusterSpec);
uint16_t emZclParseUri(const uint8_t *payload, EmZclUriContext_t *context);

size_t emZclThingToUriPath(const EmberZclApplicationDestination_t *destination,
                           const EmberZclClusterSpec_t *clusterSpec,
                           char thing,
                           uint8_t *result);

size_t emZclDestinationToUri(const EmberZclDestination_t *destination,
                             uint8_t *result);
bool emZclUriToDestination(const uint8_t *uri, EmberZclDestination_t *result);
bool emZclUriToDestinationAndCluster(const uint8_t *uri,
                                     EmberZclDestination_t *result,
                                     EmberZclClusterSpec_t *clusterSpec);
size_t emZclUidToString(const EmberZclUid_t *uid, uint16_t uidBits, uint8_t *result);
bool emZclStringToUid(const uint8_t *uid,
                      size_t length,
                      EmberZclUid_t *result,
                      uint16_t *resultBits);

#define emZclAttributeToUriPath(address, clusterSpec, result) \
  emZclThingToUriPath(address, clusterSpec, 'a', result)
#define emZclBindingToUriPath(address, clusterSpec, result) \
  emZclThingToUriPath(address, clusterSpec, 'b', result)
#define emZclCommandToUriPath(address, clusterSpec, result) \
  emZclThingToUriPath(address, clusterSpec, 'c', result)
#define emZclNotificationToUriPath(address, clusterSpec,  result) \
  emZclThingToUriPath(address, clusterSpec, 'n', result)
#define emZclReportingConfigurationToUriPath(address, clusterSpec,  result) \
  emZclThingToUriPath(address, clusterSpec, 'r', result)

size_t emZclThingIdToUriPath(const EmberZclApplicationDestination_t *destination,
                             const EmberZclClusterSpec_t *clusterSpec,
                             char thing,
                             uintmax_t thingId,
                             size_t size,
                             uint8_t *result);
#define emZclAttributeIdToUriPath(address, clusterSpec, attributeId, result) \
  emZclThingIdToUriPath(address, clusterSpec, 'a', attributeId, sizeof(EmberZclAttributeId_t), result)
#define emZclBindingIdToUriPath(address, clusterSpec, bindingId, result) \
  emZclThingIdToUriPath(address, clusterSpec, 'b', bindingId, sizeof(EmberZclBindingId_t), result)
#define emZclCommandIdToUriPath(address, clusterSpec, commandId, result) \
  emZclThingIdToUriPath(address, clusterSpec, 'c', commandId, sizeof(EmberZclCommandId_t), result)
#define emZclReportingConfigurationIdToUriPath(address, clusterSpec, reportingConfigurationId, result) \
  emZclThingIdToUriPath(address, clusterSpec, 'r', reportingConfigurationId, sizeof(EmberZclReportingConfigurationId_t), result)

bool emZclGetMulticastAddress(EmberIpv6Address * dst);
#endif

// ----------------------------------------------------------------------------
// CLI.

#ifndef DOXYGEN_SHOULD_SKIP_THIS
void emZclCliSetCurrentRequestCommand(const EmberZclClusterSpec_t *clusterSpec,
                                      EmberZclCommandId_t commandId,
                                      const ZclipStructSpec *structSpec,
                                      EmZclCliRequestCommandFunction function,
                                      const char *cliFormat);
#endif

#endif // __ZCL_CORE_H__
