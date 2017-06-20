// File: coap.c
//
// Description: A CoAP server and client.  See RFC 7252.
//
// Copyright 2015 Silicon Laboratories, Inc.                                *80*

#include PLATFORM_HEADER
#include "stack/core/ember-stack.h"
#include "hal/hal.h"
#include "stack/include/error.h"
#include "stack/framework/buffer-management.h"
#include "stack/framework/event-queue.h"
#include "stack/ip/ip-address.h"

#include "coap.h"

// for lose()
#include "stack/ip/tls/debug.h"

// for size_t
#include <stddef.h>
// for strncmp() and strlen()
#include <string.h>

#define END_BYTE 0xFF

#define MAX_COAP_HISTORY_SIZE 5

static uint16_t nextMessageId = 1;
static uint16_t nextToken = 1;

// forward declarations
static void logAckUri(CoapRetryEvent *event);
HIDDEN void initCoapReadOptions(EmberCoapReadOptions *options,
                                const uint8_t *encodedOptions,
                                int16_t length);

//----------------------------------------------------------------

static void coapRetryEventMarker(CoapRetryEvent *event)
{
  emMarkBuffer(&event->packetHeader);
  if (EM_LOG_COAP_ENABLED) {
    emMarkBuffer(&event->uri);
  }
}

static void coapRetryEventHandler(CoapRetryEvent *event);

EventActions emCoapRetryEventActions = {
  &emStackEventQueue,
  (void (*)(Event *)) coapRetryEventHandler,
  (void (*)(Event *)) coapRetryEventMarker,
  "coap retry"
};

// If the packet header is NULL we have already received an ack and are
// waiting for the actual response.

static bool messageIdPredicate(Event *event, void *messageIdLoc)
{
  CoapRetryEvent *retryEvent = (CoapRetryEvent *) event;
  return (retryEvent->packetHeader != NULL_BUFFER
          && retryEvent->messageId == (* ((uint16_t *) messageIdLoc)));
}

// Only those events that have response timeouts have token values.

static bool tokenPredicate(Event *event, void *tokenLoc)
{
  CoapRetryEvent *retryEvent = (CoapRetryEvent *) event;
  return (retryEvent->responseTimeoutMs != 0
          && retryEvent->token == (* ((uint16_t *) tokenLoc)));
}

static CoapRetryEvent *findRetryEvent(uint16_t value,
                                      EventPredicate predicate)
{
  return (CoapRetryEvent *)
    emberFindEvent(&emStackEventQueue,
                   &emCoapRetryEventActions,
                   predicate,
                   &value);
}

#define findRetryEventByMessageId(id) (findRetryEvent((id), &messageIdPredicate))
#define findRetryEventByToken(id)     (findRetryEvent((id), &tokenPredicate))

void emNoteCoapAck(uint16_t messageId)
{
  CoapRetryEvent *event = findRetryEventByMessageId(messageId);
  if (event != NULL) {
    logAckUri(event);
  }
}

static void callResponseHandler(EmberCoapStatus status,
                                CoapMessage *message,
                                CoapRetryEvent *event)
{
  if (event->handler == NULL) {
    return;
  }

  EmberCoapResponseInfo responseInfo;
  MEMSET(&responseInfo, 0, sizeof(responseInfo));
  
  EmberCoapReadOptions readOptions;
  MEMSET(&readOptions, 0, sizeof(readOptions));
  
  if (event->responseAppDataLength != 0) {
    responseInfo.applicationData =
      ((uint8_t *) event) + sizeof(CoapRetryEvent);
    responseInfo.applicationDataLength =
      event->responseAppDataLength;
  }
  
  if (message == NULL) {
    // provide the user with the remote address that timed out
    assert(status == EMBER_COAP_MESSAGE_TIMED_OUT);
    MEMCOPY(&responseInfo.remoteAddress,
            &event->remoteAddress,
            sizeof(EmberIpv6Address));
    event->handler(status,
                   EMBER_COAP_CODE_EMPTY,
                   &readOptions,
                   NULL,
                   0,
                   &responseInfo);
  } else {
    MEMCOPY(&responseInfo.localAddress,
            &message->localAddress,
            sizeof(EmberIpv6Address));
    MEMCOPY(&responseInfo.remoteAddress,
            &message->remoteAddress,
            sizeof(EmberIpv6Address));
    responseInfo.localPort = message->localPort;
    responseInfo.remotePort = message->remotePort;
    emInitCoapReadOptions(&readOptions, 
                          message->encodedOptions,
                          message->encodedOptionsLength);
    event->handler(status,
                   message->code,
                   &readOptions,
                   // Cast away mistaken 'const'.
                   (uint8_t *) message->payload,
                   message->payloadLength,
                   &responseInfo);
  }    
}

static void coapRetryEventHandler(CoapRetryEvent *event)
{
  if (COAP_MAX_RETRANSMIT <= event->retryCount
      || event->packetHeader == NULL_BUFFER) {
    callResponseHandler(EMBER_COAP_MESSAGE_TIMED_OUT, NULL, event);
  } else {
    emLogLine(COAP, "retry ID %u", event->messageId);
    event->retryCount += 1;
    emApiRetryCoapMessage(event);
    emberEventSetDelayMs((Event *) event,
                         COAP_ACK_TIMEOUT_MS << (event->retryCount - 1));
  }
}

//----------------------------------------------------------------
// Names of values.

typedef const struct {
  uint8_t value;
  const uint8_t * const name;
} NameMap;

static const uint8_t *lookupName(uint8_t value, NameMap *map, uint8_t count)
{
  uint8_t i;

  for (i = 0; i < count; i++) {
    if (map[i].value == value) {
      return map[i].name;
    }
  }

  return (const uint8_t *) "?????";
}

static NameMap coapCodeResponseMap[] = {
  {EMBER_COAP_CODE_EMPTY,                          (const uint8_t *)"EMPTY"},
  {EMBER_COAP_CODE_GET,                            (const uint8_t *)"GET"},
  {EMBER_COAP_CODE_POST,                           (const uint8_t *)"POST"},
  {EMBER_COAP_CODE_PUT,                            (const uint8_t *)"PUT"},
  {EMBER_COAP_CODE_DELETE,                         (const uint8_t *)"DELETE"},
  {EMBER_COAP_CODE_201_CREATED,                    (const uint8_t *)"2.01 Created"},
  {EMBER_COAP_CODE_202_DELETED,                    (const uint8_t *)"2.02 Deleted"},
  {EMBER_COAP_CODE_203_VALID,                      (const uint8_t *)"2.03 Valid"},
  {EMBER_COAP_CODE_204_CHANGED,                    (const uint8_t *)"2.04 Changed"},
  {EMBER_COAP_CODE_205_CONTENT,                    (const uint8_t *)"2.05 Content"},
  {EMBER_COAP_CODE_400_BAD_REQUEST,                (const uint8_t *)"4.00 Bad Request"},
  {EMBER_COAP_CODE_401_UNAUTHORIZED,               (const uint8_t *)"4.01 Unauthorized"},
  {EMBER_COAP_CODE_402_BAD_OPTION,                 (const uint8_t *)"4.02 Bad Option"},
  {EMBER_COAP_CODE_403_FORBIDDEN,                  (const uint8_t *)"4.03 Forbidden"},
  {EMBER_COAP_CODE_404_NOT_FOUND,                  (const uint8_t *)"4.04 Not Found"},
  {EMBER_COAP_CODE_405_METHOD_NOT_ALLOWED,         (const uint8_t *)"4.05 Method Not Allowed"},
  {EMBER_COAP_CODE_406_NOT_ACCEPTABLE,             (const uint8_t *)"4.06 Not Acceptable"},
  {EMBER_COAP_CODE_412_PRECONDITION_FAILED,        (const uint8_t *)"4.12 Precondition Failed"},
  {EMBER_COAP_CODE_413_REQUEST_ENTITY_TOO_LARGE,   (const uint8_t *)"4.13 Request Entity Too Large"},
  {EMBER_COAP_CODE_415_UNSUPPORTED_CONTENT_FORMAT, (const uint8_t *)"4.15 Unsupported Content-Format"},
  {EMBER_COAP_CODE_500_INTERNAL_SERVER_ERROR,      (const uint8_t *)"5.00 Internal Server Error"},
  {EMBER_COAP_CODE_501_NOT_IMPLEMENTED,            (const uint8_t *)"5.01 Not Implemented"},
  {EMBER_COAP_CODE_502_BAD_GATEWAY,                (const uint8_t *)"5.02 Bad Gateway"},
  {EMBER_COAP_CODE_503_SERVICE_UNAVAILABLE,        (const uint8_t *)"5.03 Service Unavailable"},
  {EMBER_COAP_CODE_504_GATEWAY_TIMEOUT,            (const uint8_t *)"5.04 Gateway Timeout"},
  {EMBER_COAP_CODE_505_PROXYING_NOT_SUPPORTED,     (const uint8_t *)"5.05 Proxying Not Supported"},
};

const uint8_t *emGetCoapCodeName(EmberCoapCode type)
{
  return lookupName(type, coapCodeResponseMap, COUNTOF(coapCodeResponseMap));
}

static NameMap contentFormatNames[] = {
  {EMBER_COAP_CONTENT_FORMAT_TEXT_PLAIN,   (const uint8_t *)"text/plain"},
  {EMBER_COAP_CONTENT_FORMAT_LINK_FORMAT,  (const uint8_t *)"application/link-format"},
  {EMBER_COAP_CONTENT_FORMAT_XML,          (const uint8_t *)"application/xml"},
  {EMBER_COAP_CONTENT_FORMAT_OCTET_STREAM, (const uint8_t *)"application/octet-stream"},
  {EMBER_COAP_CONTENT_FORMAT_EXI,          (const uint8_t *)"application/exi"},
  {EMBER_COAP_CONTENT_FORMAT_JSON,         (const uint8_t *)"application/json"},
  {EMBER_COAP_CONTENT_FORMAT_CBOR,         (const uint8_t *)"application/cbor"},
  {EMBER_COAP_CONTENT_FORMAT_NONE,         (const uint8_t *)"unknown"},
};

const uint8_t *emGetCoapContentFormatTypeName(EmberCoapContentFormatType type)
{
  return lookupName(type, contentFormatNames, COUNTOF(contentFormatNames));
}

static const uint8_t * const statusNames[] = {
  "TIMED_OUT", "ACKED", "RESET", "RESPONSE"
};

const uint8_t *emGetCoapStatusName(EmberCoapStatus status)
{
  if (status <= EMBER_COAP_MESSAGE_RESPONSE) {
    return statusNames[status];
  } else {
    return  (const uint8_t *) "?????";
  }
}

static NameMap typeNames[] = {
  {COAP_TYPE_CONFIRMABLE,     "CON"},
  {COAP_TYPE_NON_CONFIRMABLE, "NON"},
  {COAP_TYPE_ACK,             "ACK"},
  {COAP_TYPE_RESET,           "RESET"}
};

const uint8_t *emGetCoapTypeName(CoapType type)
{
  return lookupName(type, typeNames, COUNTOF(typeNames));
}

//----------------------------------------------------------------
// CoAP options

// Reduces the contents of 'valueLoc' to a nibble by writing larger values
// to 'finger', as per RFC 7252.

static uint8_t *writeOptionHeader(uint8_t *finger, uint16_t *valueLoc)
{
  uint16_t value = *valueLoc;
  if (0xFF + 13 < value) {
    emberStoreHighLowInt16u(finger, value - 269);
    *valueLoc = 14;
    return finger + 2;
  } else if (12 < value) {
    *finger = value - 13; 
    *valueLoc = 13;
    return finger + 1;
  } else {
    return finger;
  }
}

// Expands the contents of 'valueLoc' back up again by reading larger values
// from 'finger'.

static const uint8_t *readOptionHeader(const uint8_t *finger, 
                                       uint16_t *valueLoc)
{
  if (*valueLoc == 13) {
    *valueLoc = *finger + 13;
    return finger + 1;
  } else if (*valueLoc == 14) {
    *valueLoc = emberFetchHighLowInt16u(finger) + 269;
    return finger + 2;
  } else  {
    return finger;
  }
}

HIDDEN uint8_t *storeCoapOption(uint8_t *finger,
                                EmberCoapOptionType option,
                                EmberCoapOptionType *previousOptionLoc,
                                uint16_t length)
{
  uint8_t *start = finger;
  finger += 1;

  uint16_t delta = (uint16_t) option - (uint16_t) *previousOptionLoc;
  *previousOptionLoc = option;

  finger = writeOptionHeader(finger, &delta);
  finger = writeOptionHeader(finger, &length);
  *start = (delta << 4) | length;
  return finger;
}

static const uint8_t *fetchCoapOption(const uint8_t *finger,
                                      EmberCoapOptionType *typeLoc,
                                      uint16_t *lengthLoc)
{
  uint8_t next = *finger++;
  
  if (next == END_BYTE) {
    return NULL;
  }
  
  uint16_t delta  = next >> 4;
  *lengthLoc = next & 0x0F;
  finger = readOptionHeader(finger, &delta);
  finger = readOptionHeader(finger, lengthLoc);
  
  *typeLoc = (EmberCoapOptionType) (((uint16_t) *typeLoc) + delta);
  return finger;
}

void emInitCoapReadOptions(EmberCoapReadOptions *options,
                           const uint8_t *encodedOptions,
                           int16_t length)
{
  options->start = encodedOptions;
  options->end = encodedOptions + length;
  emberResetReadOptionPointer(options);
}

EmberCoapOptionType emberReadNextOption(EmberCoapReadOptions *options,
                                        const uint8_t **valuePointerLoc,
                                        uint16_t *valueLengthLoc)
{
  if (options->nextOption == options->end) {
    return EMBER_COAP_NO_OPTION;
  } else {
    uint16_t length;
    const uint8_t *value = fetchCoapOption(options->nextOption,
                                           &options->previousType,
                                           &length);
    *valuePointerLoc = value;
    *valueLengthLoc = length;
    options->nextOption = value + length;
    return options->previousType;
  }
}

// Reset the internal pointer back to the first option.

void emberResetReadOptionPointer(EmberCoapReadOptions *options)
{
  options->nextOption = options->start;
  options->previousType = EMBER_COAP_NO_OPTION;
}

// Decodes the value of an integer option using the value and length
// returned by emberReadNextOption().

uint32_t emberReadOptionValue(const uint8_t *value, uint16_t valueLength)
{
  uint32_t result = 0;
  uint8_t i;
  if (4 < valueLength) {
    valueLength = 4;          // we can only fit four bytes in an uint32_t.
  }
  for (i = 0; i < valueLength; i++) {
    result = (result << 8) | value[i];
  }
  return result;
}

// Utility functions for reading a particular option.  These leave 'options's
// internal pointer pointing to the option following the one returned.

bool emberReadIntegerOption(EmberCoapReadOptions *options,
                            EmberCoapOptionType type,
                            uint32_t *valueLoc)
{
  const uint8_t *value;
  uint16_t length;
  if (emberReadBytesOption(options, type, &value, &length)) {
    *valueLoc = emberReadOptionValue(value, length);
    return true;
  } else {
    return false;
  }
}

bool emberReadBytesOption(EmberCoapReadOptions *options,
                          EmberCoapOptionType type,
                          const uint8_t **valueLoc,
                          uint16_t *valueLengthLoc)
{
  emberResetReadOptionPointer(options);
  EmberCoapOptionType nextType;
  do {
    nextType = emberReadNextOption(options, valueLoc, valueLengthLoc);
  } while (nextType != EMBER_COAP_NO_OPTION
           && nextType < type);
  return nextType == type;
}

// Convert any Path options into a string.  Returns the length of the 
// path, or -1 if there are no path options.  If the path is longer than
// 'pathBufferLength' only the first 'pathBufferLength' bytes of the string
// are stored in 'pathBuffer'.  The length of the complete path is still
// returned.

static int16_t readPathOptions(EmberCoapReadOptions *options,
                               EmberCoapOptionType pathType,
                               uint8_t *pathBuffer,
                               uint16_t pathBufferLength)
{
  uint8_t *finger = pathBuffer;
  uint8_t *end = pathBuffer + pathBufferLength;
  emberResetReadOptionPointer(options);
  while (true) {
    const uint8_t *pathElement;
    uint16_t pathElementLength;
    EmberCoapOptionType type =
      emberReadNextOption(options, &pathElement, &pathElementLength);
    if (type == EMBER_COAP_NO_OPTION
        || pathType < type) {
      break;
    } else if (type == pathType) {
      int i;
      for (i = 0; i < pathElementLength; i++, finger++) {
        if (finger < end) {
          *finger = pathElement[i];
        }
      }
      if (finger < end) {
        *finger = '/';
      }
      finger += 1;
    }
  }
  if (finger == pathBuffer) {
    return -1;
  } else if (finger <= end) {
    *(finger - 1) = 0;  // replace the last '/' with 0
  }
  return (finger - pathBuffer) - 1;
}

int16_t emberReadLocationPath(EmberCoapReadOptions *options,
                              uint8_t *pathBuffer,
                              uint16_t pathBufferLength)
{
  return readPathOptions(options,
                         EMBER_COAP_OPTION_LOCATION_PATH, 
                         pathBuffer, 
                         pathBufferLength);
}

int16_t emberReadUriPath(EmberCoapReadOptions *options,
                         uint8_t *pathBuffer,
                         uint16_t pathBufferLength)
{
  return readPathOptions(options,
                         EMBER_COAP_OPTION_URI_PATH, 
                         pathBuffer, 
                         pathBufferLength);
}

// encode both URI Path and Queries
// TODO: don't overrun the buffer
static uint8_t *encodePathAndQuery(const uint8_t *path,
                                   EmberCoapOptionType pathOption,
                                   uint8_t *finger,
                                   uint8_t *end,
                                   EmberCoapOptionType *previousOptionLoc)
{
  // URI Queries uses '?' for initial appending of string/value pairs,
  // use '&' to append all following ones.
  EmberCoapOptionType option = pathOption;

  if (path == NULL) {
    return finger;
  }

  // skip past an initial slash because we don't need it
  if (path[0] == '/') {
    path++;
  }

  while (*path) {
    const uint8_t *start = path;
    for ( ; *path && (*path != '/'
                      && *path != '?'
                      && *path != '&'); path++) ;
    uint16_t length = path - start;
    finger = storeCoapOption(finger, option, previousOptionLoc, length);
    MEMCOPY(finger, start, length);
    finger += length;

    if (*path == '/') {
      path++;
      option = pathOption;
    } else if ((path != NULL)
               && (*path == '?' ||
                   *path == '&' )){
      path++;
      option = EMBER_COAP_OPTION_URI_QUERY;
    }
  }
  return finger;
}

#ifdef EMBER_TEST
// Handy test function.
uint16_t coapEncodeUri(const uint8_t *uri,
                       uint8_t *buffer,
                       uint16_t bufferLength,
                       EmberCoapOptionType previousOption)
{
  return (encodePathAndQuery(uri, 
                             EMBER_COAP_OPTION_URI_PATH,
                             buffer,
                             buffer + bufferLength,
                             &previousOption)
          - buffer);
}
#endif

// TODO: don't overrun the buffer
static uint16_t encodeOptions(uint8_t *buffer,
                              uint16_t bufferLength,
                              const uint8_t *path,
                              EmberCoapOptionType pathType,         
                              const EmberCoapOption *options,
                              uint8_t numberOfOptions)
{
  uint8_t *finger = buffer;
  uint8_t *end = buffer + bufferLength;
  EmberCoapOptionType previous = EMBER_COAP_NO_OPTION;

  while (! (path == NULL && numberOfOptions == 0)) {
    if (path != NULL
        && (numberOfOptions == 0
            || pathType < options->type)) {
      finger = encodePathAndQuery(path, pathType, finger, end, &previous);
      path = NULL;
    } else {
      uint8_t intValue[4];
      const uint8_t *value = options->value;
      uint16_t length = options->valueLength;
      if (value == NULL) {
        emberStoreHighLowInt32u(intValue, options->intValue);
        for (value = intValue, length = 4;
             0 < length && *value == 0;
             length--, value++);
      }
      finger = storeCoapOption(finger, options->type, &previous, length);
      MEMCOPY(finger, value, length);
      finger += length;
      options += 1;
      numberOfOptions -= 1;
    }
  }
  return finger - buffer;
}

// We need some kind of limit.  64 bytes is a long URI to go over 802.15.4.
#define MAX_ENCODED_URI 64

// + 1 for the 0xFF separating the options from the payload
#define MAX_COAP_HEADER_SIZE \
 (COAP_BASE_SIZE + COAP_MAX_TOKEN_LENGTH + MAX_ENCODED_URI + 1)

// We put small payloads in the same buffer as the header.  For larger ones
// we allocate a second buffer.  Making this larger reduces the number of
// packets that need a second buffer at the cost of increasing the size of
// the call stack.  100 is about the point where a packet will get fragmented
// over the air, which seems like a natural division.

#define COAP_MAX_FLAT_PAYLOAD 100

EmberStatus emSubmitCoapMessage(CoapMessage *message,
                                const uint8_t *uri,
                                Buffer payloadBuffer)
{
  // 1 for 0b11111111
  uint8_t coapHeader[MAX_COAP_HEADER_SIZE + COAP_MAX_FLAT_PAYLOAD];
  uint8_t *finger = coapHeader;

  if (emberCoapIsRequest(message->code)) {
    emberStoreHighLowInt16u(message->token, nextToken);
    nextToken += 1;
    message->tokenLength = 2;
  }

  if (message->localPort == 0) {
    message->localPort = EMBER_COAP_PORT;
  }

  if (message->remotePort == 0) {
    message->remotePort = EMBER_COAP_PORT;
  }

  // store the version
  *finger++ = (COAP_VERSION
               | message->type
               | message->tokenLength);

  // store the code
  *finger++ = message->code;

  // increment the message ID?
  if (message->type != COAP_TYPE_ACK
      && message->type != COAP_TYPE_RESET) {
    message->messageId = nextMessageId;
    nextMessageId += 1;
  }

  emberStoreHighLowInt16u(finger, message->messageId);
  finger += 2;

  assert(message->tokenLength <= COAP_MAX_TOKEN_LENGTH);
  MEMCOPY(finger, 
          message->token,
          message->tokenLength);
  finger += message->tokenLength;

  uint16_t payloadLength = emTotalPayloadLength(payloadBuffer);
  bool havePayload = (0 < message->payloadLength
                      || (payloadBuffer != NULL_BUFFER
                          && 0 < payloadLength));

  if (message->encodedOptions != NULL) {
    MEMCOPY(finger,
            message->encodedOptions,
            message->encodedOptionsLength);
    finger += message->encodedOptionsLength;
  } else {
    EmberCoapOptionType previousOption = EMBER_COAP_NO_OPTION;

    if (emberCoapIsRequest(message->code)) {
      assert(uri != NULL);
      previousOption = EMBER_COAP_NO_OPTION;
      uint8_t *limit = finger + MAX_ENCODED_URI;
      // We save the encoded path for debug printing by emApiFinishCoapMessage.
      message->encodedUri = finger;
      finger = encodePathAndQuery(uri, 
                                  EMBER_COAP_OPTION_URI_PATH,
                                  finger,
                                  limit,
                                  &previousOption);
      message->encodedUriLength =
        finger - message->encodedUri;
    }
  }

  if (havePayload) {
    // The payload buffer for Joiner Entrust responses is a special
    // MAC key, not a real payload, so don't add the payload marker.
    if (! (emIsJoinerEntrustMessage(message)
           && message->type == COAP_TYPE_ACK)) {
      *finger++ = 0xFF;
    }
    if (finger + message->payloadLength - coapHeader 
        <= sizeof(coapHeader)) {
      MEMCOPY(finger,
              message->payload,
              message->payloadLength);
      finger += message->payloadLength;
    } else {
      Buffer firstPayloadBuffer = 
        emFillBuffer(message->payload,
                     message->payloadLength);
      if (firstPayloadBuffer == NULL_BUFFER) {
        return EMBER_NO_BUFFERS;
      } 
      emSetPayloadLink(firstPayloadBuffer, payloadBuffer);
      payloadBuffer = firstPayloadBuffer;
    }
  }
  
  assert(finger - coapHeader <= sizeof(coapHeader));
  Buffer header;
  EmberStatus status =  emApiFinishCoapMessage(message,
                                               coapHeader,
                                               finger - coapHeader,
                                               payloadBuffer,
                                               &header);

  // Add handler to retry CON messages and/or to pass responses
  // to the application.
  if (! (status == EMBER_SUCCESS
         && (message->type == COAP_TYPE_CONFIRMABLE
             || (emberCoapIsRequest(message->code)
                 && message->responseHandler != NULL)))) {
    return status;
  }

  Buffer eventBuffer =
    emAllocateBuffer(sizeof(CoapRetryEvent) + message->responseAppDataLength);
  
  if (eventBuffer == NULL_BUFFER) {
    return status;     // would be good to signal failure
  }
  
  CoapRetryEvent *retryEvent =
    (CoapRetryEvent *)(void *)emGetBufferPointer(eventBuffer);
  
  MEMSET(retryEvent, 0, sizeof(CoapRetryEvent));
  
  if (EM_LOG_COAP_ENABLED
      && uri != NULL) {
    // don't care if the buffer allocation fails
    retryEvent->uri = emFillBuffer(uri, strlen(uri) + 1); // +1 for the nul
  }
  
  MEMCOPY(((uint8_t *) retryEvent) + sizeof(CoapRetryEvent),
          message->responseAppData,
          message->responseAppDataLength);
  retryEvent->responseAppDataLength = message->responseAppDataLength;
  retryEvent->event.actions = &emCoapRetryEventActions;
  retryEvent->event.next = NULL;
  retryEvent->retryCount = 0;
  retryEvent->messageId = message->messageId;
  if (message->responseHandler != NULL) {
    retryEvent->handler = message->responseHandler;
    retryEvent->responseTimeoutMs = 
      (message->responseTimeoutMs == 0
       ? EMBER_COAP_DEFAULT_TIMEOUT_MS
       : message->responseTimeoutMs);
    retryEvent->token = emberFetchHighLowInt16u(message->token);
  }
  
  if (message->type == COAP_TYPE_CONFIRMABLE) {
    // Add data needed for resending.
    retryEvent->packetHeader = header;
    MEMCOPY(&retryEvent->localAddress,
            &message->localAddress,
            sizeof(EmberIpv6Address));
    retryEvent->localPort = message->localPort;
    MEMCOPY(&retryEvent->remoteAddress,
            &message->remoteAddress,
            sizeof(EmberIpv6Address));
    retryEvent->remotePort = message->remotePort;
    retryEvent->transmitHandler = message->transmitHandler;
    retryEvent->transmitHandlerData = message->transmitHandlerData;
    emberEventSetDelayMs((Event *) retryEvent, COAP_ACK_TIMEOUT_MS);
  } else {
    // Just wait for a response.
    emberEventSetDelayMs((Event *) retryEvent, retryEvent->responseTimeoutMs);
  }

  return status;
}

EmberStatus emberCoapSend(const EmberIpv6Address *destination,
                          EmberCoapCode code,
                          const uint8_t *path,
                          const uint8_t *payload,
                          uint16_t payloadLength,
                          EmberCoapResponseHandler responseHandler,
                          const EmberCoapSendInfo *info)
{
  CoapMessage message;
  MEMSET(&message, 0, sizeof(message));
  MEMCOPY(&message.localAddress, &info->localAddress.bytes, 16);
  MEMCOPY(&message.remoteAddress, &destination->bytes, 16);
  message.localPort = info->localPort;
  message.remotePort = info->remotePort;
  message.transmitHandler = info->transmitHandler;
  message.transmitHandlerData = info->transmitHandlerData;

  message.code = code;
  message.payload = payload;
  message.payloadLength = payloadLength;
  
  uint8_t encodedOptions[100];  // TODO: handle arbitrary lengths
  uint16_t encodedOptionsLength = 
    encodeOptions(encodedOptions,
                  sizeof(encodedOptions),
                  path,
                  EMBER_COAP_OPTION_URI_PATH,
                  info->options,
                  info->numberOfOptions);
  assert(encodedOptionsLength <= sizeof(encodedOptions));
  if (encodedOptionsLength != 0) {
    message.encodedOptions = encodedOptions;
    message.encodedOptionsLength = encodedOptionsLength;
  }

  if (info->nonConfirmed || 
      (info->transmitHandler == NULL
       && emIsMulticastAddress(destination->bytes))) {
    message.type = COAP_TYPE_NON_CONFIRMABLE;
  } else {
    message.type = COAP_TYPE_CONFIRMABLE;
  }

  if (responseHandler != NULL) {
    message.responseTimeoutMs = info->responseTimeoutMs;
    message.responseHandler = responseHandler;
    message.responseAppData = info->responseAppData;
    message.responseAppDataLength = info->responseAppDataLength;
  }

  return emSubmitCoapMessage(&message, NULL, NULL_BUFFER);
}

//----------------------------------------------------------------
// History Table
//
// RFC 7252 says that duplicates are identified by their message ID and
// source endpoint.  For non-secured messages the endpoint is the source
// IP address and port.  For secured messages it is the security association.
//
// TODO: we should time these out.

typedef struct {
  uint16_t messageId;
  EmberCoapTransmitHandler handler;
  union {
    struct {
      EmberIpv6Address remoteAddress;
      uint16_t remotePort;
    } udp;
    void *transmitHandlerData;
  };
} MessageHistoryEntry;

static Buffer historyTable = NULL_BUFFER;

HIDDEN bool isInMessageHistory(const EmberIpv6Address *remoteAddress,
                               uint16_t remotePort,
                               EmberCoapTransmitHandler handler,
                               void *handlerData,
                               uint16_t messageId)
{
  MessageHistoryEntry entry;
  MEMSET(&entry, 0, sizeof(entry));

  entry.handler = handler;
  entry.messageId = messageId;
  if (handler == NULL) {
    MEMCOPY(entry.udp.remoteAddress.bytes, 
            remoteAddress->bytes,
            sizeof(EmberIpv6Address));
    entry.udp.remotePort = remotePort;
  } else {
    entry.transmitHandlerData = handlerData;
  }

  Buffer buffer;
  for (buffer = emBufferQueueHead(&historyTable);
       buffer != NULL_BUFFER;
       buffer = emBufferQueueNext(&historyTable, buffer)) {
    if (MEMCOMPARE(&entry, emGetBufferPointer(buffer), sizeof(entry))
        == 0) {
      return true;
    }
  }
  
  buffer = emFillBuffer((uint8_t *) &entry, sizeof(entry));
  if (buffer != NULL_BUFFER) {
    emBufferQueueAdd(&historyTable, buffer);
    if (MAX_COAP_HISTORY_SIZE < emBufferQueueLength(&historyTable)) {
      emBufferQueueRemoveHead(&historyTable);
    }
  }
  return false;
}

//----------------------------------------------------------------

void emCoapMarkBuffers(void)
{
  emMarkBuffer(&historyTable);
}

// TODO: add failure cases for parsing
bool emParseCoapMessage(const uint8_t *data,
                        uint16_t dataLength,
                        CoapMessage *coapMessage)
{
  MEMSET(coapMessage, 0, sizeof(CoapMessage));
  coapMessage->tokenLength = data[0] & COAP_TOKEN_LENGTH_MASK;

  if ((data[0] & COAP_VERSION_MASK) != COAP_VERSION) {
    return false;
  }

  coapMessage->type = (CoapType)(data[0] & COAP_TYPE_MASK);
  coapMessage->code = (EmberCoapCode)data[1];
  coapMessage->messageId = emberFetchHighLowInt16u(data + 2);
  MEMCOPY(coapMessage->token,
          data + COAP_TOKEN_INDEX,
          coapMessage->tokenLength);
  uint8_t overhead = COAP_BASE_SIZE + (data[0] & COAP_TOKEN_LENGTH_MASK);
  coapMessage->encodedOptions = data + overhead;
  coapMessage->encodedOptionsLength = dataLength - overhead;

  EmberCoapOptionType option = EMBER_COAP_NO_OPTION;
  EmberCoapOptionType previousOption = EMBER_COAP_NO_OPTION;
  const uint8_t *finger = coapMessage->encodedOptions;
  const uint8_t *limit =
    finger + coapMessage->encodedOptionsLength;

  while (finger < limit && *finger != END_BYTE) {
    uint16_t length;
    const uint8_t *optionStart = finger;
    previousOption = option;
    finger = fetchCoapOption(finger, &option, &length);
    if (option == EMBER_COAP_OPTION_URI_PATH
        && previousOption != EMBER_COAP_OPTION_URI_PATH) {
      coapMessage->encodedUri = optionStart;
    } else if (option != EMBER_COAP_OPTION_URI_PATH
               && previousOption == EMBER_COAP_OPTION_URI_PATH) {
      coapMessage->encodedUriLength =
        optionStart - coapMessage->encodedUri;
    }
    finger += length;
  }

  if (option == EMBER_COAP_OPTION_URI_PATH) {
    coapMessage->encodedUriLength =
      finger - coapMessage->encodedUri;
  }

  if (finger < limit && *finger == END_BYTE) {
    coapMessage->encodedOptionsLength =
      finger - coapMessage->encodedOptions;
    coapMessage->payload = finger + 1;
    coapMessage->payloadLength =
      limit - coapMessage->payload;
  } else if (limit < finger) {
    return false;       // options parsing ran past end of message
  }
  return true;
}

// Resets and non-piggybacking acks

static void sendResponse(CoapMessage *coapMessage, CoapType type)
{
  CoapMessage reply;
  MEMSET(&reply, 0, sizeof(reply));
  reply.type = type;
  reply.messageId = coapMessage->messageId;
  MEMCOPY(&reply.localAddress,
          &coapMessage->localAddress,
          16);
  MEMCOPY(&reply.remoteAddress,
          &coapMessage->remoteAddress,
          16);
  reply.localPort = coapMessage->localPort;
  reply.remotePort = coapMessage->remotePort;
  reply.transmitHandler = coapMessage->transmitHandler;
  reply.transmitHandlerData = coapMessage->transmitHandlerData;

  // the URI isn't sent OTA, just logged
  reply.encodedUri = coapMessage->encodedUri;
  reply.encodedUriLength =
    coapMessage->encodedUriLength;

  emSubmitCoapMessage(&reply, NULL, NULL_BUFFER);
}

static void logAckUri(CoapRetryEvent *event)
{
  // print out the acked URI if COAP logging is enabled
  if (EM_LOG_COAP_ENABLED
      && (IS_EMBER_TEST || emLogIsActive(COAP))
      && event != NULL
      && event->uri != NULL_BUFFER) {
    emLogLine(COAP,
              "CoAP RX message id: %u | uri: %s | type: ACK",
              event->messageId,
              emGetBufferPointer(event->uri));
  }
}

void emProcessCoapMessage(Buffer header,
                          const uint8_t *message,
                          uint16_t messageLength,
                          CoapRequestHandler requestHandler,
                          EmberCoapRequestInfo *requestInfo)
{
  CoapMessage coapMessage;
  if (! emParseCoapMessage(message, messageLength, &coapMessage)) {
    emLogBytesLine(COAP, "RX fail", message, messageLength);
    loseVoid(COAP);
  }

  requestInfo->tokenLength = coapMessage.tokenLength;
  MEMCOPY(requestInfo->token,
          coapMessage.token,
          coapMessage.tokenLength);

  // transition code
  MEMCOPY(&coapMessage.localAddress,
          &requestInfo->localAddress,
          16);
  MEMCOPY(&coapMessage.remoteAddress,
          &requestInfo->remoteAddress,
          16);
  coapMessage.localPort = requestInfo->localPort;
  coapMessage.remotePort = requestInfo->remotePort;
  coapMessage.message = header;
  coapMessage.transmitHandler = requestInfo->transmitHandler;
  coapMessage.transmitHandlerData = requestInfo->transmitHandlerData;

  emLogLine(COAP,
            "RX %s | id: %u | %s | from: %b bytes: %b",
            emGetCoapTypeName(coapMessage.type),
            coapMessage.messageId,
            emGetCoapCodeName(coapMessage.code),
            &requestInfo->remoteAddress,
            16,
            coapMessage.payload,
            coapMessage.payloadLength);

  switch (coapMessage.type) {

  case COAP_TYPE_RESET: {
    CoapRetryEvent *event = findRetryEventByMessageId(coapMessage.messageId);

    emLogLine(COAP,
              "Received RESET for message: %u",
              coapMessage.messageId);

    if (event != NULL) {
      callResponseHandler(EMBER_COAP_MESSAGE_RESET, &coapMessage, event);
    }
    return;
  }

  case COAP_TYPE_ACK: {
    CoapRetryEvent *event = findRetryEventByMessageId(coapMessage.messageId);
    logAckUri(event);

    if (event == NULL) {
      // Do nothing.  It's a duplicate, or the sender is confused.
    } else if (event->handler == NULL) {
      // Nothing left to do.
    } else if (coapMessage.code != EMBER_COAP_CODE_EMPTY
               && coapMessage.tokenLength == 2
               && (emberFetchHighLowInt16u(coapMessage.token)
                   == event->token)) {
      callResponseHandler(EMBER_COAP_MESSAGE_RESPONSE, &coapMessage, event);
    } else {
      callResponseHandler(EMBER_COAP_MESSAGE_ACKED, &coapMessage, event);
      // if (coapMessage->code != EMBER_COAP_CODE_EMPTY) {
      //   if (coapMessage->tokenLength != 2) {
      //     simPrint("token size mismatch %d != 2", 
      //              coapMessage->tokenLength);
      //   } else {
      //     simPrint("token value mismatch %d != %d",
      //              emberFetchHighLowInt16u(coapMessage->token),
      //              event->token);
      //   }
      // }
      if (event->responseTimeoutMs != 0) {
        event->packetHeader = NULL_BUFFER;
        emberEventSetDelayMs((Event *) event, event->responseTimeoutMs);
      }
    }
    return;
  }
    
  case COAP_TYPE_CONFIRMABLE:
  case COAP_TYPE_NON_CONFIRMABLE:
    if (coapMessage.code == EMBER_COAP_CODE_EMPTY) {
      // RFC says to send a reset in response to an empty CON or NON message.
      sendResponse(&coapMessage, COAP_TYPE_RESET);
      return;
    } else if (isInMessageHistory(&requestInfo->remoteAddress,
                                  requestInfo->remotePort,
                                  requestInfo->transmitHandler,
                                  requestInfo->transmitHandlerData,
                                  coapMessage.messageId)) {
      // We really should wait to add it until we know we have processed
      // it correctly.
      emLogLine(COAP,
                "Message %u is already in history",
                coapMessage.messageId);
      if (coapMessage.type == COAP_TYPE_CONFIRMABLE) {
        sendResponse(&coapMessage, COAP_TYPE_ACK);
      }
      return;
    }

    if (coapMessage.type == COAP_TYPE_CONFIRMABLE) {
      requestInfo->ackData = &coapMessage.messageId;
    }
    
    if (GET_COAP_CLASS(coapMessage.code)
        == EMBER_COAP_CLASS_REQUEST) {
      EmberCoapReadOptions options;
      emInitCoapReadOptions(&options, 
                            coapMessage.encodedOptions,
                            coapMessage.encodedOptionsLength);
      uint8_t uri[128];
      if (sizeof(uri) < emberReadUriPath(&options, uri, sizeof(uri))) {
        // what to do?  
        assert(false);
      }
      requestHandler(coapMessage.code,
                     uri,
                     &options,
                     // cast 'const' away so that the handler can use
                     // the temporary space, if necessary
                     (uint8_t *)coapMessage.payload,
                     coapMessage.payloadLength,
                     requestInfo,
                     header);
    } else if (coapMessage.tokenLength == 2) {
      CoapRetryEvent *event =
        findRetryEventByToken(emberFetchHighLowInt16u(coapMessage.token));
      if (event != NULL) {
        callResponseHandler(EMBER_COAP_MESSAGE_RESPONSE, &coapMessage, event);
      }
    } else {
      emLogLine(COAP,
                "Response with funny token length: %d",
                coapMessage.tokenLength);
    }

    if (requestInfo->ackData != NULL) {
      sendResponse(&coapMessage, COAP_TYPE_ACK);
      requestInfo->ackData = NULL;
    }
    break;
  }
}

void emberSaveRequestInfo(const EmberCoapRequestInfo *from,
                          EmberCoapRequestInfo *to)
{
  MEMCOPY(to, from, sizeof(EmberCoapRequestInfo));
  to->ackData = NULL;
}

EmberStatus emberCoapRespondWithPath(const EmberCoapRequestInfo *requestInfo,
                                     EmberCoapCode code,
                                     const uint8_t *path,
                                     const EmberCoapOption *options,
                                     uint8_t numberOfOptions,
                                     const uint8_t *payload,
                                     uint16_t payloadLength)
{
  CoapMessage reply;
  MEMSET(&reply, 0, sizeof(reply));
  
  if (requestInfo->ackData != NULL) {
    reply.type = COAP_TYPE_ACK;
    reply.messageId = *((uint16_t *) requestInfo->ackData);
    // Cast away the 'const' so that we can remove the ackData to prevent
    // future acks.
    ((EmberCoapRequestInfo *) requestInfo)->ackData = NULL;
  } else if (code == EMBER_COAP_CODE_EMPTY) {
    // Resets take the place of ACKs, so only send one if we would send an ACK.
    return EMBER_SUCCESS;
  } else {
    // This was NON_CONFIRMABLE, which seems like the wrong choice, especially
    // if the original was a multicast.
    reply.type = COAP_TYPE_CONFIRMABLE;
  }

  reply.code = code;
  MEMCOPY(&reply.remoteAddress, &requestInfo->remoteAddress, 16);
  MEMCOPY(&reply.localAddress,  &requestInfo->localAddress, 16);
  reply.localPort = requestInfo->localPort;
  reply.remotePort = requestInfo->remotePort;
  reply.transmitHandler = requestInfo->transmitHandler;
  reply.transmitHandlerData = requestInfo->transmitHandlerData;

  MEMCOPY(reply.token,
          requestInfo->token, 
          requestInfo->tokenLength);
  reply.tokenLength = requestInfo->tokenLength;

  // This needs to be integrated into emSubmitCoapMessage() so that
  // we can handle arbitrary lengths.
  uint8_t encodedOptions[100];
  uint16_t encodedOptionsLength = 
    encodeOptions(encodedOptions,
                  sizeof(encodedOptions),
                  path,
                  EMBER_COAP_OPTION_LOCATION_PATH,
                  options,
                  numberOfOptions);
  assert(encodedOptionsLength <= sizeof(encodedOptions));
  if (encodedOptionsLength != 0) {
    reply.encodedOptions = encodedOptions;
    reply.encodedOptionsLength = encodedOptionsLength;
  }

  reply.payload = payload;
  reply.payloadLength = payloadLength;

  return emSubmitCoapMessage(&reply, NULL, NULL_BUFFER);
}

void emCoapInitialize(void)
{
  nextMessageId = halCommonGetRandom();
  nextToken = halCommonGetRandom();
  historyTable = NULL_BUFFER;
}

// This function returns the length of the string form of rawUri, including a
// null terminator.  If this length is longer than convertedUriLength, then
// convertedUri will be incomplete and unterminated.  Otherwise, convertedUri
// will contain the string form, including a null terminator.  Note that, in
// order to compute the length, decoding does not stop if convertedUri is too
// small, but writing to convertedUri does stop.
static uint16_t decodeUri(const uint8_t *rawUri,
                          uint16_t rawUriLength,
                          uint8_t *convertedUri,
                          uint16_t convertedUriLength)
{
  const uint8_t *rawFinger = rawUri;
  const uint8_t *rawLimit = rawFinger + rawUriLength;
  uint8_t *convertedFinger = convertedUri;
  const uint8_t *convertedLimit = convertedUri + convertedUriLength;
  EmberCoapOptionType option = EMBER_COAP_NO_OPTION;
  bool overflow = false;

  while (rawFinger < rawLimit) {
    uint16_t length = 0;
    rawFinger = fetchCoapOption(rawFinger, &option, &length);
    if ((convertedFinger + length) >= convertedLimit) {
      overflow = true;
    }

    if (! overflow) {
      MEMCOPY(convertedFinger, rawFinger, length);
    }
    convertedFinger += length;
    rawFinger += length;

    if (rawFinger < rawLimit) {
      if (!overflow) {
        *convertedFinger = '/';
      }
      convertedFinger++;
    }
  }

  if (convertedFinger >= convertedLimit) {
    overflow = true;
  }

  if (! overflow) {
    *convertedFinger = 0;
  }
  convertedFinger++;

  return convertedFinger - convertedUri;
}

bool emberCoapDecodeUri(const uint8_t *rawUri,
                        uint16_t rawUriLength,
                        uint8_t *convertedUri,
                        uint16_t convertedUriLength)
{
  uint16_t decodedUriLength = decodeUri(rawUri,
                                        rawUriLength,
                                        convertedUri,
                                        convertedUriLength);
  return (decodedUriLength <= convertedUriLength);
}

bool emberCoapIsRequest(EmberCoapCode code)
{
  // Codes 0.01--0.31 indicate a request.  Note that 0.00 indicates an empty
  // message and is not a request.
  return (code != EMBER_COAP_CODE_EMPTY
          && GET_COAP_CLASS(code) == EMBER_COAP_CLASS_REQUEST);
}

bool emberCoapIsResponse(EmberCoapCode code)
{
  // Codes 2.00--5.31 indicate a response.
  uint8_t class = GET_COAP_CLASS(code);
  return (EMBER_COAP_CLASS_SUCCESS_RESPONSE <= class
          && class <= EMBER_COAP_CLASS_SERVER_ERROR_RESPONSE);
}

// This is used as a marker and never actually called.

bool emJoinerEntrustTransmitHandler(const uint8_t *payload,
                                    uint16_t payloadLength,
                                    const EmberIpv6Address *localAddress,
                                    uint16_t localPort,
                                    const EmberIpv6Address *remoteAddress,
                                    uint16_t remotePort,
                                    void *transmitHandlerData)
{
  return false;
}

