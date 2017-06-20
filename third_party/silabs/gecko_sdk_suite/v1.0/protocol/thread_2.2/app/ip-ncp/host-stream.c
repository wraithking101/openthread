// File: host-stream.c
//
// Description: host message streams 
//
// Copyright 2013 by Silicon Laboratories. All rights reserved.             *80*

#include <sys/select.h>
#include <stdlib.h>
#include <unistd.h>

#include PLATFORM_HEADER
#include "stack/core/ember-stack.h"
#include "hal/hal.h"
#include "stack/ip/ip-header.h"
#include "tool/simulator/child/posix-sim.h"

#include "uart-link-protocol.h"
#include "host-stream.h"

void emRemoveStreamBytes(Stream *stream, int count)
{
  assert(count <= stream->index);
  memmove(stream->buffer, stream->buffer + count, stream->index - count);
  stream->index -= count;

  // help the debugger
  MEMSET(stream->buffer + stream->index,
         0,
         sizeof(stream->buffer) - stream->index);
}  

IpModemReadStatus readIpModemInput(int fd,
                                   Stream *stream,
                                   IpModemMessageHandler *handler)
{
  int got = EMBER_READ(fd,
                       stream->buffer + stream->index,
                       sizeof(stream->buffer) - stream->index);
  IpModemReadStatus result = IP_MODEM_READ_PENDING;

  if (got < 0) {
    return IP_MODEM_READ_IO_ERROR;
  } else if (0 == got) {
      return IP_MODEM_READ_EOF;
  } else {
    stream->index += got;
    // if (0 < got) {
    //   emLogBytesLine(IP_MODEM, "ipModemRead(%d, ...) ",
    //                  stream->buffer,
    //                  stream->index,
    //                  fd);
    // }
    while (4 <= stream->index) {
      result = processIpModemInput(stream, handler);
      if (result != IP_MODEM_READ_PROGRESS) {
        break;
      }
    }
  }

  return result;
}

IpModemReadStatus processIpModemInput(Stream *stream,
                                      IpModemMessageHandler *handler)
{
  IpModemReadStatus result = IP_MODEM_READ_PENDING;
  uint16_t length = emberFetchHighLowInt16u(stream->buffer + 2);
  uint8_t messageType = stream->buffer[1];

  if (stream->buffer[0] != '['
      || MAX_UART_LINK_TYPE < messageType
      || length == 0) {
    // We get here if there is a framing error
    return IP_MODEM_READ_FORMAT_ERROR;
  }

  if (UART_LINK_HEADER_SIZE + length <= stream->index) {
    handler(messageType, stream->buffer + UART_LINK_HEADER_SIZE, length);
    emRemoveStreamBytes(stream, length + UART_LINK_HEADER_SIZE);
    result = IP_MODEM_READ_PROGRESS;      
  }

  return (stream->index == 0
          ? IP_MODEM_READ_DONE
          : result);
}

IpModemReadStatus readIpv6Input(int fd,
                                Stream *stream,
                                SerialLinkMessageType type,
                                Ipv6PacketHandler *handler)
{
  int got = EMBER_READ(fd,
                       stream->buffer + stream->index,
                       sizeof(stream->buffer) - stream->index);
  if (got < 0) {
    return IP_MODEM_READ_IO_ERROR;
  } else {
    stream->index += got;
    return processIpv6Input(stream, type, handler);
  }
}

IpModemReadStatus processIpv6Input(Stream *stream,
                                   SerialLinkMessageType type,
                                   Ipv6PacketHandler *handler)
{
  IpModemReadStatus result = IP_MODEM_READ_PENDING;

  while (IPV6_HEADER_SIZE <= stream->index) {
    uint16_t length =
      IPV6_HEADER_SIZE
      + emberFetchHighLowInt16u(stream->buffer + IPV6_PAYLOAD_LENGTH_INDEX);
    if (length <= stream->index) {
      handler(stream->buffer, type, length);
      emRemoveStreamBytes(stream, length);
      result = IP_MODEM_READ_PROGRESS;
    } else {
      break;
    }
  }
  return (stream->index == 0
          ? IP_MODEM_READ_DONE
          : result);
}

IpModemReadStatus processCommAppJoinerInput(Stream *stream,
                                            SerialLinkMessageType type,
                                            Ipv6PacketHandler *handler)
{
  IpModemReadStatus result = IP_MODEM_READ_PENDING;

  uint16_t length = IPV6_HEADER_SIZE
                    + emberFetchHighLowInt16u(stream->buffer
                                              + stream->index
                                              + IPV6_PAYLOAD_LENGTH_INDEX);
  if (length > IPV6_HEADER_SIZE) {
    handler(stream->buffer + stream->index, type, length);
    stream->index += length;
    emRemoveStreamBytes(stream, length);
    result = IP_MODEM_READ_PROGRESS;
  }
  return (stream->index == 0
          ? IP_MODEM_READ_DONE
          : result);
}
