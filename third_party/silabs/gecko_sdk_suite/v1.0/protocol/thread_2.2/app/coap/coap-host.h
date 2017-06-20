// File: coap-host.h
//
// Description: CoAP host-specific functionality
//
// Copyright 2015 Silicon Laboratories, Inc.                                *80*

void emCoapHostIncomingMessageHandler(const uint8_t *bytes,
                                      uint16_t bytesLength,
                                      const EmberIpv6Address *localAddress,
                                      uint16_t localPort,
                                      const EmberIpv6Address *remoteAddress,
                                      uint16_t remotePort);
