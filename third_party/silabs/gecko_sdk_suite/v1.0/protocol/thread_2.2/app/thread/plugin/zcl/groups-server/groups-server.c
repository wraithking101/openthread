// Copyright 2015 Silicon Laboratories, Inc.

#include PLATFORM_HEADER
#include CONFIGURATION_HEADER
#include EMBER_AF_API_STACK
#ifdef EMBER_AF_API_DEBUG_PRINT
  #include EMBER_AF_API_DEBUG_PRINT
#endif
#include EMBER_AF_API_ZCL_CORE

bool emZclGroupsServerPreAttributeChangeHandler(EmberZclEndpointId_t endpointId,
                                                const EmberZclClusterSpec_t *clusterSpec,
                                                EmberZclAttributeId_t attributeId,
                                                const void *buffer,
                                                size_t bufferLength)
{
  // Group names are not supported, so force the NameSupport attribute to stay
  // at 0, indicating no support.
  return (!emberZclCompareClusterSpec(&emberZclClusterGroupsServerSpec,
                                      clusterSpec)
          || attributeId != EMBER_ZCL_CLUSTER_GROUPS_SERVER_ATTRIBUTE_GROUP_NAME_SUPPORT
          || *(const uint8_t *)buffer == 0);
}

void emberZclClusterGroupsServerCommandAddGroupRequestHandler(const EmberZclCommandContext_t *context,
                                                              const EmberZclClusterGroupsServerCommandAddGroupRequest_t *request)
{
  emberAfCorePrintln("RX: AddGroup");

  EmberZclClusterGroupsServerCommandAddGroupResponse_t response = {0};
  response.status = emberZclAddEndpointToGroup(context->endpointId,
                                               request->groupId);
  response.groupId = request->groupId;

  emberZclSendClusterGroupsServerCommandAddGroupResponse(context, &response);
}

void emberZclClusterGroupsServerCommandViewGroupRequestHandler(const EmberZclCommandContext_t *context,
                                                               const EmberZclClusterGroupsServerCommandViewGroupRequest_t *request)
{
  emberAfCorePrintln("RX: ViewGroup");

  EmberZclClusterGroupsServerCommandViewGroupResponse_t response = {0};
  uint8_t emptyString[] = {0};
  response.status = (emberZclIsEndpointInGroup(context->endpointId,
                                               request->groupId)
                     ? EMBER_ZCL_STATUS_SUCCESS
                     : EMBER_ZCL_STATUS_NOT_FOUND);
  response.groupId = request->groupId;
  response.groupName = emptyString; // no group name support

  emberZclSendClusterGroupsServerCommandViewGroupResponse(context, &response);
}

void emberZclClusterGroupsServerCommandGetGroupMembershipRequestHandler(const EmberZclCommandContext_t *context,
                                                                        const EmberZclClusterGroupsServerCommandGetGroupMembershipRequest_t *request)
{
  // TODO: Get Group Membership can't be implemented until CBOR arrays are
  // implemented.
  emberAfCorePrintln("RX: GetGroupMembership");
  emberZclSendDefaultResponse(context, EMBER_ZCL_STATUS_UNSUP_CLUSTER_COMMAND);
}

void emberZclClusterGroupsServerCommandRemoveGroupRequestHandler(const EmberZclCommandContext_t *context,
                                                                 const EmberZclClusterGroupsServerCommandRemoveGroupRequest_t *request)
{
  emberAfCorePrintln("RX: RemoveGroup");

  EmberZclClusterGroupsServerCommandRemoveGroupResponse_t response = {0};
  response.status = emberZclRemoveEndpointFromGroup(context->endpointId,
                                                    request->groupId);
  response.groupId = request->groupId;

  emberZclSendClusterGroupsServerCommandRemoveGroupResponse(context, &response);
}

void emberZclClusterGroupsServerCommandRemoveAllGroupsRequestHandler(const EmberZclCommandContext_t *context,
                                                                     const EmberZclClusterGroupsServerCommandRemoveAllGroupsRequest_t *request)
{
  emberAfCorePrintln("RX: RemoveAllGroups");

  EmberZclStatus_t status
    = emberZclRemoveEndpointFromAllGroups(context->endpointId);

  emberZclSendDefaultResponse(context, status);
}

void emberZclClusterGroupsServerCommandAddGroupIfIdentifyingRequestHandler(const EmberZclCommandContext_t *context,
                                                                           const EmberZclClusterGroupsServerCommandAddGroupIfIdentifyingRequest_t *request)
{
  emberAfCorePrintln("RX: AddGroupIfIdentifying");

  uint16_t identifyTimeS = 0;
  EmberZclStatus_t status = EMBER_ZCL_STATUS_FAILURE;

  emberZclReadAttribute(context->endpointId,
                        &emberZclClusterIdentifyServerSpec,
                        EMBER_ZCL_CLUSTER_IDENTIFY_SERVER_ATTRIBUTE_IDENTIFY_TIME,
                        &identifyTimeS,
                        sizeof(identifyTimeS));
  if (identifyTimeS != 0) {
    status = emberZclAddEndpointToGroup(context->endpointId, request->groupId);
  }

  emberZclSendDefaultResponse(context, status);
}
