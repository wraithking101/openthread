// Copyright 2015 Silicon Laboratories, Inc.

#include PLATFORM_HEADER
#include CONFIGURATION_HEADER
#include EMBER_AF_API_STACK
#ifdef EMBER_AF_API_DEBUG_PRINT
  #include EMBER_AF_API_DEBUG_PRINT
#endif
#include EMBER_AF_API_ZCL_CORE

void emberZclClusterBasicServerCommandResetToFactoryDefaultsRequestHandler(const EmberZclCommandContext_t *context,
                                                                           const EmberZclClusterBasicServerCommandResetToFactoryDefaultsRequest_t *request)
{
  emberAfCorePrintln("RX: ResetToFactoryDefaults");
  emberZclResetAttributes(context->endpointId);
  emberZclSendDefaultResponse(context, EMBER_ZCL_STATUS_SUCCESS);
}
