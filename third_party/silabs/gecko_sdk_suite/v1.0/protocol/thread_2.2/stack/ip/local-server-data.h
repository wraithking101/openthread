/*
 * File: local-server-data.h
 * Description: information about this node's servers
 *
 * Copyright 2013 by Silicon Laboratories. All rights reserved.             *80*
 */

extern Buffer emExternalRouteTable;

// Directly from the most that emMakeLocalServerData() can write.
//#define MAX_MY_NETWORK_DATA_SIZE ((2 + 17) + (2 * 12))
// FIXME make 100 for now, find fittier value later
#define MAX_MY_NETWORK_DATA_SIZE 100

void emVerifyLocalServerData(void);
void emInitializeLeaderServerData(void);

// Returns pointer to next byte after data.
uint8_t *emMakeLocalServerData(uint8_t *finger, const uint8_t *end);

bool emRemovePrefixTlv(const uint8_t *prefix, uint8_t prefixLengthInBits);

void emExternalRouteTableInit(void);

bool emHaveExternalRoute(const uint8_t *prefix, 
	                     uint8_t prefixLengthInBits,
	                     bool match);

uint8_t emCountNodeInNetworkData(EmberNodeId nodeId);
uint8_t emCountNodeInLocalServerData(EmberNodeId nodeId);
void emResendNodeServerData(EmberNodeId oldNodeId);
void emRemoveChildServerData(EmberNodeId nodeId);

