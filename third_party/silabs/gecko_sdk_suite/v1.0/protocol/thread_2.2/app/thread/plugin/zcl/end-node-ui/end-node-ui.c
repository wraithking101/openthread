// Copyright 2017 Silicon Laboratories, Inc.

#include PLATFORM_HEADER
#include CONFIGURATION_HEADER
#include EMBER_AF_API_HAL
#include EMBER_AF_API_LED_BLINK
#include EMBER_AF_API_STACK
#include EMBER_AF_API_ZCL_CORE
#ifdef EMBER_AF_API_DEBUG_PRINT
  #include EMBER_AF_API_DEBUG_PRINT
#endif

#define DEBOUNCE_TIME_MS \
          EMBER_AF_PLUGIN_END_NODE_UI_BUTTON_DEBOUNCE_TIME_MS
#define BUTTON_NETWORK_LEAVE_TIME_MS  (1 * MILLISECOND_TICKS_PER_SECOND)

// These are default values to modify the UI LED's blink pattern for network
// join and network leave.
#define LED_LOST_ON_TIME_MS          250
#define LED_LOST_OFF_TIME_MS         750
#define LED_BLINK_ON_TIME_MS         200
#define LED_FOUND_BLINK_OFF_TIME_MS  250
#define LED_FOUND_BLINK_ON_TIME_MS   250
#define LED_IDENTIFY_ON_TIME_MS      250
#define LED_IDENTIFY_OFF1_TIME_MS    250
#define LED_IDENTIFY_OFF2_TIME_MS    1250
#define DEFAULT_NUM_IDENTIFY_BLINKS  100
#define EZ_BIND_FOUND_ON_TIME_MS     750
#define EZ_BIND_FOUND_OFF_TIME_MS    250
#define EZ_BIND_FOUND_NUM_BLINKS     4

#define LED_SEARCH_BLINK_OFF_TIME_MS 1800
#define DEFAULT_NUM_SEARCH_BLINKS    100
#define DEFAULT_EZ_MODE_BLINKS       1
#define DEFAULT_NUM_FOUND_BLINKS     6
#define DEFAULT_NUM_LEAVE_BLINKS     3

#define EZ_MODE_BLINK_REPEAT_TIME_MS  (4 * MILLISECOND_TICKS_PER_SECOND)

static void resetBindingsAndAttributes(void);
static void enableIdentify(void);
static void leaveNetwork(void);
static size_t getNumberOfUsedBinds(void);

EmberEventControl emEndNodeUiEzModeControl;
EmberEventControl emEndNodeUiEzBlinkControl;
EmberEventControl emEndNodeUiButtonPressCountEventControl;

// State variables for controlling LED blink behavior on network join/leave
static uint16_t networkLostBlinkPattern[] =
  { LED_LOST_ON_TIME_MS, LED_LOST_OFF_TIME_MS };
static uint16_t ezModeSearchingBlinkPattern[] =
  { LED_BLINK_ON_TIME_MS, LED_SEARCH_BLINK_OFF_TIME_MS,
    LED_BLINK_ON_TIME_MS, LED_SEARCH_BLINK_OFF_TIME_MS,
    LED_BLINK_ON_TIME_MS, LED_SEARCH_BLINK_OFF_TIME_MS };
static uint16_t networkSearchBlinkPattern[] =
  { LED_BLINK_ON_TIME_MS, LED_SEARCH_BLINK_OFF_TIME_MS };
static uint16_t networkFoundBlinkPattern[] =
  { LED_FOUND_BLINK_ON_TIME_MS, LED_FOUND_BLINK_OFF_TIME_MS };
static uint16_t ezModeBindCreatedBlinkPattern[] =
  { EZ_BIND_FOUND_ON_TIME_MS, EZ_BIND_FOUND_OFF_TIME_MS};
static uint16_t networkIdentifyBlinkPattern[] =
  { LED_IDENTIFY_ON_TIME_MS, LED_IDENTIFY_OFF1_TIME_MS,
    LED_IDENTIFY_ON_TIME_MS, LED_IDENTIFY_OFF2_TIME_MS };

static uint8_t consecutiveButtonPressCount = 0;
static size_t numBindingsStartEzMode = 0;

void emberAfPluginButtonInterfaceButton0PressedShortCallback(uint16_t timePressedMs)
{
  // If the button was not held for longer than the debounce time, ignore the
  // press.
  if (timePressedMs < DEBOUNCE_TIME_MS) {
    return;
  }

  if (timePressedMs >= BUTTON_NETWORK_LEAVE_TIME_MS) {
    leaveNetwork();
  } else {
    consecutiveButtonPressCount++;
    emberEventControlSetDelayMS(emEndNodeUiButtonPressCountEventControl,
                                EMBER_AF_PLUGIN_END_NODE_UI_CONSECUTIVE_PRESS_TIMEOUT_MS);
  }
}

void emEndNodeUiButtonPressCountEventHandler(void)
{
  emberEventControlSetInactive(emEndNodeUiButtonPressCountEventControl);

  if (emberNetworkStatus() == EMBER_JOINED_NETWORK_ATTACHED) {
    // If on a network:
    // 1 press   starts EZ Mode commissioning
    // 2 presses activates identify
    // 3 presses blinks network status
    // 4 presses resets the bindings and attributes
    // 5 presses sends an identify to anyone the device is controlling (not yet
    //   supported)
    switch (consecutiveButtonPressCount) {
    case 1:
      emberEventControlSetActive(emEndNodeUiEzModeControl);
    case 2:
      enableIdentify();
      break;
    case 3:
      emberAfAppPrintln("Blinking user requested network status");
      halLedBlinkPattern(DEFAULT_NUM_FOUND_BLINKS,
                         COUNTOF(networkFoundBlinkPattern),
                         networkFoundBlinkPattern);
      break;
    case 4:
      resetBindingsAndAttributes();
    default:
      break;
    }
  } else {
    halLedBlinkPattern(DEFAULT_NUM_LEAVE_BLINKS,
                       COUNTOF(networkLostBlinkPattern),
                       networkLostBlinkPattern);
  }

  consecutiveButtonPressCount = 0;
}

void emEndNodeUiNetworkStatusHandler(EmberNetworkStatus newNetworkStatus,
                                     EmberNetworkStatus oldNetworkStatus,
                                     EmberJoinFailureReason reason)
{
  static bool blinkingSearchPattern = false;

  //  Eventually, something like the connection manager in ZigBee can be used
  //  here so that network reconnect attempt timing, behavior on disconnect,
  //  etc. can be handled here.  For now, just blink LEDs to show when
  //  different network activity is taking place.
  switch (newNetworkStatus) {
  case EMBER_NO_NETWORK:
  case EMBER_JOINED_NETWORK_ATTACHING:
    // If we used to be attached, blink the "lost network" pattern.  Otherwise,
    // the device is just mid-search, so continue blinking the search pattern
    if (oldNetworkStatus == EMBER_JOINED_NETWORK_ATTACHED) {
      blinkingSearchPattern = false;
      halLedBlinkPattern(DEFAULT_NUM_LEAVE_BLINKS,
                         COUNTOF(networkLostBlinkPattern),
                         networkLostBlinkPattern);
    }
    break;
  case EMBER_JOINING_NETWORK:
  case EMBER_JOINED_NETWORK_NO_PARENT:
    // If no parent is found or the device is mid joining, blink the search
    // pattern (unless it is already being blinked)
    if (!blinkingSearchPattern) {
      blinkingSearchPattern = true;
      halLedBlinkPattern(DEFAULT_NUM_SEARCH_BLINKS,
                         COUNTOF(networkSearchBlinkPattern),
                         networkSearchBlinkPattern);
    }
    break;
  case EMBER_JOINED_NETWORK_ATTACHED:
    blinkingSearchPattern = false;
    halLedBlinkPattern(DEFAULT_NUM_FOUND_BLINKS,
                       COUNTOF(networkFoundBlinkPattern),
                       networkFoundBlinkPattern);
    break;
  default:
    break;
  }
}

// Before starting EZ Mode, the system will determine how many binds are
// present in the binding table (as there is no callback generated when a bind
// is created).  A call to emberZclStartEzMode will then initiate EZ mode
// operation, and the device will start to blink the EZ Mode search pattern. The
// ezModeBlink event will then be used to poll the number of binds created
// every few seconds to see if any new entries have been generated.
void emEndNodeUiEzModeHandler(void)
{
  EmberStatus status;

  emberEventControlSetInactive(emEndNodeUiEzModeControl);

  status = emberZclStartEzMode();

  if (status == EMBER_SUCCESS) {
    emberAfCorePrintln("starting ez mode");
    numBindingsStartEzMode = getNumberOfUsedBinds();

    halLedBlinkPattern(DEFAULT_EZ_MODE_BLINKS,
                       COUNTOF(ezModeSearchingBlinkPattern),
                       ezModeSearchingBlinkPattern);

    emberEventControlSetDelayMS(emEndNodeUiEzBlinkControl,
                                EZ_MODE_BLINK_REPEAT_TIME_MS);
  } else {
    emberAfCorePrintln("Unable to start EZ mode: %d", status);
  }
}

void emEndNodeUiEzBlinkHandler(void)
{
  size_t numBindings;
  bool blinkForNewBind = false;

  numBindings = getNumberOfUsedBinds();

  if (numBindings != numBindingsStartEzMode) {
    emberAfCorePrintln("%d new bindings created",
                       numBindings - numBindingsStartEzMode);
    blinkForNewBind = true;

    numBindingsStartEzMode = numBindings;
    emberEventControlSetDelayMS(emEndNodeUiEzBlinkControl,
                                EZ_MODE_BLINK_REPEAT_TIME_MS);
  }

  // If the network went down mid ez-mode, the LED will be blinking some type of
  // search pattern, so stop blinking the EZ pattern
  if (emberNetworkStatus() != EMBER_JOINED_NETWORK_ATTACHED) {
    emberEventControlSetInactive(emEndNodeUiEzBlinkControl);
  } else if (emberZclEzModeIsActive()) {
    if (blinkForNewBind) {
      halLedBlinkPattern(EZ_BIND_FOUND_NUM_BLINKS,
                         COUNTOF(ezModeBindCreatedBlinkPattern),
                         ezModeBindCreatedBlinkPattern);
    } else {
      halLedBlinkPattern(DEFAULT_EZ_MODE_BLINKS,
                         COUNTOF(ezModeSearchingBlinkPattern),
                         ezModeSearchingBlinkPattern);
    }
    emberEventControlSetDelayMS(emEndNodeUiEzBlinkControl,
                                EZ_MODE_BLINK_REPEAT_TIME_MS);
  } else {
    halLedBlinkLedOff(0);
    emberEventControlSetInactive(emEndNodeUiEzBlinkControl);
  }
}

static size_t getNumberOfUsedBinds(void)
{
  EmberZclBindingId_t numberOfBinds = 0;
  EmberZclBindingId_t currentBindIndex;
  EmberZclBindingEntry_t currentBind;

  for (currentBindIndex = 0;
       currentBindIndex < EMBER_ZCL_BINDING_TABLE_SIZE;
       currentBindIndex++) {
    // Check to see if the binding table entry is active
    if (emberZclGetBinding(currentBindIndex, &currentBind)) {
      numberOfBinds++;
    }
  }
  return numberOfBinds;
}

static void resetBindingsAndAttributes(void)
{
  size_t i;
  EmberZclEndpointId_t currentEndpoint;

  emberAfCorePrintln("Clearing binding table and resetting all attributes\n");
  emberZclRemoveAllBindings();

  for (i = 0; i < emZclEndpointCount; i++) {
    currentEndpoint = emberZclEndpointIndexToId(i, NULL);
    emberZclResetAttributes(currentEndpoint);
  }
}

static void enableIdentify(void)
{
  // Identify via button press is not yet supported
  return;
}

void emberResetNetworkStateReturn(EmberStatus status)
{
  if (status != EMBER_SUCCESS) {
    emberAfCorePrintln("Error when trying to reset the network: 0x%x", status);
  } else {
    emberAfCorePrintln("Network reset");
    halLedBlinkPattern(DEFAULT_NUM_LEAVE_BLINKS,
                       COUNTOF(networkLostBlinkPattern),
                       networkLostBlinkPattern);
  }
}

static void leaveNetwork(void)
{
  // No need to blink here, as the blink will be performed in the network
  // status handler
  emberAfCorePrintln("Resetting the network connection");
  emberResetNetworkState();
}

void emberZclIdentifyServerStartIdentifyingCallback(uint16_t identifyTimeS)
{
  emberAfCorePrintln("Identifying...");
  halLedBlinkPattern(DEFAULT_NUM_IDENTIFY_BLINKS,
                     COUNTOF(networkIdentifyBlinkPattern),
                     networkIdentifyBlinkPattern);
}

void emberZclIdentifyServerStopIdentifyingCallback(void)
{
  emberAfCorePrintln("Identify complete");
  halLedBlinkLedOff(0);
}
