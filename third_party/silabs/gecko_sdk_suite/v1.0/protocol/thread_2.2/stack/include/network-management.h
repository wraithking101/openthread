/**
 * @file network-management.h
 * @brief  Network Management API.
 *
 * This file contains network management API functions that are
 * available on both host and SOC platforms.
 *
 * Callback naming conventions:
 *   ...Handler()       // unilateral event, e.g. a beacon arriving
 *   ...Return()        // result of a call
 */

#ifndef __NETWORK_MANAGEMENT_H__
#define __NETWORK_MANAGEMENT_H__

#include <stddef.h>
#include "stack/include/ember-types.h"

/**
 * @addtogroup network_utilities
 *
 * See network-management.h for source code.
 * @{
 */

/** @brief  Initializes the Ember stack. */
void emberInit(void);
/** @brief  Provides the result of a call to emberInit(). */
void emberInitReturn(EmberStatus status);

/** @brief  A periodic tick routine that must be called in the application's 
 * main event loop.
 */
void emberTick(void);

/** @brief  Application structure to hold useful network parameters. */
typedef struct {
  /** @brief  This will only be NUL terminated if the length of the network id
   * is less than EMBER_NETWORK_ID_SIZE.
   */
  uint8_t  networkId[EMBER_NETWORK_ID_SIZE];
  EmberIpv6Prefix ulaPrefix;
  EmberIpv6Prefix legacyUla;
  uint8_t  extendedPanId[EXTENDED_PAN_ID_SIZE];
  uint16_t panId;
  uint8_t  channel;
  EmberNodeType nodeType;
  int8_t  radioTxPower;
  EmberKeyData masterKey;
  /** @brief  This will only be NUL terminated if the length of the key is less
   * than EMBER_JOIN_KEY_MAX_SIZE.
   */
  uint8_t joinKey[EMBER_JOIN_KEY_MAX_SIZE];
  uint8_t joinKeyLength;
} EmberNetworkParameters;

/** @brief  Erases the network state stored in nonvolatile memory
 * after which the network status will be EMBER_NO_NETWORK.
 * This function should not be called to rejoin a former network;
 * use emberResumeNetwork() instead.  There may be
 * difficulties joining a former network after resetting the network state,
 * due to security considerations.
 */
void emberResetNetworkState(void);

/** @brief Provides the result of a call to emberResetNetworkState(). */
void emberResetNetworkStateReturn(EmberStatus status);

// Power Management for sleepy end devices.

/** @brief Application handler for deep sleep on sleepy end devices.
 * This call is ignored for non-sleepy devices.  The device may or may not sleep
 * depending on the internal state.
 *
 * @return true if going to deep sleep.
 */
bool emberDeepSleepTick(void);

/** @brief Turns chip deep sleep on or off for sleepy end devices.
 * This call is ignored on non-sleepy devices.  The device may or may not sleep
 * depending on the internal state */
void emberDeepSleep(bool sleep);

/** @brief Provides the result of a call to emberDeepSleep(). */
void emberDeepSleepReturn(EmberStatus status);

/** @brief For a sleepy end device, report how long the chip went to deep sleep.
 * In a NCP + host setup, the stack reports this to the host app.
 */
void emberDeepSleepCompleteHandler(uint16_t sleepDuration);

/** @brief Required radio state while stack is idle.
 */
typedef enum {
   /** Incoming messages are expected and the radio must be left on. */
  IDLE_WITH_RADIO_ON,
   /** Incoming messages are expected and must be polled for. */
  IDLE_WITH_POLLING,
   /** No messages are expected and the radio may be left off. */
  IDLE_WITH_RADIO_OFF
} EmberIdleRadioState;

/** @brief Returns the time the stack will be idle, in milliseconds.
 * Also sets radioStateResult to the required radio state while the
 * stack is idle.
 *
 * This function returns directly, rather than having a ...Return()
 * callback, because it is only available on SOCs.
 *
 * @param radioStateResult      Used to return the required radio
 *  state while the stack is idle.
 *
 * @return The number of milliseonds for which the stack will be idle.
 */
uint32_t emberStackIdleTimeMs(EmberIdleRadioState *radioStateResult);

/** @brief Defines tasks that prevent the stack from sleeping.
 */
enum
{
   /** There are messages waiting for transmission. */
   EMBER_OUTGOING_MESSAGES = 0x01,
   /** One or more incoming messages are being processed. */
   EMBER_INCOMING_MESSAGES = 0x02,
   /** The radio is currently powered on.  On sleepy devices the radio is
    *  turned off when not in use.  On non-sleepy devices (::EMBER_ROUTER or
    * ::EMBER_END_DEVICE) the radio is always on.
    */
   EMBER_RADIO_IS_ON = 0x04,
};

/** @brief A mask of the high priority tasks that prevent a device from sleeping.
 * Devices should not sleep if any high priority tasks are active.  
 */
#define EMBER_HIGH_PRIORITY_TASKS \
(EMBER_OUTGOING_MESSAGES | EMBER_INCOMING_MESSAGES | EMBER_RADIO_IS_ON)

/** @brief Indicates whether the stack is currently in a state where
 * there are no high priority tasks and may sleep.  
 *
 * There may be tasks expecting incoming messages, in which case the device should 
 * periodically wake up and call ::emberPollForData() in order to receive messages.
 * This function can only be called for sleepy end devices.
 */
bool emberOkToNap(void);

/** @brief  If implementing event-driven sleep on an NCP host, this method will
 * return the bitmask indicating the stack's current tasks. (see enum above)
 *
 *  The mask ::EMBER_HIGH_PRIORITY_TASKS defines which tasks are high
 *  priority.  Devices should not sleep if any high priority tasks are active.
 *  Active tasks that are not high priority are waiting for
 *  messages to arrive from other devices.  If there are active tasks,
 *  but no high priority ones, the device may sleep but should periodically
 *  wake up and call ::emberPollForData() in order to receive messages.  Parents
 *  will hold messages for ::EMBER_INDIRECT_TRANSMISSION_TIMEOUT (in quarter
 *  seconds) before discarding them.
 *
 * @return A bitmask of the stack's active tasks.
 */
void emberOkToNapReturn(uint8_t stateMask);

/** @brief This method is called any time an event is scheduled from within an
 * ISR context. It can be used to determine when to stop a long running sleep
 * to see what application or stack events now need to be processed.
 * @param event The event that was scheduled by the ISR.
 */
void emberEventDelayUpdatedFromIsrHandler(Event *event);

// Stack power down utilities

/** @brief  Get the stack ready for power down, or deep sleep.  Purges the MAC
 *  indirect queue, and empties the phy-to-mac and mac-to-network queues.
 */
void emberStackPrepareForPowerDown(void);

/** @brief  Returns true if the stack is currently emptying any message queues
 * or false if the MAC queue is currently not empty.
 */
bool emberStackPreparingForPowerDown(void);
   
/** @brief Immediately turns the radio power completely off.
 *
 * After calling this function, you must not call any other stack function
 * except emberStackPowerUp(). This is because all other stack 
 * functions require that the radio is powered on for their 
 * proper operation.  
 */
void emberStackPowerDown(void);

/** @brief Initializes the radio.  Typically called coming out of deep sleep.
 * 
 * For non-sleepy devices, also turns the radio on and leaves it in rx mode.
 */
void emberStackPowerUp(void);

/** @brief For sleepy hosts, use this call to have the stack manage polling for
 * sleepy end devices.  In a host/NCP setup, this means that the NCP app will
 * take care of periodic data polling.
 */
void emberStackPollForData(uint32_t pollMs);

/** @brief Provides the result of a call to emberStackPollForData(). */
void emberStackPollForDataReturn(EmberStatus status);

/** @brief Use this call if setting up polling for sleepy end devices on the
 * application.
 *
 * This function allows a sleepy end device to query its parent for any
 * pending data.
 *
 * Sleepy end devices must call this function periodically to maintain contact
 * with their parent.  The parent will remove a sleepy end device from its child
 * table if it has not received a poll from it within the last
 * ::EMBER_SLEEPY_CHILD_POLL_TIMEOUT seconds.
 *
 * If the sleepy end device has lost contact with its parent, it re-joins then
 * network using another router.
 * 
 * The default values for the timeouts are set in 
 * config/ember-configuration-defaults.h, and can be overridden in the 
 * application's configuration header.  
 */
void emberPollForData(void);

/** @brief Provides the result of a call to emberPollForData().
 * @param An EmberStatus value:
 * - ::EMBER_SUCCESS      - The poll message has been submitted for transmission
 * - ::EMBER_INVALID_CALL - The node is not a sleepy end device.
 * - ::EMBER_NOT_JOINED   - The node is not part of a network.
 */
void emberPollForDataReturn(EmberStatus status);

// Network info.

/** @brief  Returns the EUI64 of the Ember chip. */
const EmberEui64 *emberEui64(void);

/** @brief  Returns the current status of the network.
 * Prior to calling emberInitNetwork(), the status is EMBER_NETWORK_UNINITIALIZED.
 */
EmberNetworkStatus emberNetworkStatus(void);

/** @brief  Reports a change to the network status.  For example, the network
 * status changes while going through the joining process, or while reattaching
 * to the network, which can happen for a variety of reasons.  In particular,
 * after issuing a form, join, resume, or attach command, the application
 * knows that the device is on the network and ready to communicate when
 * this handler is called with a newNetworkStatus of
 * EMBER_JOINED_NETWORK_ATTACHED.
 *
 * If the status handler is reporting a join failure, then the newNetworkStatus
 * argument will have a value of EMBER_NO_NETWORK and the reason argument
 * will contain an appropriate value.  For other network status reports, the
 * reason argument does not apply and is set to EMBER_JOIN_FAILURE_REASON_NONE.
 */
void emberNetworkStatusHandler(EmberNetworkStatus newNetworkStatus,
                               EmberNetworkStatus oldNetworkStatus,
                               EmberJoinFailureReason reason);

/** @brief Fetches the current network parameters into the supplied pointer. */
void emberGetNetworkParameters(EmberNetworkParameters *parameters);

/** @brief Returns the pan id of the network.
 */
EmberPanId emberGetPanId(void);

/** @brief Structure that holds information about a routing table entry for use
 * on the application.  See ::emberGetRipEntry
 */
typedef struct {
  uint8_t longId[8];
  EmberNodeType type;
  int8_t rollingRssi;
  uint8_t nextHopIndex;
  uint8_t ripMetric;
  uint8_t incomingLinkQuality;
  uint8_t outgoingLinkQuality;
  bool mleSync;
  uint8_t age;
  uint8_t routeDelta;
} EmberRipEntry;

/** @brief  Gets the EmberRipEntry at the specified index of the RIP table.
 * The result is returned to the application via the emberGetRipEntryReturn()
 * callback.
 *
 * The index argument can theoretically lie between 0 and EMBER_RIP_TABLE_MAX_SIZE.
 * However, the RIP table is usually smaller, depending on how many neighbors
 * a node has.
 *
 * The caller can pass in a 0xFF index to request all valid RIP table entries.
 * Note that the stack will ONLY return valid entries when 0xFF is passed. Once
 * all valid entries have been returned by this method, an extra zeroed-out entry
 * is returned to indicate completion.
 *
 * When the application requests an EmberRipEntry at a certain index, it can
 * check for the validity of the returned EmberRipEntry by checking whether
 * it is zeroed out.  For example, the 'type' parameter should never be zero.
 * (it should be a valid node type: EMBER_ROUTER@cond EMBER_WAKEUP_STACK 
 * or EMBER_LURKER@endcond)
 */
void emberGetRipEntry(uint8_t index);

/** @brief Provides the result of a call to emberGetRipEntry(). */
void emberGetRipEntryReturn(uint8_t index, const EmberRipEntry *entry);

/** @brief  Gets the value for the specified counter.  The result is returned
 * to the application via emberGetCounterReturn().
 */
void emberGetCounter(EmberCounterType type);

/** @brief Provides the result of a call to emberGetCounter(). */
void emberGetCounterReturn(EmberCounterType type, uint16_t value);

/** @brief  Resets all counter values to 0. */
void emberClearCounters(void);

/** @brief A callback invoked to inform the application of the 
 * occurrence of an event defined by EmberCounterType, for example,
 * transmissions and receptions at different layers of the stack.
 *
 * The application must define EMBER_APPLICATION_HAS_COUNTER_HANDLER
 * in its CONFIGURATION_HEADER to use this.
 * This function may be called in ISR context, so processing should
 * be kept to a minimum.
 *
 * @param type       The type of the event.
 * @param increment  Specify the increase in the counter's tally.
 *
 */
void emberCounterHandler(EmberCounterType type, uint16_t increment);

/** @brief A callback invoked to query the application for the
 * countervalue of an event defined by EmberCounterType.
 *
 * The application must define EMBER_APPLICATION_HAS_COUNTER_VALUE_HANDLER
 * in its CONFIGURATION_HEADER to use this.
 *
 * @param   type       The type of the event.
 * @returns The counter's tally.
 *
 */
uint16_t emberCounterValueHandler(EmberCounterType type);

// Scanning.

/** @brief Starts a scan.  Note that while a scan can be initiated
  * while the node is currently joined to a network, the node will generally
  * be unable to communicate with its PAN during the scan period, so care
  * should be taken when performing scans of any significant duration while
  * presently joined to an existing PAN.
  *
  * Upon completion of the scan, a status is returned via ::emberScanReturn().
  * Possible EmberStatus values and their meanings:
  * - ::EMBER_SUCCESS, the scan completed succesffully.
  * - ::EMBER_MAC_SCANNING, we are already scanning.
  * - ::EMBER_MAC_BAD_SCAN_DURATION, we have set a duration value that is 
  *   not 0..14 inclusive.
  * - ::EMBER_MAC_INCORRECT_SCAN_TYPE, we have requested an undefined 
  *   scanning type; 
  * - ::EMBER_MAC_INVALID_CHANNEL_MASK, our channel mask did not specify any
  *   valid channels on the current platform.
  * 
  * @param scanType  Indicates the type of scan to be performed.  
  *  Possible values:  ::EMBER_ENERGY_SCAN, ::EMBER_ACTIVE_SCAN.
  * 
  * @param channelMask  Bits set as 1 indicate that this particular channel
  * should be scanned. Bits set to 0 indicate that this particular channel
  * should not be scanned.  For example, a channelMask value of 0x00000001
  * would indicate that only channel 0 should be scanned.  Valid channels range
  * from 11 to 26 inclusive.  This translates to a channel mask value of 0x07
  * FF F8 00.  As a convenience, a channelMask of 0 is reinterpreted as the
  * mask for the current channel.
  * 
  * @param duration  Sets the exponent of the number of scan periods,
  * where a scan period is 960 symbols, and a symbol is 16 microseconds. 
  * The scan will occur for ((2^duration) + 1) scan periods.  The value
  * of duration must be less than 15.  The time corresponding to the first
  * few values are as follows: 0 = 31 msec, 1 = 46 msec, 2 = 77 msec,
  * 3 = 138 msec, 4 = 261 msec, 5 = 507 msec, 6 = 998 msec.
  */
void emberStartScan(EmberNetworkScanType scanType,
                    uint32_t channelMask,
                    uint8_t duration);

/** @brief  Reports the maximum RSSI value measured on the channel.
  *
  * @param channel  The 802.15.4 channel on which the RSSI value was measured.
  *
  * @param maxRssiValue  The maximum RSSI value measured (in units of dBm).
  */
void emberEnergyScanHandler(uint8_t channel, int8_t maxRssiValue);

/** @brief  Size of the island (aka network fragment) id. */
#define ISLAND_ID_SIZE 5

/** @brief  Structure to hold information about an 802.15.4 beacon for use
 * on the application.
 */
typedef struct {
  uint8_t   networkId[16];
  uint8_t   extendedPanId[8];
  uint8_t   longId[8];
  uint16_t  panId;
  uint8_t   protocolId;
  uint8_t   channel;
  bool      allowingJoin;
  uint8_t   lqi;
  int8_t    rssi;
  uint8_t   version;
  uint16_t  shortId; // deprecated, beacons now use 64-bit source
  uint8_t   steeringData[16];
  uint8_t   steeringDataLength;
} EmberMacBeaconData;

/** @brief  Reports an incoming beacon during an active scan. */
void emberActiveScanHandler(const EmberMacBeaconData *beaconData);

/** @brief  Provides the status upon completion of a scan. */
void emberScanReturn(EmberStatus status);

/** @brief  Terminates a scan in progress. */
void emberStopScan(void);

// Other.

/** @brief  Resets the Ember chip. */
void emberResetMicro(void);

/** @brief  Enumerate the various chip reset causes. */
typedef enum {
  EMBER_RESET_UNKNOWN,
  EMBER_RESET_FIB,
  EMBER_RESET_BOOTLOADER,
  EMBER_RESET_EXTERNAL,
  EMBER_RESET_POWERON,
  EMBER_RESET_WATCHDOG,
  EMBER_RESET_SOFTWARE,
  EMBER_RESET_CRASH,
  EMBER_RESET_FLASH,
  EMBER_RESET_FATAL,
  EMBER_RESET_FAULT,
  EMBER_RESET_BROWNOUT
} EmberResetCause;

/** @brief  Notifies the application of a reset on the Ember chip
 * due to the indicated cause.
 */
void emberResetMicroHandler(EmberResetCause cause);

/** @brief
 * Detects if the standalone bootloder is installed, and if so
 * returns the installed version and info about the platform, 
 * micro and phy. If not version will be set to 0xffff. A 
 * returned version of 0x1234 would indicate version 1.2 build 34.
 */
void emberGetStandaloneBootloaderInfo(void);

/** @brief
 * Provides the result of a call to ::emberGetStandaloneBootloaderInfo.
 *
 * @param version   BOOTLOADER_INVALID_VERSION if the standalone
 *                  bootloader is not present, or the version of
 *                  the installed standalone bootloader.
 * @param nodePlat  The value of PLAT on the node.
 * @param nodeMicro The value of MICRO on the node.
 * @param nodePhy   The value of PHY on the node.
 */
void emberGetStandaloneBootloaderInfoReturn(uint16_t version,
                                            uint8_t platformId,
                                            uint8_t microId,
                                            uint8_t phyId);

/** @brief
 * Launch the standalone bootloader (if installed).
 * The function returns an error if the standalone bootloader
 * is not present
 *
 * @param mode Controls the mode in which the standalone
 *             bootloader will run. See the app. note for full
 *             details. Options are:
 *             STANDALONE_BOOTLOADER_NORMAL_MODE: Will listen for
 *             an over-the-air image transfer on the current
 *             channel with current power settings.
 *             STANDALONE_BOOTLOADER_RECOVERY_MODE: Will listen for
 *             an over-the-air image transfer on the default
 *             channel with default power settings. Both modes
 *             also allow an image transfer to begin with
 *             XMODEM over the serial protocol's Bootloader
 *             Frame.
 */
void emberLaunchStandaloneBootloader(uint8_t mode);

/** @brief
 * Provides the result of a call to ::emberLaunchStandaloneBootloader.
 *
 * @param status An EmberStatus value indicating success or the
 *               reason for failure.
 */
void emberLaunchStandaloneBootloaderReturn(EmberStatus status);

/** @brief In a host/NCP setup, inform the NCP to send the network state and
 * version information.
 */
void emberInitHost(void);

/**
 * @brief In a host/NCP setup, get the network parameters, the network status and eui64 all at once.
 */
void emberState(void);

/**
 * @brief In a host/NCP setup, provides the result of a call to emberState() on the host.
 *
 * @param parameters Current network parameters
 * @param localEui64 The EUI64 of the Ember chip
 * @param macExtendedId The extended MAC ID of the Ember chip
 * @param networkStatus The current status of the network
 */
void emberStateReturn(const EmberNetworkParameters *parameters,
                      const EmberEui64 *localEui64,
                      const EmberEui64 *macExtendedId,
                      EmberNetworkStatus networkStatus);

/**
* @brief In a host/NCP setup, notifies the host to changes in the network parameters.
*
* @param parameters Current network parameters
* @param localEui64 The EUI64 of the Ember chip
* @param macExtendedId The extended MAC ID of the Ember chip
* @param networkStatus The current status of the network
*/
void emberHostStateHandler(const EmberNetworkParameters *parameters,
                           const EmberEui64 *localEui64,
                           const EmberEui64 *macExtendedId,
                           EmberNetworkStatus networkStatus);

/** @brief Sets the radio output power at which a node is to operate. Ember 
 * radios have discrete power settings. For a list of available power settings,
 * see the technical specification for the RF communication module in 
 * your Developer Kit. 
 * Note: Care should be taken when using this API on a running network,
 * as it will directly impact the established link qualities neighboring nodes
 * have with the node on which it is called.  This can lead to disruption of
 * existing routes and erratic network behavior.
 * Note: If the requested power level is not available on a given radio, this
 * function will use the next higher available power level.
 *
 * @param power  Desired radio output power, in dBm.
 *
 * @return An ::EmberStatus value indicating the success or
 *  failure of the command.  Failure indicates that the requested power level
 *  is out of range. 
 */
void emberSetRadioPower(int8_t power);

/** @brief Provides the result of a call to emberSetRadioPower() on the host. */
void emberSetRadioPowerReturn(EmberStatus status);

/** @brief Gets the radio output power at which a node is operating. Ember 
 * radios have discrete power settings. For a list of available power settings,
 * see the technical specification for the RF communication module in 
 * your Developer Kit. 
 *
 * @return Current radio output power, in dBm.
 */
void emberGetRadioPower(void);

/** @brief Provides the result of a call to emberGetRadioPower() on the host. */
void emberGetRadioPowerReturn(int8_t power);

/** @brief Enables boost power mode and/or the alternate transmit path.
  *
  * Boost power mode is a high performance radio mode
  * which offers increased transmit power and receive sensitivity at the cost of
  * an increase in power consumption.  The alternate transmit output path allows
  * for simplified connection to an external power amplifier via the
  * RF_TX_ALT_P and RF_TX_ALT_N pins on the em250.  ::emberInit() calls this
  * function using the power mode and transmitter output settings as specified
  * in the MFG_PHY_CONFIG token (with each bit inverted so that the default
  * token value of 0xffff corresponds to normal power mode and bi-directional RF
  * transmitter output).  The application only needs to call
  * ::emberSetTxPowerMode() if it wishes to use a power mode or transmitter output
  * setting different from that specified in the MFG_PHY_CONFIG token.
  * After this initial call to ::emberSetTxPowerMode(), the stack
  * will automatically maintain the specified power mode configuration across
  * sleep/wake cycles.
  *
  * @note This function does not alter the MFG_PHY_CONFIG token.  The
  * MFG_PHY_CONFIG token must be properly configured to ensure optimal radio
  * performance when the standalone bootloader runs in recovery mode.  The
  * MFG_PHY_CONFIG can only be set using external tools.  IF YOUR PRODUCT USES
  * BOOST MODE OR THE ALTERNATE TRANSMITTER OUTPUT AND THE STANDALONE BOOTLOADER
  * YOU MUST SET THE PHY_CONFIG TOKEN INSTEAD OF USING THIS FUNCTION.
  * Contact support@ember.com for instructions on how to set the MFG_PHY_CONFIG
  * token appropriately.
  *
  * @param txPowerMode  Specifies which of the transmit power mode options are
  * to be activated.  This parameter should be set to one of the literal values
  * described in stack/include/ember-types.h.  Any power option not specified 
  * in the txPowerMode parameter will be deactivated.
  * 
  * @return ::EMBER_SUCCESS if successful; an error code otherwise.
  */
EmberStatus emberSetTxPowerMode(uint16_t txPowerMode);

/** @brief Provides the result of a call to emberSetTxPowerMode() on the host. */
void emberSetTxPowerModeReturn(EmberStatus status);

/** @brief Request the current configuration of boost power mode and alternate
 * transmitter output.
 */
void emberGetTxPowerMode(void);

/** @brief Provides the result of a call to emberGetTxPowerMode() on the host.
 * @return the current tx power mode.
 */
void emberGetTxPowerModeReturn(uint16_t txPowerMode);

/** @brief Values of security parameters for use in forming or joining
  * a network. */

/** @brief Structure to hold information about pre-shared network security
 * parameters.
 */
typedef struct {
  EmberKeyData *networkKey;
  uint8_t *presharedKey;
  uint8_t presharedKeyLength;
} EmberSecurityParameters;

/** @brief Define the various options for setting network parameters.
 * Note: Only the EMBER_NETWORK_KEY_OPTION works at this time.
 */
#define EMBER_NETWORK_KEY_OPTION        BIT(0)
#define EMBER_PSK_JOINING_OPTION        BIT(1)
#define EMBER_ECC_JOINING_OPTION        BIT(2)

/** @brief
  * Called before forming or joining.  Fails if already formed or joined
  * or if the arguments are inconsistent with the stack (i.e. if ECC is
  * wanted and we have no ECC).
  *  
  * *** Only the EMBER_NETWORK_KEY_OPTION works at this time. ***
  */
void emberSetSecurityParameters(const EmberSecurityParameters *parameters,
                                uint16_t options);

/** @brief Provides the result of a call to emberSetSecurityParameters(). */
void emberSetSecurityParametersReturn(EmberStatus status);

/** @brief
  * Changes MAC encryption over to the next key.  Fails if there is no
  * next network key.
  *
  */
void emberSwitchToNextNetworkKey(void);

/** @brief Provides the result of a call to emberSwitchToNextNetworkKey(). */
void emberSwitchToNextNetworkKeyReturn(EmberStatus status);

/** @brief
 * This can be stubbed out on the SoC and host app.  It is used by the
 * NCP to update security on the driver when it is instructed to switch
 * the network key by an over the air update.
 *
 */
void emberSwitchToNextNetworkKeyHandler(EmberStatus status);

/** @brief
 *  Gets various versions:
 *  The stack version name (versionName)
 *  The management version number (managementVersionNumber, if applicable,
 *  otherwise set to 0xFFFF)
 *  The stack version number (stackVersionNumber)
 *  The stack build number (stackBuildNumber)
 *  The version type (versionType)
 *  The date / time of the build (buildTimestamp)
 */
void emberGetVersions(void);

/** @brief Provides the result of a call to emberGetVersions(). */
void emberGetVersionsReturn(const uint8_t *versionName,
                            uint16_t managementVersionNumber,
                            uint16_t stackVersionNumber,
                            uint16_t stackBuildNumber,
                            EmberVersionType versionType,
                            const uint8_t *buildTimestamp);

/** @brief
 *  Set the CCA threshold level - the noise floor above which the channel
 *  is normally considered busy. The threshold parameter is expected to be
 *  a signed 2's complement value, in dBm.
 */
void emberSetCcaThreshold(int8_t threshold);

/** @brief Provides the result of a call to emberSetCcaThreshold(). */
void emberSetCcaThresholdReturn(EmberStatus status);

/** @brief
 *  Get the current CCA threshold level.
 */
void emberGetCcaThreshold(void);

/** @brief Provides the result of a call to emberGetCcaThreshold(). */
void emberGetCcaThresholdReturn(int8_t threshold);

/** @brief Application handler to intercept "passthrough" packets and
 * handle them at the application.
 */
void emberMacPassthroughMessageHandler(PacketHeader header);

/** @brief Application handler to define "passthrough" packets.
 */
bool emberMacPassthroughFilterHandler(uint8_t *macHeader);

/** @brief
 *  Read token values stored on the Ember chip.
 */
typedef uint8_t EmberTokenId;

/** @brief
 * Enumerate the various token values that can be retrieved by the
 * application.
 */
enum {
  EMBER_CHANNEL_CAL_DATA_TOKEN // arg needed: channel (uint8_t)
};

/** @brief
 * Gets the indexed token stored in non-volatile memory on the Ember chip.
 * The result is returned depending on the tokenId provided
 * (see enum above) to the appropriate Return() API.
 */
void emberGetIndexedToken(EmberTokenId tokenId, uint8_t index);

/** @brief
 * Gets the token information for tokenId = EMBER_CHANNEL_CAL_DATA_TOKEN
 *
 * @param lna          [msb: cal needed? | bit 0-5: lna tune value]
 * @param tempAtLna    [the temp (degC) when the LNA was calibrated]
 * #param modDac       [msb: cal needed? | bit 0-5: modulation DAC tune value]
 * @param tempAtModDac [the temp (degC) when the mod DAC was calibrated]
 */
void emberGetChannelCalDataTokenReturn(uint8_t lna,
                                       int8_t tempAtLna,
                                       uint8_t modDac,
                                       int8_t tempAtModDac);

/** @brief
 *  Token identifier used when reading and writing manufacturing tokens.
 */
typedef uint8_t EmberMfgTokenId;

/** @brief
 * Enumerate the various manufacturing token values that can be read
 * or written by the application.
 */
enum {
  EMBER_CUSTOM_EUI_64_MFG_TOKEN,
  EMBER_EZSP_STORAGE_MFG_TOKEN,
  EMBER_CTUNE_MFG_TOKEN
};

/** @brief
 * Gets the manufacturer token stored in non-volatile memory on the Ember chip.
 *
 * @param tokenId Which manufacturing token to read.
 */
void emberGetMfgToken(EmberMfgTokenId tokenId);

/** @brief
 * Provides the result of a call to ::emberGetMfgToken.
 *
 * @param tokenId         Which manufacturing token read.
 * @param status          An EmberStatus value indicating success or the
 *                        reason for failure.
 * @param tokenData       The manufacturing token data.
 * @param tokenDataLength The length of the <i>tokenData</i> parameter in
 *                        bytes.
 */
void emberGetMfgTokenReturn(EmberMfgTokenId tokenId,
                            EmberStatus status,
                            const uint8_t *tokenData,
                            uint8_t tokenDataLength);

/** @brief
 * Sets the manufacturer token stored in non-volatile memory on the Ember chip.
 *
 * @param tokenId         Which manufacturing token to set.
 * @param tokenData       The manufacturing token data.
 * @param tokenDataLength The length of the <i>tokenData</i> parameter in
 *                        bytes.
 */
void emberSetMfgToken(EmberMfgTokenId tokenId,
                      const uint8_t *tokenData,
                      uint8_t tokenDataLength);

/** @brief
 * Provides the result of a call to ::emberSetMfgToken.
 *
 * @param tokenId         Which manufacturing token set.
 * @param status An EmberStatus value indicating success or the
 *               reason for failure.
 */
void emberSetMfgTokenReturn(EmberMfgTokenId tokenId,
                            EmberStatus status);
  
/**
* @brief Get the CTUNE value. (Only valid on EFR32)
*/
void emberGetCtune(void);

/** @brief
 * Provides the result of a call to ::emberGetCtune.
 *
 * @param tune   The current CTUNE value.
 * @param status An EmberStatus value indicating success or the
 *               reason for failure.
 */
void emberGetCtuneReturn(uint16_t tune,
                         EmberStatus status);

/**
 * @brief Change the CTUNE value. Involves switching to HFRCO and turning off
 * the HFXO temporarily. (Only valid on EFR32)
 *
 * @param tune   Value to set CTUNE to.
 */
void emberSetCtune(uint16_t tune);

/** @brief
 * Provides the result of a call to ::emberSetCtune.
 *
 * @param status An EmberStatus value indicating success or the
 *               reason for failure.
 */
void emberSetCtuneReturn(EmberStatus status);

/**
 * @brief Register a callback function so that the application can define
 * rules to drop incoming packets.  The callback function MUST be of
 * the form:
 * bool func_name(PacketHeader header, Ipv6Header *ipHeader)
 * {
 *   ...
 * }
 */
void emberRegisterDropIncomingMessageCallback
                   (bool (*drop)(PacketHeader header, Ipv6Header *ipHeader));

/**
 * @brief Register a callback function so that the application can define
 * serial transmit logic.  This should only be used for NCPs, and will have no
 * effect for SoCs.  The callback function MUST be of the form:
 * void uartTransmit(uint8_t type, Buffer b)
 * {
 *   ...
 * }
 */
void emberRegisterSerialTransmitCallback
               (void (*serialTransmit)(uint8_t type, PacketHeader header));

/** @} // END addtogroup
 */

/**
 * @addtogroup ipv6
 *
 * See network-management.h for source code.
 * @{
 */
// IPv6 addressing APIs

/** @brief  The maximum number of IPv6 addresses configured for the device.
 * See ::emberGetLocalIpAddress
 */
// Mesh-local 64 + Link-local 64 + global address table
#define EMBER_MAX_IPV6_ADDRESS_COUNT 10
#define EMBER_MAX_IPV6_GLOBAL_ADDRESS_COUNT (EMBER_MAX_IPV6_ADDRESS_COUNT - 2)
#define EMBER_MAX_IPV6_EXTERNAL_ROUTE_COUNT (EMBER_MAX_IPV6_ADDRESS_COUNT - 2)

/** @brief  Fetches one of the device IPv6 addresses into the supplied pointer.
 * Since there may be multiple addresses, an index argument between 0 and
 * ::EMBER_MAX_IPV6_ADDRESS_COUNT must be supplied.
 *
 * Index 0 contains the mesh-local 64 address of the node.
 * Index 1 contains the link-local 64 address of the node.
 * Index 2 and greater will return any global unicast addresses (GUAs) of this
 * node.
 * 
 * @return false if no IPv6 address is stored at the given index.
 */
bool emberGetLocalIpAddress(uint8_t index, EmberIpv6Address *address);

/** @brief  Fetch the Thread Routing Locator (RLOC)
 *
 * A Thread Routing Locator (RLOC) is an IPv6 address that identifies the
 * location of a Thread interface within a Thread partition.  Thread devices
 * use RLOCs internally for communicating control traffic.
 *
 * PLEASE NOTE:
 * Using RLOCs for application messaging is NOT recommended since device
 * identifiers used to build these RLOC addresses may change at any time
 * based on the current network state.  Please note that message delivery
 * is not guaranteed when an RLOC address is used.
 * 
 * It is recommended that application developers use ::emberGetLocalIpAddress.
 */
void emberGetRoutingLocator(void);

/** @brief Provides the result of a call to ::emberGetRoutingLocator
 *
 * @param rloc The Routing Locator as a full IPv6 address.
 */
void emberGetRoutingLocatorReturn(const EmberIpv6Address *rloc);

/** @brief Border router flags (see Thread spec chapter 5 for more information)
 */
typedef enum {
  EMBER_BORDER_ROUTER_ON_MESH_FLAG         = 0x01,
  EMBER_BORDER_ROUTER_DEFAULT_ROUTE_FLAG   = 0x02,
  EMBER_BORDER_ROUTER_CONFIGURE_FLAG       = 0x04,
  EMBER_BORDER_ROUTER_DHCP_FLAG            = 0x08,
  EMBER_BORDER_ROUTER_SLAAC_FLAG           = 0x10,
  EMBER_BORDER_ROUTER_PREFERRED_FLAG       = 0x20,
  EMBER_BORDER_ROUTER_PREFERENCE_MASK      = 0xC0,
  EMBER_BORDER_ROUTER_HIGH_PREFERENCE      = 0x40,
  EMBER_BORDER_ROUTER_MEDIUM_PREFERENCE    = 0,
  EMBER_BORDER_ROUTER_LOW_PREFERENCE       = 0xC0,
} EmberBorderRouterTlvFlag_e;

typedef uint8_t EmberBorderRouterTlvFlag;

/** @brief We enforce this limit in order to avoid overflow when converting
 * lifetimes from seconds to milliseconds.
 */
#define EMBER_MAX_LIFETIME_DELAY_SEC ((HALF_MAX_INT32U_VALUE - 1) / 1000)

/** @brief There should be at least half an hour of preferred lifetime remaining
 * in order to advertise a DHCP server.
 */
#define EMBER_MIN_PREFERRED_LIFETIME_SEC 1800

/** @brief Renew when we are down to one minute of valid lifetime.
 */
#define EMBER_MIN_VALID_LIFETIME_SEC 60

/** @brief Configures border router behavior, such as whether this device has a
 * default route to the Internet, and whether it have a prefix that can be used
 * by network devices to configure routable addresses.
 *
 * Also de-configures a border router prefix (if it exists), if the prefix and
 * prefixLengthInBits are specified, and the borderRouterFlags argument is 0xFF.
 * 
 * This triggers an address configuration change on the border router, and the
 * application is informed of this by ::emberAddressConfigurationChangeHandler.
 *
 * Note:  If the application wants to manually configure an address and not have
 * the stack create one, then it should pass in the entire IPv6 address (in
 * bytes) for the prefix argument, with prefixLengthInBits as 128.
 * 
 * Examples:
 *
 * SLAAC:
 * -----
 * To configure a valid SLAAC border router, use:
 * EMBER_BORDER_ROUTER_SLAAC_FLAG
 * | EMBER_BORDER_ROUTER_DEFAULT_ROUTE_FLAG
 *
 * The preference of the SLAAC prefix can be set using 
 * EMBER_BORDER_ROUTER_HIGH_PREFERENCE or EMBER_BORDER_ROUTER_LOW_PREFERENCE
 * 
 * NOTE: Preferred and valid lifetimes are ignored for SLAAC prefixes.
 *       -------------------------------------------------------------
 *
 * Configuring a SLAAC prefix will trigger 
 * ::emberAddressConfigurationChangeHandler on other nodes that may choose to 
 * configure a SLAAC address.
 *
 * DHCP:
 * ----
 * To configure a valid DHCP border router, use:
 * EMBER_BORDER_ROUTER_DHCP_FLAG
 * | EMBER_BORDER_ROUTER_DEFAULT_ROUTE_FLAG)
 * 
 * EMBER_BORDER_ROUTER_CONFIGURE_FLAG may be set if this border router is a
 * DHCP server that supplies other configurationd data, such as the identity
 * of DNS servers.
 * 
 * The DHCP preferred lifetime should be configured to lie between
 * ::EMBER_MIN_PREFERRED_LIFETIME_SEC and ::EMBER_MAX_LIFETIME_DELAY_SEC.
 * 
 * The DHCP valid lifetime should be configured to lie between
 * ::EMBER_MIN_VALID_LIFETIME_SEC and ::EMBER_MAX_LIFETIME_DELAY_SEC.
 *
 * Configuring a DHCP prefix will trigger ::emberDhcpServerChangeHandler and 
 * other devices may choose to request a DHCP address by calling 
 * ::emberRequestDhcpAddress. If they get an address, they are informed 
 * via ::emberAddressConfigurationChangeHandler.
 *
 * @param borderRouterFlags   See ::EmberBorderRouterTlvFlag
 * @param isStable            If true, the border router that uses this prefix
 *                            offers a route that is expected to be available
 *                            for at least MIN_STABLE_LIFETIME (168) hours.
 * @param prefix              Prefix for the border router.
 * @param prefixLengthInBits  Prefix length in bits.
 * @param domainId            Domain ID.
 * @param preferredLifetime   Ignored for SLAAC.
 *                            Should be between EMBER_MIN_PREFERRED_LIFETIME_SEC
 *                            and EMBER_MAX_LIFETIME_DELAY_SEC for DHCP.
 * @param validLifetime       Ignored for SLAAC.
 *                            Should be between EMBER_MIN_VALID_LIFETIME_SEC
 *                            and EMBER_MAX_LIFETIME_DELAY_SEC for DHCP.
 */
void emberConfigureGateway(uint8_t borderRouterFlags,
                           bool isStable,
                           const uint8_t *prefix,
                           const uint8_t prefixLengthInBits,
                           uint8_t domainId,
                           uint32_t preferredLifetime,
                           uint32_t validLifetime);

/** @brief Provides the result of a call to ::emberConfigureGateway */
void emberConfigureGatewayReturn(EmberStatus status);

// This should be changed to a boolean in the emberConfigureExternalRoute API.
#define EMBER_EXTERNAL_ROUTE_TEMPORARY_FLAG 0x01

/** @brief Defines an external route set, a route for a Thread network IPv6
 * packet that must traverse a border router and be forwarded to an exterior
 * network.
 *
 * Also de-configures an external route prefix (if it exists), if the 
 * extRoutePrefix and extRoutePrefixLengthInBits arguments are specified, and 
 * the externalRouteFlags argument is 0xFF.
 *
 * @param extRouteFlags               Used for two flags:
 *                                    EMBER_EXTERNAL_ROUTE_TEMPORARY_FLAG and
 *                                    EMBER_BORDER_ROUTER_PREFERENCE_*
 * @param extRoutePrefix              Prefix for the route
 * @param extRoutePrefixLengthInBits  Prefix length in bits
 * @param extRouteDomainId            Domain ID
 */
void emberConfigureExternalRoute(uint8_t extRouteFlags,
                                 const uint8_t *extRoutePrefix,
                                 uint8_t extRoutePrefixLengthInBits,
                                 uint8_t extRouteDomainId);

/** @brief Provides the result of a call to ::emberConfigureExternalRoute */
void emberConfigureExternalRouteReturn(EmberStatus status);

/** @brief
 * Address configuration flags.
 * These flags denote the properties of a Thread IPv6 address.
 *
 * The EMBER_GLOBAL_ADDRESS_AM_ flags are set for a border router that is
 * supplying prefixes.
 * 
 * The rest of the EMBER_GLOBAL_ADDRESS_ flags are set for prefixes that
 * have been administered on other devices.
 *
 * EMBER_LOCAL_ADDRESS is supplied if this a Thread mesh-local or link-local
 * IPv6 address.  No other flags are set in this case.
 */

typedef enum {
  // Flags for the device supplying prefixes.
  EMBER_GLOBAL_ADDRESS_AM_GATEWAY      = 0x01,
  EMBER_GLOBAL_ADDRESS_AM_DHCP_SERVER  = 0x02,
  EMBER_GLOBAL_ADDRESS_AM_SLAAC_SERVER = 0x04,

  // Flags for administered prefixes.
  EMBER_GLOBAL_ADDRESS_DHCP       = 0x08,
  EMBER_GLOBAL_ADDRESS_SLAAC      = 0x10,
  EMBER_GLOBAL_ADDRESS_CONFIGURED = 0x20,

  // DHCP request status flags
  EMBER_GLOBAL_ADDRESS_REQUEST_SENT   = 0x40,
  EMBER_GLOBAL_ADDRESS_REQUEST_FAILED = 0x80,

  EMBER_LOCAL_ADDRESS                 = 0xFF
} LocalServerFlag_e;

typedef uint8_t LocalServerFlag;

/** @brief
 * This is called when a new address is configured on the application.
 *
 * If addressFlags is EMBER_LOCAL_ADDRESS, it means that the address configured
 * is a Thread-local address.
 *
 * Otherwise, it means that the address assigned is a global address (DHCP or
 * SLAAC).
 *
 * In either case, if the valid lifetime is zero, then the address is no
 * longer available.
 *
 * @param address            the address
 * @param preferredLifetime  the preferred lifetime of the address (in seconds)
 * @param validLifetime      the valid lifetime of the address (in seconds)
 * @param addressFlags       address configuration flags (see LocalServerFlag_e)
 */
void emberAddressConfigurationChangeHandler(const EmberIpv6Address *address,
                                            uint32_t preferredLifetime,
                                            uint32_t validLifetime,
                                            uint8_t addressFlags);

/** @brief
 * Returns the list of global prefixes that we know about.
 *
 * ::emberGetGlobalPrefixReturn callbacks contain information about the border 
 * routers.
 *
 * Once all valid entries have been returned, an extra zeroed-out entry is
 * returned to indicate completion.
 */
void emberGetGlobalPrefixes(void);

/** @brief Provides the result of a call to ::emberGetGlobalPrefix
 *
 * @param borderRouterFlags   Border router flags (EMBER_BORDER_ROUTER_*)
 * @param isStable            Stable or temporary prefix
 * @param prefix              Border router prefix
 * @param prefixLengthInBits  Prefix length in bits
 * @param domainId            Provisioning domain ID
 * @param preferredLifetime   Preferred lifetime (in seconds)
 * @param validLifetime       Valid lifetime (in seconds)
 */
void emberGetGlobalPrefixReturn(uint8_t borderRouterFlags,
                                bool isStable,
                                const uint8_t *prefix,
                                uint8_t prefixLengthInBits,
                                uint8_t domainId,
                                uint32_t preferredLifetime,
                                uint32_t validLifetime);

/** @brief
 * Returns the list of DHCP clients if we are DHCP server.
 *
 * ::emberGetDhcpClientReturn callbacks contain the full IPv6 addresses
 * of our DHCP clients.
 *
 * Once all valid entries have been returned, an extra zeroed-out entry is
 * returned to indicate completion.
 *
 */
void emberGetDhcpClients(void);

/** @brief Provides the result of a call to ::emberGetDhcpClients */
void emberGetDhcpClientReturn(const EmberIpv6Address *address);

/** @brief
 * This is called when the stack knows about a new dhcp server or if
 * a dhcp server has become unavailable.
 *
 * "available" means the DHCP server can offer us an address if
 * requested.
 *
 * @param prefix                  dhcp server prefix
 * @param prefixLengthInBits      length in bits of the prefix
 * @param available               whether this dhcp server is available
 */
void emberDhcpServerChangeHandler(const uint8_t *prefix,
                                  uint8_t prefixLengthInBits,
                                  bool available);

/** @brief
 * The application can choose to request a new DHCP address when it is
 * informed via ::emberDhcpServerChangeHandler of an available DHCP server.
 *
 * The application can also call ::emberGetGlobalPrefixes to look for
 * DHCP servers that it can request for an address.
 * 
 * When the address is obtained, the application is informed of this via
 * ::emberAddressConfigurationChangeHandler.
 *
 * @param prefix                  dhcp server prefix
 * @param prefixLengthInBits      length in bits of the prefix
 */
void emberRequestDhcpAddress(const uint8_t *prefix, uint8_t prefixLengthInBits);

/** @brief
 * Provides the result of a call to ::emberRequestDhcpAddress
 *
 * This call only indicates the status of the request (EMBER_ERR_FATAL if no
 * DHCP server is found, and EMBER_SUCCESS otherwise).  The assigned IPv6
 * address is returned via ::emberAddressConfigurationChangeHandler
 *
 * @param status                  Status of DHCP Address Request
 * @param prefix                  Prefix requested in ::emberRequestDhcpAddress
 * @param prefixLengthInBits      Prefix length in bits requested in 
 *                                ::emberRequestDhcpAddress
 */
void emberRequestDhcpAddressReturn(EmberStatus status,
                                   const uint8_t *prefix,
                                   uint8_t prefixLengthInBits);

/** @brief
 * This is called when the stack knows about a new SLAAC prefix or if
 * a SLAAC server has become unavailable.
 *
 * "available" means we can configure a SLAAC address.
 *
 * @param prefix                  SLAAC prefix
 * @param prefixLengthInBits      length in bits of the prefix
 * @param available               whether we can configure an address
 */
void emberSlaacServerChangeHandler(const uint8_t *prefix,
                                   uint8_t prefixLengthInBits,
                                   bool available);

/** @brief
 * The application can choose to request a new SLAAC address when it is
 * informed via ::emberSlaacServerChangeHandler of an available SLAAC prefix.
 *
 * The application can also call ::emberGetGlobalPrefixes to look for
 * SLAAC prefixes that it can use to configure an address.
 *
 * If the application wants to manually configure an address and not have
 * the stack create one, then it should pass in the entire IPv6 address (in
 * bytes) for the prefix argument, with prefixLengthInBits as 128.
 *
 * When the address is obtained, the application is informed of this via
 * ::emberAddressConfigurationChangeHandler.
 *
 * @param prefix                  SLAAC prefix
 * @param prefixLengthInBits      length in bits of the prefix
 */
void emberRequestSlaacAddress(const uint8_t *prefix, uint8_t prefixLengthInBits);

/** @brief
 * Provides the result of a call to ::emberRequestSlaacAddress
 *
 * This call only indicates the status of the request (EMBER_ERR_FATAL if no
 * SLAAC server is found, and EMBER_SUCCESS otherwise).  The assigned IPv6
 * address is returned via ::emberAddressConfigurationChangeHandler
 *
 * @param status                  Status of SLAAC Address Request
 * @param prefix                  Prefix requested in ::emberRequestSlaacAddress
 * @param prefixLengthInBits      Prefix length in bits requested in 
 *                                ::emberRequestSlaacAddress
 */
void emberRequestSlaacAddressReturn(EmberStatus status,
                                    const uint8_t *prefix,
                                    uint8_t prefixLengthInBits);

/** @brief
 * Returns the list of global addresses configured on this device.
 *
 * ::emberGetGlobalAddressReturn callbacks contain information about these
 * global addresses.
 *
 * Once all valid entries have been returned, an extra zeroed-out entry is
 * returned to indicate completion.
 *
 * @param prefix                  Address prefix
 * @param prefixLengthInBits      length in bits of the prefix
 */
void emberGetGlobalAddresses(const uint8_t *prefix, uint8_t prefixLengthInBits);

/** @brief Provides the result of a call to ::emberGetGlobalAddresses
 *
 * @param address             IPv6 global address
 * @param preferredLifetime   Preferred lifetime (in seconds)
 * @param validLifetime       Valid lifetime (in seconds)
 * @param addressFlags        Address configuration flags (EMBER_GLOBAL_ADDRESS_*) 
 */
void emberGetGlobalAddressReturn(const EmberIpv6Address *address,
                                 uint32_t preferredLifetime,
                                 uint32_t validLifetime,
                                 uint8_t addressFlags);

/** @brief
 * Resigns this IPv6 global address from this node.  If this is a DHCP address,
 * then the server is informed about it.  If it is a SLAAC address, we remove it
 * locally.
 */
void emberResignGlobalAddress(const EmberIpv6Address *address);

/** @brief
 * Provides the result of a call to emberResignGlobalAddress().
 */
void emberResignGlobalAddressReturn(EmberStatus status);

/** @brief
 * This is called when the stack knows about a border router that has
 * an external route to a prefix.
 *
 * @param prefix                  external route prefix
 * @param prefixLengthInBits      length in bits of the prefix 
 * @param available               whether this external route is available.
 */
void emberExternalRouteChangeHandler(const uint8_t *prefix,
                                     uint8_t prefixLengthInBits,
                                     bool available);

/** @brief
 * This is called when the stack receives new Thread Network Data.  The 
 * networkData argument may be NULL, in which case ::emberGetNetworkData
 * can be used to obtain the new Thread Network Data.
 *
 * @param networkData             the Network Data
 * @param length                  length in bytes of the Network Data
 */
void emberNetworkDataChangeHandler(const uint8_t *networkData, uint16_t length);

/** @brief
 * Called to obtain the current Thread Network Data.
 *
 * @param networkDataBuffer       the Network Data will be copied to here
 * @param bufferLength            length in bytes of the buffer
 */
void emberGetNetworkData(uint8_t *networkDataBuffer, uint16_t bufferLength);

/** @brief
 * Provides the result of a call to ::emberGetNetworkData.
 *
 * The status value is one of:
 *  - EMBER_SUCCESS
 *  - EMBER_NETWORK_DOWN
 *  - EMBER_BAD_ARGUMENT (the supplied buffer was too small)
 *
 * @param status
 * @param networkData             location of the Network Data
 * @param dataLength              length in bytes of the Network Data
 */
void emberGetNetworkDataReturn(EmberStatus status,
                               uint8_t *networkData, 
                               uint16_t bufferLength);

/** @} // END addtogroup
 */

/**
 * @addtogroup form_and_join
 *
 * See network-management.h for source code.
 * @{
 */

/** @brief  The following denote which network parameters to use when forming
 *  or joining a network.  Construct an uint16_t "options" flag for use in various
 *  network formation calls.
 */
#define EMBER_USE_DEFAULTS            0
#define EMBER_NETWORK_ID_OPTION       BIT(0)
#define EMBER_ULA_PREFIX_OPTION       BIT(1)
#define EMBER_EXTENDED_PAN_ID_OPTION  BIT(2)
#define EMBER_PAN_ID_OPTION           BIT(3)
#define EMBER_NODE_TYPE_OPTION        BIT(4)
#define EMBER_TX_POWER_OPTION         BIT(5)
#define EMBER_MASTER_KEY_OPTION       BIT(6)
#define EMBER_LEGACY_ULA_OPTION       BIT(7)
#define EMBER_JOIN_KEY_OPTION         BIT(8)

/** @brief  Configures network parameters.
 * 
 * This assigns the network configuration values that will be used when the
 * device forms or joins a network.
 *
 * This function may only be called when the network status is EMBER_NO_NETWORK.
 * If the node was previously part of a network, use ::emberResumeNetwork() to
 * recover after a reboot.  To forget the network and return to a status
 * of EMBER_NO_NETWORK, please read cautions for ::emberResetNetworkState(). 
 *
 * @param parameters Some parameters may be supplied by the caller.
 *
 * @param options A bitmask indicating which network parameters are being
 * supplied by the caller.  The following list enumerates the options that
 * can be set.
 *
 * - EMBER_NETWORK_ID_OPTION
 * - EMBER_EXTENDED_PAN_ID_OPTION
 * - EMBER_ULA_PREFIX_OPTION
 * - EMBER_MASTER_KEY_OPTION
 */
void emberConfigureNetwork(const EmberNetworkParameters *parameters,
                           uint16_t options);

/** @brief  Forms a new network.
 * 
 * The forming node chooses a random extended pan id, network ula prefix,
 * and pan id for the new network.  It peforms an energy scan of the channels 
 * indicated by the channelMask argument and selects the one with lowest 
 * detected energy.  It performs an active scan on that channel to ensure 
 * there is no pan id conflict.  The status of whether the form process
 * was initiated is reported to the application via ::emberFormNetworkReturn().
 * Changes to the network status resulting from the form process are reported
 * to the application via ::emberNetworkStatusHandler().
 *
 * This function may only be called when the network status is EMBER_NO_NETWORK,
 * and when a scan is not underway.
 * If the node was previously part of a network, use ::emberResumeNetwork() to
 * recover after a reboot.  To forget the network and return to a status
 * of EMBER_NO_NETWORK, please read cautions for ::emberResetNetworkState(). 
 *
 * @param parameters Some parameters may be supplied by the caller.
 *
 * @param options A bitmask indicating which network parameters are being
 * supplied by the caller.  The following list enumerates the allowed options
 * and the default value used if the option is not specified:
 * - EMBER_NETWORK_ID_OPTION  default: ember
 * - EMBER_ULA_PREFIX_OPTION  default: random
 * - EMBER_NODE_TYPE_OPTION   default: EMBER_ROUTER
 * - EMBER_TX_POWER_OPTION    default: 3
 *
 * @param channelMask A mask indicating the channels to be scanned.  
 * See ::emberStartScan() for format details.
 */
void emberFormNetwork(const EmberNetworkParameters *parameters,
                      uint16_t options,
                      uint32_t channelMask);

/** @brief A callback that indicates whether a prior call to
 * ::emberFormNetwork() successfully initiated the form process.
 * The status argument is either EMBER_SUCCSS, or EMBER_INVALID_CALL
 * if resume was called when the network status was not EMBER_NO_NETWORK,
 * or a scan was underway.
 */
void emberFormNetworkReturn(EmberStatus status);

/** @brief  Joins an existing network.
 *
 * The joining node performs an active scan of the channels indicated by
 * the channelMask argument.  It looks for networks matching the criteria
 * specified via the supplied parameters, and which currently allow joining.
 * The status of whether the join process was initiated is reported to the
 * application via ::emberResumeNetworkReturn().  Changes to the network
 * status resulting from the join process are reported to the application via
 * ::emberNetworkStatusHandler().
 *
 * This function may only be called when the network status is EMBER_NO_NETWORK,
 * and when a scan is not underway.
 * If the node was previously part of a network, use ::emberResumeNetwork() to
 * recover after a reboot.  To forget the network and return to a status
 * of EMBER_NO_NETWORK, please read cautions for ::emberResetNetworkState(). 
 *
 * @param parameters Some parameters may be supplied by the caller.
 *
 * @param options A bitmask indicating which network parameters are being
 * supplied by the caller.  The following list enumerates the allowed options
 * and the default value used if the option is not specified:
 * - EMBER_NETWORK_ID_OPTION       default: looks for any network id
 * - EMBER_EXTENDED_PAN_ID_OPTION  default: looks for any extended pan id
 * - EMBER_PAN_ID_OPTION           default: looks for any pan id
 * - EMBER_NODE_TYPE_OPTION        default: EMBER_ROUTER
 * - EMBER_TX_POWER_OPTION         default: 3
 * - EMBER_JOIN_KEY_OPTION         default: empty
 *
 * @param channelMask A mask indicating the channels to be scanned.  
 * See ::emberStartScan() for format details.
 */
void emberJoinNetwork(const EmberNetworkParameters *parameters,
                      uint16_t options,
                      uint32_t channelMask);

/** @brief  Joins an already-commissioned network.
 *
 * This function assumes that commissioning data has already been cached
 * via a call to ::emberConfigureNetwork().
 *
 * @param radioTxPower        Desired radio output power, in dBm.
 * @param nodeType            Type of device.
 * @param requireConnectivity If commissioned join fails, specify whether
 *                            this node should start a new fragment.
 *                            Note: The short PAN ID MUST be commissioned
 *                                  if this is true.
 */
void emberJoinCommissioned(int8_t radioTxPower,
                           EmberNodeType nodeType,
                           bool requireConnectivity);

/** @brief  A callback that indicates whether the join process was successfully
 * initiated via a prior call to ::emberJoinNetwork() or
 * ::emberJoinCommissioned().  The possible EmberStatus values are:
 * EMBER_SUCCESS, EMBER_BAD_ARGUMENT, or EMBER_INVALID_CALL (if join was called
 * when the network status was something other than EMBER_NO_NETWORK).
 */
void emberJoinNetworkReturn(EmberStatus status);

/** @brief  Resumes network operation after a reboot of the Ember micro.
 * 
 * If the device was previously part of a network, it will recover its former
 * network parameters including pan id, extended pan id, node type, etc.
 * and resume participation in the network.  The status of whether the resume
 * process was initiated is reported to the application via
 * ::emberResumeNetworkReturn().  Changes to the network status resulting
 * from the resume process are reported to the application via 
 * ::emberNetworkStatusHandler().
 *
 * This function may only be called when the network status is 
 * EMBER_SAVED_NETWORK and when a scan is not underway.
 */
void emberResumeNetwork(void);

/** @brief A callback that indicates whether a prior call to
 * ::emberResumeNetwork() successfully initiated the resume process.
 * The status argument is either EMBER_SUCCESS, or
 * EMBER_INVALID_CALL if resume was called when the network status was
 * not EMBER_SAVED_NETWORK, or while a scan was underway.
 */
void emberResumeNetworkReturn(EmberStatus status);

/** @brief  On an end device, this initiates an attach with any available
 * router-eligible devices in the network.  This call must only be made if the
 * network materials have been pre-commissioned on this device, or if previously
 * completed obtaining the commissioning materials from another device.
 * 
 * The status of whether the attach process was initiated is reported
 * to the application via ::emberAttachToNetworkReturn().  Changes to
 * the network status resulting from the attach process are reported
 * to the application via ::emberNetworkStatusHandler().
 * 
 * This function may only be called when the network status is
 * EMBER_JOINED_NETWORK_NO_PARENT and an attach is not already underway.
 */
void emberAttachToNetwork(void);

/** @brief A callback that indicates whether the attach process was
 * successfully initiated via a prior call to ::emberAttachToNetwork().
 * The status argument is either EMBER_SUCCESS, or EMBER_INVALID_CALL
 * if attach was called when the network status was not
 * EMBER_JOINED_NETWORK_NO_PARENT, or while an attach was underway.
 */
void emberAttachToNetworkReturn(EmberStatus status);

/** @brief Callback that is generated when the host's address changes
 *
 * @param address IP address, 16 bytes
 */
void emberSetAddressHandler(const uint8_t *address);

/** @brief Callback to the IP driver to tell it to change its address
 *
 * @param address IP address, 16 bytes
 */
void emberSetDriverAddressHandler(const uint8_t *address);

/** @brief Callback to tell the host to start security commissioning
 *
 * @param address parent IP address, 16 bytes
 */
void emberStartHostJoinClientHandler(const uint8_t *parentAddress);

/** @brief Callback to the IP driver to tell it the network keys
 *
 * @param sequence sequence number
 * @param masterKey master key, 16 bytes
 * @param sequence2 second sequence number
 * @param masterKey2NotUsed second key, 16 bytes
 */
void emberSetNetworkKeysHandler(uint32_t sequence,
                                const uint8_t *masterKey,
                                uint32_t sequence2,
                                const uint8_t *masterKey2);

/** @brief Callback to provide a commissioner app on the host with the
 * requisite network parameters
 */
void emberSetCommAppParametersHandler(const uint8_t *extendedPanId,
                                      const uint8_t *networkId,
                                      const uint8_t *ulaPrefix,
                                      uint16_t panId,
                                      uint8_t channel,
                                      const EmberEui64 *eui64,
                                      const EmberEui64 *macExtendedId,
                                      EmberNetworkStatus networkStatus);

/** @brief Callback to provide a commissioner app on the host with the
 * requisite active dataset material
 */
void emberSetCommAppDatasetHandler(const uint8_t *activeTimestamp,
                                   const uint8_t *pendingTimestamp,
                                   uint32_t delayTimer,
                                   const uint8_t *securityPolicy,
                                   const uint8_t *channelMask,
                                   const uint8_t *channel);

/** @brief Callback to provide a commissioner app on the host with the
 * requisite security material
 */
void emberSetCommAppSecurityHandler(const uint8_t *masterKey,
                                    uint32_t sequenceNumber);

/** @brief Callback to provide a commissioner app on the host with
 * our mesh local address
 */
void emberSetCommAppAddressHandler(const uint8_t *address);

/** @brief  Change the node type of a joined device.
 *
 * The device must be joined to a network prior to making this call.
 *
 * @param newType  The node type to change to.
 */
void emberChangeNodeType(EmberNodeType newType);

/** @brief Provices the result of a call to emberChangeNodeType():
 * either EMBER_SUCCESS, or EMBER_INVALID_CALL.
 */
void emberChangeNodeTypeReturn(EmberStatus status);
/** @} // END addtogroup
 */

/**
 * @addtogroup commissioning
 *
 * See network-management.h for source code.
 * @{
 */
/** @brief Petitions to make this device the commissioner for the network.
 * This will succeed if there is no active commissioner and fail if there
 * is one.
 * 
 * @param deviceName    A name for this device as a human-readable string.
 *  If this device becomes the commissioner this name is sent to any other
 *  would-be commissioners so that the user can identify the current
 *  commissioner.
 *
 * @param deviceNameLength    The length of the name.
 */
void emberBecomeCommissioner(const uint8_t *deviceName, 
                             uint8_t deviceNameLength);

/** @brief Return call for emberBecomeCommissioner().  The status is
 * EMBER_SUCCESS if a petition was sent or EMBER_ERR_FATAL if some
 * temporary resource shortage prevented doing so.
 */
void emberBecomeCommissionerReturn(EmberStatus status);

/** @brief Causes this device to cease being the active commissioner.  This
 * call always succeeds and has no return.
 *
 * When this call is made, emberCommissionerStatusHandler will not return
 * the EMBER_AM_COMMISSIONER flag anymore.
 */
void emberStopCommissioning(void);      // no return

/** @brief Causes the stack to call emberCommissionerStatusHandler() to
 * report the current commissioner status.  This always succeeds and has
 * no return.
 */
void emberGetCommissioner(void);        // no return

/** @brief Causes the stack to allow a connection to a native commissioner.
 * 
 * @param on  Enable / disable connections to native commissioners
 */
void emberAllowNativeCommissioner(bool on);

/** @brief Provides the result of a call to emberAllowNativeCommissioner():
 * either EMBER_SUCCESS or EMBER_INVALID_CALL.
 */
void emberAllowNativeCommissionerReturn(EmberStatus status);

/** @brief Sets the key that a native commissioner must use in order to
 * establish a connection to a Thread router.
 * 
 * @param commissionerKey        the key
 * @param commissionerKeyLength  key length (should be < 16)
 */
void emberSetCommissionerKey(const int8u *commissionerKey,
                             int8u commissionerKeyLength);

/** @brief Provides the result of a call to emberSetCommissionerKey():
 * either EMBER_SUCCESS or EMBER_INVALID_CALL.
 */
void emberSetCommissionerKeyReturn(EmberStatus status);

/**
 * @brief Flag values for emberCommissionerStatusHandler().
 */
enum {
  EMBER_NO_COMMISSIONER            = 0,
  EMBER_HAVE_COMMISSIONER          = BIT(0),
  EMBER_AM_COMMISSIONER            = BIT(1),
  EMBER_JOINING_ENABLED            = BIT(2),
  EMBER_JOINING_WITH_EUI_STEERING  = BIT(3)
};

/** @brief Reports on the current commissioner state.
 * 
 * @param flags    A combination of zero or more of the following:
 * - EMBER_HAVE_COMMISSIONER           a commissioner is active in the network
 * - EMBER_AM_COMMISSIONER             this device is the active commissioner
 *                                     if emberStopCommissioning is called, then
 *                                     this flag is not returned as we are open
 *                                     to commissioner petitions
 * - EMBER_JOINING_ENABLED             joining is enabled
 * - EMBER_JOINING_WITH_EUI_STEERING   steering data restricts which devices can
 *                                     join.  if not set, no restriction, any
 *                                     device can join (significant only when
 *                                     EMBER_JOINING_ENABLED is set)
 *
 * @param commissionerName    The name of the active commissioner, or
 *                            NULL if there is none or the name is not
 *                            known.
 *
 * @param commissionerNameLength    The length of commissonerName.
 */
void emberCommissionerStatusHandler(uint16_t flags,
                                    const uint8_t *commissionerName,
                                    uint8_t commissionerNameLength);

/**
 * @brief Joining modes, passed to emberSetJoiningMode() on the commissioner.
 * No change takes place until emberSendSteeringData() is called.
 * If steering is used, the EUI-64s of the joining devices should be passed
 * to emberAddSteeringEui64() before calling emberSendSteeringData().
 */
typedef enum {
  /** @brief Disable joining. */
  EMBER_NO_JOINING,
  /** @brief Allow joining, with no steering information. */
  EMBER_JOINING_ALLOW_ALL_STEERING,
  /** @brief Allow joining, clearing steering data. */
  EMBER_JOINING_ALLOW_EUI_STEERING,
  /** @brief Allow joining, clearing steering data. Only the low three bytes
    of EUI-64s will be used for steering. */
  /** Note: This option is deprecated in Thread 1.1. */
  EMBER_JOINING_ALLOW_SMALL_EUI_STEERING,
} EmberJoiningMode;

/** @brief Sets the joining mode, clearing the steering data if steering
 * is to be used.
 * No change takes place until emberSendSteeringData() is called.
 * If steering is used, the EUI-64s of the joining devices should be passed
 * to emberAddSteeringEui64() before calling emberSendSteeringData().
 */

void emberSetJoiningMode(EmberJoiningMode mode, uint8_t length);

/** @brief Adds the given EUI64 to the steering data, if this device
 * is the active commissioner; has no effect otherwise.
 * 
 * The steering data is a Bloom filter for the EUI64s of the
 * devices that are expected to join the network.  Each added EUI64 is
 * passed to a hash function to choose a set of bits in the filter,
 * and those bits are set.  Each potential joiner can then hash their
 * own EUI64 and check if the resulting bits are set in the advertised
 * filter.  If so, the device is (probably) expected to join; if not,
 * it definitely is not expected to join.
 */
void emberAddSteeringEui64(const EmberEui64 *eui64);

/** @brief Sends the current steering data to the network, enabling
 * joining in the process.
 */
void emberSendSteeringData(void);

/** @brief Provides the result of a call to emberSendSteeringData(). */
void emberSendSteeringDataReturn(EmberStatus status);

void emberSetSteeringData(const uint8_t *steeringData, uint8_t steeringDataLength);

// Need a call to turn joining off again.
//void emberRescindSteeringData(void); ?

/** @brief Supplies the commissioner with the key a joining node will be using.
 * 
 * @param eui64     The EUI64 of the next node expected to join.  NULL
 *                  may be used if the EUI64 is not known.
 *
 * @param key       The joining key that the device will be using.
 *
 * @param keyLength The length of the joining key.
 */
void emberSetJoinKey(const EmberEui64 *eui64,
                     const uint8_t *key,
                     uint8_t keyLength);

/** @brief  Enables commissioning on the host.  When a device wants to join
 * a Thread network or is acting as a border router for other devices that
 * are added by an external commissioner, this call is made in order to
 * force the host, rather than the NCP, to do the commissioning and
 * security handshake calculations.
 *
 * For our default Thread Border Router implementation, this is enabled
 * automatically.
 *
 * @param enable   If true, commissioning is done on the host and not the NCP.
 */
void emberEnableHostJoinClient(bool enable);

/** @brief
 * Commission the network.
 *
 * This call must be made prior to calling emberJoinCommissioned().  It
 * will not be successful if the node is already on a network.
 * 
 * All options except panId are REQUIRED.  If a REQUIRED option is not
 * provided, the callback emberJoinNetworkReturn will be sent to the app
 * with an EMBER_BAD_ARGUMENT status.
 *
 * Notes:  The fallbackChannelMask is ignored in this current version.
 *         In order to scan all channels, set preferredChannel to 0.
 *
 * @param preferredChannel    [the preferred channel]
 * @param fallbackChannelMask [the fallback channel mask]
 * @param networkId           [the network ID]
 * @param networkIdLength     [the string length of networkId]
 * @param panId               [the short pan ID]
 * @param ulaPrefix           [the 8-byte ULA prefix]
 * @param extendedPanId       [the 8-byte extended pan ID]
 * @param key                 [the master key]
 * @param keySequence         [starting key sequence, default: 0]
 */
void emberCommissionNetwork(uint8_t preferredChannel,
                            uint32_t fallbackChannelMask,
                            const uint8_t *networkId,
                            uint8_t networkIdLength,
                            uint16_t panId,
                            const uint8_t *ulaPrefix,
                            const uint8_t *extendedPanId,
                            const EmberKeyData *key,
                            uint32_t keySequence);

/**
 * @brief Provides the result of a call to emberCommissionNetwork.
 *
 * Returns EMBER_SUCCESS if successful
 *         EMBER_BAD_ARGUMENT if any of the options are wrong
 *         EMBER_INVALID_CALL if the node is already on a network
 *
 * @param status Whether the call to emberCommissionNetwork was successful
 */
void emberCommissionNetworkReturn(EmberStatus status);

/** @} // END addtogroup
 */

/** 
 * @brief  Deprecated, not for use by Thread networks.
 * Tells the stack to allow other nodes to join the network
 * with this node as their parent.  Joining is initially disabled by default.
 * This function may only be called when the network status is EMBER_JOINED.
 *
 * @param durationSeconds  A value of 0 disables joining. 
 * Any other value enables joining for that number of seconds.
 */
void emberPermitJoining(uint16_t durationSeconds);

/** @brief Provides the result of a call to emberPermitJoining(). */
void emberPermitJoiningReturn(EmberStatus status);

/** @brief  Informs the application when the permit joining value changes.
 *
 * @param joiningAllowed  Set to true when permit joining is allowed.
 */
void emberPermitJoiningHandler(bool joiningAllowed);

/** @brief Send a custom message from the Host to the NCP
 *
 * @param message message to send
 * @param messageLength length of message
 */
void emberCustomHostToNcpMessage(const uint8_t *message,
                                 uint8_t messageLength);

/** @brief NCP handler called to process a custom message from the Host.
 *
 * @param message message received
 * @param messageLength length of message
 */
void emberCustomHostToNcpMessageHandler(const uint8_t *message,
                                        uint8_t messageLength);

/** @brief Send a custom message from the NCP to the Host
 *
 * @param message message to send
 * @param messageLength length of message
 */
void emberCustomNcpToHostMessage(const uint8_t *message,
                                 uint8_t messageLength);

/** @brief Host handler called to process a custom message from the NCP.
 *
 * @param message message received
 * @param messageLength length of message
 */
void emberCustomNcpToHostMessageHandler(const uint8_t *message,
                                        uint8_t messageLength);

/** @brief Set the EUI.
*
* @param eui64  Value of EUI to be set.
*/
void emberSetEui64(const EmberEui64 *eui64);

/** @brief Send a no-op with data payload from the Host to the NCP.
*
* @param bytes bytes of payload
* @param bytesLength length of payload
*/
void emberHostToNcpNoOp(const uint8_t *bytes, uint8_t bytesLength);

/** @brief Send a no-op with data payload from the NCP to the Host.
*
* @param bytes bytes of payload
* @param bytesLength length of payload
*/
void emberNcpToHostNoOp(const uint8_t *bytes, uint8_t bytesLength);


/** @brief A callback invoked when the leader data changes.
 *
 * @param leaderData the leader data
 */
void emberLeaderDataHandler(const uint8_t *leaderData);

/** @brief Get a Network Data TLV.
 *
 * @param type the type for requested TLV
 * @param index if there are multiple TLVs of the given type then this value
 * indicates which one to return. A value of 0 will return the first TLV of
 * the given type.
 */
void emberGetNetworkDataTlv(uint8_t type, uint8_t index);

/** @brief Provides the result of a call to emberGetNetworkDataTlv().
 *
 * @param type the type of TLV returned. This is the same value as
 * the value specified in the emberGetNetworkDataTlv() call.
 * @param index the instance number of the TLV. This is the same value as
 * the value specified in the emberGetNetworkDataTlv() call.
 * @param versionNumber the network data version
 * @param tlv the TLV corresponding to type or NULL.
 * @param tlvLength length of tlv
 */
void emberGetNetworkDataTlvReturn(uint8_t type,
                                  uint8_t index,
                                  uint8_t versionNumber,
                                  const uint8_t *tlv,
                                  uint8_t tlvLength);

/** @brief
 * Test command. Echo data to the NCP.
 */
void emberEcho(const uint8_t *data, uint8_t length);
void emberEchoReturn(const uint8_t *data, uint8_t length);

/** @brief Sent from the NCP to the host when an assert occurs.
 */
void emberAssertInfoReturn(const uint8_t *fileName, uint32_t lineNumber);

/** Need documentation comments for these.
*/
void emberStartXonXoffTest(void);
bool emberPing(const uint8_t *destination,
               uint16_t id,
               uint16_t sequence,
               uint16_t length,
               uint8_t hopLimit);
void emberEnableNetworkFragmentation(void);
void emberHostJoinClientComplete(uint32_t keySequence,
                                 const uint8_t *key,
                                 const uint8_t *ulaPrefix);

/** @brief Network data values.
 */
#define EMBER_NETWORK_DATA_LEADER_SIZE 8

/** @brief Maximum size of a string written by ::emberIpv6AddressToString,
 * including a NUL terminator.  It is sufficient to store the string
 * "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff" and a NUL terminator.
 */
#define EMBER_IPV6_ADDRESS_STRING_SIZE 40

/** @brief Maximum size of a string written by ::emberIpv6PrefixToString,
 * including a NUL terminator.  It is sufficient to store the string
 * "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff/128" and a NUL terminator.
 */
#define EMBER_IPV6_PREFIX_STRING_SIZE 44

/** @brief Number of bits in an IPv6 address. */
#define EMBER_IPV6_BITS 128

/** @brief Number of bytes in an IPv6 address. */
#define EMBER_IPV6_BYTES (EMBER_IPV6_BITS / 8)

/** @brief Number of fields in an IPv6 address. */
#define EMBER_IPV6_FIELDS (EMBER_IPV6_BYTES / 2)

/** @brief Converts an ::EmberIpv6Address to a NUL-terminated string.
 *
 * @param src the ::EmberIpv6Address to convert
 * @param dst the buffer where the string will be written
 * @param dstSize the size of buffer in bytes
 *
 * @return true if the address was converted.
 */
bool emberIpv6AddressToString(const EmberIpv6Address *src,
                              uint8_t *dst,
                              size_t dstSize);

/** @brief Converts an ::EmberIpv6Address and prefix length to a NUL-terminated
 *  string.
 *
 * @param src the ::EmberIpv6Address to convert
 * @param srcPrefixBits the size of the prefix in bits
 * @param dst the buffer where the string will be written
 * @param dstSize the size of buffer in bytes
 *
 * @return true if the prefix was converted.
 */
bool emberIpv6PrefixToString(const EmberIpv6Address *src,
                             uint8_t srcPrefixBits,
                             uint8_t *dst,
                             size_t dstSize);

/** @brief Converts a NUL-terminated string to an ::EmberIpv6Address.
 *
 * @param src the string to convert
 * @param dst the ::EmberIpv6Address where the address will be written
 *
 * @return true if the string was converted.
 */
bool emberIpv6StringToAddress(const uint8_t *src, EmberIpv6Address *dst);

/** @brief Converts a NUL-terminated string to an ::EmberIpv6Address with a
 *  prefix length.
 *
 * @param src the string to convert
 * @param dst the ::EmberIpv6Address where the address will be written
 * @param dstPrefixBits the number of prefix bits in the string
 *
 * @return true if the string was converted.
 */
bool emberIpv6StringToPrefix(const uint8_t *src,
                             EmberIpv6Address *dst,
                             uint8_t *dstPrefixBits);

/** @brief Checks an ::EmberIpv6Address to see if it is set to all zeroes
 * which represents an unspecified address.
 *
 * @param address the ::EmberIpv6Address to check
 *
 * @return true if the address is all zeroes (unspecified address).
 */
bool emberIsIpv6UnspecifiedAddress(const EmberIpv6Address *address);

/** @brief Checks an ::EmberIpv6Address to see if it is all zeroes, except
 * the last byte, which is set to one, representing the loopback address
 *
 * @param address the ::EmberIpv6Address to check
 *
 * @return true if the address is zero for bytes 0-14 and one for byte 15
 */
bool emberIsIpv6LoopbackAddress(const EmberIpv6Address *address);

/** @brief Enables or disables Radio HoldOff support
 *
 * @param enable  When true, configures ::RHO_GPIO in BOARD_HEADER
 * as an input which, when asserted, will prevent the radio from
 * transmitting.  When false, configures ::RHO_GPIO for its original
 * default purpose.
 */
void emberSetRadioHoldOff(bool enable);

/** @brief
 * Provides the result of a call to ::emberSetRadioHoldOff.
 *
 * @param status An EmberStatus value indicating success or the
 *               reason for failure. EMBER_SUCCESS if Radio HoldOff
 *               was configured as desired or EMBER_BAD_ARGUMENT
 *               if requesting it be enabled but RHO has not been
 *               configured by the BOARD_HEADER.
 */
void emberSetRadioHoldOffReturn(EmberStatus status);

/** @brief
 * Fetch whether packet traffic arbitration is enabled or disabled.
 */
void emberGetPtaEnable(void);

/** @brief
 * Provides the result of a call to ::emberGetPtaEnable.
 *
 * @param enabled When true, indicates packet traffic arbitration
 * is enabed. When false, indicates packet traffic arbitration is
 * disabled.
 */
void emberGetPtaEnableReturn(bool enabled);

/** @brief
 * Enable or disable packet traffic arbitration
 *
 * @param enabled When true, enables packet traffic arbitration.
 * When false, disables packet traffic arbitration.
 */
void emberSetPtaEnable(bool enabled);

/** @brief
 * Provides the result of a call to ::emberSetPtaEnable.
 *
 * @param status An EmberStatus value indicating success or the
 *               reason for failure.
 */
void emberSetPtaEnableReturn(EmberStatus status);

/** @brief
 * Fetch packet traffic arbitration configuration options.
 */
void emberGetPtaOptions(void);

/** @brief
 * Provides the result of a call to ::emberGetPtaOptions.
 *
 * @param indicates packet traffic arbitration options
 * bit field.
 * Field                              Bit Position      Size(bits)
 * RX retry timeout ms                0                 8
 * Enable ack radio holdoff           8                 1
 * Abort mid TX if grant is lost      9                 1
 * TX request is high priority        10                1
 * RX request is high prioirity       11                1
 * RX retry request is high priority  12                1
 * RX retry request is enabled        13                1
 * Radio holdoff is enabled           14                1
 * Toggle request on mac retransmit   15                1
 * Reserved                           16                15
 * Hold request across CCA failures   31                1
 */
void emberGetPtaOptionsReturn(uint32_t options);

/** @brief
 * Configure packet traffic arbitration options
 *
 * @param indicates packet traffic arbitration options
 * bit field.
 * Field                              Bit Position      Size(bits)
 * RX retry timeout ms                0                 8
 * Enable ack radio holdoff           8                 1
 * Abort mid TX if grant is lost      9                 1
 * TX request is high priority        10                1
 * RX request is high prioirity       11                1
 * RX retry request is high priority  12                1
 * RX retry request is enabled        13                1
 * Radio holdoff is enabled           14                1
 * Toggle request on mac retransmit   15                1
 * Reserved                           16                15
 * Hold request across CCA failures   31                1
 */
void emberSetPtaOptions(uint32_t options);

/** @brief
 * Provides the result of a call to ::emberSetPtaOptions.
 *
 * @param status An EmberStatus value indicating success or the
 *               reason for failure.
 */
void emberSetPtaOptionsReturn(EmberStatus status);

/** @brief
 * Fetch the current antenna mode.
 */
void emberGetAntennaMode(void);

/** @brief
 * Provides the result of a call to ::emberGetAntennaMode.
 *
 * @param status An EmberStatus value indicating success or the
 *               reason for failure.
 * @param mode the current antenna mode, 0-primary, 
 *             1-secondary, 2-toggle on tx ack fail
 */
void emberGetAntennaModeReturn(EmberStatus status,
                               uint8_t mode);

/** @brief 
 * Configure the antenna mode.
 *
 * @param mode 0-primary, 1-secondary, 2-toggle on tx ack fail
 */
void emberSetAntennaMode(uint8_t mode);

/** @brief
 * Provides the result of a call to ::emberSetAntennaMode.
 *
 * @param EMBER_SUCCESS if antenna mode is configured as desired
 * or EMBER_BAD_ARGUMENT if antenna mode is not supported.
 */
void emberSetAntennaModeReturn(EmberStatus status);

/** @brief
 * Gets a true random number out of radios that support this.
 * This will typically take a while, and so should be used to
 * seed a PRNG and not as a source of random numbers for regular 
 * use.
 * 
 * @param count - the count of uint16_t values to be returned.
 */
void emberRadioGetRandomNumbers(uint8_t count);

/** @brief
 * Provides the result of a call to ::emberRadioGetRandomNumbers.
 *
 * @param status An EmberStatus value indicating success or the
 *               reason for failure. When EMBER_SUCCESS is returned 
 *               ::rn and ::count will contain valid data.  ::rn 
                 and ::count are undefined when EMBER_SUCCESS is not
 *               returned.
 * @param rn the uint16_t random values
 * @param count - the count of uint16_t values located at ::rn
 */
void emberRadioGetRandomNumbersReturn(EmberStatus status, 
                                      const uint16_t *rn, 
                                      uint8_t count);

#endif
