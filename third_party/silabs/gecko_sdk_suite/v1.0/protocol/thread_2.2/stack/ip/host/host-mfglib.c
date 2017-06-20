// File: host-mfglib.c
//
// Description: Functions on the host for the manufacturing library, so as to
// test and verify the RF component of products at manufacture time.
//
// Copyright 2013 by Silicon Laboratories. All rights reserved.             *80*

#include PLATFORM_HEADER
#include "stack/include/ember.h"
#include "app/util/serial/command-interpreter2.h"
#include "app/ip-ncp/binary-management.h"
#include "host-mfglib.h"
#include "app/tmsp/tmsp-enum.h"

static void (*hostMfglibRxCallback)(uint8_t *packet, uint8_t linkQuality, int8_t rssi);

// Host -> NCP management commands

void mfglibStart(void (*mfglibRxCallback)(uint8_t *packet,
                                          uint8_t linkQuality,
                                          int8_t rssi))
{
  hostMfglibRxCallback = mfglibRxCallback;
  tmspHostMfglibStart(mfglibRxCallback != NULL);
}

void mfglibEnd(void)
{
  hostMfglibRxCallback = NULL;
  tmspHostMfglibEnd();
}

void mfglibStartTone(void)
{
  tmspHostMfglibStartActivity(TONE);
}

void mfglibStopTone(void)
{
  tmspHostMfglibStopActivity(TONE);
}

void mfglibStartStream(void)
{
  tmspHostMfglibStartActivity(STREAM);
}

void mfglibStopStream(void)
{
  tmspHostMfglibStopActivity(STREAM);
}

void mfglibSendPacket(uint8_t *packet, uint16_t repeat)
{
  tmspHostMfglibSendPacket(packet, packet[0] + 1, repeat);
}

void mfglibSetChannel(uint8_t channel)
{
  tmspHostMfglibSet(CHANNEL, channel, 0);
}

void mfglibGetChannel(void)
{
  tmspHostMfglibGet(CHANNEL);
}

void mfglibSetPower(uint16_t txPowerMode, int8_t power)
{
  tmspHostMfglibSet(POWER, txPowerMode, power);
}

void mfglibGetPower(void)
{
  tmspHostMfglibGet(POWER);
}

void mfglibGetPowerMode(void)
{
  tmspHostMfglibGet(POWER_MODE);
}

void mfglibSetSynOffset(int8_t synOffset)
{
  tmspHostMfglibSet(SYN_OFFSET, 0, synOffset);
}

void mfglibGetSynOffset(void)
{
  tmspHostMfglibGet(SYN_OFFSET);
}

void mfglibTestContModCal(uint8_t channel, uint32_t duration)
{
  tmspHostMfglibTestContModCal(channel, duration);
}

// NCP -> host callback methods

void tmspHostMfglibStartTestReturn(EmberStatus status)
{
  mfglibStartReturn(status);
}

void tmspHostMfglibRxReturn(const uint8_t *payload,
                             uint8_t payloadLength,
                             uint8_t lqi,
                             int8_t rssi)
{
  if (hostMfglibRxCallback) {
    hostMfglibRxCallback((uint8_t *)payload, lqi, rssi);
  }
}

void tmspHostMfglibEndTestReturn(EmberStatus status,
                                  uint32_t mfgReceiveCount)
{
  mfglibEndReturn(status, mfgReceiveCount);
}

void tmspHostMfglibStartReturn(uint8_t type,
                                EmberStatus status)
{
  switch (type) {
  case TONE:
    mfglibStartToneReturn(status);
    break;

  case STREAM:
    mfglibStartStreamReturn(status);
    break;

  default:
   break;
  }
}

void tmspHostMfglibStopReturn(uint8_t type,
                               EmberStatus status)
{
  switch (type) {
  case TONE:
    mfglibStopToneReturn(status);
    break;

  case STREAM:
    mfglibStopStreamReturn(status);
    break;

  default:
   break;
  }
}

void tmspHostMfglibSendPacketEventHandler(EmberStatus status)
{
  mfglibSendPacketReturn(status);
}

void tmspHostMfglibSetReturn(uint8_t type,
                              EmberStatus status)
{
  switch (type) {
  case CHANNEL:
    mfglibSetChannelReturn(status);
    break;

  case POWER:
    mfglibSetPowerReturn(status);
    break;

  default:
   break;
  }
}

void tmspHostMfglibGetChannelReturn(uint8_t channel)
{
  mfglibGetChannelReturn(channel);
}

void tmspHostMfglibGetPowerReturn(int8_t power)
{
  mfglibGetPowerReturn(power);
}

void tmspHostMfglibGetPowerModeReturn(uint16_t txPowerMode)
{
  mfglibGetPowerModeReturn(txPowerMode);
}

void tmspHostMfglibGetSynOffsetReturn(int8_t synOffset)
{
  mfglibGetSynOffsetReturn(synOffset);
}

