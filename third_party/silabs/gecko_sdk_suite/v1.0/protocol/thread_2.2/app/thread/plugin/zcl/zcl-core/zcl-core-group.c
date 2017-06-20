// Copyright 2015 Silicon Laboratories, Inc.

#include PLATFORM_HEADER
#include CONFIGURATION_HEADER
#include EMBER_AF_API_HAL
#include "zcl-core.h"

void emZclGroupNetworkStatusHandler(EmberNetworkStatus newNetworkStatus,
                                    EmberNetworkStatus oldNetworkStatus,
                                    EmberJoinFailureReason reason)
{
  // If the device is no longer associated with a network, its groups are
  // removed, because groups are specific to a network.
  if (newNetworkStatus == EMBER_NO_NETWORK) {
    emberZclRemoveAllGroups();
  }
}

bool emZclHasGroup(EmberZclGroupId_t groupId)
{
  for (size_t i = 0; i < emZclEndpointCount; i++) {
    if (emberZclIsEndpointInGroup(emZclEndpointTable[i].endpointId, groupId)) {
      return true;
    }
  }
  return false;
}

bool emberZclIsEndpointInGroup(EmberZclEndpointId_t endpointId,
                               EmberZclGroupId_t groupId)
{
  if (EMBER_ZCL_GROUP_MIN <= groupId && groupId <= EMBER_ZCL_GROUP_MAX) {
    for (size_t i = 0; i < EMBER_ZCL_GROUP_TABLE_SIZE; i++) {
      EmberZclGroupEntry_t entry;
      halCommonGetIndexedToken(&entry, TOKEN_ZCL_CORE_GROUP_TABLE, i);
      if (groupId == entry.groupId && endpointId == entry.endpointId) {
        return true;
      }
    }
  }
  return (groupId == EMBER_ZCL_GROUP_ALL_ENDPOINTS);
}

EmberZclStatus_t emberZclAddEndpointToGroup(EmberZclEndpointId_t endpointId,
                                            EmberZclGroupId_t groupId)
{

  if (emberZclIsEndpointInGroup(endpointId, groupId)) {
    return EMBER_ZCL_STATUS_DUPLICATE_EXISTS;
  } else if (groupId < EMBER_ZCL_GROUP_MIN || EMBER_ZCL_GROUP_MAX < groupId) {
    return EMBER_ZCL_STATUS_INVALID_FIELD;
  } else {
    for (size_t i = 0; i < EMBER_ZCL_GROUP_TABLE_SIZE; i++) {
      EmberZclGroupEntry_t entry;
      halCommonGetIndexedToken(&entry, TOKEN_ZCL_CORE_GROUP_TABLE, i);
      if (entry.groupId == EMBER_ZCL_GROUP_NULL) {
        entry.groupId = groupId;
        entry.endpointId = endpointId;
        halCommonSetIndexedToken(TOKEN_ZCL_CORE_GROUP_TABLE, i, &entry);
        return EMBER_ZCL_STATUS_SUCCESS;
      }
    }
    return EMBER_ZCL_STATUS_INSUFFICIENT_SPACE;
  }
}

enum {
 GROUP_FLAG      = 0x01,
 NULL_GROUP_FLAG = 0x02,
 ENDPOINT_FLAG   = 0x04,
};

static EmberZclStatus_t removeEndpoints(uint8_t mask,
                                        EmberZclEndpointId_t endpointId,
                                        EmberZclGroupId_t groupId)
{
  if ((mask & GROUP_FLAG)
      && (groupId < EMBER_ZCL_GROUP_MIN || EMBER_ZCL_GROUP_MAX < groupId)) {
    return EMBER_ZCL_STATUS_INVALID_FIELD;
  }

  EmberZclStatus_t status = EMBER_ZCL_STATUS_NOT_FOUND;
  for (size_t i = 0; i < EMBER_ZCL_GROUP_TABLE_SIZE; i++) {
    EmberZclGroupEntry_t entry;
    halCommonGetIndexedToken(&entry, TOKEN_ZCL_CORE_GROUP_TABLE, i);
    uint8_t flags = 0;
    if (groupId == entry.groupId) {
      flags |= GROUP_FLAG;
    }
    if (endpointId == entry.endpointId) {
      flags |= ENDPOINT_FLAG;
    }
    if (mask == NULL_GROUP_FLAG || (flags & mask) == mask) {
      entry.groupId = EMBER_ZCL_GROUP_NULL;
      entry.endpointId = EMBER_ZCL_ENDPOINT_NULL;
      halCommonSetIndexedToken(TOKEN_ZCL_CORE_GROUP_TABLE, i, &entry);
      if ((mask & (GROUP_FLAG | ENDPOINT_FLAG))
          == (GROUP_FLAG | ENDPOINT_FLAG)) {
        return EMBER_ZCL_STATUS_SUCCESS;
      } else {
        status = EMBER_ZCL_STATUS_SUCCESS;
      }
    }
  }
  return status;
}

EmberZclStatus_t emberZclRemoveEndpointFromGroup(EmberZclEndpointId_t endpointId,
                                                 EmberZclGroupId_t groupId)
{
  return removeEndpoints(GROUP_FLAG | ENDPOINT_FLAG, endpointId, groupId);
}

EmberZclStatus_t emberZclRemoveEndpointFromAllGroups(EmberZclEndpointId_t endpointId)
{
  return removeEndpoints(ENDPOINT_FLAG, endpointId, EMBER_ZCL_GROUP_NULL);
}

EmberZclStatus_t emberZclRemoveGroup(EmberZclGroupId_t groupId)
{
  return removeEndpoints(GROUP_FLAG, EMBER_ZCL_ENDPOINT_NULL, groupId);
}

EmberZclStatus_t emberZclRemoveAllGroups(void)
{
  return removeEndpoints(NULL_GROUP_FLAG,
                         EMBER_ZCL_ENDPOINT_NULL,
                         EMBER_ZCL_GROUP_NULL);
}

// zcl/g:
//   GET: list groups.
//   OTHER: not allowed.
void emZclUriGroupHandler(EmZclContext_t *context)
{
  EmberZclGroupId_t groups[EMBER_ZCL_GROUP_TABLE_SIZE + 1] = {
    EMBER_ZCL_GROUP_ALL_ENDPOINTS,
  };
  size_t count = 1;
  for (size_t i = 0; i < EMBER_ZCL_GROUP_TABLE_SIZE; i++) {
    EmberZclGroupEntry_t entry;
    halCommonGetIndexedToken(&entry, TOKEN_ZCL_CORE_GROUP_TABLE, i);
    if (entry.groupId != EMBER_ZCL_GROUP_NULL) {
      size_t j;
      for (j = 1; j < count; j++) {
        if (entry.groupId == groups[j]) {
          break;
        } else if (entry.groupId < groups[j]) {
          MEMMOVE(groups + j + 1,
                  groups + j,
                  (count - j) * sizeof(EmberZclGroupId_t));
          groups[j] = entry.groupId;
          count++;
          break;
        }
      }
      if (j == count) {
        groups[j] = entry.groupId;
        count++;
      }
    }
  }

  CborState state;
  uint8_t buffer[128];
  emCborEncodeIndefiniteArrayStart(&state, buffer, sizeof(buffer));
  for (size_t i = 0; i < count; i++) {
    if (!emCborEncodeValue(&state,
                           EMBER_ZCLIP_TYPE_UNSIGNED_INTEGER,
                           sizeof(EmberZclGroupId_t),
                           (const uint8_t *)&groups[i])) {
      emZclRespond500InternalServerError();
      return;
    }
  }
  emCborEncodeBreak(&state);
  emZclRespond205ContentCborState(&state);
}

// GET zcl/g/XXXX:
//   GET: list endpoints in group.
//   OTHER: not allowed.
void emZclUriGroupIdHandler(EmZclContext_t *context)
{
  CborState state;
  uint8_t buffer[128];
  emCborEncodeIndefiniteArrayStart(&state, buffer, sizeof(buffer));
  for (size_t i = 0; i < emZclEndpointCount; i++) {
    if (emberZclIsEndpointInGroup(emZclEndpointTable[i].endpointId,
                                  context->groupId)
        && !emCborEncodeValue(&state,
                              EMBER_ZCLIP_TYPE_UNSIGNED_INTEGER,
                              sizeof(EmberZclEndpointId_t),
                              &emZclEndpointTable[i].endpointId)) {
      emZclRespond500InternalServerError();
      return;
    }
  }
  emCborEncodeBreak(&state);
  emZclRespond205ContentCborState(&state);
}
