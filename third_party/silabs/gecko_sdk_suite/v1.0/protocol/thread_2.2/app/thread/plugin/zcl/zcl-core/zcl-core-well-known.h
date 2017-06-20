// Copyright 2016 Silicon Laboratories, Inc.

#include PLATFORM_HEADER
#include CONFIGURATION_HEADER
#include "zcl-core.h"

void emberZclDiscInit(void);
bool emberZclDiscSetMode(uint8_t mode);
bool emberZclDiscSend(EmberCoapResponseHandler responseHandler);

bool emberZclDiscByClusterId(const EmberZclClusterSpec_t *clusterSpec,
                             EmberCoapResponseHandler responseHandler);
bool emberZclDiscByEndpoint(EmberZclEndpointId_t endpointId,
                            EmberZclDeviceId_t deviceId,
                            EmberCoapResponseHandler responseHandler);
bool emberZclDiscByUid(const EmberZclUid_t *uid,
                       uint16_t uidBits,
                       EmberCoapResponseHandler responseHandler);
bool emberZclDiscByClusterRev(EmberZclClusterRevision_t version,
                              EmberCoapResponseHandler responseHandler);
bool emberZclDiscByDeviceId(EmberZclDeviceId_t deviceId,
                            EmberCoapResponseHandler responseHandler);
bool emberZclDiscByResourceVersion(EmberZclClusterRevision_t version,
                                   EmberCoapResponseHandler responseHandler);

#ifndef DOXYGEN_SHOULD_SKIP_THIS
typedef enum {
  EM_ZCL_DISC_URI_STRING_APPEND_REQUEST,
  EM_ZCL_DISC_URI_STRING_APPEND_RESPONSE,
} EmZclDiscUriStringAppendMode;

extern EmZclUriPath emZclWellKnowUriPaths[];

#define EM_ZCL_URI_QUERY_UID                                "ep=nih:sha-256;"
#define EM_ZCL_URI_QUERY_PREFIX_CLUSTER_ID                  "rt=urn:zcl"
#define EM_ZCL_URI_QUERY_PREFIX_VERSION                     "if=urn:zcl:"
#define EM_ZCL_URI_QUERY_PROTOCOL_REVISION_FORMAT           "if=urn:zcl:v%x"
#define EM_ZCL_URI_QUERY_CLUSTER_REVISION_FORMAT            "if=urn:zcl:c.v%x"
#define EM_ZCL_URI_QUERY_PREFIX_DEVICE_TYPE_AND_ENDPOINT    "ze=urn:zcl:"
#define EM_ZCL_URI_QUERY_POSTFIX_DEVICE_ID                  "d."
#define EM_ZCL_URI_QUERY_CLUSTER_ID_LEN                     (3)
#define EM_ZCL_URI_QUERY_DOT                                '.'
#define EM_ZCL_URI_QUERY_WILDCARD                           '*'
#define EM_ZCL_URI_QUERY_VERSION_KEY                        "c.v"
#define EM_ZCL_URI_WELL_KNOWN                               ".well-known"
#define EM_ZCL_URI_CORE                                     "core"
#define EM_ZCL_URI_WELL_KNOWN_CORE                          ".well-known/core"
#define EM_ZCL_URI_RESPONSE_DELIMITER                       ";"

uint16_t emZclUriAppendUriPath(char *finger,
                               char *endOfBuffer,
                               EmberZclEndpointId_t endpointId,
                               const EmberZclClusterSpec_t *clusterSpec);
bool emZclUriBreak(char *finger);
bool emZclDiscByUidString(const uint8_t *uidString, EmberCoapResponseHandler responseHandler);
#endif
