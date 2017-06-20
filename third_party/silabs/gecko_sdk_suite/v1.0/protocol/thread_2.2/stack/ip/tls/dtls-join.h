/*
 * File: dtls-join.h
 * Description: joining using DTLS
 *
 * Copyright 2015 Silicon Laboratories, Inc.                                *80*
 */

enum {
  DLTS_JOIN_THREAD_COMMISSIONING,       // the default
  DLTS_JOIN_JPAKE_TEST
};

extern uint8_t emDtlsJoinMode;
extern uint16_t emUdpJoinPort;
extern uint16_t emUdpCommissionPort;

#define DTLS_COMMISSION_PORT 49191
#define DTLS_JOIN_PORT 19786

// Thread wants use to require a cookie for DTLS join handshakes.
extern bool emDtlsJoinRequireCookie;

void emDtlsJoinInit(void);

void emSetJoinSecuritySuites(uint16_t suites);

void emCloseDtlsJoinConnection(void);

// Process a message that arrives from a joiner, either directly over the
// radio (relayed == false) or via the commissioning app (relayed == true).
// The value of 'relayed' is used only to determine how to send any
// response.
void emIncomingJoinMessageHandler(PacketHeader header,
                                  Ipv6Header *ipHeader,
                                  bool relayed);

void emHandleJoinDtlsMessage(EmberUdpConnectionData *connection,
                             uint8_t *packet,
                             uint16_t length,
                             Buffer buffer);

bool emStartJoinClient(const uint8_t *address,
                          uint16_t remotePort,
                          const uint8_t *key,
                          uint8_t keyLength);

void emSetCommissionKey(const uint8_t *key, uint8_t keyLength);
void emGetCommissionKey(uint8_t *key);

extern EmberUdpConnectionHandle emParentConnectionHandle;

void emMarkDtlsJoinBuffers(void);

void emDerivePskc(const uint8_t *passphrase,
                  int16_t passphraseLen,
                  const uint8_t *extendedPanId,
                  const uint8_t *networkName,
                  uint8_t *result);
