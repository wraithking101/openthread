// Copyright 2017 Silicon Laboratories, Inc.

#include PLATFORM_HEADER
#include CONFIGURATION_HEADER
#include EMBER_AF_API_STACK
#include EMBER_AF_API_HAL
#ifdef EMBER_AF_API_DEBUG_PRINT
  #include EMBER_AF_API_DEBUG_PRINT
#endif
#include EMBER_AF_API_ZCL_CORE

// WARNING: This sample application uses fixed network parameters and the well-
// know sensor/sink network key as the master key.  This is done for
// demonstration purposes, so nodes can join without a commissioner (i.e., out-
// of-band commissioning), and so that packets will decrypt automatically in
// Simplicity Studio.  Predefined network parameters only work for a single
// deployment and using predefined keys is a significant security risk.  Real
// devices MUST use random parameters and keys.
//
// These parameters have been chosen to match the border router sample
// application, which will allow this design to perform out of band joining with
// the border router sample application without need for modification.
static const uint8_t preferredChannel = 19;
static const uint8_t networkId[EMBER_NETWORK_ID_SIZE] = "precommissioned";
static const EmberPanId panId = 0x1075;
static const EmberIpv6Prefix ulaPrefix = {
  {0xFD, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};
static const uint8_t extendedPanId[EXTENDED_PAN_ID_SIZE] = {
  0xc6, 0xef, 0xe1, 0xb4, 0x5f, 0xc7, 0x8e, 0x4f
};
static const EmberKeyData masterKey = {
  {0x65, 0x6D, 0x62, 0x65, 0x72, 0x20, 0x45, 0x4D,
   0x32, 0x35, 0x30, 0x20, 0x63, 0x68, 0x69, 0x70,},
};

enum {
  INITIAL                 = 0,
  RESUME_NETWORK          = 1,
  COMMISSION_NETWORK      = 2,
  JOIN_COMMISSIONED       = 3,
  STEADY                  = 5,
  RESET_NETWORK_STATE     = 6,
};
static uint8_t state = INITIAL;
EmberEventControl stateEventControl;
static void setNextState(uint8_t nextState);

#define DEFAULT_REPORTABLE_LEVEL_CHANGE   10

void emberAfNetworkStatusCallback(EmberNetworkStatus newNetworkStatus,
                                  EmberNetworkStatus oldNetworkStatus,
                                  EmberJoinFailureReason reason)
{
  // This callback is called whenever the network status changes, like when
  // we finish joining to a network or when we lose connectivity.  If we have
  // no network, we try joining to one.  If we have a saved network, we try to
  // resume operations on that network.  When we are joined and attached to the
  // network, we are in the steady state and wait for input from other nodes.

  emberEventControlSetInactive(stateEventControl);

  switch (newNetworkStatus) {
  case EMBER_NO_NETWORK:
    if (reason != EMBER_JOIN_FAILURE_REASON_NONE) {
      emberAfCorePrintln("ERR: Joining failed: 0x%x", reason);
    }
    setNextState(COMMISSION_NETWORK);
    break;
  case EMBER_SAVED_NETWORK:
    setNextState(RESUME_NETWORK);
    break;
  case EMBER_JOINING_NETWORK:
    // Wait for either the "attaching" or "no network" state.
    break;
  case EMBER_JOINED_NETWORK_ATTACHING:
    // Wait for the "attached" state.
    if (oldNetworkStatus == EMBER_JOINED_NETWORK_ATTACHED) {
      emberAfCorePrintln("Trying to re-connect...");
    }
    break;
  case EMBER_JOINED_NETWORK_ATTACHED:
    emberAfCorePrintln("Attached");
    state = STEADY;
    break;
  case EMBER_JOINED_NETWORK_NO_PARENT:
    // We always join as a router, so we should never end up in the "no parent"
    // state.
    assert(false);
    break;
  default:
    assert(false);
    break;
  }
}

static void resumeNetwork(void)
{
  emberAfCorePrintln("Resuming...");
  emberResumeNetwork();
}

void emberResumeNetworkReturn(EmberStatus status)
{
  // This return indicates whether the stack is going to attempt to resume.  If
  // so, the result is reported later as a network status change.  If we cannot
  // even attempt to resume, we just give up and reset our network state, which
  // will trigger join instead.

  if (status != EMBER_SUCCESS) {
    emberAfCorePrintln("ERR: Unable to resume: 0x%x", status);
    setNextState(RESET_NETWORK_STATE);
  }
}

static void commissionNetwork(void)
{
  emberAfCorePrintln("Commissioning...");
  emberCommissionNetwork(preferredChannel,
                         0,                 // fallback channel mask - ignored
                         networkId,
                         sizeof(networkId),
                         panId,
                         ulaPrefix.bytes,
                         extendedPanId,
                         &masterKey,
                         0);                // key sequence
}

void emberCommissionNetworkReturn(EmberStatus status)
{
  // This return indicates whether the network was commissioned.  If so, we can
  // proceed to joining.  Otherwise, we just give up and reset our network
  // state, which will trigger a fresh join attempt.

  if (status == EMBER_SUCCESS) {
    setNextState(JOIN_COMMISSIONED);
  } else {
    emberAfCorePrintln("ERR: Unable to commission: 0x%x", status);
    setNextState(RESET_NETWORK_STATE);
  }
}

static void joinCommissioned(void)
{
  emberAfCorePrintln("Joining...");
  emberJoinCommissioned(3,            // radio tx power
                        EMBER_ROUTER,
                        true);        // require connectivity
}

void emberJoinNetworkReturn(EmberStatus status)
{
  // This return indicates whether the stack is going to attempt to join.  If
  // so, the result is reported later as a network status change.  Otherwise,
  // we just give up and reset our network state, which will trigger a fresh
  // join attempt.

  if (status != EMBER_SUCCESS) {
    emberAfCorePrintln("ERR: Unable to join: 0x%x", status);
    setNextState(RESET_NETWORK_STATE);
  }
}

static void resetNetworkState(void)
{
  emberAfCorePrintln("Resetting...");
  emberResetNetworkState();
}

void emberZclGetPublicKeyCallback(const uint8_t **publicKey,
                                  uint16_t *publicKeySize)
{
  *publicKey = emberEui64()->bytes;
  *publicKeySize = EUI64_SIZE;
}

void emberZclGetDefaultReportingConfigurationCallback(EmberZclEndpointId_t endpointId,
                                                      const EmberZclClusterSpec_t *clusterSpec,
                                                      EmberZclReportingConfiguration_t *configuration)
{
  // We have reportable attributes in the On/Off and Level Control servers.

  if (emberZclCompareClusterSpec(&emberZclClusterOnOffServerSpec,
                                 clusterSpec)) {
    configuration->minimumIntervalS =  1;
    configuration->maximumIntervalS = 60;
  } else if (emberZclCompareClusterSpec(&emberZclClusterLevelControlServerSpec,
                                        clusterSpec)) {
    configuration->minimumIntervalS = 10;
    configuration->maximumIntervalS = 60;
  }
}

bool emberAfPluginIdleSleepOkToSleepCallback(uint32_t durationMs)
{
  // The device can be a router, so it should never sleep.

  return false;
}

void emberZclGetDefaultReportableChangeCallback(EmberZclEndpointId_t endpointId,
                                                const EmberZclClusterSpec_t *clusterSpec,
                                                EmberZclAttributeId_t attributeId,
                                                void *buffer,
                                                size_t bufferLength)
{
  int8u level = DEFAULT_REPORTABLE_LEVEL_CHANGE;

  if (emberZclCompareClusterSpec(&emberZclClusterLevelControlServerSpec, clusterSpec)
      && (attributeId == EMBER_ZCL_CLUSTER_LEVEL_CONTROL_SERVER_ATTRIBUTE_CURRENT_LEVEL)) {
    *(int8u *)buffer = level;
  }
}

void stateEventHandler(void)
{
  emberEventControlSetInactive(stateEventControl);

  switch (state) {
  case RESUME_NETWORK:
    resumeNetwork();
    break;
  case COMMISSION_NETWORK:
    commissionNetwork();
    break;
  case JOIN_COMMISSIONED:
    joinCommissioned();
    break;
  case RESET_NETWORK_STATE:
    resetNetworkState();
    break;
  default:
    assert(false);
    break;
  }
}

static void setNextState(uint8_t nextState)
{
  state = nextState;
  emberEventControlSetActive(stateEventControl);
}
