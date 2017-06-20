/*
 * File: network-fragmentation.h
 * Description: Network fragmentation functionality.
 * Author(s): Nate Smith, nathan.smith@silabs.com
 *
 * Copyright 2014 by Silicon Laboratories. All rights reserved.             *80*
 */

/*

Network Fragmentation
---------------------

Network fragments are identified by a five byte network fragment identifier
(NFI), called the "island id" here.

The first four bytes of the network fragment identifier are random. The
remaining byte is a weighting byte which indicates the desirability of the
leader. The NFI is created when a leader creates a network fragment. The NFI is
stored in the child ID response during the initial MLE handshake, and also in
the leader TLV in the network data.

When a router has not heard an updated leader for a while (120 seconds), it
enters a listening stage where it listens for beacons that contain NFIs that are
better than its own. The amount of time spent in the listening stage is a
jittered value between 0 and 10 seconds. If the router hears a better network
fragment, it will attempt to join to the router that transmitted the beacon. If
the router does not hear a better NFI before the listening stage expires, it
will create its own network fragment.

Routers will forget their children when joining or forming new fragments. When
this occurs their children will eventually realize their parent is missing, and
each will attempt to find a new parent.

Note that nodes cannot send application messages to nodes on different network
fragments.

*/

#define emWasOnDifferentFragment() (emPreviousNodeId != 0xFFFE)

extern EmberNodeId emPreviousNodeId;

extern bool emForceIslandId;
extern uint8_t emForcedIslandId[];

extern uint8_t emFragmentState;

typedef enum {
  FRAGMENT_IDLE                    = 0,
  FRAGMENT_FIND_PARENT             = 1,
} fragmentStates;

void emFragmentInit(void);

#define emIsRepairingFragment() (emFragmentState != FRAGMENT_IDLE)

void emStartLeaderTimeout(void);
void emStopLeaderTimeout(void);

void emStartNewIsland(void);
void emJoinBetterIsland(void);
void emFillIslandId(uint8_t *islandId, uint8_t weight);

bool emIsIslandIdBetter(const uint8_t *first,
                        bool firstIsSingleton,
                        const uint8_t *second,
                        bool secondIsSingleton);

bool emIsIslandIdBetterThanMine(const uint8_t *islandId,
                                bool isSingleton);

#define emIsIslandIdEqual(left, right)            \
  (MEMCOMPARE(left, right, ISLAND_ID_SIZE) == 0)

// Check that sequence > emRipIdSequenceNumber
#define emIsRipIdSequenceBetter(sequence)                           \
  (! timeGTorEqualInt8u(emRipIdSequenceNumber, (sequence)))

void emBlacklistMyIslandId(void);
boolean emIsBlacklistedIslandId(const int8u *id);
bool emIsCandidateIslandId(const uint8_t *otherIslandId,
                           bool otherIslandSize,
                           uint8_t sequence);

void emSetIslandId(const uint8_t* Id);

void emIslandInit(void);

// debugging hook
uint8_t *emLocalIslandId(void);

// 250 to convert from ms to seconds, and four beacon periods.  Worst
// case of having only one neighbor this gives us about three beacons
// to miss.  Makes you want to ping an upstream node when the timeout
// starts getting stale.
#define LEADER_TIMEOUT_MS (MLE_MAX_ADVERTISEMENT_INTERVAL_MS * 4)

#define FRAGMENT_REPAIR_SCAN_TIMEOUT_MS 1200
// Allow time for:
// - request to dark router to upgrade (.5 s)
// - time for dark router to upgrade (1 s)
// - time for rip id sequence to propagate to us (2 s)
#define FRAGMENT_REPAIR_REQUEST_TIMEOUT_MS 4000

#define JOIN_PARTITION_JITTER_MS 2000

// equals LEADER_TIMEOUT_MS by default
extern uint32_t emLeaderTimeoutMs;
