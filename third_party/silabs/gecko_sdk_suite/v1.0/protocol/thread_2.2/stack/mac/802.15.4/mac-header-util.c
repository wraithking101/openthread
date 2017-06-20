/*
 * File: mac-header-util.c
 * Description: emMacHeaderLength()
 * Author(s): Richard Kelsey, kelsey@ember.com
 *
 * Copyright 2009 by Ember Corporation. All rights reserved.                *80*
 */

#include "core/ember-stack.h"
#include "mac/mac-header.h"
#include "stack/ip/zigbee/join.h"

//------------------------------------------------------------------------------

// I removed this from framework/packet-header.c in order to make it
// available to applications that did not use linked buffers.

// 802.15.4 header size
//
// Base header size is three bytes (two for the frame control and one for
// the sequence number) plus the address information.  The source and
// destination address can be either not present, two bytes, or eight
// bytes.  If the destination is present it will include a two-byte PAN ID.
//
// The addresses are encoded as follows:
//  00 no address
//  01 reserved
//  10 short      4 bytes
//  11 long       10 bytes
//
// If both addresses are present and the the intra-PAN bit is set then
// the source address has no Pan ID.  The numbers in the array assume
// that the intra-PAN bit is set.
//
// The address fields are separated by two reserved bits in the frame control,
// so we have to do some bit manipulation to get the right index.
//
// Reserved address modes give a size of zero.

static PGM uint8_t baseHeaderLength[] = {
         // src dest  
  3,     // 00  00
  0,     // 00  01
  7,     // 00  10
  13,    // 00  11
  0,     // 01  00
  0,     // 01  01
  0,     // 01  10
  0,     // 01  11
  7,     // 10  00
  0,     // 10  01
  9,     // 10  10
  15,    // 10  11
  13,    // 11  00
  0,     // 11  01
  15,    // 11  10
  21     // 11  11
};

// Returns 0xFF if the frame control is invalid.
// It is important to carefully check inegrity, as this function
// is used by the receive ISR before the CRC is checked, so
// it is often operating on a corrupt frame control. -MNP

uint8_t emMacHeaderLength(uint8_t *header)
{
  uint16_t control = emberFetchLowHighInt16u(header);
  uint8_t addressBits = (control >> 10) & 0x33;
  uint8_t length = baseHeaderLength[(addressBits | addressBits >> 2) & 0x0F];
  uint8_t frameType = control & MAC_FRAME_TYPE_MASK;

  if ((frameType & MAC_FRAME_RESERVED_BITS)
      || length == 0)
    return 0xFF;

  // Add in space for a PAN ID for inter-PAN destinations.
  if (! (control & MAC_FRAME_FLAG_INTRA_PAN)
      && (addressBits & 0x03)
      && (addressBits & 0x30))
    length += 2;
  
//  fprintf(stderr, "[%X: emMacHeaderLength() control %X -> length %d (source %s, dest %s, %s-pan)]\n",
//          simulatorId,
//          control,
//          length,
//          (((control & MAC_FRAME_SOURCE_MODE_MASK) ==
//            MAC_FRAME_SOURCE_MODE_SHORT)
//           ? "short"
//           : (((control & MAC_FRAME_SOURCE_MODE_MASK) ==
//               MAC_FRAME_SOURCE_MODE_LONG)
//              ? "long"
//              : "none")),
//          (((control & MAC_FRAME_DESTINATION_MODE_MASK) ==
//            MAC_FRAME_DESTINATION_MODE_SHORT)
//           ? "short"
//           : (((control & MAC_FRAME_DESTINATION_MODE_MASK) ==
//               MAC_FRAME_DESTINATION_MODE_LONG)
//              ? "long"
//              : "none")),
//          (control & MAC_FRAME_FLAG_INTRA_PAN) ? "intra" : "inter");

  return length;
}

