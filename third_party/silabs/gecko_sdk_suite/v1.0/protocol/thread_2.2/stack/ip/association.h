/*
 * File: association.h
 * Description: Beacon and MAC command handler support
 *
 * Copyright 2015 by Ember Corporation. All rights reserved.                *80*
 */

extern uint8_t emBeaconPayloadBuffer[];
extern uint8_t emBeaconPayloadSize;

void emMacCommandHandler(PacketHeader header);

