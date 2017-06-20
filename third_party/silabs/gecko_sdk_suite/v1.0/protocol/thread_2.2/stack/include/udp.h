/**
 * @file udp.h
 * @brief Simple UDP API
 *
 * <!--Copyright 2013 by Silicon Labs. All rights reserved.              *80*-->
 */

/**
 * @addtogroup udp
 *
 * See udp.h for source code.
 * @{
 */

/** @brief  Set up a listener for UDP messages for the given address.
 *
 * @param port    Port to bind the UDP address to
 * @param address The IPV6 address that we're listening for messages on.
 *
 * @return EMBER_SUCCESS    if successful
 *         EMBER_TABLE_FULL if we failed to set up a listener
 *         EMBER_ERR_FATAL  other fatal failure
 */
EmberStatus emberUdpListen(uint16_t port, const uint8_t *address);


/** @brief  Send a UDP message.
 *
 * @param destination     IPV6 destination address
 * @param sourcePort      UDP source port
 * @param destinationPort UDP destination port
 * @param payload         UDP transport payload
 * @param payloadLength   payload length
 *
 * @return EMBER_SUCCESS    if successful
 *         EMBER_NO_BUFFERS if we failed to allocate a buffer
 *         EMBER_ERR_FATAL  other fatal failure
 */
EmberStatus emberSendUdp(const uint8_t *destination,
                         uint16_t sourcePort,
                         uint16_t destinationPort,
                         uint8_t *payload,
                         uint16_t payloadLength);

/** @brief  Application callback for an incoming UDP message.
 *
 * @param destination     IPV6 destination address
 * @param source          IPV6 source address
 * @param localPort       UDP source port
 * @param remotePort      UDP destination port
 * @param payload         UDP transport payload
 * @param payloadLength   payload length
 */
void emberUdpHandler(const uint8_t *destination,
                     const uint8_t *source,
                     uint16_t localPort,
                     uint16_t remotePort,
                     const uint8_t *payload,
                     uint16_t payloadLength);

/** @brief  Application callback for an incoming UDP multicast.
 *
 * @param destination     IPV6 destination address
 * @param source          IPV6 source address
 * @param localPort       UDP source port
 * @param remotePort      UDP destination port
 * @param payload         UDP transport payload
 * @param payloadLength   payload length
 */
void emberUdpMulticastHandler(const uint8_t *destination,
                              const uint8_t *source,
                              uint16_t localPort,
                              uint16_t remotePort,
                              const uint8_t *payload,
                              uint16_t payloadLength);
/** @} // END addtogroup
 */

