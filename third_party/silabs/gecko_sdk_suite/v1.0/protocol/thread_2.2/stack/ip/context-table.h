/*
 * File: context-table.h
 * Description: Table of prefixes for use in 6lowpan context compression.
 * Author(s): Matteo Paris, matteo@ember.com
 *
 * Copyright 2010 by Ember Corporation. All rights reserved.                *80*
 */

// Two hours.  Refreshing the context lifetime requires distributing an
// updated set of network data, which we don't want to do any oftener
// than necessary in a stable network.  There are 15 context IDs
// available (the network ULA uses context zero), so there should
// always be plenty available.  There should be no need to time them
// out faster than this.
#define RIP_DEFAULT_CONTEXT_LIFETIME_SEC 7200

// Half an hour.  If the lifetime of a context that is in use drops below
// this, the leader bumps it back up to the default lifetime.
#define RIP_MIN_CONTEXT_LIFETIME_SEC 1800

// Contexts are stored in the over-the-air format used for network data.
// See network-data.h for a description.

#define PREFIX_6CO_ID_MASK       0x0F
#define PREFIX_6CO_COMPRESS_FLAG 0x10

#define MAX_6LOWPAN_HC_CONTEXT_ID 15
#define MAX_6LOWPAN_HC_CONTEXTS   16

// Prefix TLV header + prefix + 6LoWPAN ID TLV header + ID + lifetime
#define MAXIMUM_PREFIX_DATA_SIZE (2 + 16 + 2 + 1 + 2)

void emAddContext(uint8_t id,
                  const uint8_t *prefix,
                  uint8_t prefixLengthInBits,
                  bool compress,
                  uint16_t lifetime);

void emDeleteContext(uint8_t contextId);

uint8_t *emWriteNdContextOptions(uint8_t *finger);

uint8_t *emClearNdContexts(uint8_t *finger);
uint8_t *emReadNdContextOption(uint8_t *finger, uint8_t *option);
bool emSetNdContexts(uint8_t *start, uint8_t *end);

// Copies the context's prefix into 'address', returning the
// number of bits copied.
uint8_t emUncompressContextPrefix(uint8_t context, uint8_t *address);

// Returns the number of bits that matched a context's prefix,
// and the ID of the context.
uint8_t emCompressContextPrefix(const uint8_t *address, 
                                uint8_t matchLength,
                                uint8_t *context);

// Returns a bitmask of the in-use context IDs.
uint16_t emFindAll6lowpanContexts(void);

// This is intended for debugging only.  The prefix pointer will
// become invalid the next time the network data changes, so it should
// be used immediately.
typedef struct {
  uint8_t contextId;
  uint8_t flags;
  uint8_t *prefix;
  uint8_t prefixLengthInBits;
  uint16_t lifetime;
} LowpanContext;
bool emFindContext(uint8_t contextId, LowpanContext *result);

#ifdef EMBER_TEST
// for context-table-test.c
uint8_t *findContext(uint8_t contextId,
                   const uint8_t *address,
                   uint8_t prefixLength,
                   uint8_t requiredFlags);
#endif
