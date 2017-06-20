// Copyright 2015 Silicon Laboratories, Inc.

#include PLATFORM_HEADER
#include CONFIGURATION_HEADER
#include EMBER_AF_API_STACK
#include EMBER_AF_API_BUFFER_MANAGEMENT
#include EMBER_AF_API_COMMAND_INTERPRETER2
#ifdef EMBER_AF_API_DEBUG_PRINT
  #include EMBER_AF_API_DEBUG_PRINT
#endif

const uint8_t *emGetCoapCodeName(EmberCoapCode type);
const uint8_t *emGetCoapContentFormatTypeName(EmberCoapContentFormatType type);
const uint8_t *emGetCoapStatusName(EmberCoapStatus status);

static void responseHandler(EmberCoapStatus status,
                            EmberCoapCode code,
                            EmberCoapReadOptions *options,
                            uint8_t *payload,
                            uint16_t payloadLength,
                            EmberCoapResponseInfo *info)
{
  emberAfAppPrint("CoAP CLI:");

  emberAfAppPrint(" %s", emGetCoapStatusName(status));

  if (status != EMBER_COAP_MESSAGE_TIMED_OUT) {
    emberAfAppPrint(" %s", emGetCoapCodeName(code));

    int16_t locationPathLength = emberReadLocationPath(options, NULL, 0);
    if (locationPathLength != -1) {
      uint8_t tmp[100] = {0};
      uint8_t *locationPath = tmp;
      bool truncated = false;
      if (sizeof(tmp) <= locationPathLength) {
        Buffer buffer = emAllocateBuffer(locationPathLength + 1);
        if (buffer == NULL_BUFFER) {
          locationPathLength = sizeof(tmp) - 1;
          truncated = true;
        } else {
          locationPath = emGetBufferPointer(buffer);
        }
      }
      emberReadLocationPath(options, locationPath, locationPathLength);
      emberAfAppPrint(" l=%s", locationPath);
      if (truncated) {
        emberAfAppPrint("...");
      }
    }

    if (payloadLength != 0) {
      uint32_t valueLoc;
      EmberCoapContentFormatType contentFormat
        = (emberReadIntegerOption(options,
                                  EMBER_COAP_OPTION_CONTENT_FORMAT,
                                  &valueLoc)
           ? (EmberCoapContentFormatType)valueLoc
           : EMBER_COAP_CONTENT_FORMAT_NONE);
      emberAfAppPrint(" f=%s p=", emGetCoapContentFormatTypeName(contentFormat));
      if (contentFormat == EMBER_COAP_CONTENT_FORMAT_TEXT_PLAIN
          || contentFormat == EMBER_COAP_CONTENT_FORMAT_LINK_FORMAT) {
        emberAfAppDebugExec(emberAfPrintCharacters(EMBER_AF_PRINT_APP,
                                                   payload,
                                                   payloadLength));
      } else {
        emberAfAppPrintBuffer(payload, payloadLength, false);
      }
    }
  }

  emberAfAppPrintln("");
}

static EmberStatus coapCommand(EmberCoapCode code)
{
  EmberIpv6Address destination;
  if (!emberGetIpv6AddressArgument(0, &destination)) {
    emberAfAppPrintln("%p: %p", "ERR", "invalid ip");
    return EMBER_BAD_ARGUMENT;
  }

  uint8_t *path = emberStringCommandArgument(1, NULL);

  uint8_t payloadLength = 0;
  uint8_t *payload = NULL;
  if (emberCommandArgumentCount() > 2) {
    payload = emberStringCommandArgument(2, &payloadLength);
  }

  EmberCoapSendInfo info = {0}; // use defaults
  return emberCoapSend(&destination,
                       code,
                       path,
                       payload,
                       payloadLength,
                       responseHandler,
                       &info);
}

// coap listen <address>
void coapListenCommand(void)
{
  EmberIpv6Address address;
  if (!emberGetIpv6AddressArgument(0, &address)) {
    emberAfAppPrintln("%p: %p", "ERR", "invalid ip");
    return;
  }

  EmberStatus status = emberUdpListen(EMBER_COAP_PORT, address.bytes);
  emberAfAppPrintln("%p 0x%x", "listen", status);
}

// coap get <destination> <uri>
void coapGetCommand(void)
{
  EmberStatus status = coapCommand(EMBER_COAP_CODE_GET);
  emberAfAppPrintln("%p 0x%x", "get", status);
}

// coap post <destination> <uri> [<body>]
void coapPostCommand(void)
{
  EmberStatus status = coapCommand(EMBER_COAP_CODE_POST);
  emberAfAppPrintln("%p 0x%x", "post", status);
}

// coap put <destination> <uri> [<body>]
void coapPutCommand(void)
{
  EmberStatus status = coapCommand(EMBER_COAP_CODE_PUT);
  emberAfAppPrintln("%p 0x%x", "put", status);
}

// coap delete <destination> <uri>
void coapDeleteCommand(void)
{
  EmberStatus status = coapCommand(EMBER_COAP_CODE_DELETE);
  emberAfAppPrintln("%p 0x%x", "delete", status);
}
