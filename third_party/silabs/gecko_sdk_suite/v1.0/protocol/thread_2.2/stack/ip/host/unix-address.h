// File: unix-address.h
//
// Description: Unix host address functionality
//
// Copyright 2013 by Silicon Laboratories. All rights reserved.             *80*

struct ifaddrs;
struct sockaddr_storage;

bool emPopulateSockAddr(struct sockaddr_storage *address,
                           const uint8_t *hostString,
                           int family,
                           int streamType,
                           int protocol);

uint8_t emGetIpv6Addresses(struct ifaddrs *addresses[],
                         uint8_t addressesCount,
                         struct ifaddrs **fullIfAddrs);

bool emChooseInterface(uint8_t optionalInterfaceChoice,
                          const uint8_t *optionalPrefixChoice,
                          bool produceOutput);

bool emSetIpv6Address(EmberIpv6Address *target, const uint8_t *remoteAddress);

extern EmberIpv6Address emMyIpAddress;
