// Copyright 2017 Silicon Laboratories, Inc.

#include PLATFORM_HEADER
#include CONFIGURATION_HEADER
#include EMBER_AF_API_STACK
#ifdef EMBER_AF_API_DEBUG_PRINT
  #include EMBER_AF_API_DEBUG_PRINT
#endif
#include EMBER_AF_API_ZCL_CORE

#define MILLI_TO_DECI_CONVERSION_FACTOR 100

//------------------------------------------------------------------------------
// Implemented callbacks

void emberAfPluginBatteryMonitorDataCallback(uint16_t voltageMilliV)
{
  EmberZclStatus_t status;
  uint8_t voltageDeciV;
  EmberZclEndpointId_t endpoint;
  EmberZclEndpointIndex_t i;

  emberAfCorePrintln("New voltage reading: %d mV", voltageMilliV);

  // convert from mV to 100 mV, which are the units specified by zigbee spec for
  // the power configuration cluster's voltage attribute.
  voltageDeciV = ((uint8_t) (voltageMilliV / MILLI_TO_DECI_CONVERSION_FACTOR));

  for (i = 0; i < emZclEndpointCount; i++) {
    endpoint = emberZclEndpointIndexToId(i,
                                         &emberZclClusterPowerConfigServerSpec);
    if (endpoint != EMBER_ZCL_ENDPOINT_NULL) {
      status
        = emberZclWriteAttribute(endpoint,
                                 &emberZclClusterPowerConfigServerSpec,
                                 EMBER_ZCL_CLUSTER_POWER_CONFIG_SERVER_ATTRIBUTE_BATTERY_VOLTAGE,
                                 (uint8_t *)&voltageDeciV,
                                 sizeof(voltageDeciV));
      if (status != EMBER_ZCL_STATUS_SUCCESS) {
        emberAfCorePrintln("Power Configuration Server: failed to write value "
                           "0x%x to endpoint %d",
                           voltageDeciV,
                           endpoint);
      }
    }
  }
}
