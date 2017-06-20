// Copyright 2017 Silicon Laboratories, Inc.

#include PLATFORM_HEADER
#include CONFIGURATION_HEADER
#include EMBER_AF_API_STACK
#include EMBER_AF_API_BUFFER_MANAGEMENT
#include EMBER_AF_API_EVENT_QUEUE
#include EMBER_AF_API_HAL
#include "zcl-core.h"

// TODO: Use an appropriate timeout.
#define DISCOVERY_TIMEOUT_MS (MILLISECOND_TICKS_PER_SECOND)

// When memory for this structure is allocated, enough space is reserved at the
// end to also hold the URI (with NUL terminator), payload, and application
// data.
typedef struct {
  Event event;
  EmberZclNetworkDestination_t destination;
  EmberIpv6Address remoteAddress;
  EmberCoapCode code;
  EmZclMessageResponseHandler handler;
  size_t uriPathLength; // includes NUL terminator
  uint16_t payloadLength;
  uint16_t applicationDataLength;
  //uint8_t uriPath[uriPathLength];
  //uint8_t payload[payloadLength];
  //uint8_t applicationData[applicationDataLength];
} MessageEvent;

static bool getAddress(const EmberZclNetworkDestination_t *destination,
                       EmberIpv6Address *address);
static EmberStatus discoverAddress(const EmberZclNetworkDestination_t *destination);
//static void discoveryResponseHandler(const EmberZclNetworkDestination_t *destination,
//                                     const EmberIpv6Address *address);
static EmberStatus sendCoapMessage(Buffer buffer);
static void coapResponseHandler(EmberCoapStatus status,
                                EmberCoapCode code,
                                EmberCoapReadOptions *options,
                                uint8_t *payload,
                                uint16_t payloadLength,
                                EmberCoapResponseInfo *info);
static void eventHandler(MessageEvent *event);
static void eventMarker(MessageEvent *event);
//static bool destinationPredicate(MessageEvent *event,
//                                 const EmberZclNetworkDestination_t *destination);
static EmberZclMessageStatus_t remapCoapStatus(EmberCoapStatus status);

extern EventQueue emAppEventQueue;
static EventActions actions = {
  .queue = &emAppEventQueue,
  .handler = (void (*)(struct Event_s *))eventHandler,
  .marker = (void (*)(struct Event_s *))eventMarker,
  .name = "zcl core messaging"
};

EmberStatus emZclSend(const EmberZclNetworkDestination_t *destination,
                      EmberCoapCode code,
                      const uint8_t *uriPath,
                      const uint8_t *payload,
                      uint16_t payloadLength,
                      EmZclMessageResponseHandler handler,
                      void *applicationData,
                      uint16_t applicationDataLength)

{
  size_t uriPathLength = strlen((const char *)uriPath) + 1; // include the NUL
  Buffer buffer = emAllocateBuffer(sizeof(MessageEvent)
                                   + uriPathLength
                                   + payloadLength
                                   + applicationDataLength);
  if (buffer == NULL_BUFFER) {
    return EMBER_NO_BUFFERS;
  }
  uint8_t *finger = emGetBufferPointer(buffer);
  MessageEvent *event = (MessageEvent *)(void *)finger;
  event->event.actions = &actions; // may be unused
  event->event.next = NULL;        // may be unused
  event->event.timeToExecute = 0;  // may be unused
  event->destination = *destination;
  MEMSET(&event->remoteAddress, 0, sizeof(EmberIpv6Address)); // filled in later
  event->code = code;
  event->handler = handler;
  event->uriPathLength = uriPathLength;
  event->payloadLength = payloadLength;
  event->applicationDataLength = applicationDataLength;
  finger += sizeof(MessageEvent);
  MEMCOPY(finger, uriPath, event->uriPathLength);
  finger += event->uriPathLength;
  MEMCOPY(finger, payload, event->payloadLength);
  finger += event->payloadLength;
  MEMCOPY(finger, applicationData, event->applicationDataLength);
  //finger += event->applicationDataLength;

  if (getAddress(destination, &event->remoteAddress)) {
    return sendCoapMessage(buffer);
  } else {
    EmberStatus status = discoverAddress(&event->destination);
    if (status == EMBER_SUCCESS) {
      emberEventSetDelayMs((Event *)event, DISCOVERY_TIMEOUT_MS);
    }
    return status;
  }
}

static bool getAddress(const EmberZclNetworkDestination_t *destination,
                       EmberIpv6Address *address)
{
  // If the destination is an address, we can use it directly.  Otherwise, we
  // check the cache for the address corresponding to the key.  If we don't
  // have the address, we will have to discover it.
  if (destination->type == EMBER_ZCL_NETWORK_DESTINATION_TYPE_ADDRESS) {
    MEMCOPY(address, &destination->data.address, sizeof(EmberIpv6Address));
    return true;
  } else {
    EmberZclNetworkDestination_t tmp;
    bool found = emZclCacheGet(destination, &tmp);
    MEMCOPY(address, &tmp.data.address, sizeof(EmberIpv6Address));
    return found;
  }
}

static EmberStatus discoverAddress(const EmberZclNetworkDestination_t *destination)
{
  // TODO.
  //if (destination->type == EMBER_ZCL_NETWORK_DESTINATION_TYPE_UID) {
  //  return emberZclDiscoverIpv6AddressByUid(&destination->data.uid,
  //                                          discoveryResponseHandler);
  //} else if (destination->type == EMBER_ZCL_NETWORK_DESTINATION_TYPE_HOSTNAME) {
  //  return emberZclDiscoverIpv6AddressByHostname(&destination->data.hostname,
  //                                               discoveryResponseHandler);
  //} else {
    return EMBER_BAD_ARGUMENT;
  //}
}

// TODO.
//static void discoveryResponseHandler(const EmberZclNetworkDestination_t *destination,
//                                     const EmberIpv6Address *address)
//{
//  if (destination->type == EMBER_ZCL_NETWORK_DESTINATION_TYPE_UID) {
//    emberZclAddCacheEntry(&destination->data.uid, address);
//  } else if (destination->type == EMBER_ZCL_NETWORK_DESTINATION_TYPE_HOSTNAME) {
//    emberZclAddCacheEntry(&destination->data.hostname, address);
//  }
//
//  MessageEvent *event
//    = (MessageEvent *)emberFindAllEvents(&emAppEventQueue,
//                                         &actions,
//                                         (EventPredicate)destinationPredicate,
//                                         (void *)destination);
//  while (event != NULL) {
//    sendCoapMessage(emBufferPointerToBuffer((uint16_t *)event));
//    event = (MessageEvent *)event->event.next;
//  }
//}

static EmberStatus sendCoapMessage(Buffer buffer)
{
  const uint8_t *finger = emGetBufferPointer(buffer);
  const MessageEvent *event = (const MessageEvent *)(const void *)finger;
  finger += sizeof(MessageEvent);
  const uint8_t *uriPath = finger;
  finger += event->uriPathLength;
  const uint8_t *payload = finger;
  //finger += event->payloadLength;
  //void *applicationData = finger;
  //finger += event->applicationDataLength;

  EmberCoapOption options[] = {
    {EMBER_COAP_OPTION_CONTENT_FORMAT, NULL, 1, EMBER_COAP_CONTENT_FORMAT_CBOR,},
  };
  EmberCoapSendInfo info = {
    .nonConfirmed = false,

    .localAddress = {{0}}, // use default
    .localPort = 0,        // use default
    .remotePort = 0,       // use default

    .options = options,
    .numberOfOptions = COUNTOF(options),

    .responseTimeoutMs = 0, // use default

    .responseAppData = emGetBufferPointer(buffer),
    .responseAppDataLength = emGetBufferLength(buffer),

    .transmitHandler = NULL // unused
  };
  return emberCoapSend(&event->remoteAddress,
                       event->code,
                       uriPath,
                       payload,
                       event->payloadLength,
                       coapResponseHandler,
                       &info);
}

static void coapResponseHandler(EmberCoapStatus status,
                                EmberCoapCode code,
                                EmberCoapReadOptions *options,
                                uint8_t *payload,
                                uint16_t payloadLength,
                                EmberCoapResponseInfo *info)
{
  assert(sizeof(MessageEvent) <= info->applicationDataLength);
  const uint8_t *finger = info->applicationData;
  const MessageEvent *event = (const MessageEvent *)finger;
  finger += sizeof(MessageEvent);
  assert((sizeof(MessageEvent)
          + event->uriPathLength
          + event->payloadLength
          + event->applicationDataLength)
         == info->applicationDataLength);

  // When a message times out, we assume the remote address has changed and
  // needs to be rediscovered, so we remove any key pointing to that address in
  // our cache.  The next attempt to send to one of those keys will result in a
  // rediscovery.
  if (status == EMBER_COAP_MESSAGE_TIMED_OUT) {
    emZclCacheRemoveAllByIpv6Prefix(&event->remoteAddress, EMBER_IPV6_BITS);
  }

  if (event->handler != NULL) {
    //const uint8_t *uriPath = finger;
    finger += event->uriPathLength;
    //const uint8_t *payload = finger;
    finger += event->payloadLength;
    const void *applicationData = finger;
    //finger += event->applicationDataLength;
    (*event->handler)(remapCoapStatus(status),
                      code,
                      payload,
                      payloadLength,
                      applicationData,
                      event->applicationDataLength);
  }
}

static void eventHandler(MessageEvent *event)
{
  if (event->handler != NULL) {
    const uint8_t *finger = (const uint8_t *)event;
    finger += sizeof(MessageEvent);
    //const uint8_t *uriPath = finger;
    finger += event->uriPathLength;
    //const uint8_t *payload = (event->payloadLength == 0 ? NULL : finger);
    finger += event->payloadLength;
    const void *applicationData = finger;
    //finger += event->applicationDataLength;

    (*event->handler)(EMBER_ZCL_MESSAGE_STATUS_DISCOVERY_TIMEOUT,
                      EMBER_COAP_CODE_EMPTY,
                      NULL, // payload
                      0,    // payload length
                      applicationData,
                      event->applicationDataLength);
  }
}

static void eventMarker(MessageEvent *event)
{
}

// TODO.
//static bool destinationPredicate(MessageEvent *event,
//                                 const EmberZclNetworkDestination_t *destination)
//{
//  if (destination->type == event->destination.type) {
//    if (destination->type == EMBER_ZCL_NETWORK_DESTINATION_TYPE_ADDRESS) {
//      return (MEMCOMPARE(&destination->data.address,
//                         &event->destination.data.address,
//                         sizeof(EmberIpv6Address))
//              == 0);
//    } else if (destination->type == EMBER_ZCL_NETWORK_DESTINATION_TYPE_UID) {
//      ...;
//    } else if (destination->type == EMBER_ZCL_NETWORK_DESTINATION_TYPE_HOSTNAME) {
//      return (strcmp((const char *)destination->data.hostname,
//                     (const char *)event->destination.data.hostname)
//              == 0);
//    }
//  }
//  return false;
//}

static EmberZclMessageStatus_t remapCoapStatus(EmberCoapStatus status)
{
  switch (status) {
  case EMBER_COAP_MESSAGE_TIMED_OUT:
    return EMBER_ZCL_MESSAGE_STATUS_COAP_TIMEOUT;
  case EMBER_COAP_MESSAGE_ACKED:
    return EMBER_ZCL_MESSAGE_STATUS_COAP_ACK;
  case EMBER_COAP_MESSAGE_RESET:
    return EMBER_ZCL_MESSAGE_STATUS_COAP_RESET;
  case EMBER_COAP_MESSAGE_RESPONSE:
    return EMBER_ZCL_MESSAGE_STATUS_COAP_RESPONSE;
  default:
    return EMBER_ZCL_MESSAGE_STATUS_NULL;
  }
}

typedef struct {
  EmberZclMessageStatus_t status;
  const uint8_t * const name;
} ZclMessageStatusEntry;
static const ZclMessageStatusEntry statusTable[] = {
  {EMBER_ZCL_MESSAGE_STATUS_DISCOVERY_TIMEOUT, (const uint8_t *)"DISCOVERY TIMEOUT",},
  {EMBER_ZCL_MESSAGE_STATUS_COAP_TIMEOUT,      (const uint8_t *)"COAP TIMEOUT",},
  {EMBER_ZCL_MESSAGE_STATUS_COAP_ACK,          (const uint8_t *)"COAP ACK",},
  {EMBER_ZCL_MESSAGE_STATUS_COAP_RESET,        (const uint8_t *)"COAP RESET",},
  {EMBER_ZCL_MESSAGE_STATUS_COAP_RESPONSE,     (const uint8_t *)"COAP RESPONSE",},
};
const uint8_t *emZclGetMessageStatusName(EmberZclMessageStatus_t status)
{
  for (size_t i = 0; i < COUNTOF(statusTable); i++) {
    if (status == statusTable[i].status) {
      return statusTable[i].name;
    }
  }
  return (const uint8_t *)"?????";
}
