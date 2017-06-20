/**
 * @file icmp.h
 * @brief Simple ICMP API
 *
 * <!--Copyright 2013 by Silicon Labs. All rights reserved.              *80*-->
 */

/**
 * @addtogroup icmp
 *
 * See icmp.h for source code.
 * @{
 */

/** @brief  Set up a listener for ICMP messages for the given address.
 *
 * @param address The IPV6 address that we're listening for messages on.
 *
 * @return EMBER_SUCCESS    if successful
 *         EMBER_TABLE_FULL if we failed to set up a listener
 *         EMBER_ERR_FATAL  other fatal failure
 */
EmberStatus emberIcmpListen(const uint8_t *address);

/** @brief  Send an ICMP ECHO REQUEST message.
 *
 * @param destination     IPV6 destination address
 * @param id              IPV6 unique id
 * @param sequence        IPV6 unique sequence
 * @param length          payload length
 * @param hopLimit        IPV6 hop limit
 *
 * @return EMBER_SUCCESS    if successful
 *         EMBER_NO_BUFFERS if we failed to allocate a buffer
 *         EMBER_ERR_FATAL  other fatal failure
 */
bool emberIpPing(uint8_t *destination,
                 uint16_t id,
                 uint16_t sequence,
                 uint16_t length,
                 uint8_t hopLimit);

/** @brief  Application callback for an incoming ICMP message.
 *
 * @param ipHeader        Pointer to an IPV6 buffer
 */
void emberIncomingIcmpHandler(Ipv6Header *ipHeader);

/** @} // END addtogroup
 */

