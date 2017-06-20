// Copyright 2017 Silicon Laboratories, Inc.

#include PLATFORM_HEADER
#include CONFIGURATION_HEADER
#include EMBER_AF_API_STACK
#include EMBER_AF_API_HAL
#include EMBER_AF_API_COMMAND_INTERPRETER2
#ifdef EMBER_AF_API_DEBUG_PRINT
  #include EMBER_AF_API_DEBUG_PRINT
#endif

// Required for 'getline'
#define _GNU_SOURCE

#include <arpa/inet.h>
#include <ctype.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <linux/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define ALIAS(x) x##Alias

// Preprocessor convert var to "var"
#define STRINGIFY(var) #var
// Preprocessor get number of digits in var minus null
#define LENGTHOF(var) (sizeof(STRINGIFY(var)) - 1)

#include "app/thread/plugin/address-configuration-debug/address-configuration-debug.c"

// WARNING: By default, this sample application uses the well-know sensor/sink
// network key as the master key.  This is done for demonstration purposes, so
// that packets will decrypt automatically in Ember Desktop.  Using predefined
// keys is a significant security risk.  Real devices MUST NOT used fixed keys
// and MUST use random keys instead.  The stack automatically generates random
// keys if emberSetSecurityParameters is not called prior to forming.
//#define USE_RANDOM_MASTER_KEY

#if defined(USE_RANDOM_MASTER_KEY)
  #define SET_SECURITY_PARAMETERS_OR_FORM_NETWORK FORM_NETWORK
#else
  #define SET_SECURITY_PARAMETERS_OR_FORM_NETWORK SET_SECURITY_PARAMETERS
#endif

static EmberKeyData masterKey = {
  {0x65, 0x6D, 0x62, 0x65, 0x72, 0x20, 0x45, 0x4D,
    0x32, 0x35, 0x30, 0x20, 0x63, 0x68, 0x69, 0x70,}
};

// This definition is used when a Thread network should be joined out of band.
// If useCommissioner is false a precommissioned network will be used
// instead of commissioning. Use this feature with a sensor-actuator-node
// application that also has this defined and uses the same settings specified
// below. This value will change upon reading the USE_COMMISSIONER value in the
// border-router.conf. Set USE_COMMISSIONER to a 1 to use the official
// commissioning app.
static bool useCommissioner = false;

// WARNING:  Using a static fixed join-key will result in a significant 
// security vulnerability.  The code below should only be utilized
// to facilitate debug, and should not be provided in shipping products
//#define USE_FIXED_JOIN_KEY
#if defined(USE_FIXED_JOIN_KEY)
#define DEFAULT_FIXED_JOIN_KEY "ABCD1234"
#endif

#define DEFAULT_GLOBAL_NETWORK_PREFIX {0x20, 0x01, 0x0D, 0xB8, 0x03, 0x85, 0x93, 0x18}
#define DEFAULT_GLOBAL_NETWORK_PREFIX_WIDTH 64
#define DEFAULT_ULA_PREFIX {0xfd, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
#define DEFAULT_CHANNEL 19
#define DEFAULT_FALLBACK_CHANNEL_MASK 0
#define DEFAULT_PAN_ID 0x1075
#define DEFAULT_EXTENDED_PAN_ID {0xc6, 0xef, 0xe1, 0xb4, 0x5f, 0xc7, 0x8e, 0x4f}
#define DEFAULT_KEY_SEQUENCE 0

// The network interface that this border router manages
//TODO: read the mgmt iface from border-router.conf
static char borderRouterMgmtIfaceName[IFNAMSIZ] = "tun0";

// The default networkId for this mesh network
// WARNING: networkId is 16 + 1 bytes wide; Do not overflow!
#define DEFAULT_NETWORK_ID "border-router"
static uint8_t networkId[EMBER_NETWORK_ID_SIZE + 1] = DEFAULT_NETWORK_ID;
static const uint8_t commissionerId[] = "border-router";

static uint8_t preferredChannel = DEFAULT_CHANNEL; 
static uint16_t defaultPanId = DEFAULT_PAN_ID; 

// Network management data for global address
// This /64 prefix will be handed to the mesh to assign ipv6 addresses from
//
// WARNING: the 2001:DB8::/32 prefix is specified as the 'Documentation Only'
// prefix and is not suitable for globally-routed traffic use a different
// prefix for a production device! Border router is using
// 2001:0db8:0385:9318::/64 as a default mesh subnet.
static EmberIpv6Address globalNetworkPrefix = {
  DEFAULT_GLOBAL_NETWORK_PREFIX,
};

// The default prefix above is 64 bits wide
static uint8_t globalNetworkPrefixBitWidth = DEFAULT_GLOBAL_NETWORK_PREFIX_WIDTH;

// MAX_PREFIX in IPv6 is 128 bits
static const uint8_t ulaPrefix[EMBER_IPV6_BYTES] = DEFAULT_ULA_PREFIX;

static uint32_t fallbackChannelMask = DEFAULT_FALLBACK_CHANNEL_MASK;
static uint8_t extendedPanId[EXTENDED_PAN_ID_SIZE + 1] = DEFAULT_EXTENDED_PAN_ID;
static uint32_t keySequence = DEFAULT_KEY_SEQUENCE;

// Adds a fixed key that any joiner can use to join the network
#if defined(USE_FIXED_JOIN_KEY)
static uint8_t fixedJoinKey[EMBER_JOIN_KEY_MAX_SIZE + 1] = DEFAULT_FIXED_JOIN_KEY;
#endif

// An optional configuration file that will be parsed to get default network
// settings
#define CONFIGURATION_FILE "/etc/siliconlabs/border-router.conf"
static FILE *inputFp = NULL; 

// Maximum width in chars of any conf file entry
#define MAX_CONF_KEY_LENGTH 32
// Maximum width in chars of any conf file line
#define MAX_CONF_LINE_LENGTH (MAX_CONF_KEY_LENGTH + 128)
// Maximum format width of any confile entry
#define MAX_CONF_FORMAT_LENGTH 32

// The version number of the configuration file
static uint8_t confFileVersion = 0;

// An enumeration to identify valid keys in the conf file. When support for new
// conf is added, update this enum and the corresponding 'ConfKey' below
typedef enum ConfKeyEnum {
  FILE_VERSION_KEY = 0,
  USE_COMMISSIONER_KEY,
  NETWORK_ID_KEY,
  IPV6_SUBNET_KEY,
  DEFAULT_CHANNEL_KEY,
  PAN_ID_KEY,
  EX_PAN_ID_KEY,
#if defined(USE_FIXED_JOIN_KEY)
  FIXED_JOIN_KEY, // the conf file can optionally read in a custom fixed join key
#endif

  // WARNING: this must always appear at the end of the key enum. When new keys
  // are added, you must update validConfEntries, and
  // all switch statements acting on ConfKey
  MAX_CONF_KEY,
  UNKNOWN_KEY,
} ConfKey;

typedef struct ConfEntryStruct {
  char key[MAX_CONF_KEY_LENGTH];
  char format[MAX_CONF_FORMAT_LENGTH];
  uint8_t sizeInBytes;
  void *addr;
} ConfEntry;

static  ConfEntry validConfEntries[MAX_CONF_KEY] = {
  { "FILE_VERSION", " %2d", sizeof(confFileVersion), &confFileVersion },
  { "USE_COMMISSIONER", "%d", sizeof(useCommissioner), &useCommissioner},
  { "NETWORK_ID", " %%%ds", EMBER_NETWORK_ID_SIZE, networkId },
  { "MESH_SUBNET", " %s", sizeof(globalNetworkPrefix.bytes), globalNetworkPrefix.bytes },
  { "DEFAULT_CHANNEL", " %2d", sizeof(preferredChannel), &preferredChannel },
  { "PAN_ID", " %2d", sizeof(defaultPanId), &defaultPanId },
  { "EXTENDED_PAN_ID", "%%%ds", EXTENDED_PAN_ID_SIZE, extendedPanId },
#if defined(USE_FIXED_JOIN_KEY)
  { "FIXED_JOIN_KEY", "%s", EMBER_JOIN_KEY_MAX_SIZE, fixedJoinKey },
#endif
};

static ConfKey identifyEntry(char *confFileLine, ConfEntry *validEntries);
static void trimWhitespace(char* inString, size_t inStringLength);
static uint32_t parseConfigurationFile(FILE *confFp);
static int hexByteParser(const char const *inString, ConfEntry *entry);
static int simpleParser(const char const *inString, ConfEntry *entry);
static int ipv6PrefixParser(const char const *inString, ConfEntry *entry);

static void networkStateTransition(EmberNetworkStatus newNetworkStatus,
                                   EmberNetworkStatus oldNetworkStatus,
                                   EmberJoinFailureReason reason);
static void resumeNetwork(void);
#if !defined(USE_RANDOM_MASTER_KEY)
  static void setSecurityParameters(void);
#endif
static void formNetwork(void);
static void getCommissioner(void);
static void formNetworkCommissionedCompletion(void);
static EmberStatus coapListen(const EmberIpv6Address*);
static void configureListeners(void);
static void discover(void);
static void resetNetworkState(void);

// local cache to track the IP addresses the stack has assigned to the host
static EmberIpv6Address hostAddressTable[EMBER_MAX_IPV6_GLOBAL_ADDRESS_COUNT] = {0};
static uint8_t hostAddressTableEntries = 0;

typedef enum GatewayStatusEnum {
  GATEWAY_STATUS_UNKNOWN = 0,
  GATEWAY_STATUS_PENDING,
  GATEWAY_CONFIGURED,
  GATEWAY_UNCONFIGURED
} GatewayStatus;

static GatewayStatus borderRouterGatewayStatus = GATEWAY_STATUS_UNKNOWN;

static bool borderRouterIsGateway = false;
static bool borderRouterIsInitialized = false;

static void clearHostAddressTable(void) {
  borderRouterIsGateway = false;
  MEMSET(hostAddressTable, 0, sizeof(hostAddressTable));
  hostAddressTableEntries = 0;
}

static void resetBorderRouterStatus(void) {
  borderRouterIsGateway = false;
  borderRouterIsInitialized = false;
  borderRouterGatewayStatus = GATEWAY_STATUS_UNKNOWN;
  clearHostAddressTable();
}

// A second coap port for the web GUI (used for discover) 
#define MGMT_COAP_PORT 4983

static void deviceAnnounceResponseHandler(EmberCoapStatus status,
                                          EmberCoapCode code,
                                          EmberCoapReadOptions *options,
                                          uint8_t *payload,
                                          uint16_t payloadLength,
                                          EmberCoapResponseInfo *info);

#define IDLE_PERIOD_TIMEOUT_MS (60 * MILLISECOND_TICKS_PER_SECOND)
#define INIT_PERIOD_TIMEOUT_MS (5 * MILLISECOND_TICKS_PER_SECOND)

// The All Thread Nodes multicast address is filled in once the ULA prefix is
// known.  It is an RFC3306 address with the format ff33:40:<ula prefix>::1.
static EmberIpv6Address allThreadNodes = {
  {0xFF, 0x33, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,}
};

#define RFC3306_NETWORK_PREFIX_OFFSET 4

static const EmberIpv6Address allMeshNodes = {
  {0xFF, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,}
};

static const EmberIpv6Address allMeshRouters = {
  {0xFF, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,}
};

// The border router assumes it will be running on a Raspberry Pi with a fixed
// wlan0 IPv6 address of 2001:db8:8569:b2b1::1, this should be obtained
// automatically in the future, or use localhost
static const EmberIpv6Address serverAddress = {
  {0x20, 0x01, 0x0D, 0xB8, 0x85, 0x69, 0xB2, 0xB1,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,}
};

static const uint8_t serverDiscoverUri[] = "server/discover";
static const uint8_t borderRouterDiscoverUri[] = "borderrouter/discover";
static const uint8_t nodeJoinedUri[] = "borderrouter/nodejoined";
static const uint8_t nodeDimOutUri[] = "device/dimout";
static const uint8_t nodeOnOffOutUri[] = "device/onoffout";
static const uint8_t onPayload[] = { '1' };
static const uint8_t offPayload[] = { '0' };

// This commissionerKey is used by the Commissioner to establish trust with
// this border router.
// WARNING: Using a static password across all devices is unsafe for
// productized releases, use pseudorandom paswords
static const uint8_t commissionerKey[] = "COMMPW1234";

static const uint8_t borderRouterFlags = 0x32;
static bool stackInitialized = false;

enum {
  INITIAL                       = 0,
  RESUME_NETWORK                = 1,
  SET_SECURITY_PARAMETERS       = 2,
  FORM_NETWORK                  = 3,
  FORM_NETWORK_COMPLETION       = 4,
  GET_COMMISSIONER              = 5,
  CONFIGURE_BORDER_ROUTER       = 6,
  IDLE_STATE                    = 7,
  DISCOVER                      = 8,
  RESET_NETWORK_STATE           = 9,  
};

static uint8_t state = INITIAL;
EmberEventControl stateEventControl;
static void setNextStateWithDelay(uint8_t nextState, uint32_t delayMs);
#define setNextState(nextState)       setNextStateWithDelay((nextState), 0)
#define repeatStateWithDelay(delayMs) setNextStateWithDelay(state, (delayMs))

static const uint8_t *printableNetworkId(void);

static EmberNetworkStatus previousNetworkStatus = EMBER_NO_NETWORK;
static EmberNetworkStatus networkStatus = EMBER_NO_NETWORK;
static EmberJoinFailureReason joinFailureReason = EMBER_JOIN_FAILURE_REASON_NONE;

// All asynchronous callbacks except emberAfnetworkStateTransition that will 
// change 'state' must first ensure that an ongoing reset isn't occuring 
// before proceeding. This prevents a race condition where callbacks that 
// assert( state == X) can be entered after having the state transitioned 
// by a network reset.
//
// Because state == RESET_NETWORK_STATE can only be exited by the stack when
// emberAfNetworkStateTransition is called, it is the only asynchronous
// callback allowed to change state during reset
#define returnIfInNetworkReset(state) \
  do{ if ((state) == RESET_NETWORK_STATE) return; } while(0)

void loadBorderRouterConfiguration(void)
{
  static bool printedNoConfFileOnce = false;
  inputFp = fopen(CONFIGURATION_FILE, "r");
  if (inputFp != NULL) {
    parseConfigurationFile(inputFp);
    fclose(inputFp);
    inputFp = NULL;
    printedNoConfFileOnce = false;
  } else { 
    switch (errno) {
    case ENOENT:
      // ENOENT is hit when a file doesn't exist. Only print this once.
      if (!printedNoConfFileOnce) {
        printedNoConfFileOnce=true;
        emberAfCorePrintln("No Configuration file \"%s\"; Defaulting to precompiled settings.",
                           CONFIGURATION_FILE);
      }
      break;
    case EACCES: case EAGAIN: case EBADF:  case EDEADLK: case EFAULT: 
    case EFBIG:  case EINTR:  case EINVAL: case EISDIR:  case EMFILE:  
    case ENFILE: case ENOLCK: case ENOMEM: case EPERM:
    //default:
      emberAfCorePrintln("Failed to read configuration file (%d): %s",
                         errno,
                         strerror(errno));
      break;
    }
  }
}

static bool networkParametersChanged(EmberNetworkParameters netParams) 
{
  loadBorderRouterConfiguration();
  // The only setting that persists in commissioning mode is networkId
  for (int i = 0; i < EMBER_NETWORK_ID_SIZE; i++) {
    if (netParams.networkId[i] != (uint8_t)networkId[i]) {
      emberAfCorePrintln("Network ID changed to %s", networkId);
      return true;
    }
  } 

  // In JOOB mode, other settings, in addition to networkId, are important
  if (useCommissioner == false) {
    for (int i = 0; i < EXTENDED_PAN_ID_SIZE; i++) {
      if (netParams.extendedPanId[i] != extendedPanId[i]) {
        emberAfCorePrint("oldExtendedPanId: ");
        emberAfCoreDebugExec(emberAfPrintExtendedPanId(netParams.extendedPanId));
        emberAfCorePrint(" new: ");
        emberAfCoreDebugExec(emberAfPrintExtendedPanId(extendedPanId));
        emberAfCorePrintln("");
        return true;
      }
    }
  
    if (netParams.panId != defaultPanId) {
      emberAfCorePrintln("PanId changed from %X to %X",
                         netParams.panId,
                         defaultPanId);
      return true;
    }
  
    if (netParams.channel != preferredChannel) {
      emberAfCorePrintln("Channel changed from %d to %d",
                         netParams.channel,
                         preferredChannel);
      return true;
    }
    #if defined(USE_FIXED_JOIN_KEY)
      // Optional code to check the conf file for a fixedJoinKey
      for (int i = 0; i < EMBER_JOIN_KEY_MAX_SIZE; i++) {
        if (netParams.joinKey[i] != fixedJoinKey[i]) {
          emberAfCorePrintln("Join key change detected at byte_%d: %x",
                             i,
                             fixedJoinLey[i]);
          return true;
        }
      }
    #endif
  }
  return false;
}

void initialState(void) 
{
  assert(state == INITIAL);
  emberEventControlSetInactive(stateEventControl);
  if (stackInitialized) {
    networkStateTransition(previousNetworkStatus,
                           networkStatus,
                           joinFailureReason); 
  } else {
    emberAfCorePrintln("Border Router waiting on stack initialization..."); 
    repeatStateWithDelay(INIT_PERIOD_TIMEOUT_MS);
  }
}

void emberAfMainCallback(void)
{
  loadBorderRouterConfiguration();
}

void emberAfInitCallback(void)
{
  if (!stackInitialized) {
    emberAfCorePrintln("Stack initialized");
    stackInitialized = true;

    // On init, both previous and current status are equal
    previousNetworkStatus = emberNetworkStatus();
    networkStatus = previousNetworkStatus;
  }
 
  // if the stack was reinitialized, clear host assumptions
  resetBorderRouterStatus(); 
}

static void networkStateTransition(EmberNetworkStatus newNetworkStatus,
                                   EmberNetworkStatus oldNetworkStatus,
                                   EmberJoinFailureReason reason)
{
  // The network state logic can be clocked by emberAfNetworkStatusCallback
  // from the stack, but it can also be clocked later by border-router due to
  // the possibility of missing network status events before
  // emberAfInitCallback has been called
  if (!stackInitialized) {
    previousNetworkStatus = oldNetworkStatus;
    networkStatus = newNetworkStatus;
    joinFailureReason = reason; 
    setNextState(INITIAL);
    return;
  }

  EmberNetworkParameters parameters = {{0}};
  emberGetNetworkParameters(&parameters);

  // If we have no network, we try joining to one.  If we have a saved network, 
  // we try to resume operations on that network.  When we are joined and 
  // attached to the network, we wait for an advertisement and then begin
  // reporting.

  switch (newNetworkStatus) {
  case EMBER_NO_NETWORK:
    if (oldNetworkStatus == EMBER_JOINING_NETWORK) {
      emberAfCorePrintln("ERR: Forming failed: 0x%x", reason);
    }

    // If NCP is still attached to an old network, reset
    if (oldNetworkStatus == EMBER_JOINED_NETWORK_ATTACHED) {
      setNextState(RESET_NETWORK_STATE);
    } else {
      setNextState(SET_SECURITY_PARAMETERS_OR_FORM_NETWORK);
    }
    break;
  case EMBER_SAVED_NETWORK:
    // Check to see if the .conf file changed
    if (networkParametersChanged(parameters)) {
      emberAfCorePrintln("Configuration file change detected.");
      setNextState(RESET_NETWORK_STATE);
      return;
    } else {
      setNextState(RESUME_NETWORK);
    }
    break;
  case EMBER_JOINING_NETWORK:
    // Wait for either the "attaching" or "no network" state.
    break;
  case EMBER_JOINED_NETWORK_ATTACHING:
    // Wait for the "attached" state. 
    break;
  case EMBER_JOINED_NETWORK_ATTACHED:
    emberAfCorePrintln("%s network \"%s\"",
                       (state == RESUME_NETWORK
                        ? "Resumed operation on"
                        : (state == FORM_NETWORK
                          ? "Formed"
                          : "Rejoined")),
                       printableNetworkId());

    if (useCommissioner) {
      setNextState(GET_COMMISSIONER);
    } else {
      setNextState(CONFIGURE_BORDER_ROUTER); 
    }

    break;
  case EMBER_JOINED_NETWORK_NO_PARENT:
    // We always form as a router, so we should never end up in the "no parent"
    // state.
    assert(false);
    break;
  default:
    assert(false);
    break;
  }
}

void emberAfNetworkStatusCallback(EmberNetworkStatus newNetworkStatus,
                                  EmberNetworkStatus oldNetworkStatus,
                                  EmberJoinFailureReason reason)
{
  // This callback is called whenever the network status changes, like when
  // we finish joining to a network or when we lose connectivity. 

  emberEventControlSetInactive(stateEventControl);

  // If the stack is initialized accept network transitions
  networkStateTransition(newNetworkStatus, oldNetworkStatus, reason);
}

static void resumeNetwork(void)
{
  assert(state == RESUME_NETWORK);

  emberAfCorePrintln("Resuming operation on network \"%s\"",
                     printableNetworkId());
  emberResumeNetwork();
}

void emberResumeNetworkReturn(EmberStatus status)
{
  
  returnIfInNetworkReset(state);

  // This return indicates whether the stack is going to attempt to resume.  If
  // so, the result is reported later as a network status change.  If we cannot
  // even attempt to resume, we just give up and try forming instead.

  assert(state == RESUME_NETWORK);

  if (status != EMBER_SUCCESS) {
    emberAfCorePrintln("ERR: Unable to resume: 0x%x", status);
    setNextState(SET_SECURITY_PARAMETERS_OR_FORM_NETWORK);
  }
}

#if !defined(USE_RANDOM_MASTER_KEY)
static void setSecurityParameters(void)
{
  // Setting a fixed master key is a security risk, but is useful for
  // demonstration purposes.  See the warning above.

  EmberSecurityParameters parameters = {0};

  assert(state == SET_SECURITY_PARAMETERS);

  emberAfCorePrint("Setting master key to ");
  emberAfCoreDebugExec(emberAfPrintZigbeeKey(masterKey.contents));

  parameters.networkKey = &masterKey;
  emberSetSecurityParameters(&parameters, EMBER_NETWORK_KEY_OPTION);
}
#endif

void emberSetSecurityParametersReturn(EmberStatus status)
{
#if !defined(USE_RANDOM_MASTER_KEY)
  // After setting our security parameters, we can move on to actually forming
  // the network.

  returnIfInNetworkReset(state);
  assert(state == SET_SECURITY_PARAMETERS);

  if (status == EMBER_SUCCESS) {
    setNextState(FORM_NETWORK);
  } else {
    emberAfCorePrint("ERR: Setting master key to ");
    emberAfCoreDebugExec(emberAfPrintZigbeeKey(masterKey.contents));
    emberAfCorePrintln(" failed:  0x%x", status);
    setNextState(SET_SECURITY_PARAMETERS);
  }
#else
  // defined(USE_RANDOM_MASTER_KEY)
  if (!useCommissioner) {
    emberAfCorePrintln("Cannot USE_RANDOM_MASTER_KEY when using 'join out of band' mode.");
    assert(0);
  }
#endif
}

static void formNetwork(void)
{
  assert(state == FORM_NETWORK);
  if (useCommissioner) {

    EmberNetworkParameters parameters = {{0}};

    emberAfCorePrintln("Forming network \"%s\"", networkId);

    MEMCOPY(parameters.networkId, networkId, sizeof(networkId));
    parameters.nodeType = EMBER_ROUTER;
    parameters.radioTxPower = 3;

    emberFormNetwork(&parameters,
                     (EMBER_NETWORK_ID_OPTION
                      | EMBER_NODE_TYPE_OPTION
                      | EMBER_TX_POWER_OPTION),
                     EMBER_ALL_802_15_4_CHANNELS_MASK);

  } else { // !useCommissioner == JOOB

    emberAfCorePrintln("Forming precommissioned network");

    emberAfCorePrint("preferredChannel: %d\n"
                     "networkId: [%s]\n"
                     "defaultPanId: %2x\n",
                     preferredChannel,
                     networkId,
                     defaultPanId);

    emberAfCorePrint("extendedPanId: ");
    emberAfCoreDebugExec(emberAfPrintExtendedPanId(extendedPanId));
    emberAfCorePrintln("");

    emberCommissionNetwork(preferredChannel,
                           fallbackChannelMask,
                           networkId,
                           EMBER_NETWORK_ID_SIZE,
                           defaultPanId,
                           ulaPrefix,
                           extendedPanId,
                           &masterKey,
                           keySequence);
  }
}

void emberCommissionNetworkReturn(EmberStatus status)
{
  returnIfInNetworkReset(state);

  if (useCommissioner) {
    emberAfCorePrintln("external commissioner present, emberCommissionNetworkReturn: %d",
                       status);
  } else { // !useCommissioner
    switch (status) {
    case EMBER_SUCCESS:
      // Delay one tick so the network stack can unwind
      emberAfCorePrintln("Pre-Commission stack call successful.");
      setNextState(FORM_NETWORK_COMPLETION);
      break;
    case EMBER_BAD_ARGUMENT:
    case EMBER_INVALID_CALL:
      // Try to form the network again if it fails
      emberAfCorePrintln("Pre-Commission failed status %x, retrying", status);
      setNextState(FORM_NETWORK);
      break;
    default:
      break;
    }
  }
}

void formNetworkCommissionedCompletion(void)
{
  emberAfCorePrintln("Completing precommisioned join");
  emberJoinCommissioned(3, EMBER_ROUTER, true);
}

void emberJoinNetworkReturn(EmberStatus status)
{
  returnIfInNetworkReset(state);

  if (useCommissioner) {
    emberAfCorePrintln("emberJoinNetworkReturn in-band: %d", status);
  } else {

    // This return indicates whether the stack is going to attempt to join.  If
    // so, the result is reported later as a network status change.  Otherwise,
    // we just try again.

    if (status != EMBER_SUCCESS) {
      emberAfCorePrintln("ERR: Unable to join pre-commissioned: 0x%x", status);
      setNextState(FORM_NETWORK);
    } else {
      emberAfCorePrintln("emberJoinNetworkReturn pre-commissioned success.");
    }
  }
}

void emberFormNetworkReturn(EmberStatus status)
{
  returnIfInNetworkReset(state);

  // This return indicates whether the stack is going to attempt to form.  If
  // so, the result is reported later as a network status change.  Otherwise,
  // we just try again.
  assert(state == FORM_NETWORK);

  if (status != EMBER_SUCCESS) {
    emberAfCorePrintln("ERR: Unable to form: 0x%x", status);
    setNextState(SET_SECURITY_PARAMETERS_OR_FORM_NETWORK);
  } 
}

static void getCommissioner(void)
{
  assert(state == GET_COMMISSIONER);

  emberGetCommissioner();
}

void emberCommissionerStatusHandler(uint16_t flags,
                                    const uint8_t *commissionerName,
                                    uint8_t commissionerNameLength)
{
  returnIfInNetworkReset(state);

  if (state == GET_COMMISSIONER) {
    if (flags == EMBER_NO_COMMISSIONER) {
      emberAfCorePrintln("Setting fixed commissioner key: \"%s\"",
                         commissionerKey);
      // There is no commissioner so add a commissioner
      emberSetCommissionerKey(commissionerKey, sizeof(commissionerKey) - 1);
      emberAllowNativeCommissioner(true);
    } else {
      // If we are not the commissioner, print out the name of the commissioner
      if (!READBITS(flags, EMBER_AM_COMMISSIONER)) {
        emberAfCorePrint("Network already has a commissioner ");
        if (commissionerName != NULL) {
          emberAfCorePrint(": \"");
          uint8_t i;
          for (i = 0; i < commissionerNameLength; i++) {
            emberAfCorePrint("%c", commissionerName[i]);
          }
        }
        emberAfCorePrintln("\"");
      }
    }
    setNextState(CONFIGURE_BORDER_ROUTER); 
  }
}

static EmberStatus coapListen(const EmberIpv6Address *address)
{
  EmberStatus status = emberUdpListen(EMBER_COAP_PORT, address->bytes);
  if (status != EMBER_SUCCESS) {
    emberAfCorePrint("ERR: Listening for CoAP on ");
    emberAfCoreDebugExec(emberAfPrintIpv6Address(address));
    emberAfCorePrintln(" failed: 0x%x", status);
  } else {
    emberAfCorePrint("Listening for CoAP on ");
    emberAfCoreDebugExec(emberAfPrintIpv6Address(address));
    emberAfCorePrintln("");
  }
  return status;
}

static void makeAllThreadNodesAddress(void)
{
  EmberNetworkParameters parameters = {{0}};

  emberAfCorePrint("Making 'all thread nodes' address: ");
  emberGetNetworkParameters(&parameters);

  MEMCOPY(allThreadNodes.bytes + RFC3306_NETWORK_PREFIX_OFFSET,
          parameters.ulaPrefix.bytes,
          sizeof(parameters.ulaPrefix.bytes));

  emberAfCoreDebugExec(emberAfPrintIpv6Address(&allThreadNodes));
  emberAfCorePrintln("");
}

static void configureListeners(void)
{
  // Setup a listener on the multicast addresses
  emberAfCorePrintln("Listening on all mesh nodes multicast");
  coapListen(&allMeshNodes);

  emberAfCorePrintln("Listening on all mesh routers multicast");
  coapListen(&allMeshRouters);

  emberAfCorePrintln("Listening on all thread nodes multicast");
  coapListen(&allThreadNodes);
}

static uint8_t popcountIpv6Netmask(uint8_t *ifNetmaskBytes)
{
  unsigned int *netmaskU32 = (unsigned int*)ifNetmaskBytes;
  return    __builtin_popcount(netmaskU32[0]) 
          + __builtin_popcount(netmaskU32[1]) 
          + __builtin_popcount(netmaskU32[2])
          + __builtin_popcount(netmaskU32[3]);
}

static void initializeManagementInterface(void) 
{
  struct ifaddrs *interfaceList = NULL;
  if (getifaddrs(&interfaceList) < 0) {
    emberAfCorePrintln("getifaddrs failed %s (%d)", strerror(errno), errno);
    return false;
  }

  EmberIpv6Address hostAddresses[EMBER_MAX_IPV6_GLOBAL_ADDRESS_COUNT] = {0};
  char ipv6AddrStr[EMBER_IPV6_PREFIX_STRING_SIZE] = {0};
  struct ifaddrs *interface = interfaceList;
  uint8_t *hostAddrBytes = NULL;
  bool addrBelongsToHost = false;
  uint8_t ifNetmaskBitWidth = 0;
  uint8_t *ifNetmaskBytes = NULL;
  uint8_t *ifAddrBytes = NULL;
  uint8_t foundEntries = 0;

  // get a local copy of the address table to do a destructive search on
  MEMCOPY(hostAddresses, hostAddressTable, sizeof(hostAddressTable));

  emberAfCorePrintln("Initializing management interface [%s]...", 
                      borderRouterMgmtIfaceName);

  for (; interface != NULL; interface = interface->ifa_next) {
    if (strcmp(interface->ifa_name, borderRouterMgmtIfaceName) == 0
        && interface->ifa_addr != NULL
        && interface->ifa_addr->sa_family == AF_INET6) {


      ifAddrBytes = ((struct sockaddr_in6*)
                     interface->ifa_addr)->sin6_addr.s6_addr;
      ifNetmaskBytes = ((struct sockaddr_in6*)
                   interface->ifa_netmask)->sin6_addr.s6_addr;

      // Ignore loopback and NULL addresses
      // If the address is an fe80 or ULA address ignore it for now.  
      // This could induce a mismatch if duplicate fe80/ULA 
      // default host addresses are assigned by the stack in the future
      if(emberIsIpv6UnspecifiedAddress((EmberIpv6Address*)ifAddrBytes)
         || emberIsIpv6LoopbackAddress((EmberIpv6Address*)ifAddrBytes)
         || (inet_ntop(AF_INET6, ifAddrBytes, ipv6AddrStr, sizeof(ipv6AddrStr))
             && (strncasecmp(ipv6AddrStr, "fe80:", strlen("fe80:")) == 0
                 || MEMCOMPARE(ifAddrBytes, ulaPrefix, EMBER_IPV6_BYTES / 2) == 0)))
      {
        continue;
      }

      // if the host has an address that the stack does not know about,
      // remove the address from the host
      addrBelongsToHost = false;

      // stop searching after all hostAddresses have been accounted for
      if(foundEntries < hostAddressTableEntries) {

        for (int i=0; i< hostAddressTableEntries; i++) {
          hostAddrBytes = hostAddresses[i].bytes;
          if (emberIsIpv6UnspecifiedAddress(
                (EmberIpv6Address*)hostAddrBytes)){
            continue;
          }

           if (MEMCOMPARE(hostAddrBytes,
                          ifAddrBytes,
                          EMBER_IPV6_BYTES) == 0) {
            addrBelongsToHost = true;
            memset(hostAddrBytes, 0, EMBER_IPV6_BYTES);
            foundEntries++;
            break;  
          }
        }
      }
     
      if (addrBelongsToHost == false) {
        ifNetmaskBitWidth = popcountIpv6Netmask(ifNetmaskBytes);
        emberAfCorePrintln("Removing stale address: %s/%d from %s",
                            ifAddrBytes,
                            ifNetmaskBitWidth,
                            interface->ifa_name);                  
        emberRemoveHostAddress((const EmberIpv6Address *)ifAddrBytes);
      }
    }
  }
  freeifaddrs(interfaceList);
}

static void initializeBorderRouter(void)
{
  if (borderRouterIsInitialized) {
    return;
  }

  emberAfCorePrintln("Initializing border router");
  makeAllThreadNodesAddress();

  configureListeners();
  // Join any known keys to the network automatically here
#if defined(USE_FIXED_JOIN_KEY)
  // Optional reference code to add fixedJoinKey; see WARNING at top of source.
  emberAfCorePrintln("Adding fixed join key: \"%s\"", fixedJoinKey);
  emberSetJoinKey(NULL, // EUI64 of the joining node, or NULL for any node
                  fixedJoinKey, // Static key a node can use to join the network
                  EMBER_JOIN_KEY_MAX_SIZE); // Length of the fixed key
#endif
  borderRouterIsInitialized = true;
}

static void determineGatewayStatus(void) 
{
  // check the addresses on the mesh to determine if 
  // the stack is configured as a gateway on globalNetworkPrefix
  borderRouterGatewayStatus = GATEWAY_STATUS_PENDING;
  borderRouterIsGateway = false;
  clearHostAddressTable();

  emberGetGlobalAddresses(globalNetworkPrefix.bytes, 
                          globalNetworkPrefixBitWidth);
}


static void configureBorderRouter(void)
{
  assert(state == CONFIGURE_BORDER_ROUTER);
  if (stackInitialized) {

    if (borderRouterIsInitialized == false) {
      initializeBorderRouter();
    }

    switch (borderRouterGatewayStatus) {
      case GATEWAY_STATUS_UNKNOWN:
          emberAfCorePrintln("Determining gateway status...");
          determineGatewayStatus();
        break;
      case GATEWAY_STATUS_PENDING:
        break;
      case GATEWAY_CONFIGURED:
          emberAfCorePrintln("Border Router operating as a gateway");
          setNextState(IDLE_STATE);
        break;
      case GATEWAY_UNCONFIGURED:
          emberConfigureGateway(borderRouterFlags,
                      true, // use a static, stable prefix
                      globalNetworkPrefix.bytes, // the prefix
                      globalNetworkPrefixBitWidth, //prefixLengthInBits
                      0, // domain id
                      0, // Preferred lifetime unused
                      0);// Valid lifetime unused
        break;
      default:
        assert(0);
    }
  }
}

void emberConfigureGatewayReturn(EmberStatus status) 
{
  returnIfInNetworkReset(state);

  if (status != EMBER_SUCCESS) {
    emberAfCorePrintln("ERR: unable to configure gateway");
    setNextState(RESET_NETWORK_STATE);
  } else {
    emberAfCorePrint("Configuring default gateway for: ");
    emberAfCoreDebugExec(
        emberAfPrintIpv6Prefix(&globalNetworkPrefix, 
                               globalNetworkPrefixBitWidth));
    emberAfCorePrintln("");
    borderRouterGatewayStatus = GATEWAY_CONFIGURED;
    setNextState(IDLE_STATE);
  } 
}

void emberGetGlobalAddressReturn(const EmberIpv6Address *  address,
    uint32_t  preferredLifetime,
    uint32_t  validLifetime,
    uint8_t   addressFlags)  
{
  returnIfInNetworkReset(state);

  // a null address denotes the last address was sent
  if (emberIsIpv6UnspecifiedAddress(address)) {
      borderRouterGatewayStatus = (borderRouterIsGateway ? 
                                   GATEWAY_CONFIGURED
                                   : GATEWAY_UNCONFIGURED);
      setNextState(CONFIGURE_BORDER_ROUTER);
  } else {
    // add all returned addresses to the host address cache 

    if (hostAddressTableEntries < EMBER_MAX_IPV6_GLOBAL_ADDRESS_COUNT) {
      MEMCOPY(hostAddressTable[hostAddressTableEntries].bytes, 
              address->bytes, 
              EMBER_IPV6_BYTES);
      hostAddressTableEntries++;
    } else {
      emberAfCorePrintln("ERR: emberGetGlobalAddressReturn returned"
                         " %d entries; max table size is: %d", 
                         hostAddressTableEntries + 1, // counting the zeroth entry 
                         EMBER_MAX_IPV6_GLOBAL_ADDRESS_COUNT);
      assert(0);
    }

    if ((addressFlags & EMBER_GLOBAL_ADDRESS_AM_GATEWAY)
        && (MEMCOMPARE(globalNetworkPrefix.bytes, 
                       address->bytes, 
                       globalNetworkPrefixBitWidth)==0)) {
      borderRouterIsGateway = true;
    }
  }
}

void emberSendSteeringDataReturn(EmberStatus status)
{
  // The steering data helps bring new devices into our network.

  if (status == EMBER_SUCCESS) {
    emberAfCorePrintln("Sent steering data");
  } else {
    emberAfCorePrintln("ERR: Sending steering data failed: 0x%x", status);
  }
}

void emberAddressConfigurationChangeHandler(const EmberIpv6Address *address,
                                            uint32_t preferredLifetime,
                                            uint32_t validLifetime,
                                            uint8_t addressFlags)
{
  ALIAS(emberAddressConfigurationChangeHandler)(address,
                                                preferredLifetime,
                                                validLifetime,
                                                addressFlags);

  if (validLifetime != 0) {
    EmberStatus status;

    status = emberIcmpListen(address->bytes);
    if (status != EMBER_SUCCESS) {
      emberAfCorePrintln("ERR: Listening for ICMP failed: 0x%x", status);
    }

    // Log output is printed within coapListen
    coapListen(address);
  }
}

static void idleState(void)
{
  assert(state == IDLE_STATE);

  EmberNetworkParameters parameters = {{0}};
  emberGetNetworkParameters(&parameters);

  if (networkParametersChanged(parameters)) {
    emberAfCorePrintln("Configuration file change detected.");
    setNextState(RESET_NETWORK_STATE);
    return;
  } else {
    emberAfCorePrintln("Border router is idle in %s mode",
                       (useCommissioner ? "Commissioning": "JOOB"));
  }

  repeatStateWithDelay(IDLE_PERIOD_TIMEOUT_MS);
}

// discover
void discoverCommand(void)
{
  // If we are in an idle state, we can manually send a new discovery request
  // using a CLI command.

  if (state == IDLE_STATE) {
    setNextState(DISCOVER);
  }
}

void serverDiscoverHandler(EmberCoapCode code,
                           uint8_t *uri,
                           EmberCoapReadOptions *options,
                           const uint8_t *payload,
                           uint16_t payloadLength,
                           const EmberCoapRequestInfo *info)
{
  emberAfCorePrintln("Received Device Discovery CoAP Message from Server");

  if (state == IDLE_STATE) {
    setNextState(DISCOVER);
  }
}

static void discover(void)
{
  // Discovery will send a discovery URI to all nodes in the mesh. Thread
  // devices that hear these advertisements will announce their global address
  // to the border-router's global address.

  EmberStatus status;

  assert(state == DISCOVER);

  emberAfCorePrint("Sending discovery URI to ");
  emberAfCoreDebugExec(emberAfPrintIpv6Address(&allThreadNodes));
  emberAfCorePrintln("");

  // Use defaults for everything except the NON.
  EmberCoapSendInfo info = {
    .nonConfirmed = true,
  };
  status = emberCoapPost(&allThreadNodes,
                         borderRouterDiscoverUri,
                         NULL, // body
                         0,    // body length
                         NULL, // handler
                         &info);
  if (status != EMBER_SUCCESS) {
    emberAfCorePrintln("ERR: Discovery failed: 0x%x", status);
  }

  setNextState(IDLE_STATE);
}

// Helper function used to specify source/destination port
static EmberStatus coapSendUri(EmberCoapCode code,
                               const EmberIpv6Address *destination,
                               const uint16_t destinationPort,
                               const uint16_t sourcePort,
                               const uint8_t *uri,
                               const uint8_t *body,
                               uint16_t bodyLength,
                               EmberCoapResponseHandler responseHandler)
{
  // Use defaults for everything except the ports.
  EmberCoapSendInfo info = {
    .localPort = sourcePort,
    .remotePort = destinationPort,
  };
  return emberCoapSend(destination,
                       code,
                       uri,
                       body,
                       bodyLength,
                       responseHandler,
                       &info);
}

void deviceAnnounceHandler(EmberCoapCode code,
                           uint8_t *uri,
                           EmberCoapReadOptions *options,
                           const uint8_t *payload,
                           uint16_t payloadLength,
                           const EmberCoapRequestInfo *info)
{
  EmberStatus status;

  emberAfCorePrintln("Received Device Announce CoAP Message: "
                     "Payload String (length=%d) %s",
                     payloadLength,
                     payload);

  // Here we are relaying a CoAP message to the server address that contains
  // the same payload as our nodejoined message. This is done only so the
  // server running locally can be aware of devices that become available. A
  // proper discovery method should be added in for this.

  status = coapSendUri(EMBER_COAP_CODE_POST, 
                       &serverAddress,
                       MGMT_COAP_PORT, //dport for mgmt coap to webserver
                       EMBER_COAP_PORT, //sport for border-router listener
                       nodeJoinedUri, 
                       payload,
                       payloadLength,
                       deviceAnnounceResponseHandler);

  if (status != EMBER_SUCCESS) {
    emberAfCorePrintln("ERR: Error reporting device to host, status: 0x%x",
                       status);
  }
}

static void deviceAnnounceResponseHandler(EmberCoapStatus status,
                                          EmberCoapCode code,
                                          EmberCoapReadOptions *options,
                                          uint8_t *payload,
                                          uint16_t payloadLength,
                                          EmberCoapResponseInfo *info)
{
  if (status != EMBER_COAP_MESSAGE_ACKED) {
    emberAfCorePrintln("ERR: CoAP Request Failed: 0x%x", status);
  }
}

// multicast-on
void multicastonCommand(void)
{
  emberAfCorePrint("Sending ALL ON URI to ");
  emberAfCoreDebugExec(emberAfPrintIpv6Address(&allThreadNodes));
  emberAfCorePrintln("");

  // Use defaults for everything except the NON.
  EmberCoapSendInfo info = {
    .nonConfirmed = true,
  };
  EmberStatus status = emberCoapPost(&allThreadNodes,
                                     nodeOnOffOutUri,
                                     onPayload,
                                     sizeof(onPayload),
                                     NULL, // handler
                                     &info);
  if (status != EMBER_SUCCESS) {
    emberAfCorePrintln("ERR: ALL ON failed: 0x%x", status);
  }
}

void deviceMulticastOnHandler(EmberCoapCode code,
                              uint8_t *uri,
                              EmberCoapReadOptions *options,
                              const uint8_t *payload,
                              uint16_t payloadLength,
                              const EmberCoapRequestInfo *info)
{
  EmberStatus status;

  emberAfCorePrintln("Received Multicast ON CoAP Message");

  emberAfCorePrint("Sending ALL ON URI to ");
  emberAfCoreDebugExec(emberAfPrintIpv6Address(&allThreadNodes));
  emberAfCorePrintln("");

  // Use defaults for everything except the NON.
  EmberCoapSendInfo sendInfo = {
    .nonConfirmed = true,
  };
  status = emberCoapPost(&allThreadNodes,
                         nodeOnOffOutUri,
                         onPayload,
                         sizeof(onPayload),
                         NULL, // handler
                         &sendInfo);
  if (status != EMBER_SUCCESS) {
    emberAfCorePrintln("ERR: ALL ON failed: 0x%x", status);
  }
}

// multicast-off
void multicastoffCommand(void)
{
  emberAfCorePrint("Sending ALL OFF URI to ");
  emberAfCoreDebugExec(emberAfPrintIpv6Address(&allThreadNodes));
  emberAfCorePrintln("");

  // Use defaults for everything except the NON.
  EmberCoapSendInfo info = {
    .nonConfirmed = true,
  };
  EmberStatus status = emberCoapPost(&allThreadNodes,
                                     nodeOnOffOutUri,
                                     offPayload,
                                     sizeof(offPayload),
                                     NULL, // handler
                                     &info);
  if (status != EMBER_SUCCESS) {
    emberAfCorePrintln("ERR: ALL OFF failed: 0x%x", status);
  }
}

void deviceMulticastOffHandler(EmberCoapCode code,
                               uint8_t *uri,
                               EmberCoapReadOptions *options,
                               const uint8_t *payload,
                               uint16_t payloadLength,
                               const EmberCoapRequestInfo *info)
{
  EmberStatus status;

  emberAfCorePrintln("Received Multicast OFF CoAP Message");

  emberAfCorePrint("Sending ALL OFF URI to ");
  emberAfCoreDebugExec(emberAfPrintIpv6Address(&allThreadNodes));
  emberAfCorePrintln("");

  // Use defaults for everything except the NON.
  EmberCoapSendInfo sendInfo = {
    .nonConfirmed = true,
  };
  status = emberCoapPost(&allThreadNodes,
                         nodeOnOffOutUri,
                         offPayload,
                         sizeof(offPayload),
                         NULL, // handler
                         &sendInfo);
  if (status != EMBER_SUCCESS) {
    emberAfCorePrintln("ERR: ALL OFF failed: 0x%x", status);
  }
}

void deviceMulticastDimHandler(EmberCoapCode code,
                               uint8_t *uri,
                               EmberCoapReadOptions *options,
                               const uint8_t *payload,
                               uint16_t payloadLength,
                               const EmberCoapRequestInfo *info)
{
  EmberStatus status;

  emberAfCorePrintln("Received Multicast Dim CoAP Message");

  emberAfCorePrint("Sending DIM URI to ");
  emberAfCoreDebugExec(emberAfPrintIpv6Address(&allThreadNodes));
  emberAfCorePrintln("");

  // Use defaults for everything except the NON.
  EmberCoapSendInfo sendInfo = {
    .nonConfirmed = true,
  };
  status = emberCoapPost(&allThreadNodes,
                         nodeDimOutUri,
                         payload,
                         payloadLength,
                         NULL, // handler
                         &sendInfo);
  if (status != EMBER_SUCCESS) {
    emberAfCorePrintln("ERR: DIM ALL failed: 0x%x", status);
  }
}

void netResetHandler(EmberCoapCode code,
                     uint8_t *uri,
                     EmberCoapReadOptions *options,
                     const uint8_t *payload,
                     uint16_t payloadLength,
                     const EmberCoapRequestInfo *info)
{
  if (state == IDLE_STATE) {
    emberAfCorePrintln("Performing net reset from CoAP");
    // No CoAP response required, these will be non-confirmable requests
    setNextState(RESET_NETWORK_STATE);
  }
}

static void resetNetworkState(void)
{
  emberAfCorePrintln("Resetting network state");
  resetBorderRouterStatus();
  // Reload the conf file in case the user wanted to change configuration by
  // issuing this reset
  loadBorderRouterConfiguration();
  emberResetNetworkState();
}

void emberResetNetworkStateReturn(EmberStatus status)
{
  // If we ever leave the network, we go right back to forming again.  This
  // could be triggered by an external CLI command.
  if (status == EMBER_SUCCESS) {
    emberAfCorePrintln("Reset network state complete");
  }
}

void stateEventHandler(void)
{
  emberEventControlSetInactive(stateEventControl);

  switch (state) {
  case INITIAL:
    initialState();
    break;
  case RESUME_NETWORK:
    resumeNetwork();
    break;
#if !defined(USE_RANDOM_MASTER_KEY)
  case SET_SECURITY_PARAMETERS:
    setSecurityParameters();
    break;
#endif
  case FORM_NETWORK:
    formNetwork();
    break;
  case GET_COMMISSIONER:
    getCommissioner();
    break;
  case FORM_NETWORK_COMPLETION:
    formNetworkCommissionedCompletion();
    break;
  case CONFIGURE_BORDER_ROUTER:
    configureBorderRouter();
    break;
  case IDLE_STATE:
    idleState();
    break;
  case DISCOVER:
    discover();
    break;
  case RESET_NETWORK_STATE:
    resetNetworkState();
    break;
  default:
    assert(false);
  }
}

static void setNextStateWithDelay(uint8_t nextState, uint32_t delayMs)
{
  state = nextState;
  emberEventControlSetDelayMS(stateEventControl, delayMs);
}

static const uint8_t *printableNetworkId(void)
{
  EmberNetworkParameters parameters = {{0}};
  static uint8_t networkId[EMBER_NETWORK_ID_SIZE + 1] = {0};
  emberGetNetworkParameters(&parameters);
  MEMCOPY(networkId, parameters.networkId, EMBER_NETWORK_ID_SIZE);
  return networkId;
}

// Configuration file-parsing helpers
static ConfKey identifyEntry(char *confLine, ConfEntry *entries)
{
  // Searches 'confLine' for valid 'entries' and returns the index in entries
  // corresponding to the found entry or else 'UNKNOWN_KEY' on failure
  ConfKey key;
  ConfEntry *entry;
  int rc = 0;

  if (entries != NULL && confLine != NULL) {
    for (key = (ConfKey)0; key < MAX_CONF_KEY; key++) {
      entry = entries + key;

      rc = strncasecmp(confLine, entry->key, strlen(entry->key));

      if (rc == 0) {
        return key;
      }
    }
  }
  return UNKNOWN_KEY;
}

static void trimWhitespace(char* inString, size_t bufLen)
{
  // Trims leading and trailing whitespace from inString and does not exceed
  // bufLen
  assert(inString);

  if (bufLen == 0) {
    return;
  }

  const char* stringStart = inString;
  char *stringEnd = 0;
  size_t strLength = 0;

  // Remove all whitespace at the start of the string
  while (*stringStart != '\0' 
         && isspace(*stringStart) 
         && (stringStart < (inString + bufLen))) {
    stringStart++;
  }

  strLength = strlen(stringStart);

  // In-place shift the string including the terminating NULL
  if (inString != stringStart) {
    MEMMOVE(inString, stringStart, strLength + 1);
  }

  stringEnd = strchr(inString, '\0') - 1;

  // Remove all whitespace at the end of the string
  while (inString < stringEnd  && isspace(*stringEnd)) {
    stringEnd--;
  }
  *(stringEnd + 1) = '\0';
}

static uint32_t parseConfigurationFile(FILE *confFp)
{
  // Takes a valid file pointer and parses the entries into memory
  assert(confFp);
  char *buffer = 0;
  size_t buffSz = 0;
  ssize_t rc = 0;
  char *valuesStart = 0;
  ConfKey key = UNKNOWN_KEY;
  ConfEntry *entry = NULL;
  uint32_t keys = 0; 

  // This value needs to hold at least 6 characters at the start
  // (% % % d s \0), but may hold more characters if the number of digits in
  // EMBER_NETWORK_ID_SIZE increases the simplest thing to do in preprocessor
  // is to just add 6 to LENGTHOF(EMBER_NETWORK_ID_SIZE) 
  char tempFmt[LENGTHOF(EMBER_NETWORK_ID_SIZE) + 6] = { '%','%','%','d','s', 0 };
  char *valueLocation = NULL;
  // The networkId string needs to be restricted to max of
  // EMBER_NETWORK_ID_SIZE bytes
  snprintf(validConfEntries[NETWORK_ID_KEY].format,
           sizeof(entry->format),
           tempFmt,
           EMBER_NETWORK_ID_SIZE);

  while (!feof(confFp)) {
    if (feof(confFp)) {
      break;
    }

    rc = getline(&buffer, &buffSz, confFp);

    if (rc < 0) {
      switch(errno) {
      //only print fatal errors:
      case EACCES: case EAGAIN: case EBADF: 
      case EINVAL: case ENFILE: case ENOMEM: 
      case EPERM: 
      //default:
        emberAfCorePrintln("Unable to getline: errno: (%d) \"%s\"",
                           errno,
                           strerror(errno));
        break;
      }
    } else if (rc > 0) {
      // Bytes were read by getline, parse the line
      trimWhitespace(buffer, buffSz);
      if ((buffer[0] == '#') 
          || (buffer[0] == '/') 
          || (buffer[0] == '\n') 
          || buffer[0] == 0) {
        // Detected a comment, skip this line
        continue;
      } else {
        // Detected a line to parse
        key = identifyEntry(buffer, validConfEntries);
        keys |= (1<<key);
        if (key == UNKNOWN_KEY) {
          //emberAfCorePrintln("Unknown configuration entry skipped: (%s)",
          //                    buffer);
          continue;
        } else {
          // Found a valid entry
          entry = validConfEntries + key;
          valuesStart = buffer + strlen(entry->key);
          //emberAfCorePrintln("Parsing: %s", entry->key);
          switch (key) {
          case FILE_VERSION_KEY:
            rc = simpleParser(valuesStart, entry); 
            break;
          case USE_COMMISSIONER_KEY:
            trimWhitespace(valuesStart, strlen(valuesStart));
            if ((strncasecmp(valuesStart, "false", 5) == 0) 
                || (strncasecmp(valuesStart, "no", 2) == 0) 
                || (strncmp(valuesStart, "0", 1) == 0)) {
              //emberAfCorePrintln("Using 'Join Out Of Band' mode.");
              *((bool*)(entry->addr)) = false;
            } else {
              //emberAfCorePrintln("Using 'External Commissioner' mode.");
              *((bool*)(entry->addr)) = true;
            }
            rc = true;
            break;
          case NETWORK_ID_KEY:
            rc = simpleParser(valuesStart, entry); 
            // Check to see if the networkId is too long
            valueLocation = strstr(buffer, entry->addr);
            if ((valueLocation != NULL) 
                && (strlen(valueLocation) > (sizeof(networkId)-1))) {
              emberAfCorePrintln("WARNING: networkId: %s is longer than %d "
                                 "characters and will be truncated!",
                                 valueLocation,
                                 sizeof(networkId)-1);
            }
            break;
          case IPV6_SUBNET_KEY:
            trimWhitespace(valuesStart, strlen(valuesStart));
            rc = ipv6PrefixParser(valuesStart, entry);
            break;
          case DEFAULT_CHANNEL_KEY:
            rc = simpleParser(valuesStart, entry);
            break;
          case PAN_ID_KEY:
            rc = hexByteParser(valuesStart, entry);
            break;
          case EX_PAN_ID_KEY:
            rc = hexByteParser(valuesStart, entry);
            break;
#if defined(USE_FIXED_JOIN_KEY)
          case FIXED_JOIN_KEY:
            rc = hexByteParser(valuesStart, entry);
            break;
#endif
          default:
            // Not a valid key
            assert("Invalid .conf key detected.");
            break;
          } // end 'switch(key)'
          if (rc != true) {
            emberAfCorePrintln("Unable to parse: \"%s\"", valuesStart);
            keys &= ~(1<<key);
          }
        } // end 'else found known key'
      } // end 'else detected a line to parse
    } // end 'else (getline rc>0)
  } // end 'while (!feof(confFp))'

  if (buffer != NULL) {
    free(buffer);
    buffer = NULL;
  }
  return keys;
}

static int ipv6PrefixParser(const char const *inString, ConfEntry *entry)
{
  // Extract the prefix from the line
  EmberIpv6Address temp = {{0}};
  uint8_t prefixWidth = 0;
  if (!emberIpv6StringToPrefix((const uint8_t *)inString, &temp, &prefixWidth)) {
    emberAfCorePrintln("Failed to parse \"%s\"", entry->key);
    return false;
  } else {
    // Copy the entire 16 bytes
    MEMCOPY(entry->addr, temp.bytes, entry->sizeInBytes);

    // Set the bit-width of the global address
    globalNetworkPrefixBitWidth = prefixWidth;
  }
  return true;
}

static int simpleParser(const char const *inString, ConfEntry *entry)
{
  // Takes the entry found in 'string' and places it into 'entry' using
  // 'format'
  int rc = 0;
  char tempLineBuffer[MAX_CONF_LINE_LENGTH + 1] = {0};
  rc = sscanf(inString, entry->format, tempLineBuffer);
  if ((rc == 1) 
      || (strcmp(entry->key,  validConfEntries[NETWORK_ID_KEY].key) == 0)) {
    MEMCOPY(entry->addr, tempLineBuffer, entry->sizeInBytes);
  } else { 
    emberAfCorePrintln("simpleParser returned false for entry->key: %s",
                       entry->key);
    return false;
  }
  return true;
}

static int hexByteParser(const char const *inString, ConfEntry *entry)
{
  // Treats the entry found in 'string' as hexidecimal representation
  // parsing by bytes for entry->sizeInBytes into 'entry-addr' 
  // 'format' is ignored
  int i = 0;
  int rc = 0;
  const char *stringStart = inString;
  const char *stringEnd = 0; 
  size_t charsConsumed = 0;
  uint8_t byteVal = 0;
  uint8_t tempLineBuffer[MAX_CONF_LINE_LENGTH + 1] = {0};

  if (!inString || !entry) {
    return false;
  }

  if (strlen(inString) > MAX_CONF_LINE_LENGTH) {
    stringEnd = inString + MAX_CONF_LINE_LENGTH;
  } else {
    stringEnd = inString + strlen(inString);
  }

  while (stringStart < stringEnd) {
    if (i >= entry->sizeInBytes) {
      break;
    }

    rc = sscanf(stringStart, "%2hhx%n", &byteVal, &charsConsumed);

    // Skip any : characters
    while (*(stringStart + charsConsumed) == ':') { 
      charsConsumed++;
    }

    if (rc == 1) {
      stringStart += charsConsumed;
      tempLineBuffer[i++] = byteVal;
    } else {
      emberAfCorePrintln("parse halt at: %s sscanf rc: %d for key: %s",
                         stringStart,
                         rc,
                         entry->key);
      return false;
    }
  } 

  // If no errors were found, copy the temporary into its final address
  MEMCOPY(entry->addr, tempLineBuffer, entry->sizeInBytes);
  return true;
}
