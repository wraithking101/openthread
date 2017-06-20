// Copyright 2017 Silicon Laboratories, Inc.

#include PLATFORM_HEADER
#include CONFIGURATION_HEADER
#include EMBER_AF_API_STACK
#include EMBER_AF_API_HAL
#ifdef EMBER_AF_API_DEBUG_PRINT
  #include EMBER_AF_API_DEBUG_PRINT
#endif

#include EMBER_AF_API_SB1_GESTURE_SENSOR
#include EMBER_AF_API_LED_BLINK

//------------------------------------------------------------------------------
// Application specific macros

// Macros used to enable/disable application behavior
#ifndef COLOR_CHANGE_ENABLED
#define COLOR_CHANGE_ENABLED            1
#endif

#ifndef LED_ENABLED
#define LED_ENABLED                     1
#endif

#ifndef SWIPING_ENABLED
#define SWIPING_ENABLED                 1
#endif

// Macros used to determine if switch is controlling Hue, Temp, or brightness
#define NORMAL_TAB 0
#define TEMP_TAB   1
#define HUE_TAB    2

// Macros used in color temperature and hue stepping commands
#define COLOR_TEMP_STEP_POSITIVE        1
#define COLOR_TEMP_STEP_NEGATIVE        3
#define COLOR_TEMP_STEP_AMOUNT          50
#define COLOR_HUE_STEP_POSITIVE         1
#define COLOR_HUE_STEP_NEGATIVE         3
#define COLOR_HUE_STEP_AMOUNT           10
#define LEVEL_STEP_POSITIVE             0
#define LEVEL_STEP_NEGATIVE             1
#define LEVEL_STEP_AMOUNT               30
#define LEVEL_STEP_TIME                 3

//------------------------------------------------------------------------------
// Application specific global variables

// State variable for what tab is currently being controlled
static uint8_t tabState = NORMAL_TAB;

//------------------------------------------------------------------------------
// Event function forward declaration
EmberEventControl frameTimeoutEventControl;
EmberEventControl stateEventControl;

static void sendCommandFromHoldUp();
static void sendCommandFromHoldDown();
void clearBindingTable(void);
static void genericMessageResponseHandler(EmberCoapStatus status,
                                          const EmberZclCommandContext_t *context,
                                          void *commandResponse);

// WARNING: This sample application uses fixed network parameters and the well-
// know sensor/sink network key as the master key.  This is done for
// demonstration purposes, so nodes can join without a commissioner (i.e., out-
// of-band commissioning), and so that packets will decrypt automatically in
// Simplicity Studio.  Predefined network parameters only work for a single
// deployment and using predefined keys is a significant security risk.  Real
// devices MUST use random parameters and keys.
//
// These parameters have been chosen to match the border router, which will
// allow this design to perform out of band joining with the border router
// without need for modification.
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
  ATTACH_TO_NETWORK       = 4,
  STEADY                  = 5,
  RESET_NETWORK_STATE     = 6,
};
static uint8_t state = INITIAL;
static void setNextState(uint8_t nextState);

bool okToLongPoll = true;

static uint8_t outboundMessageCount = 0;

void emberAfNetworkStatusCallback(EmberNetworkStatus newNetworkStatus,
                                  EmberNetworkStatus oldNetworkStatus,
                                  EmberJoinFailureReason reason)
{
  // This callback is called whenever the network status changes, like when
  // we finish joining to a network or when we lose connectivity.  If we have
  // no network, we try joining to one.  If we have a saved network, we try to
  // resume operations on that network.  When we are joined and attached to the
  // network, we are in the steady state and wait for input from the user.

  emberEventControlSetInactive(stateEventControl);

  emberAfCorePrintln("net stat callback with status of %d", emberNetworkStatus());
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
    // Wait for either the "attached" state.
    if (oldNetworkStatus == EMBER_JOINED_NETWORK_ATTACHED) {
      emberAfCorePrintln("Trying to re-connect...");
    }
    break;
  case EMBER_JOINED_NETWORK_ATTACHED:
    emberAfCorePrintln("Attached");
    state = STEADY;
    break;
  case EMBER_JOINED_NETWORK_NO_PARENT:
    if (state == ATTACH_TO_NETWORK) {
      emberAfCorePrintln("ERR: Reattaching failed. Old reason %d, reason %d",
                         oldNetworkStatus,
                         reason);
    } else {
      emberAfCorePrintln("ERR: No connection to network.  Old reason %d, reason %d",
                         oldNetworkStatus,
                         reason);
    }
    setNextState(ATTACH_TO_NETWORK);
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
  emberJoinCommissioned(3,                       // radio tx power
                        EMBER_SLEEPY_END_DEVICE,
                        false);                  // don't require connectivity
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

static void attachToNetwork(void)
{
  emberAfCorePrintln("Reattaching...");
  emberAttachToNetwork();
}

void emberAttachToNetworkReturn(EmberStatus status)
{
  // This return indicates whether the stack is going to attempt to attach.  If
  // so, the result is reported later as a network status change.  If we cannot
  // even attempt to attach, we just give up and reset our network state, which
  // will trigger a fresh join attempt.

  if (status != EMBER_SUCCESS) {
    emberAfCorePrintln("ERR: Unable to reattach: 0x%x", status);
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

void emberZclNotificationCallback(const EmberZclNotificationContext_t *context,
                                  const EmberZclClusterSpec_t *clusterSpec,
                                  EmberZclAttributeId_t attributeId,
                                  const void *buffer,
                                  size_t bufferLength)
{
  if (emberZclCompareClusterSpec(&emberZclClusterOnOffServerSpec, clusterSpec)
      && attributeId == EMBER_ZCL_CLUSTER_ON_OFF_SERVER_ATTRIBUTE_ON_OFF) {
    bool onOff = *((bool *)buffer);
    emberAfCorePrintln("Light is %s", (onOff ? "on" : "off"));
  } else if (emberZclCompareClusterSpec(&emberZclClusterLevelControlServerSpec, clusterSpec)
             && attributeId == EMBER_ZCL_CLUSTER_LEVEL_CONTROL_SERVER_ATTRIBUTE_CURRENT_LEVEL) {
    uint8_t currentLevel = *((uint8_t *)buffer);
    emberAfCorePrintln("Light is at 0x%x", currentLevel);
  }
}

/** @brief Ok To Long Poll
 *
 * This function is called by the Polling plugin to determine if the node can
 * wait an extended period of time between polls.  Generally, a node can poll
 * infrequently when it does not expect to receive data, via its parent, from
 * other nodes in the network.  When data is expected, the node must poll more
 * frequently to avoid having its parent discard stale data due to the MAC
 * indirect transmission timeout (::EMBER_INDIRECT_TRANSMISSION_TIMEOUT).  The
 * application should return true if it is not expecting data or false
 * otherwise.
 */
bool emberAfPluginPollingOkToLongPollCallback(void)
{
  if (emberZclEzModeIsActive()) {
    return false;
  }

  return okToLongPoll;
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
  case ATTACH_TO_NETWORK:
    attachToNetwork();
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

void sendCommandToBindings(uint16_t cluster, uint16_t command, void *request)
{
  uint8_t numSends = 0;
  EmberZclBindingId_t currentBindIndex;
  EmberZclBindingEntry_t currentBind;
  EmberZclDestination_t destination;
  EmberStatus status;

  for (currentBindIndex = 0;
       currentBindIndex < EMBER_ZCL_BINDING_TABLE_SIZE;
       currentBindIndex++) {
    // Check to see if the binding table entry matches the cluster requested
    if ((emberZclGetBinding(currentBindIndex, &currentBind)) &&
        (currentBind.clusterSpec.id == cluster)) {
      destination = currentBind.destination;

      switch (cluster) {
      case EMBER_ZCL_CLUSTER_ON_OFF:
        // For ON_OFF, support the on command and off command.
        switch (command) {
        case EMBER_ZCL_CLUSTER_ON_OFF_SERVER_COMMAND_OFF:
          numSends++;
          status = emberZclSendClusterOnOffServerCommandOffRequest(
            &destination,
            NULL,
            (EmberZclClusterOnOffServerCommandOffResponseHandler)(&genericMessageResponseHandler));
          if (status != EMBER_SUCCESS) {
            emberAfCorePrintln("Error 0x%x when sending command 0x%x from cluster 0x%x",
                               status,
                               command,
                               cluster);
          } else {
            outboundMessageCount++;
            okToLongPoll = false;
          }
          break;
        case EMBER_ZCL_CLUSTER_ON_OFF_SERVER_COMMAND_ON:
          numSends++;
          status = emberZclSendClusterOnOffServerCommandOnRequest(
            &destination,
            NULL,
            (EmberZclClusterOnOffServerCommandOnResponseHandler)(&genericMessageResponseHandler));
          if (status != EMBER_SUCCESS) {
            emberAfCorePrintln("Error 0x%x when sending command 0x%x from cluster 0x%x",
                               status,
                               command,
                               cluster);
          } else {
            outboundMessageCount++;
            okToLongPoll = false;
          }
          break;
        default:
          emberAfCorePrintln("Unsupported command from OnOff cluster: 0x%x",
                             command);
          break;
        }
        break;

      case EMBER_ZCL_CLUSTER_LEVEL_CONTROL:
        // For Level Control, only the step with on off command is supported
        switch (command) {
        case EMBER_ZCL_CLUSTER_LEVEL_CONTROL_SERVER_COMMAND_MOVE_TO_LEVEL:
          numSends++;
          status = emberZclSendClusterLevelControlServerCommandMoveToLevelRequest(
            &destination,
            (EmberZclClusterLevelControlServerCommandMoveToLevelRequest_t *)request,
            (EmberZclClusterLevelControlServerCommandMoveToLevelResponseHandler)(&genericMessageResponseHandler));
          if (status != EMBER_SUCCESS) {
            emberAfCorePrintln("Error 0x%x when sending command 0x%x",
                               status,
                               command);
          } else {
            outboundMessageCount++;
            okToLongPoll = false;
          }
          break;
        case EMBER_ZCL_CLUSTER_LEVEL_CONTROL_SERVER_COMMAND_STEP:
          numSends++;
          status = emberZclSendClusterLevelControlServerCommandStepRequest(
            &destination,
            (EmberZclClusterLevelControlServerCommandStepRequest_t *)request,
            (EmberZclClusterLevelControlServerCommandStepResponseHandler)(&genericMessageResponseHandler));
          if (status != EMBER_SUCCESS) {
            emberAfCorePrintln("Error 0x%x when sending command 0x%x",
                               status,
                               command);
          } else {
            outboundMessageCount++;
            okToLongPoll = false;
          }
          break;
        case EMBER_ZCL_CLUSTER_LEVEL_CONTROL_SERVER_COMMAND_STEP_WITH_ON_OFF:
          numSends++;
          status = emberZclSendClusterLevelControlServerCommandStepWithOnOffRequest(
            &destination,
            (EmberZclClusterLevelControlServerCommandStepWithOnOffRequest_t *)request,
            (EmberZclClusterLevelControlServerCommandStepWithOnOffResponseHandler)(&genericMessageResponseHandler));
          if (status != EMBER_SUCCESS) {
            emberAfCorePrintln("Error 0x%x when sending command 0x%x",
                               status,
                               command);
          } else {
            outboundMessageCount++;
            okToLongPoll = false;
          }
          break;
        default:
          emberAfCorePrintln("Unsupported command from LevelControl cluster: 0x%x",
                             command);
          break;
        }
        break;

      case EMBER_ZCL_CLUSTER_COLOR_CONTROL:
        // For thermostat, we currently only support the setpoint raise/lower command
        switch (command) {
        case EMBER_ZCL_CLUSTER_COLOR_CONTROL_SERVER_COMMAND_STEP_HUE:
          numSends++;
          status = emberZclSendClusterColorControlServerCommandStepHueRequest(
            &destination,
            (EmberZclClusterColorControlServerCommandStepHueRequest_t *)request,
            (EmberZclClusterColorControlServerCommandStepHueResponseHandler)(&genericMessageResponseHandler));
          if (status != EMBER_SUCCESS) {
            emberAfCorePrintln("Error 0x%x when sending command 0x%x",
                               status,
                               command);
          } else {
            outboundMessageCount++;
            okToLongPoll = false;
          }
          break;
        case EMBER_ZCL_CLUSTER_COLOR_CONTROL_SERVER_COMMAND_MOVE_TO_SATURATION:
          numSends++;
          status = emberZclSendClusterColorControlServerCommandMoveToSaturationRequest(
            &destination,
            (EmberZclClusterColorControlServerCommandMoveToSaturationRequest_t *)request,
            (EmberZclClusterColorControlServerCommandMoveToSaturationResponseHandler)(&genericMessageResponseHandler));
          if (status != EMBER_SUCCESS) {
            emberAfCorePrintln("Error 0x%x when sending command 0x%x",
                               status,
                               command);
          } else {
            outboundMessageCount++;
            okToLongPoll = false;
          }
          break;
        case EMBER_ZCL_CLUSTER_COLOR_CONTROL_SERVER_COMMAND_STEP_COLOR_TEMPERATURE:
          numSends++;
          status = emberZclSendClusterColorControlServerCommandStepColorTemperatureRequest(
            &destination,
            (EmberZclClusterColorControlServerCommandStepColorTemperatureRequest_t *)request,
            (EmberZclClusterColorControlServerCommandStepColorTemperatureResponseHandler)(&genericMessageResponseHandler));
          if (status != EMBER_SUCCESS) {
            emberAfCorePrintln("Error 0x%x when sending command 0x%x",
                               status,
                               command);
          } else {
            outboundMessageCount++;
            okToLongPoll = false;
          }
          break;
        default:
          emberAfCorePrintln("Unsupported command from ColorControl cluster: 0x%x",
                             command);
          break;
        }
        break;

      default:
        emberAfCorePrintln("Unsupported command 0x%x on unsupported cluster 0x%x",
                           command,
                           cluster);
        break;
      }

    //If the binding table doesn't match, do nothing.
    }
  }

  if (numSends == 0) {
    emberAfCorePrintln("Tried to send command 0x%x and cluster 0x%x, but no binds or supported commands found",
                       command,
                       cluster);
  }
}

static void genericMessageResponseHandler(EmberCoapStatus status,
                                          const EmberZclCommandContext_t *context,
                                          void *commandResponse)
{
  if (outboundMessageCount) {
    outboundMessageCount--;
    if (!outboundMessageCount) {
      okToLongPoll = true;
    }
  } else {
    emberAfCorePrintln("ERR! Received more responseHandler calls than cmd/att sends recorded");
  }
}

//------------------------------------------------------------------------------
// Callback triggered when the SB1 gesture plugin receives a new gesture.  This
// will contain the gesture received and the button on which it was received.
// This function will handle all UI based state transitions, and generate radio
// radio traffic based on the user's actions.
void emberAfPluginSb1GestureSensorGestureReceivedCallback(uint8_t gesture,
                                                          uint8_t ui8SwitchNum)
{
  uint8_t blinkAck;

  // Reset the frame timeout on each button press
  emberEventControlSetInactive(frameTimeoutEventControl);
  emberEventControlSetDelayQS(frameTimeoutEventControl, 4*10);

  // Clear the flag tracking whether we need to send a command
  blinkAck = 0;

  // Form the ZigBee command to send based on the state of the device and which
  // button saw which gesture
  switch (ui8SwitchNum) {
  case SB1_GESTURE_SENSOR_SWITCH_TOP:
    switch (gesture) {
    // A touch on the top button maps to an "on" command
    case SB1_GESTURE_SENSOR_GESTURE_TOUCH:
      emberAfCorePrintln("Top button touched");
      sendCommandToBindings(EMBER_ZCL_CLUSTER_ON_OFF,
                            EMBER_ZCL_CLUSTER_ON_OFF_SERVER_COMMAND_ON,
                            NULL);
      blinkAck = 1;
      break;

    // A hold on the top button maps to an "up" command
    case SB1_GESTURE_SENSOR_GESTURE_HOLD:
      emberAfCorePrintln("Top button held");
      sendCommandFromHoldUp();
      blinkAck = 1;
      break;

    // Swiping right on the top or bottom will move the frame to the right.
    // Frame layout is: TEMP - NORMAL - HUE
    case SB1_GESTURE_SENSOR_GESTURE_SWIPE_R:
      // If the token is set to disable swiping, do nothing
      emberAfCorePrintln("Top button right swiped");
      if (SWIPING_ENABLED == 0) {
        emberAfCorePrintln("Swipe disabled by token!");
      } else {
        if (tabState == NORMAL_TAB) {
          //If the token is set to disable the color hue control, do nothing
          if (COLOR_CHANGE_ENABLED == 1) {
            tabState = HUE_TAB;
            emberAfCorePrintln("Switch to HUE");
            halLedBlinkSetActivityLed(BOARDLED0);
            halLedBlinkLedOn(0);
            halLedBlinkSetActivityLed(BOARDLED1);
            halLedBlinkLedOff(0);
          } else {
            emberAfCorePrintln("Color tab disabled by token!");
          }
        } else if (tabState == TEMP_TAB) {
          tabState = NORMAL_TAB;
          halLedBlinkSetActivityLed(BOARDLED0);
          halLedBlinkLedOff(0);
          halLedBlinkSetActivityLed(BOARDLED1);
          halLedBlinkLedOff(0);
          emberAfCorePrintln("Switch to NORMAL");
        }
      }
      // Swipe commands only modify internal state, so no radio message
      // needs to be sent
      blinkAck = 0;
      break;

    // Swiping right on the top or bottom will move the frame to the right.
    // Frame layout is: TEMP - NORMAL - HUE
    case SB1_GESTURE_SENSOR_GESTURE_SWIPE_L:
      // If the token is set to disable swiping, do nothing
      emberAfCorePrintln("Top button left swiped");
      if (SWIPING_ENABLED == 0) {
        emberAfCorePrintln("Swipe disabled by token!");
      } else {
        if (tabState == NORMAL_TAB) {
          tabState = TEMP_TAB;
          emberAfCorePrintln("Switch to TEMP");
          halLedBlinkSetActivityLed(BOARDLED1);
          halLedBlinkLedOn(0);
          halLedBlinkSetActivityLed(BOARDLED0);
          halLedBlinkLedOff(0);
        } else if (tabState == HUE_TAB) {
          tabState = NORMAL_TAB;
          halLedBlinkSetActivityLed(BOARDLED0);
          halLedBlinkLedOff(0);
          halLedBlinkSetActivityLed(BOARDLED1);
          halLedBlinkLedOff(0);
          emberAfCorePrintln("Switch to NORMAL");
        }
      }
      blinkAck = 0;
      break;

    // If we got here, we likely had a bad i2c transaction.  Ignore read data
    default:
      emberAfCorePrintln("bad gesture: 0x%02x", gesture);
      blinkAck = 0;
      return;
    }
    break;

  case SB1_GESTURE_SENSOR_SWITCH_BOTTOM:
    switch (gesture) {
    // A touch on the bottom button maps to an "off" command
    case SB1_GESTURE_SENSOR_GESTURE_TOUCH:
      emberAfCorePrintln("Bottom button touched");
      sendCommandToBindings(EMBER_ZCL_CLUSTER_ON_OFF,
                            EMBER_ZCL_CLUSTER_ON_OFF_SERVER_COMMAND_OFF,
                            NULL);
      blinkAck = 1;
      break;

    // A hold on the bottom button maps to a "level down" command
    case SB1_GESTURE_SENSOR_GESTURE_HOLD:
      emberAfCorePrintln("Bottom button held");
      sendCommandFromHoldDown();
      blinkAck = 1;
      break;

    // Swiping right on the top or bottom will move the frame to the right.
    // Frame layout is: TEMP - NORMAL - HUE
    case SB1_GESTURE_SENSOR_GESTURE_SWIPE_R:
      // If the token is set to disable swiping, do nothing
      emberAfCorePrintln("ZCL SW_R\r\n");
      if (SWIPING_ENABLED == 0) {
        emberAfCorePrintln("Swipe disabled by token!");
      } else {
        if (tabState == NORMAL_TAB) {
          //If the token is set to disable the color hue control, do nothing
          if (COLOR_CHANGE_ENABLED == 1) {
            tabState = HUE_TAB;
            emberAfCorePrintln("Switch to HUE");
            halLedBlinkSetActivityLed(BOARDLED0);
            halLedBlinkLedOn(0);
            halLedBlinkSetActivityLed(BOARDLED1);
            halLedBlinkLedOff(0);
          } else {
            emberAfCorePrintln("Color tab disabled by token!");
          }
        } else if (tabState == TEMP_TAB) {
          tabState = NORMAL_TAB;
          halLedBlinkSetActivityLed(BOARDLED0);
          halLedBlinkLedOff(0);
          halLedBlinkSetActivityLed(BOARDLED1);
          halLedBlinkLedOff(0);
          emberAfCorePrintln("Switch to NORMAL");
        }
      }
      // Swipe commands only modify internal state, so no radio message
      // needs to be sent
      blinkAck = 0;
      break;

    // Swiping right on the top or bottom will move the frame to the right.
    // Frame layout is: TEMP - NORMAL - HUE
    case SB1_GESTURE_SENSOR_GESTURE_SWIPE_L:
      // If the token is set to disable swiping, do nothing
      emberAfCorePrintln("ZCL SW_L \r\n");
      if (SWIPING_ENABLED == 0) {
        emberAfCorePrintln("Swipe disabled by token!");
      } else {
        if (tabState == NORMAL_TAB) {
          tabState = TEMP_TAB;
          emberAfCorePrintln("Switch to TEMP");
          halLedBlinkSetActivityLed(BOARDLED1);
          halLedBlinkLedOn(0);
          halLedBlinkSetActivityLed(BOARDLED0);
          halLedBlinkLedOff(0);
        } else if (tabState == HUE_TAB) {
          tabState = NORMAL_TAB;
          halLedBlinkSetActivityLed(BOARDLED0);
          halLedBlinkLedOff(0);
          halLedBlinkSetActivityLed(BOARDLED1);
          halLedBlinkLedOff(0);
          emberAfCorePrintln("Switch to NORMAL");
        }
      }
      blinkAck = 0;
      break;

      // If we got here, we likely had a bad i2c transaction.  Ignore read data
      default:
        emberAfCorePrintln("bad gesture: 0x%02x", gesture);
        blinkAck = 0;
        return;
    }
    break;

  // If we got here, we likely had a bad i2c transaction.  Ignore read data
  default:
    emberAfCorePrintln("unknown button: 0x%02x\r\n", ui8SwitchNum);
    blinkAck = 0;
    return;
  }

  // blink the LED to acknowledge a gesture was received
  if (blinkAck) {
    //blink the LED to show that a gesture was recognized
    halLedBlinkBlink( 1, 100);
  }
}

void sendCommandFromHoldUp(void)
{
  EmberZclClusterLevelControlServerCommandStepWithOnOffRequest_t levelRequest;
  EmberZclClusterColorControlServerCommandStepHueRequest_t colorHueRequest;
  EmberZclClusterColorControlServerCommandStepColorTemperatureRequest_t colorTempRequest;

  switch (tabState) {
  case NORMAL_TAB:
    emberAfCorePrintln("DIM UP");
    levelRequest.stepMode = LEVEL_STEP_POSITIVE;
    levelRequest.stepSize = LEVEL_STEP_AMOUNT;
    levelRequest.transitionTime = LEVEL_STEP_TIME;
    sendCommandToBindings(EMBER_ZCL_CLUSTER_LEVEL_CONTROL,
                          EMBER_ZCL_CLUSTER_LEVEL_CONTROL_SERVER_COMMAND_STEP_WITH_ON_OFF,
                          (void*)(&levelRequest));
    break;
  case TEMP_TAB:
    emberAfCorePrintln("TEMP UP");
    colorTempRequest.stepMode = COLOR_TEMP_STEP_POSITIVE;
    colorTempRequest.stepSize = COLOR_TEMP_STEP_AMOUNT;
    colorTempRequest.transitionTime = 0;
    colorTempRequest.colorTemperatureMinimum = 0;
    colorTempRequest.colorTemperatureMaximum = 0xFFFF;
    sendCommandToBindings(EMBER_ZCL_CLUSTER_COLOR_CONTROL,
                          EMBER_ZCL_CLUSTER_COLOR_CONTROL_SERVER_COMMAND_STEP_COLOR_TEMPERATURE,
                          (void*)(&colorTempRequest));
    break;
  case HUE_TAB:
    emberAfCorePrintln("HUE UP");

    colorHueRequest.stepMode = COLOR_HUE_STEP_POSITIVE;
    colorHueRequest.stepSize = COLOR_HUE_STEP_AMOUNT;
    colorHueRequest.transitionTime = 0;
    sendCommandToBindings(EMBER_ZCL_CLUSTER_COLOR_CONTROL,
                          EMBER_ZCL_CLUSTER_COLOR_CONTROL_SERVER_COMMAND_STEP_HUE,
                          (void*)(&colorHueRequest));
    break;
  }
}

void sendCommandFromHoldDown(void)
{
  EmberZclClusterLevelControlServerCommandStepWithOnOffRequest_t levelRequest;
  EmberZclClusterColorControlServerCommandStepHueRequest_t colorHueRequest;
  EmberZclClusterColorControlServerCommandStepColorTemperatureRequest_t colorTempRequest;

  switch (tabState) {
  case NORMAL_TAB:
    emberAfCorePrintln("DIM DOWN");
    levelRequest.stepMode = LEVEL_STEP_NEGATIVE;
    levelRequest.stepSize = LEVEL_STEP_AMOUNT;
    levelRequest.transitionTime = LEVEL_STEP_TIME;
    sendCommandToBindings(EMBER_ZCL_CLUSTER_LEVEL_CONTROL,
                          EMBER_ZCL_CLUSTER_LEVEL_CONTROL_SERVER_COMMAND_STEP_WITH_ON_OFF,
                          (void*)(&levelRequest));
    break;
  case TEMP_TAB:
    emberAfCorePrintln("TEMP DOWN");
    colorTempRequest.stepMode = COLOR_TEMP_STEP_NEGATIVE;
    colorTempRequest.stepSize = COLOR_TEMP_STEP_AMOUNT;
    colorTempRequest.transitionTime = 0;
    colorTempRequest.colorTemperatureMinimum = 0;
    colorTempRequest.colorTemperatureMaximum = 0xFFFF;
    sendCommandToBindings(EMBER_ZCL_CLUSTER_COLOR_CONTROL,
                          EMBER_ZCL_CLUSTER_COLOR_CONTROL_SERVER_COMMAND_STEP_COLOR_TEMPERATURE,
                          (void*)(&colorTempRequest));
    break;
  case HUE_TAB:
    emberAfCorePrintln("HUE DOWN");
    colorHueRequest.stepMode = COLOR_HUE_STEP_NEGATIVE;
    colorHueRequest.stepSize = COLOR_HUE_STEP_AMOUNT;
    colorHueRequest.transitionTime = 0;
    sendCommandToBindings(EMBER_ZCL_CLUSTER_COLOR_CONTROL,
                          EMBER_ZCL_CLUSTER_COLOR_CONTROL_SERVER_COMMAND_STEP_HUE,
                          (void*)(&colorHueRequest));
    break;
  }
}

//------------------------------------------------------------------------------
// FrameTimeout event handler
// This handler is called a long time (default 10 seconds) after the user
// swipes left or right.  It will return the switch to the normal on/off tab.
void frameTimeoutEventHandler( void )
{
  //Make sure we don't stay on the HUE/TEMP tab for too long
  if ((tabState == HUE_TAB) || (tabState == TEMP_TAB)) {
    emberAfCorePrintln("Tab timeout, back to normal\n");
    tabState = NORMAL_TAB;
    halLedBlinkSetActivityLed(BOARDLED0);
    halLedBlinkLedOff(0);
    halLedBlinkSetActivityLed(BOARDLED1);
    halLedBlinkLedOff(0);
  }
  emberEventControlSetInactive(frameTimeoutEventControl);
}

//------------------------------------------------------------------------------
// Main Tick
// Whenever main application tick is called, this callback will be called at the
// end of the main tick execution.  It ensure that no LED activity will occur
// when the LED_ENABLED macro is set to disabled mode.
void emberAfTickCallback(void)
{
  //Use blinking led to indicate which tab is active, unless disabled by tokens
  if (LED_ENABLED == 0) {
    halLedBlinkSetActivityLed(BOARDLED0);
    halLedBlinkLedOff(0);
    halLedBlinkSetActivityLed(BOARDLED1);
    halLedBlinkLedOff(0);
  }
}

//------------------------------------------------------------------------------
// Ok To Sleep
//
// This function is called by the Idle/Sleep plugin before sleeping.  It is
// called with interrupts disabled.  The application should return true if the
// device may sleep or false otherwise.
//
// param durationMs The maximum duration in milliseconds that the device will
// sleep.  Ver.: always
bool emberAfPluginIdleSleepOkToSleepCallback(uint32_t durationMs)
{
  if (halSb1GestureSensorCheckForMsg()) {
    return false;
  } else {
    return true;
  }
}

//------------------------------------------------------------------------------
// Ok To Idle
//
// This function is called by the Idle/Sleep plugin before idling.  It is called
// with interrupts disabled.  The application should return true if the device
// may idle or false otherwise.
bool emberAfPluginIdleSleepOkToIdleCallback(void)
{
  if (halSb1GestureSensorCheckForMsg()) {
    return false;
  } else {
    return true;
  }
}
