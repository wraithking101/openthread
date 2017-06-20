// File: data-client.c
//
// Description: host end of IP modem data stream
//
// Copyright 2013 by Silicon Laboratories. All rights reserved.             *80*

void emHostIpv6Dispatch(const uint8_t *packet,
                        SerialLinkMessageType type,
                        uint16_t length);

// Callback used to send packets down to the NCP.  The app and
// driver each has its own definition of this.
void emSendHostIpv6(SerialLinkMessageType type,
                    const uint8_t *packet,
                    uint16_t length);
