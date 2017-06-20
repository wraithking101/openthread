/** @file host-mfglib.h
 *  
 * <!--Copyright 2013 by Silicon Labs. All rights reserved.              *80*-->
 */

#ifndef __HOST_MFGLIB_H__
#define __HOST_MFGLIB_H__

void tmspHostMfglibStart(bool requestRxCallback);
void tmspHostMfglibStartTestReturn(EmberStatus status);
void tmspHostMfglibRxReturn(const uint8_t *payload,
                            uint8_t payloadLength,
                            uint8_t lqi,
                            int8_t rssi);
void tmspHostMfglibEnd(void);
void tmspHostMfglibEndTestReturn(EmberStatus status,
                                 uint32_t mfgReceiveCount);
void tmspHostMfglibStartActivity(uint8_t type);
void tmspHostMfglibStartReturn(uint8_t type,
                               EmberStatus status);
void tmspHostMfglibStopActivity(uint8_t type);
void tmspHostMfglibStopReturn(uint8_t type,
                              EmberStatus status);
void tmspHostMfglibSendPacket(const uint8_t *packet, uint8_t packetLength, uint16_t repeat);
void tmspHostMfglibSendPacketEventHandler(EmberStatus status);
void tmspHostMfglibSet(uint8_t type, uint16_t arg1, int8_t arg2);
void tmspHostMfglibSetReturn(uint8_t type,
                             EmberStatus status);
void tmspHostMfglibGet(uint8_t type);
void tmspHostMfglibGetChannelReturn(uint8_t channel);
void tmspHostMfglibGetPowerReturn(int8_t power);
void tmspHostMfglibGetPowerModeReturn(uint16_t txPowerMode);
void tmspHostMfglibGetSynOffsetReturn(int8_t synOffset);
void tmspHostMfglibTestContModCal(uint8_t channel, uint32_t duration);

#endif // __HOST_MFGLIB_H__

