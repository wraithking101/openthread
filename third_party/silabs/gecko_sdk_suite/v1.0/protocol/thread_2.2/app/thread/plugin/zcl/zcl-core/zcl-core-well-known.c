// Copyright 2016 Silicon Laboratories, Inc.

#include PLATFORM_HEADER
#include CONFIGURATION_HEADER
#include EMBER_AF_API_STACK

#ifdef EMBER_AF_API_DEBUG_PRINT
  #include EMBER_AF_API_DEBUG_PRINT
#endif

#include <stdio.h>
#include "zcl-core.h"
#include "zcl-core-well-known.h"

static uint16_t appendClusterId(char *finger,
                                char *endOfBuffer,
                                const EmberZclClusterSpec_t *spec,
                                bool includeTrailingClusterTag,
                                EmZclDiscUriStringAppendMode mode);
static uint16_t appendEndpointId(char *finger,
                                 char *endOfBuffer,
                                 EmberZclEndpointId_t endpointId,
                                 EmberZclDeviceId_t deviceId,
                                 EmZclDiscUriStringAppendMode mode);
static uint16_t appendClusterRev(char *finger,
                                 char *endOfBuffer,
                                 EmberZclEndpointId_t endpointId,
                                 const EmberZclClusterSpec_t *clusterSpec,
                                 EmZclDiscUriStringAppendMode mode);
static uint16_t appendClusterRevByVal(char *finger,
                                      char *endOfBuffer,
                                      const uint16_t version,
                                      EmZclDiscUriStringAppendMode mode);
static uint16_t appendProtocolRev(char *finger,
                                  char *endOfBuffer,
                                  const uint16_t version,
                                  EmZclDiscUriStringAppendMode mode);

static bool uriDone(char *finger);

const uint8_t *emGetCoapCodeName(EmberCoapCode type);
const uint8_t *emGetCoapContentFormatTypeName(EmberCoapContentFormatType type);
const uint8_t *emGetCoapStatusName(EmberCoapStatus status);
uint16_t appendUriDelimiter(char * finger, EmZclDiscUriStringAppendMode mode);

#define EM_ZCL_URI_PATH_APPEND_MAX_LEN (17)
#define EM_ZCL_URI_ENDPOINT_APPEND_MAX_LEN (17)
#define EM_ZCL_URI_CLUSTER_REVISION_APPEND_MAX_LEN (20)
#define EM_ZCL_URI_CLUSTER_ID_APPEND_MAX_LEN (20)
#define EM_ZCL_URI_DEVICE_ID_AND_ENDPOINT_APPEND_MAX_LEN (16)

// Enums for helping with CLI commands.

typedef enum {
  EM_ZCL_DISCOVERY_REQUEST_SINGLE_QUERY = 0, // discovery message are sent per individual zcl disc query command.
  EM_ZCL_DISCOVERY_REQUEST_MULTIPLE_QUERY = 1, // all zcl disc query commands are combined then sent out.
  EM_ZCL_DISCOVERY_REQUEST_MODE_MAX = 2 
} EmZclDiscoveryRequestMode;

typedef enum {
  EM_ZCL_CLI_DISCOVERY_REQUEST_BY_CLUSTER_ID,
  EM_ZCL_CLI_DISCOVERY_REQUEST_BY_DEVICE_ID,
  EM_ZCL_CLI_DISCOVERY_REQUEST_BY_ENDPOINT_AND_DEVICE_ID,
  EM_ZCL_CLI_DISCOVERY_REQUEST_BY_UID_STRING,
  EM_ZCL_CLI_DISCOVERY_REQUEST_BY_RESOURCE_VERSION, // if=urn:zcl:v0
  EM_ZCL_CLI_DISCOVERY_REQUEST_BY_CLUSTER_REVISION, // if=urn:zcl:c.v0
} EmZclCliDiscoveryRequestType;

typedef struct {
  EmZclCliDiscoveryRequestType type;
  union {
    const EmberZclClusterSpec_t *clusterSpec;
    EmberZclEndpointId_t endpointId;
    const uint8_t *uidString;
    EmberZclClusterRevision_t version;
  } data;
  EmberZclDeviceId_t deviceId;
} EmZclDiscoveryRequest;

// URI handlers
static void wellKnownUriHandler(EmZclContext_t *context);
static void wellKnownUriQueryHandler(EmZclContext_t *context);
static bool wellKnownUriQueryDeviceTypeAndEndpointParse(EmZclContext_t *context,
                                                        void *castString,
                                                        uint8_t depth);
static bool wellKnownUriQueryVersionParse(EmZclContext_t *context,
                                          void *castString,
                                          uint8_t depth);
static bool wellKnownUriQueryClusterIdParse(EmZclContext_t *context,
                                            void *castString,
                                            uint8_t depth);
static bool wellKnownUriQueryUidParse(EmZclContext_t *context,
                                      void *data,
                                      uint8_t depth);

static EmZclUriQuery wellKnownUriQueries[] = {
  // target attributes / relation type
  { emZclUriQueryStringPrefixMatch, EM_ZCL_URI_QUERY_PREFIX_CLUSTER_ID,                  wellKnownUriQueryClusterIdParse              },
  { emZclUriQueryStringPrefixMatch, EM_ZCL_URI_QUERY_PREFIX_VERSION,                     wellKnownUriQueryVersionParse                },
  { emZclUriQueryStringPrefixMatch, EM_ZCL_URI_QUERY_PREFIX_DEVICE_TYPE_AND_ENDPOINT,    wellKnownUriQueryDeviceTypeAndEndpointParse  },
  { emZclUriQueryStringPrefixMatch, EM_ZCL_URI_QUERY_UID,                                wellKnownUriQueryUidParse                    },

  // terminator
  { NULL,                           NULL,                                                NULL                                  },
};

EmZclUriPath emZclWellKnowUriPaths[] = {
  // .well-known/core
  {   1, 255, EM_ZCL_URI_FLAG_METHOD_GET, emZclUriPathStringMatch, ".well-known", NULL,                NULL },
  { 255, 255, EM_ZCL_URI_FLAG_METHOD_GET, emZclUriPathStringMatch, "core",        wellKnownUriQueries, wellKnownUriHandler },
};

//----------------------------------------------------------------
// URI segment matching functions

bool emUriQueryZclStringtoCluster(EmZclContext_t *context,
                                  uint8_t depth,
                                  const uint16_t offset)
{
  const char *finger = (const char *) context->uriQuery[depth] + offset
                       + EM_ZCL_URI_QUERY_CLUSTER_ID_LEN;
  uintmax_t clusterId;
  EmberZclClusterSpec_t *clusterSpec = &context->clusterSpec;
  char *dot = memchr(finger,
                     EM_ZCL_URI_QUERY_DOT, 
                     context->uriQuery[depth] +
                     context->uriQueryLength[depth]
                     - (const uint8_t *)finger);
  
  if (dot != NULL){
    // parse cluster id
    if (*finger != EM_ZCL_URI_QUERY_WILDCARD) {
      bool status = emZclHexStringToInt((const uint8_t *)finger, dot - finger, &clusterId);
      if (!status){
        return false;
      }
      clusterSpec->id = clusterId;
      clusterSpec->manufacturerCode = EMBER_ZCL_MANUFACTURER_CODE_NULL;
      context->flags |= EMBER_ZCL_CONTEXT_FILTER_BY_CLUS_ID_WITH_ID;
    }

    // parse role
    if ((dot[1] != EM_ZCL_URI_QUERY_WILDCARD)){
      if (dot[1] == 'c'){
        clusterSpec->role = EMBER_ZCL_ROLE_CLIENT;
        context->flags |= EMBER_ZCL_CONTEXT_FILTER_BY_CLUS_ID_WITH_ROLE;
      } else if (dot[1] == 's') {
        clusterSpec->role = EMBER_ZCL_ROLE_SERVER;
        context->flags |= EMBER_ZCL_CONTEXT_FILTER_BY_CLUS_ID_WITH_ROLE;
      } else {
        return false;
      }
    }

    if ((context->flags & EMBER_ZCL_CONTEXT_FILTER_BY_CLUS_ID_WITH_ROLE)
        ||(context->flags & EMBER_ZCL_CONTEXT_FILTER_BY_CLUS_ID_WITH_ID)){
      return true;
    }
  } else if (*finger == EM_ZCL_URI_QUERY_WILDCARD){
    context->flags |= EMBER_ZCL_CONTEXT_FILTER_BY_CLUS_ID;
    return true;
  }

  return false;
}

static bool wellKnownUriQueryDeviceTypeAndEndpointParse(EmZclContext_t *context,
                                                        void *castString,
                                                        uint8_t depth)
{
  const uint16_t length = strlen((char *) castString);
  char * start = (char *)(context->uriQuery[depth] + length);
  char * finger = start;
  char * end = (char *)(context->uriQuery[depth] + context->uriQueryLength[depth]);

  if (context->uriQueryLength[depth] <= length){
    return false;
  }

 if (MEMCOMPARE(finger,
                EM_ZCL_URI_QUERY_POSTFIX_DEVICE_ID,
                strlen(EM_ZCL_URI_QUERY_POSTFIX_DEVICE_ID)) == 0){
    uintmax_t deviceId;
    uintmax_t endpointId;
    finger += strlen(EM_ZCL_URI_QUERY_POSTFIX_DEVICE_ID);
    char * dot = strchr(finger, EM_ZCL_URI_QUERY_DOT);
    if (dot == NULL){
      return false;
    }

    if (emZclHexStringToInt((const uint8_t *)finger, dot - finger, &deviceId)){
      if (dot[1] == EM_ZCL_URI_QUERY_WILDCARD){
        context->flags |= EMBER_ZCL_CONTEXT_FILTER_BY_DEVICE_ID_WITH_ID;
        context->deviceId = deviceId;
        return true;
      } else {
        if (emZclHexStringToInt((const uint8_t *)&dot[1], end - &dot[1], &endpointId)){
          const EmZclEndpointEntry_t * ep = emZclFindEndpoint(endpointId);
          if (ep != NULL){
            context->flags |= EMBER_ZCL_CONTEXT_FILTER_BY_DEVICE_ID_WITH_ID;
            context->deviceId |= deviceId;
            context->flags |= EMBER_ZCL_CONTEXT_FILTER_BY_ENDPOINT;
            context->endpoint = ep;
            return true;
          }
        }
      }
    }
  }

  return false;
}

static bool wellKnownUriQueryVersionParse(EmZclContext_t *context,
                                          void *castString,
                                          uint8_t depth)
{
  char *string = (char *) castString;
  const uint16_t length = strlen(string);
  uintmax_t clusterRevision;

  // is it the weird c.v# or just v#?
  char *start = (char *)context->uriQuery[depth];
  char *dot = strstr(start + length, EM_ZCL_URI_QUERY_VERSION_KEY);
  char *end = start + context->uriQueryLength[depth];
  bool status = false;

  if (context->uriQueryLength[depth] <= length){
    return false;
  }
  
  if (dot != NULL){ // c.v#
    status = emZclHexStringToInt((const uint8_t *)dot + strlen(EM_ZCL_URI_QUERY_VERSION_KEY),
                                 (size_t)(end - dot - strlen(EM_ZCL_URI_QUERY_VERSION_KEY)),
                                 &clusterRevision);
  if (status){
      context->clusterRevision = clusterRevision;
      context->flags |= EMBER_ZCL_CONTEXT_FILTER_BY_CLUSTER_REVISION;
      return true;
    }
  } else { // v#
    status = emZclHexStringToInt((const uint8_t *)start + length + 1,
                                 context->uriQueryLength[depth] - length - 1,
                                 &clusterRevision);
    if (status){
      context->flags |= EMBER_ZCL_CONTEXT_QUERY_FOR_ZCLIP_SUPPORT_BY_RESOURCE_VERSION;
      context->clusterRevision = clusterRevision;
      return true;
    }
  }

  return false;
}

static bool wellKnownUriQueryClusterIdParse(EmZclContext_t *context,
                                            void *castString,
                                            uint8_t depth)
{
  char *string = (char *) castString;
  const uint16_t length = strlen(string);

  if (context->uriQueryLength[depth] == length) {
    context->flags |= EMBER_ZCL_CONTEXT_QUERY_FOR_ZCLIP_SUPPORT_BY_CLUS;
    return true;
  } else {
    bool matched;
    matched = emUriQueryZclStringtoCluster(context,
                                           depth,
                                           length);
    if (matched) {
      return true;
    }
  }

  return false;
}

static bool wellKnownUriQueryUidParse(EmZclContext_t *context,
                                      void *data,
                                      uint8_t depth)
{
  context->flags |= EMBER_ZCL_CONTEXT_QUERY_FOR_UID;
  uint16_t length = context->uriQueryLength[depth];
  if (context->uriQuery[depth][length - 1] == EM_ZCL_URI_QUERY_WILDCARD) {
    context->flags |= EMBER_ZCL_CONTEXT_QUERY_FOR_UID_PREFIX;
    length--;
  }
  return emZclStringToUid((context->uriQuery[depth]
                           + strlen((const char *)data)),
                          length - strlen((const char *)data),
                          &context->uid,
                          &context->uidBits);
}

// get well-known/core:
static const char *wellKnownCorePayload = "</zcl>;rt=urn:zcl;if=urn:zcl:v0";

static void wellKnownUriHandler(EmZclContext_t *context)
{
  if (context->flags == 0) {
    if (context->uriPathSegments >= 2
        && (MEMCOMPARE(context->uriPath[0], &EM_ZCL_URI_WELL_KNOWN, strlen(EM_ZCL_URI_WELL_KNOWN)) == 0)
        && (MEMCOMPARE(context->uriPath[1], EM_ZCL_URI_CORE, strlen(EM_ZCL_URI_CORE)) == 0)){
      emZclRespond205ContentLinkFormat((const uint8_t *)wellKnownCorePayload,
                                       strlen(wellKnownCorePayload));
    } else {
      emZclRespond405MethodNotAllowed();
    }
  } else {
    wellKnownUriQueryHandler(context);
  }
}

#define MAX_WELL_KNOWN_REPLY_PAYLOAD (400)
// get well-known/core?a=A&b=B:
static void wellKnownUriQueryHandler(EmZclContext_t *context)
{
  // code to reply to discovery by both endpoint and class type.
  char payload[MAX_WELL_KNOWN_REPLY_PAYLOAD];
  char *endOfPayload = &payload[MAX_WELL_KNOWN_REPLY_PAYLOAD];

  MEMSET(payload, 0, COUNTOF(payload));
  char *finger = payload;
  uint16_t payloadLength = 0;

  if (context->flags == EMBER_ZCL_CONTEXT_FILTER_BY_NONE) {
    return;
  }

  // /.well-known/core?rt=urn:zcl response to signal ZCLIP is supported.
  if (context->flags & EMBER_ZCL_CONTEXT_QUERY_FOR_ZCLIP_SUPPORT_BY_CLUS) {
    finger += emZclUriAppendUriPath(finger, endOfPayload, EMBER_ZCL_ENDPOINT_NULL, NULL);
    finger += sprintf(finger, EM_ZCL_URI_QUERY_PREFIX_CLUSTER_ID);
    finger += sprintf(finger, EM_ZCL_URI_RESPONSE_DELIMITER);
    finger += sprintf(finger, EM_ZCL_URI_QUERY_PREFIX_VERSION);
    finger += sprintf(finger, "v0;");
  } else if (context->flags & EMBER_ZCL_CONTEXT_QUERY_FOR_ZCLIP_SUPPORT_BY_RESOURCE_VERSION) {
    emZclRespond205ContentLinkFormat((const uint8_t *)wellKnownCorePayload,
                                     strlen(wellKnownCorePayload));
    return;
  } else if ((context->flags & EMBER_ZCL_CONTEXT_FILTER_BY_CLUS_ID)
             || (context->flags & EMBER_ZCL_CONTEXT_FILTER_BY_CLUS_ID_WITH_ID)
             || (context->flags & EMBER_ZCL_CONTEXT_FILTER_BY_CLUS_ID_WITH_ROLE)
             || (context->flags & EMBER_ZCL_CONTEXT_FILTER_BY_CLUSTER_REVISION)) {
    size_t i;
    for (i = 0; i < emZclEndpointCount; i++) {
      const EmZclEndpointEntry_t *epEntry = &emZclEndpointTable[i];
      const EmberZclClusterSpec_t **clusterSpecs = epEntry->clusterSpecs;
      const EmberZclClusterSpec_t *spec = NULL;
      spec = *clusterSpecs;

      while (spec != NULL) {
        bool append = true;
        if ((spec->manufacturerCode == EMBER_ZCL_MANUFACTURER_CODE_NULL)
            || (spec->manufacturerCode == context->clusterSpec.manufacturerCode)) {
          append = true;
        }

        if (append
            && (context->flags & EMBER_ZCL_CONTEXT_FILTER_BY_CLUS_ID_WITH_ID)
            && (spec->id != context->clusterSpec.id)) {
          append = false;
        }

        if (append
            && (context->flags & EMBER_ZCL_CONTEXT_FILTER_BY_CLUS_ID_WITH_ROLE)
            && (spec->role != context->clusterSpec.role)) {
          append = false;
        }

        if (append
            && (context->flags & EMBER_ZCL_CONTEXT_FILTER_BY_CLUSTER_REVISION)) {
          EmberZclClusterRevision_t clusterRevision;
          EmberZclStatus_t status;
          status = emberZclReadAttribute(epEntry->endpointId,
                                         spec,
                                         EMBER_ZCL_ATTRIBUTE_CLUSTER_REVISION,
                                         &clusterRevision,
                                         sizeof(clusterRevision));

          if ((status != EMBER_ZCL_STATUS_SUCCESS)
              || context->clusterRevision != clusterRevision) {
            append = false;
          }
        }

        if (append
            && (context->flags & EMBER_ZCL_CONTEXT_FILTER_BY_ENDPOINT)) { 
          if (epEntry->endpointId != context->endpoint->endpointId){
            append = false;
          }
        }

        if (append) {
          finger += emZclUriAppendUriPath(finger,
                                          endOfPayload,
                                          epEntry->endpointId,
                                          spec);
          finger += appendEndpointId(finger,
                                     endOfPayload,
                                     epEntry->endpointId,
                                     epEntry->deviceId,
                                     EM_ZCL_DISC_URI_STRING_APPEND_RESPONSE);
          finger += appendClusterRev(finger,
                                     endOfPayload,
                                     epEntry->endpointId,
                                     spec,
                                     EM_ZCL_DISC_URI_STRING_APPEND_RESPONSE);
          finger += appendClusterId(finger,
                                    endOfPayload,
                                    spec,
                                    true,
                                    EM_ZCL_DISC_URI_STRING_APPEND_RESPONSE);
          emZclUriBreak(finger);
        }

        clusterSpecs++;
        spec = *clusterSpecs;
      }
    }
  } else if ((context->flags & EMBER_ZCL_CONTEXT_FILTER_BY_DEVICE_ID_WITH_ID)
             || (context->flags & EMBER_ZCL_CONTEXT_FILTER_BY_DEVICE_ID_WITH_WILDCARD)
             || (context->flags & EMBER_ZCL_CONTEXT_FILTER_BY_ENDPOINT)) {
    size_t i;
    for (i = 0; i < emZclEndpointCount; i++) {
      const EmZclEndpointEntry_t *epEntry = &emZclEndpointTable[i];

      if ((context->deviceId == epEntry->deviceId)
          || (context->flags & EMBER_ZCL_CONTEXT_FILTER_BY_DEVICE_ID_WITH_WILDCARD)
          || ((context->flags & EMBER_ZCL_CONTEXT_FILTER_BY_ENDPOINT)
              && (context->endpoint->endpointId == epEntry->endpointId))) {
        finger += emZclUriAppendUriPath(finger, endOfPayload, epEntry->endpointId, NULL);
        finger += appendEndpointId(finger,
                                   endOfPayload,
                                   epEntry->endpointId,
                                   epEntry->deviceId,
                                   EM_ZCL_DISC_URI_STRING_APPEND_RESPONSE);
        emZclUriBreak(finger);
      }
    }
  } else if (context->flags & EMBER_ZCL_CONTEXT_QUERY_FOR_UID) {
    uint16_t bits = emMatchingPrefixBitLength(emZclUid.bytes,
                                              sizeof(emZclUid.bytes) * 8,
                                              context->uid.bytes,
                                              context->uidBits);
    if (bits == context->uidBits
        && (sizeof(emZclUid.bytes) * 8 == context->uidBits
            || (context->flags & EMBER_ZCL_CONTEXT_QUERY_FOR_UID_PREFIX))) {
      finger += emZclUriAppendUriPath(finger,
                                      endOfPayload,
                                      EMBER_ZCL_ENDPOINT_NULL,
                                      NULL); // cluster spec
      finger += sprintf(finger, EM_ZCL_URI_QUERY_UID);
      finger += emZclUidToString(&emZclUid,
                                 EMBER_ZCL_UID_BITS,
                                 (uint8_t *)finger);
    }
  }

  payloadLength = finger - payload;

  if ((payloadLength != 0) && uriDone(finger)) {
    payloadLength--;
  }

  emZclRespond205ContentLinkFormat((const uint8_t *)payload, payloadLength);
}

//----------------------------------------------------------------
// URI segment matching functions

// append "</zcl/e/EE/[cs]CCCC>;" without quotes.
uint16_t emZclUriAppendUriPath(char *finger,
                               char *endOfBuffer,
                               EmberZclEndpointId_t endpointId,
                               const EmberZclClusterSpec_t *clusterSpec)
{
  char *start = finger;

  if (endOfBuffer != NULL && (endOfBuffer - finger < EM_ZCL_URI_PATH_APPEND_MAX_LEN)) {
    return 0;
  }

  finger += sprintf(finger, "</zcl");

  if (endpointId != EMBER_ZCL_ENDPOINT_NULL) {
    finger += sprintf(finger, "/e/%d", endpointId);
    if (clusterSpec != NULL) {
      finger += sprintf(finger,
                        "/%c%x",
                        (clusterSpec->role == EMBER_ZCL_ROLE_CLIENT) ? 'c' : 's',
                        clusterSpec->id);
    }
  }

  finger += sprintf(finger, ">;");

  return finger - start;
}

static uint16_t appendEndpointId(char *finger,
                                 char *endOfBuffer,
                                 EmberZclEndpointId_t endpointId,
                                 EmberZclDeviceId_t deviceId,
                                 EmZclDiscUriStringAppendMode mode)
{
  char *start = finger;

  if (endOfBuffer != NULL && endOfBuffer - finger < EM_ZCL_URI_ENDPOINT_APPEND_MAX_LEN ) {
    return 0;
  }

  if (endpointId != EMBER_ZCL_ENDPOINT_NULL) {
    finger += sprintf(finger,
                      "%s%s%x.%d",
                      EM_ZCL_URI_QUERY_PREFIX_DEVICE_TYPE_AND_ENDPOINT, 
                      EM_ZCL_URI_QUERY_POSTFIX_DEVICE_ID,
                      deviceId,
                      endpointId);

    finger += appendUriDelimiter(finger, mode);

  }

  return finger - start;
}

static uint16_t appendUidString(char *finger,
                                const char *endOfBuffer,
                                const uint8_t *uidString,
                                EmZclDiscUriStringAppendMode mode)
{
  const char *start = finger;
  finger += sprintf(finger, EM_ZCL_URI_QUERY_UID);
  size_t length = strlen((const char *)uidString);
  MEMCOPY(finger, uidString, length);
  finger += length;
  finger += appendUriDelimiter(finger, mode);
  return finger - start;
}

static uint16_t appendProtocolRev(char *finger,
                                  char *endOfBuffer,
                                  const uint16_t version,
                                  EmZclDiscUriStringAppendMode mode)
{
  char * start = finger;
  if (endOfBuffer != NULL && endOfBuffer - finger < EM_ZCL_URI_CLUSTER_REVISION_APPEND_MAX_LEN) {
    return 0;
  }

  finger += sprintf(finger,
                    EM_ZCL_URI_QUERY_PROTOCOL_REVISION_FORMAT,
                    version);

  finger += appendUriDelimiter(finger, mode);
  return finger - start;
}


static uint16_t appendClusterRevByVal(char *finger,
                                      char *endOfBuffer,
                                      const uint16_t version,
                                      EmZclDiscUriStringAppendMode mode)
{
  char * start = finger;
  if (endOfBuffer != NULL && endOfBuffer - finger < EM_ZCL_URI_CLUSTER_REVISION_APPEND_MAX_LEN ) {
    return 0;
  }

  finger += sprintf(finger,
                    EM_ZCL_URI_QUERY_CLUSTER_REVISION_FORMAT,
                    version);

  finger += appendUriDelimiter(finger, mode);
  return finger - start;
}

static uint16_t appendClusterRev(char *finger,
                                      char *endOfBuffer,
                                      EmberZclEndpointId_t endpointId,
                                      const EmberZclClusterSpec_t *clusterSpec,
                                      EmZclDiscUriStringAppendMode mode)
{
  EmberZclClusterRevision_t clusterRevision;
  EmberZclStatus_t status;

  status = emberZclReadAttribute(endpointId,
                                 clusterSpec,
                                 EMBER_ZCL_ATTRIBUTE_CLUSTER_REVISION,
                                 &clusterRevision,
                                 sizeof(clusterRevision));

  if (status != EMBER_ZCL_STATUS_SUCCESS) {
    return 0;
  }

  return appendClusterRevByVal(finger,
                                      endOfBuffer,
                                      clusterRevision,
                                      mode);
}

uint16_t appendUriDelimiter(char * finger, EmZclDiscUriStringAppendMode mode)
{
  if (mode == EM_ZCL_DISC_URI_STRING_APPEND_REQUEST) {
    finger += sprintf(finger, "&");
  } else {
    finger += sprintf(finger, ";");
  }
  return 1;
}

static uint16_t appendClusterId(char *finger,
                                char *endOfBuffer,
                                const EmberZclClusterSpec_t *spec,
                                bool includeTrailingClusterTag,
                                EmZclDiscUriStringAppendMode mode)
{
  char *start = finger;

  if (endOfBuffer != NULL
      && (finger == NULL
          || (endOfBuffer - finger < EM_ZCL_URI_CLUSTER_ID_APPEND_MAX_LEN ))) {
   return 0;
  }

  finger += sprintf(finger, EM_ZCL_URI_QUERY_PREFIX_CLUSTER_ID);

  if (includeTrailingClusterTag){
    finger += sprintf(finger, ":c");
  }

  if (spec != NULL){
   finger += sprintf(finger,
                     ":%x.%c",
                     spec->id,
                     (spec->role == EMBER_ZCL_ROLE_CLIENT) ? 'c': 's');
  }

  finger += appendUriDelimiter(finger, mode);

  return finger - start;
}

static uint16_t appendDeviceIdAndEndpoint(char *finger,
                                          char *endOfBuffer,
                                          EmberZclDeviceId_t deviceId,
                                          EmberZclEndpointId_t endpointId,
                                          EmZclDiscUriStringAppendMode mode)
{
  char *start = finger;

  if (endOfBuffer != NULL
      && (finger == NULL
      || (endOfBuffer - finger < EM_ZCL_URI_DEVICE_ID_AND_ENDPOINT_APPEND_MAX_LEN )
      || ((deviceId == EMBER_ZCL_DEVICE_ID_NULL)
          && (endpointId == EMBER_ZCL_ENDPOINT_NULL)))) {
   return 0;
  }

  finger += sprintf(finger, EM_ZCL_URI_QUERY_PREFIX_DEVICE_TYPE_AND_ENDPOINT);
  finger += sprintf(finger, EM_ZCL_URI_QUERY_POSTFIX_DEVICE_ID);
  finger += sprintf(finger, "%x.", deviceId);

  if (endpointId == EMBER_ZCL_ENDPOINT_NULL){
    finger += sprintf(finger, "%c", EM_ZCL_URI_QUERY_WILDCARD);
  } else {
    finger += sprintf(finger, "%x", endpointId);
  }

  finger += appendUriDelimiter(finger, mode);

  return finger - start;
}

bool emZclUriBreak(char *finger)
{
  if (finger == NULL) {
    return false;
  }

  finger--;

  if (*finger == ';') {
    *finger = ',';
    return true;
  }

  return false;
}

static bool uriDone(char *finger)
{
  if (finger == NULL) {
    return false;
  }

  finger--;
  if (*finger == ';' || *finger == ',') {
    *finger = '\0';
    return true;
  }

  return false;
}

// ----------------------------------------------------------------------------
// Discovery CLI utilities functions

EmZclDiscoveryRequestMode discRequestMode =  EM_ZCL_DISCOVERY_REQUEST_SINGLE_QUERY;
#define EM_ZCL_DISC_CLI_MAX_BUFFER_LEN (512)
char cliDiscoveryRequestUri[EM_ZCL_DISC_CLI_MAX_BUFFER_LEN] = {0};
char * cliDiscoveryRequestUriFinger = cliDiscoveryRequestUri;

bool emberZclDiscSetMode(uint8_t mode) {
  if (mode >= EM_ZCL_DISCOVERY_REQUEST_MODE_MAX){
    return false;
  }

  if (discRequestMode != mode){
    emberZclDiscInit();
  }

  discRequestMode = (EmZclDiscoveryRequestMode) mode;
  return true;
}

// when disc mode is EM_ZCL_DISCOVERY_REQUEST_MULTIPLE_QUERY,
// this function init all the bookkeeping for query string building.
void emberZclDiscInit(void) {
  const uint16_t wellKnownCoreLen = strlen(EM_ZCL_URI_WELL_KNOWN_CORE);

  MEMSET(cliDiscoveryRequestUri, 0x00, sizeof(char)*EM_ZCL_DISC_CLI_MAX_BUFFER_LEN);
  cliDiscoveryRequestUriFinger = cliDiscoveryRequestUri;

  MEMCOPY(cliDiscoveryRequestUriFinger,
          EM_ZCL_URI_WELL_KNOWN_CORE,
          wellKnownCoreLen);
  cliDiscoveryRequestUriFinger += wellKnownCoreLen;
  
  MEMCOPY(cliDiscoveryRequestUriFinger ++, "?", 1);
}

bool emberZclDiscSend(EmberCoapResponseHandler responseHandler) {
  EmberIpv6Address allThreadNodes = {{0}};

  if (!emZclGetMulticastAddress(&allThreadNodes)){
    emberAfAppPrint("Unable to send discovery message!");
    return false;
  }

  emberAfAppPrintln("Sent discovery command: %s", cliDiscoveryRequestUri);

  EmberCoapSendInfo info = {0}; // use defaults
  EmberStatus emberStatus = emberCoapGet(&allThreadNodes,
                                         (const uint8_t *) cliDiscoveryRequestUri,
                                         responseHandler,
                                         &info);
  if (emberStatus != EMBER_SUCCESS){
    emberAfAppPrintln("Failed to send discovery message with error (0x%x)", emberStatus);
  } else {
    emberAfAppPrintln("%p 0x%x", "get", emberStatus);
  }

  return emberStatus == EMBER_SUCCESS;
}

bool emberZclDiscAppendQuery(EmZclDiscoveryRequest request, 
                             EmberCoapResponseHandler responseHandler)
{
  EmZclCliDiscoveryRequestType type = request.type;
  char ** finger, * end;
  
  if (discRequestMode == EM_ZCL_DISCOVERY_REQUEST_SINGLE_QUERY){
    emberZclDiscInit();
  }

  finger = &cliDiscoveryRequestUriFinger;
  end = &cliDiscoveryRequestUri[EM_ZCL_DISC_CLI_MAX_BUFFER_LEN]; 

  if (type == EM_ZCL_CLI_DISCOVERY_REQUEST_BY_CLUSTER_ID) {
    *finger += appendClusterId(*finger,
                               end,
                               request.data.clusterSpec,
                               true,
                               EM_ZCL_DISC_URI_STRING_APPEND_REQUEST);
  } else if (type == EM_ZCL_CLI_DISCOVERY_REQUEST_BY_DEVICE_ID) {
    *finger += appendDeviceIdAndEndpoint(*finger,
                                         end,
                                         request.deviceId,
                                         EMBER_ZCL_ENDPOINT_NULL,
                                         EM_ZCL_DISC_URI_STRING_APPEND_REQUEST);
  } else if (type == EM_ZCL_CLI_DISCOVERY_REQUEST_BY_ENDPOINT_AND_DEVICE_ID) {
    *finger += appendEndpointId(*finger,
                                end,
                                request.data.endpointId,
                                request.deviceId,
                                EM_ZCL_DISC_URI_STRING_APPEND_REQUEST);
  } else if (type == EM_ZCL_CLI_DISCOVERY_REQUEST_BY_UID_STRING) {
    *finger += appendUidString(*finger,
                               end,
                               request.data.uidString,
                               EM_ZCL_DISC_URI_STRING_APPEND_REQUEST);
  } else if (type == EM_ZCL_CLI_DISCOVERY_REQUEST_BY_RESOURCE_VERSION) {
    *finger += appendProtocolRev(*finger,
                                      end,
                                      request.data.version,
                                      EM_ZCL_DISC_URI_STRING_APPEND_REQUEST);
  } else if (type == EM_ZCL_CLI_DISCOVERY_REQUEST_BY_CLUSTER_REVISION) {
    *finger += appendClusterRevByVal(*finger,
                                            end,
                                            request.data.version,
                                            EM_ZCL_DISC_URI_STRING_APPEND_REQUEST);
  } else {
    return false;
  }

  if (discRequestMode == EM_ZCL_DISCOVERY_REQUEST_SINGLE_QUERY){
    return emberZclDiscSend(responseHandler);
  }

  return true;
}

bool emberZclDiscByClusterId(const EmberZclClusterSpec_t *clusterSpec, 
                             EmberCoapResponseHandler responseHandler)
{
  EmZclDiscoveryRequest request = {.type = EM_ZCL_CLI_DISCOVERY_REQUEST_BY_CLUSTER_ID,
                                   .data.clusterSpec = clusterSpec};
  return emberZclDiscAppendQuery(request, responseHandler);
}

bool emberZclDiscByEndpoint(EmberZclEndpointId_t endpointId, 
                            EmberZclDeviceId_t deviceId,
                            EmberCoapResponseHandler responseHandler)
{
  EmZclDiscoveryRequest request = {.type = EM_ZCL_CLI_DISCOVERY_REQUEST_BY_ENDPOINT_AND_DEVICE_ID,
                                   .data.endpointId = endpointId, 
                                   .deviceId = deviceId };
  return emberZclDiscAppendQuery(request, responseHandler);
}

bool emberZclDiscByUid(const EmberZclUid_t *uid,
                       uint16_t uidBits, 
                       EmberCoapResponseHandler responseHandler)
{
  if (uidBits > EMBER_ZCL_UID_SIZE * 8) {
    return false;
  }

  uint8_t string[EMBER_ZCL_UID_STRING_SIZE];
  uint8_t *finger = string;
  finger += emZclUidToString(uid, uidBits, finger);

  // A partially-specified UID becomes a prefix match.  Everything is NUL
  // termianted.
  if (uidBits != EMBER_ZCL_UID_BITS) {
    *finger++ = EM_ZCL_URI_QUERY_WILDCARD;
  }
  *finger = '\0';

  return emZclDiscByUidString(string, responseHandler);
}

bool emZclDiscByUidString(const uint8_t *uidString, 
                          EmberCoapResponseHandler responseHandler)
{
  EmZclDiscoveryRequest request = {
    .type = EM_ZCL_CLI_DISCOVERY_REQUEST_BY_UID_STRING,
    .data.uidString = uidString,
  };
  return emberZclDiscAppendQuery(request, responseHandler);
}

bool emberZclDiscByResourceVersion(EmberZclClusterRevision_t version, 
                                   EmberCoapResponseHandler responseHandler)
{
  EmZclDiscoveryRequest request = {.type = EM_ZCL_CLI_DISCOVERY_REQUEST_BY_RESOURCE_VERSION,
                                   .data.version = version};
  return emberZclDiscAppendQuery(request, responseHandler);
}

bool emberZclDiscByClusterRev(EmberZclClusterRevision_t version, 
                              EmberCoapResponseHandler responseHandler)
{
  EmZclDiscoveryRequest request = {.type = EM_ZCL_CLI_DISCOVERY_REQUEST_BY_CLUSTER_REVISION,
                                   .data.version = version};
  return emberZclDiscAppendQuery(request, responseHandler);
}

bool emberZclDiscByDeviceId(EmberZclDeviceId_t deviceId, 
                            EmberCoapResponseHandler responseHandler)
{
  EmZclDiscoveryRequest request = {.type = EM_ZCL_CLI_DISCOVERY_REQUEST_BY_DEVICE_ID,
                                   .deviceId = deviceId};
  return emberZclDiscAppendQuery(request, responseHandler);
}
