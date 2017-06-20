/*
 * File: tls-session-state.h
 * Description: saving TLS sessions
 * Author(s): Richard Kelsey
 *
 * Copyright 2011 by Ember Corporation. All rights reserved.                *80*
 */

extern bool emUsePresharedTlsSessionState;

void emSaveTlsState(Ipv6Address *remoteIpAddress,
                    TlsSessionState *session);

bool emRestoreTlsSession(TlsSessionState *session,
                            Ipv6Address *remoteIpAddress,
                            uint8_t *sessionId,
                            uint16_t sessionIdLength);
