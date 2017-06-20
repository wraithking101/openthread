/** @file ncp-mfglib.h
 *  
 * <!--Copyright 2013 by Silicon Labs. All rights reserved.              *80*-->
 */

#ifndef __NCP_MFGLIB_H__
#define __NCP_MFGLIB_H__

void tmspNcpMfglibStart(bool requestRxCallback);
void tmspNcpMfglibStartTestReturn(EmberStatus status);
void tmspNcpMfglibRxReturn(const uint8_t *payload,
                           uint8_t payloadLength,
                           uint8_t lqi,
                           int8_t rssi);
void tmspNcpMfglibEnd(void);
void tmspNcpMfglibEndTestReturn(EmberStatus status,
                                uint32_t mfgReceiveCount);
void tmspNcpMfglibStartActivity(uint8_t type);
void tmspNcpMfglibStartReturn(uint8_t type,
                              EmberStatus status);
void tmspNcpMfglibStopActivity(uint8_t type);
void tmspNcpMfglibStopReturn(uint8_t type,
                             EmberStatus status);
void tmspNcpMfglibSendPacket(const uint8_t *packet, uint8_t packetLength, uint16_t repeat);
void tmspNcpMfglibSendPacketEventHandler(EmberStatus status);
void tmspNcpMfglibSet(uint8_t type, uint16_t arg1, int8_t arg2);
void tmspNcpMfglibSetReturn(uint8_t type,
                            EmberStatus status);
void tmspNcpMfglibGet(uint8_t type);
void tmspNcpMfglibGetChannelReturn(uint8_t channel);
void tmspNcpMfglibGetPowerReturn(int8_t power);
void tmspNcpMfglibGetPowerModeReturn(uint16_t txPowerMode);
void tmspNcpMfglibGetSynOffsetReturn(int8_t synOffset);
void tmspNcpMfglibTestContModCal(uint8_t channel, uint32_t duration);

#endif // __NCP_MFGLIB_H__

