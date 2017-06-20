// File: coap-stack.c
//
// Description: CoAP stack functionality
//
// Copyright 2015 Silicon Laboratories, Inc.                                *80*

#include PLATFORM_HEADER
#include "stack/core/ember-stack.h"
#include "hal/hal.h"
#include "stack/include/error.h"
#include "stack/framework/buffer-management.h"
#include "stack/framework/ip-packet-header.h"
#include "stack/ip/6lowpan-header.h"
#include "stack/ip/dispatch.h"
#include "stack/ip/ip-header.h"
#include "stack/ip/ip-address.h"
#include "stack/ip/udp.h"
#include "stack/ip/tcp.h"
#include "app/coap/coap.h"
#include "app/coap/coap-stack.h"
#include "app/ip-ncp/uart-link-protocol.h"
#include "stack/framework/event-queue.h"
#include "stack/ip/zigbee/join.h"
#include "stack/ip/tls/dtls-join.h"

// for lose()
#include "stack/ip/tls/debug.h"

static EmberStatus submitMessage(PacketHeader header, uint16_t delay)
{
  Ipv6Header ipHeader = {0};
  assert(emFetchIpv6Header(header, &ipHeader));
  assert(emFetchUdpHeader(&ipHeader));

  // We have to do NO_LOOPBACK because our outgoing messages are strung
  // out across multiple buffers and the code that processes the incoming
  // messages assumes that they arrive in a single buffer.  If we need to
  // get loopbacks we will need to change the code that handles the incoming
  // messages, or arrange for outgoing messages to be in a single buffer.
  if (emReallySubmitIpHeader(header,
                             &ipHeader,
                             emIsUnicastForMe(ipHeader.destination)
                             ? ALLOW_LOOPBACK
                             : NO_LOOPBACK,
                             USE_DEFAULT_RETRIES,
                             delay)) {
    return EMBER_SUCCESS;
  }

  return EMBER_ERR_FATAL;
}

bool emCoapDtlsTransmitHandler(const uint8_t *payload,
                               uint16_t payloadLength,
                               const EmberIpv6Address *localAddress,
                               uint16_t localPort,
                               const EmberIpv6Address *remoteAddress,
                               uint16_t remotePort,
                               void *transmitHandlerData)
{
  if (emSecurityToUart) {
    // DTLS is handled on the host
    Buffer buffer = emAllocateBuffer(QUEUE_STORAGE_INDEX + 1
                                     + 1 // for the dtls handle
                                     + payloadLength);
    if (buffer == NULL_BUFFER) {
      return false;
    }
    uint8_t *finger = emGetBufferPointer(buffer) + QUEUE_STORAGE_INDEX + 1;
    *finger++ = (uint8_t) (unsigned long) transmitHandlerData; // the dtls handle
    MEMCOPY(finger, payload, payloadLength);
    if (emSerialTransmitCallback != NULL) {
      emSerialTransmitCallback(UART_LINK_TYPE_COMMISSIONER_DATA, buffer);
    }
    return true;
  } else {
    UdpConnectionData *data = 
      emFindConnectionFromHandle((unsigned long) transmitHandlerData);
    if (data == NULL) {
      return false;
    }
    return (emTlsSendBufferedApplicationData((TcpConnection *) data,
                                             payload,
                                             payloadLength,
                                             NULL_BUFFER,
                                             0)
            == EMBER_SUCCESS);
  }
}

EmberStatus emApiFinishCoapMessage(CoapMessage *coapMessage,
                                   uint8_t *coapHeader,
                                   uint16_t coapHeaderLength,
                                   Buffer payloadBuffer,
                                   Buffer *headerLoc)
{
  uint16_t payloadLength = emTotalPayloadLength(payloadBuffer);
  Buffer header = NULL_BUFFER;
  EmberStatus result;
  bool isJoinerEntrust = emIsJoinerEntrustMessage(coapMessage);
  if (coapMessage->transmitHandler != NULL
      && ! isJoinerEntrust) {
    uint16_t extraLength = emTotalPayloadLength(payloadBuffer);
    if (coapMessage->type == COAP_TYPE_CONFIRMABLE
        || extraLength != 0) {
      header = emAllocateBuffer(coapHeaderLength + extraLength);
      if (header == NULL_BUFFER) {
        emLog(COAP, "Can't make header");
        return EMBER_NO_BUFFERS;
      }
      uint8_t *contents = emGetBufferPointer(header);
      MEMCOPY(contents, coapHeader, coapHeaderLength);
      emCopyFromLinkedBuffers(contents + coapHeaderLength,
                              payloadBuffer,
                              extraLength);
      coapHeader = contents;
      coapHeaderLength += extraLength;
    }
    result = 
      coapMessage->transmitHandler(coapHeader,
                                   coapHeaderLength,
                                   &coapMessage->localAddress,
                                   coapMessage->localPort,
                                   &coapMessage->remoteAddress,
                                   coapMessage->remotePort,
                                   coapMessage->transmitHandlerData)
      ? EMBER_SUCCESS
      : EMBER_ERR_FATAL;
  } else {
    uint8_t tag = HEADER_TAG_NONE;
    uint16_t ipHeaderOptions = IP_HEADER_NO_OPTIONS;
    
    if (isJoinerEntrust) {
      tag = HEADER_TAG_COMMISSIONING_KEY;
      ipHeaderOptions = IP_HEADER_LL64_SOURCE | IP_HEADER_ONE_HOP;
      payloadLength = 0;        // don't include the commissioning key
    }

    Ipv6Header ipHeader;
    header = 
      emMakeTaggedUdpHeader(&ipHeader,
                            tag,
                            ipHeaderOptions,
                            // Don't use the original destination (local)
                            // address as a source when responding to a
                            // multicast.
                            ((emIsMemoryZero(coapMessage->localAddress.bytes, 16)
                              || emIsMulticastAddress(coapMessage->localAddress.bytes))
                             ? NULL
                             : coapMessage->localAddress.bytes),
                            coapMessage->remoteAddress.bytes,
                            IPV6_DEFAULT_HOP_LIMIT,
                            (coapMessage->localPort == 0
                             ? EMBER_COAP_PORT
                             : coapMessage->localPort),
                            (coapMessage->remotePort == 0
                             ? EMBER_COAP_PORT
                             : coapMessage->remotePort),
                            coapHeader,
                            coapHeaderLength,
                            payloadLength);
    if (header == NULL_BUFFER) {
      emLog(COAP, "Can't make header");
      // This could have failed due to any number of reasons in the above call.
      return EMBER_ERR_FATAL;
    }
    
    if (payloadBuffer != NULL_BUFFER
        && emGetBufferLength(payloadBuffer) > 0) {
      emSetPayloadLink(header, payloadBuffer);
    }
    
    // Spec says joiner entrust should be delayed for 50ms.
    uint16_t delay = ((isJoinerEntrust
                       && coapMessage->type != COAP_TYPE_ACK)
                      ? 50
                      : NO_DELAY);
    
    result = submitMessage(header, delay);
  }
  
  if (emLogIsActive(COAP)) {
    if (result == EMBER_SUCCESS) {
      uint8_t convertedUri[100];
      if (! emberCoapDecodeUri(coapMessage->encodedUri,
                               coapMessage->encodedUriLength,
                               convertedUri,
                               sizeof(convertedUri))) {
        convertedUri[0] = '?';
        convertedUri[1] = 0;
      }

      emLogLine(COAP,
                "TX message id: %u | "
                "uri: %s | "
                "type: %s | "
                "code: %s | "
                "port: %d | "
                "to: %b | "
                "bytes: %b | "
                "extra: %b",
                coapMessage->messageId,
                convertedUri,
                emGetCoapTypeName(coapMessage->type),
                emGetCoapCodeName(coapMessage->code),
                coapMessage->remotePort,
                coapMessage->remoteAddress.bytes,
                16,
                coapMessage->payload,
                coapMessage->payloadLength,
                emGetBufferPointer(payloadBuffer),
                emGetBufferLength(payloadBuffer));
    } else {
      emLogLine(COAP, "TX failed");
    }
  }

  if (result == EMBER_SUCCESS) {
    *headerLoc = header;
  }
  return result;
}

EmberStatus emApiRetryCoapMessage(CoapRetryEvent *event)
{
  if (event->transmitHandler == NULL
      || event->transmitHandler == &emJoinerEntrustTransmitHandler) {
    return submitMessage(event->packetHeader, NO_DELAY);
  } else {
    return (event->transmitHandler(emGetBufferPointer(event->packetHeader),
                                   emGetBufferLength(event->packetHeader),
                                   &event->localAddress,
                                   event->localPort,
                                   &event->remoteAddress,
                                   event->remotePort,
                                   event->transmitHandlerData)
            ? EMBER_SUCCESS
            : EMBER_ERR_FATAL);
  }
}

// This called from dispatch.c for packets that were MAC-secured using a
// commissioning key.

void emHandleIncomingCoapCommission(PacketHeader header, Ipv6Header *ipHeader)
{
  // TODO: check the destination address and hop count as well.
  uint8_t *data = ipHeader->transportPayload;
  uint16_t dataLength = ipHeader->transportPayloadLength;
  CoapMessage coapMessage;

  emLogLine(COAP, "RX commission");
  if (! emParseCoapMessage(data, dataLength, &coapMessage)) {
    emLogBytesLine(COAP, "commission fail", data, dataLength);
    loseVoid(COAP);
  }

  EmberCoapRequestInfo info;
  MEMSET(&info, 0, sizeof(info));

  MEMCOPY(info.localAddress.bytes, ipHeader->destination, 16);
  MEMCOPY(info.remoteAddress.bytes, ipHeader->source, 16);
  info.localPort = ipHeader->destinationPort;
  info.remotePort = ipHeader->sourcePort;

  EmberCoapReadOptions options;
  emInitCoapReadOptions(&options, 
                        coapMessage.encodedOptions,
                        coapMessage.encodedOptionsLength);

  if (coapMessage.type == COAP_TYPE_ACK
      || coapMessage.type == COAP_TYPE_RESET) {
    emNoteCoapAck(coapMessage.messageId);
    return;
  } else if (coapMessage.code != EMBER_COAP_CODE_POST) {
    loseVoid(COAP);
  }

  uint8_t uri[128];
  if (sizeof(uri) < emberReadUriPath(&options, uri, sizeof(uri))) {
    // what to do?  
  }

  // The key is removed when this message is processed, so we need to
  // grab it first.
  Buffer keyBuffer = NULL_BUFFER;
  uint8_t *ackKey = emGetCommissioningMacKey(emMacDestinationPointer(header));
  if (ackKey != NULL) {
    keyBuffer = emFillBuffer(ackKey, 16);
  }

  emCoapCommissionRequestHandler(coapMessage.code,
                                 uri,
                                 &options,
                                 coapMessage.payload,
                                 coapMessage.payloadLength,
                                 &info);

  if (coapMessage.type == COAP_TYPE_CONFIRMABLE
      && keyBuffer != NULL_BUFFER) {
    coapMessage.type = COAP_TYPE_ACK;
    coapMessage.code = EMBER_COAP_CODE_204_CHANGED;
    MEMCOPY(&coapMessage.remoteAddress, ipHeader->source, 16);
    coapMessage.payload = NULL;
    coapMessage.payloadLength = 0;
    coapMessage.remotePort = info.remotePort;
    coapMessage.localPort = info.localPort;
    coapMessage.encodedOptions = NULL;
    coapMessage.encodedOptionsLength = 0;
    coapMessage.transmitHandler = &emJoinerEntrustTransmitHandler;
    emSubmitCoapMessage(&coapMessage, NULL, keyBuffer);
  }
}

static bool retryEventPredicate(Event *event, void *senderEui64)
{
  CoapRetryEvent *retryEvent = ((CoapRetryEvent *) event);
  return ((retryEvent->transmitHandler ==
           &emJoinerEntrustTransmitHandler)
          && (MEMCOMPARE(senderEui64,
                         emMacDestinationPointer(retryEvent->packetHeader),
                         8)
              == 0));
}

uint8_t *emGetOutgoingCommissioningMacKey(const uint8_t *senderEui64)
{
  CoapRetryEvent *event = (CoapRetryEvent *)
    emberFindEvent(&emStackEventQueue,
                   &emCoapRetryEventActions,
                   retryEventPredicate,
                   (void *)senderEui64);
  if (event == NULL) {
    return NULL;
  } else {
    emberEventSetDelayMs((Event *) event, COAP_ACK_TIMEOUT_MS);
    return emGetBufferPointer(emGetPayloadLink(event->packetHeader));
  }
}

void emCoapStackIncomingMessageHandler(PacketHeader header,
                                       Ipv6Header *ipHeader,
                                       EmberCoapTransmitHandler transmitHandler,
                                       void *transmitHandlerData,
                                       CoapRequestHandler handler)
{
  EmberCoapRequestInfo info;
  MEMSET(&info, 0, sizeof(info));
  MEMCOPY(&info.localAddress, ipHeader->destination, sizeof(EmberIpv6Address));
  MEMCOPY(&info.remoteAddress, ipHeader->source, sizeof(EmberIpv6Address));
  info.remotePort = ipHeader->sourcePort;
  info.localPort = ipHeader->destinationPort;
  info.transmitHandler = transmitHandler;
  info.transmitHandlerData = transmitHandlerData;
  emProcessCoapMessage(header,
                       ipHeader->transportPayload,
                       ipHeader->transportPayloadLength,
                       (handler == NULL
                        ? (CoapRequestHandler) &emApiCoapRequestHandler
                        : handler),
                       &info);
}

