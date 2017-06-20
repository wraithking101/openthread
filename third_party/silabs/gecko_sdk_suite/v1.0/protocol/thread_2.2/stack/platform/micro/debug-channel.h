/** @file stack/platform/micro/debug-channel.h
* 
* @brief Functions that provide access to the debug channel.
 *
 * <!-- Copyright 2005 by Ember Corporation. All rights reserved.-->   
 */

#ifndef __DEBUG_CHANNEL_H__
#define __DEBUG_CHANNEL_H__

/** @name Debug Channel Functions */

//@{

// The ISA checks that the block size for RAM input and output is always <= 32
// due to the linked buffers of yore.
#define MAX_BLOCK_SIZE 32

uint8_t emDebugAddInitialFraming(uint8_t *buff, uint16_t debugType);
EmberStatus emDebugSend(uint8_t *data, uint8_t *length);

#if DEBUG_LEVEL >= BASIC_DEBUG
EmberStatus emDebugInit(void);
void emDebugPowerDown(void);
void emDebugPowerUp(void);
bool halStackDebugActive(void);
void emDebugReceiveData(void);
#else
#define emDebugInit() do {} while (0)
#define emDebugPowerDown()
#define emDebugPowerUp()
#define emDebugReceiveData()
#endif

//@}  // end of Debug Channel Functions

#endif //__DEBUG_CHANNEL_H__
