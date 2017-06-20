// Copyright 2015 Silicon Laboratories, Inc.

#ifndef __ZCL_CORE_TYPES_H__
#define __ZCL_CORE_TYPES_H__

// The longest ZCL/IP URI is:
//   coaps://nih:sha-256;<uid>:PPPPP/zcl/g/GGGG/RMMMM_CCCC/a/AAAA
// where <uid> is a 256-bit UID represented as 64 hexademical characters, PPPPP
// is a 16-bit UDP port in decimal, GGGG is the 16-bit group id in hexademical,
// R is c or s for client or server, MMMM is the 16-bit manufacturer code in
// hexademical, CCCC is the 16-bit cluster id in hexademical, and AAAA is the
// 16-bit attribute id in hexademical.  An extra byte is reserved for a null
// terminator.
#define EMBER_ZCL_URI_MAX_LENGTH 120

// The longest ZCL/IP URI path is a manufacturer-specific attribute request
// sent to a group:
//   zcl/g/GGGG/RMMMM_CCCC/a/AAAA
// where GGGG is the 16-bit group id, R is c or s for client or server, MMMM is
// the 16-bit manufacturer code, CCCC is the 16-bit cluster id, and AAAA is the
// 16-bit attribute id.  An extra byte is reserved for a null terminator.
#define EMBER_ZCL_URI_PATH_MAX_LENGTH 29

// The longest cluster id in a ZCL/IP URI path is manufacturer-specific:
//   RMMMM_CCCC
// where R is c or s for client or server, MMMM is the 16-bit manufacturer
// code, and CCCC is the 16-bit cluster id.  An extra byte is reserved for a
// null terminator.
#define EMBER_ZCL_URI_PATH_CLUSTER_ID_MAX_LENGTH 11

// Manufacturer codes, if present, are separated from the cluster id by an
// underscore.
#define EMBER_ZCL_URI_PATH_MANUFACTURER_CODE_CLUSTER_ID_SEPARATOR '_'

// -----------------------------------------------------------------------------
// Service Discoveries.

#define EMBER_ZCL_CONTEXT_FILTER_BY_NONE                               (1 << 0)
#define EMBER_ZCL_CONTEXT_FILTER_BY_CLUS_ID                            (1 << 1)
#define EMBER_ZCL_CONTEXT_FILTER_BY_CLUS_ID_WITH_ID                    (1 << 2)
#define EMBER_ZCL_CONTEXT_FILTER_BY_CLUS_ID_WITH_ROLE                  (1 << 3)
#define EMBER_ZCL_CONTEXT_FILTER_BY_DEVICE_ID_WITH_WILDCARD            (1 << 4)
#define EMBER_ZCL_CONTEXT_FILTER_BY_DEVICE_ID_WITH_ID                  (1 << 5)
#define EMBER_ZCL_CONTEXT_FILTER_BY_CLUSTER_REVISION                   (1 << 6)
#define EMBER_ZCL_CONTEXT_FILTER_BY_ENDPOINT                           (1 << 7)
#define EMBER_ZCL_CONTEXT_QUERY_FOR_ZCLIP_SUPPORT_BY_CLUS              (1 << 8)
#define EMBER_ZCL_CONTEXT_QUERY_FOR_ZCLIP_SUPPORT_BY_RESOURCE_VERSION  (1 << 9)
#define EMBER_ZCL_CONTEXT_QUERY_FOR_UID                                (1 << 10)
#define EMBER_ZCL_CONTEXT_QUERY_FOR_UID_PREFIX                         (1 << 11)

typedef uint16_t EmberZclContextFlags_t;
typedef uint16_t EmberZclDeviceId_t;
#define EMBER_ZCL_DEVICE_ID_NULL ((EmberZclDeviceId_t)-1)

// -----------------------------------------------------------------------------
// Messages.

#ifdef DOXYGEN_SHOULD_SKIP_THIS
enum EmberZclMessageStatus_t
#else
typedef uint8_t EmberZclMessageStatus_t;
enum
#endif
{
  EMBER_ZCL_MESSAGE_STATUS_DISCOVERY_TIMEOUT = 0x00,
  EMBER_ZCL_MESSAGE_STATUS_COAP_TIMEOUT      = 0x01,
  EMBER_ZCL_MESSAGE_STATUS_COAP_ACK          = 0x02,
  EMBER_ZCL_MESSAGE_STATUS_COAP_RESET        = 0x03,
  EMBER_ZCL_MESSAGE_STATUS_COAP_RESPONSE     = 0x04,
  EMBER_ZCL_MESSAGE_STATUS_NULL              = 0xFF,
};

#ifndef DOXYGEN_SHOULD_SKIP_THIS
typedef void (*EmZclMessageResponseHandler)(EmberZclMessageStatus_t status,
                                            EmberCoapCode code,
                                            const uint8_t *payload,
                                            size_t payloadLength,
                                            const void *applicationData,
                                            uint16_t applicationDataLength);
#endif

// -----------------------------------------------------------------------------
// UIDs.

#define EMBER_ZCL_UID_BITS          256
#define EMBER_ZCL_UID_SIZE          EMBER_BITS_TO_BYTES(EMBER_ZCL_UID_BITS)
#define EMBER_ZCL_UID_STRING_LENGTH (EMBER_ZCL_UID_BITS / 4) // bits to nibbles
#define EMBER_ZCL_UID_STRING_SIZE   (EMBER_ZCL_UID_STRING_LENGTH + 1) // NUL

typedef struct {
  uint8_t bytes[EMBER_ZCL_UID_SIZE];
} EmberZclUid_t;

// -----------------------------------------------------------------------------
// Endpoints.

typedef uint8_t EmberZclEndpointId_t;
#define EMBER_ZCL_ENDPOINT_MIN  0x01
#define EMBER_ZCL_ENDPOINT_MAX  0xF0
#define EMBER_ZCL_ENDPOINT_NULL ((EmberZclEndpointId_t)-1)

typedef uint8_t EmberZclEndpointIndex_t;
#define EMBER_ZCL_ENDPOINT_INDEX_NULL ((EmberZclEndpointIndex_t)-1)

// -----------------------------------------------------------------------------
// Groups.

typedef uint16_t EmberZclGroupId_t;
#define EMBER_ZCL_GROUP_ALL_ENDPOINTS 0x0000
#define EMBER_ZCL_GROUP_MIN           0x0001
#define EMBER_ZCL_GROUP_MAX           0xFFF7
#define EMBER_ZCL_GROUP_NULL          ((EmberZclGroupId_t)-1)

// -----------------------------------------------------------------------------
// Roles.

#ifdef DOXYGEN_SHOULD_SKIP_THIS
enum EmberZclRole_t
#else
typedef uint8_t EmberZclRole_t;
enum
#endif
{
  EMBER_ZCL_ROLE_CLIENT = 0,
  EMBER_ZCL_ROLE_SERVER = 1,
};

// -----------------------------------------------------------------------------
// Manufacturer codes.

typedef uint16_t EmberZclManufacturerCode_t;
#define EMBER_ZCL_MANUFACTURER_CODE_NULL 0x0000

// -----------------------------------------------------------------------------
// Clusters.

typedef uint16_t EmberZclClusterId_t;
#define EMBER_ZCL_CLUSTER_NULL ((EmberZclClusterId_t)-1)

typedef struct {
  EmberZclRole_t role;
  EmberZclManufacturerCode_t manufacturerCode;
  EmberZclClusterId_t id;
} EmberZclClusterSpec_t;

// -----------------------------------------------------------------------------
// Attributes.

typedef uint16_t EmberZclAttributeId_t;
#define EMBER_ZCL_ATTRIBUTE_CLUSTER_REVISION 0xFFFD
#define EMBER_ZCL_ATTRIBUTE_REPORTING_STATUS 0xFFFE
#define EMBER_ZCL_ATTRIBUTE_NULL             ((EmberZclAttributeId_t)-1)

typedef uint16_t EmberZclClusterRevision_t;
#define EMBER_ZCL_CLUSTER_REVISION_PRE_ZCL6 0
#define EMBER_ZCL_CLUSTER_REVISION_ZCL6     1
#define EMBER_ZCL_CLUSTER_REVISION_NULL     ((EmberZclClusterRevision_t)-1)

typedef struct {
  EmberCoapCode code;
  EmberZclGroupId_t groupId;
  EmberZclEndpointId_t endpointId;
  const EmberZclClusterSpec_t *clusterSpec;
  EmberZclAttributeId_t attributeId;

#ifndef DOXYGEN_SHOULD_SKIP_THIS
  CborState *state;
#endif
} EmberZclAttributeContext_t;

// -----------------------------------------------------------------------------
// Bindings.

typedef uint8_t EmberZclBindingId_t;
#define EMBER_ZCL_BINDING_NULL ((EmberZclBindingId_t)-1)

typedef struct {
  EmberCoapCode code;
  EmberZclGroupId_t groupId;
  EmberZclEndpointId_t endpointId;
  const EmberZclClusterSpec_t *clusterSpec;
  EmberZclBindingId_t bindingId;
} EmberZclBindingContext_t;

// -----------------------------------------------------------------------------
// Commands.

typedef uint8_t EmberZclCommandId_t;
#define EMBER_ZCL_COMMAND_NULL ((EmberZclCommandId_t)-1)

typedef struct {
  EmberIpv6Address remoteAddress;
  EmberCoapCode code;
  const uint8_t *payload;
  uint16_t payloadLength;
  EmberZclGroupId_t groupId;
  EmberZclEndpointId_t endpointId;
  const EmberZclClusterSpec_t *clusterSpec;
  EmberZclCommandId_t commandId;

#ifndef DOXYGEN_SHOULD_SKIP_THIS
  CborState *state;
  uint8_t *buffer;
  bool success;
#endif
} EmberZclCommandContext_t;

// -----------------------------------------------------------------------------
// Reporting.

typedef uint8_t EmberZclReportingConfigurationId_t;
#define EMBER_ZCL_REPORTING_CONFIGURATION_DEFAULT 0
#define EMBER_ZCL_REPORTING_CONFIGURATION_NULL    ((EmberZclReportingConfigurationId_t)-1)

typedef struct {
  EmberIpv6Address remoteAddress;
  EmberZclEndpointId_t sourceEndpointId;
  EmberZclReportingConfigurationId_t sourceReportingConfigurationId;
  uint32_t sourceTimestamp;
  EmberZclGroupId_t groupId;
  EmberZclEndpointId_t endpointId;

#ifndef DOXYGEN_SHOULD_SKIP_THIS
  const EmberZclClusterSpec_t *clusterSpec;
  EmberZclAttributeId_t attributeId;
  uint8_t *buffer;
  size_t bufferLength;
#endif
} EmberZclNotificationContext_t;

// -----------------------------------------------------------------------------
// Types.

// From 07-5123-05, section 2.5.3, table 2-10.
#ifdef DOXYGEN_SHOULD_SKIP_THIS
enum EmberZclStatus_t
#else
typedef uint8_t EmberZclStatus_t;
enum
#endif
{
  EMBER_ZCL_STATUS_SUCCESS                     = 0x00,
  EMBER_ZCL_STATUS_FAILURE                     = 0x01,
  EMBER_ZCL_STATUS_NOT_AUTHORIZED              = 0x7E,
  EMBER_ZCL_STATUS_RESERVED_FIELD_NOT_ZERO     = 0x7F,
  EMBER_ZCL_STATUS_MALFORMED_COMMAND           = 0x80,
  EMBER_ZCL_STATUS_UNSUP_CLUSTER_COMMAND       = 0x81,
  EMBER_ZCL_STATUS_UNSUP_GENERAL_COMMAND       = 0x82,
  EMBER_ZCL_STATUS_UNSUP_MANUF_CLUSTER_COMMAND = 0x83,
  EMBER_ZCL_STATUS_UNSUP_MANUF_GENERAL_COMMAND = 0x84,
  EMBER_ZCL_STATUS_INVALID_FIELD               = 0x85,
  EMBER_ZCL_STATUS_UNSUPPORTED_ATTRIBUTE       = 0x86,
  EMBER_ZCL_STATUS_INVALID_VALUE               = 0x87,
  EMBER_ZCL_STATUS_READ_ONLY                   = 0x88,
  EMBER_ZCL_STATUS_INSUFFICIENT_SPACE          = 0x89,
  EMBER_ZCL_STATUS_DUPLICATE_EXISTS            = 0x8A,
  EMBER_ZCL_STATUS_NOT_FOUND                   = 0x8B,
  EMBER_ZCL_STATUS_UNREPORTABLE_ATTRIBUTE      = 0x8C,
  EMBER_ZCL_STATUS_INVALID_DATA_TYPE           = 0x8D,
  EMBER_ZCL_STATUS_INVALID_SELECTOR            = 0x8E,
  EMBER_ZCL_STATUS_WRITE_ONLY                  = 0x8F,
  EMBER_ZCL_STATUS_INCONSISTENT_STARTUP_STATE  = 0x90,
  EMBER_ZCL_STATUS_DEFINED_OUT_OF_BAND         = 0x91,
  EMBER_ZCL_STATUS_INCONSISTENT                = 0x92,
  EMBER_ZCL_STATUS_ACTION_DENIED               = 0x93,
  EMBER_ZCL_STATUS_TIMEOUT                     = 0x94,
  EMBER_ZCL_STATUS_ABORT                       = 0x95,
  EMBER_ZCL_STATUS_INVALID_IMAGE               = 0x96,
  EMBER_ZCL_STATUS_WAIT_FOR_DATA               = 0x97,
  EMBER_ZCL_STATUS_NO_IMAGE_AVAILABLE          = 0x98,
  EMBER_ZCL_STATUS_REQUIRE_MORE_IMAGE          = 0x99,
  EMBER_ZCL_STATUS_NOTIFICATION_PENDING        = 0x9A,
  EMBER_ZCL_STATUS_HARDWARE_FAILURE            = 0xC0,
  EMBER_ZCL_STATUS_SOFTWARE_FAILURE            = 0xC1,
  EMBER_ZCL_STATUS_CALIBRATION_ERROR           = 0xC2,
  EMBER_ZCL_STATUS_NULL                        = 0xFF,
};

// From 07-5123-05, section 2.5.2, table 2-9.
typedef uint8_t  data8_t;
typedef uint16_t data16_t;
//typedef uint24_t data24_t;
typedef uint32_t data32_t;
//typedef uint40_t data40_t;
//typedef uint48_t data48_t;
//typedef uint56_t data56_t;
typedef uint64_t data64_t;
typedef uint8_t  bitmap8_t;
typedef uint16_t bitmap16_t;
//typedef uint24_t bitmap24_t;
typedef uint32_t bitmap32_t;
//typedef uint40_t bitmap40_t;
//typedef uint48_t bitmap48_t;
//typedef uint56_t bitmap56_t;
typedef uint64_t bitmap64_t;
typedef uint8_t  enum8_t;
typedef uint16_t enum16_t;
typedef uint32_t utc_time_t;

// -----------------------------------------------------------------------------
// Addresses.

#ifdef DOXYGEN_SHOULD_SKIP_THIS
enum EmberZclScheme_t
#else
typedef uint8_t EmberZclScheme_t;
enum
#endif
{
  EMBER_ZCL_SCHEME_COAP  = 0x00,
  EMBER_ZCL_SCHEME_COAPS = 0x01,
};

#ifdef DOXYGEN_SHOULD_SKIP_THIS
enum EmberZclNetworkDestinationType_t
#else
typedef uint8_t EmberZclNetworkDestinationType_t;
enum
#endif
{
  EMBER_ZCL_NETWORK_DESTINATION_TYPE_ADDRESS  = 0x00,
  EMBER_ZCL_NETWORK_DESTINATION_TYPE_UID      = 0x01,
  //EMBER_ZCL_NETWORK_DESTINATION_TYPE_HOSTNAME = 0x02,
};

typedef struct {
  EmberZclScheme_t scheme;
  union {
    EmberIpv6Address address;
    EmberZclUid_t uid;
  } data;
  EmberZclNetworkDestinationType_t type;
  uint16_t port;
} EmberZclNetworkDestination_t;

#ifdef DOXYGEN_SHOULD_SKIP_THIS
enum EmberZclApplicationDestinationType_t
#else
typedef uint8_t EmberZclApplicationDestinationType_t;
enum
#endif
{
  EMBER_ZCL_APPLICATION_DESTINATION_TYPE_ENDPOINT = 0x00,
  EMBER_ZCL_APPLICATION_DESTINATION_TYPE_GROUP    = 0x01,
};

typedef struct {
  union {
    EmberZclEndpointId_t endpointId;
    EmberZclGroupId_t groupId;
  } data;
  EmberZclApplicationDestinationType_t type;
} EmberZclApplicationDestination_t;

typedef struct {
  EmberZclNetworkDestination_t network;
  EmberZclApplicationDestination_t application;
} EmberZclDestination_t;

#ifndef DOXYGEN_SHOULD_SKIP_THIS
typedef uint16_t EmZclCacheIndex_t;
typedef struct {
  EmberZclNetworkDestination_t key;
  EmberZclNetworkDestination_t value;
  EmZclCacheIndex_t index;
} EmZclCacheEntry_t;

typedef bool (*EmZclCacheScanPredicate)(const void *criteria,
                                        const EmZclCacheEntry_t *entry);
#endif

// -----------------------------------------------------------------------------
// Endpoints.

#ifndef DOXYGEN_SHOULD_SKIP_THIS
typedef struct {
  EmberZclEndpointId_t endpointId;
  EmberZclDeviceId_t deviceId;
  const EmberZclClusterSpec_t **clusterSpecs;
} EmZclEndpointEntry_t;
#endif

// -----------------------------------------------------------------------------
// Attributes.

typedef void (*EmberZclReadAttributeResponseHandler)(EmberZclMessageStatus_t status,
                                                     const EmberZclAttributeContext_t *context,
                                                     const void *buffer,
                                                     size_t bufferLength);
typedef void (*EmberZclWriteAttributeResponseHandler)(EmberZclMessageStatus_t status,
                                                      const EmberZclAttributeContext_t *context,
                                                      EmberZclStatus_t result);
typedef struct {
  EmberZclAttributeId_t attributeId;
  const void *buffer;
  size_t bufferLength;
} EmberZclAttributeWriteData_t;

#ifndef DOXYGEN_SHOULD_SKIP_THIS
typedef uint16_t EmZclAttributeMask_t;
enum
{
  EM_ZCL_ATTRIBUTE_STORAGE_NONE           = 0x0000, // b0000_0000 b0000_0000
  EM_ZCL_ATTRIBUTE_STORAGE_TYPE_EXTERNAL  = 0x0001, // b0000_0000 b0000_0001
  EM_ZCL_ATTRIBUTE_STORAGE_TYPE_RAM       = 0x0003, // b0000_0000 b0000_0011
  EM_ZCL_ATTRIBUTE_STORAGE_TYPE_MASK      = 0x0003, // b0000_0000 b0000_0011
  EM_ZCL_ATTRIBUTE_STORAGE_SINGLETON_MASK = 0x0004, // b0000_0000 b0000_0100
  EM_ZCL_ATTRIBUTE_STORAGE_MASK           = 0x0007, // b0000_0000 b0000_0111

  EM_ZCL_ATTRIBUTE_ACCESS_READABLE        = 0x0010, // b0000_0000 b0001_0000
  EM_ZCL_ATTRIBUTE_ACCESS_WRITABLE        = 0x0020, // b0000_0000 b0010_0000
  EM_ZCL_ATTRIBUTE_ACCESS_REPORTABLE      = 0x0040, // b0000_0000 b0100_0000
  EM_ZCL_ATTRIBUTE_ACCESS_MASK            = 0x0070, // b0000_0000 b0111_0000

  EM_ZCL_ATTRIBUTE_DATA_DEFAULT           = 0x0100, // b0000_0001 b0000_0000
  EM_ZCL_ATTRIBUTE_DATA_MINIMUM           = 0x0200, // b0000_0010 b0000_0000
  EM_ZCL_ATTRIBUTE_DATA_MAXIMUM           = 0x0400, // b0000_0100 b0000_0000
  EM_ZCL_ATTRIBUTE_DATA_MASK              = 0x0700, // b0000_0111 b0000_0000
  EM_ZCL_ATTRIBUTE_DATA_BOUNDED           = 0x0800, // b0000_1000 b0000_0000
};

typedef struct {
  const EmberZclClusterSpec_t *clusterSpec;
  EmberZclAttributeId_t attributeId;
  EmZclAttributeMask_t mask;
  size_t dataOffset;
  size_t defaultMinMaxLookupOffset;
  size_t size;
  uint8_t type;
} EmZclAttributeEntry_t;

typedef uint8_t EmZclAttributeQueryFilterType_t;
enum {
  EM_ZCL_ATTRIBUTE_QUERY_FILTER_TYPE_ID,
  EM_ZCL_ATTRIBUTE_QUERY_FILTER_TYPE_COUNT,
  EM_ZCL_ATTRIBUTE_QUERY_FILTER_TYPE_RANGE,
  EM_ZCL_ATTRIBUTE_QUERY_FILTER_TYPE_WILDCARD,
};

typedef struct {
  EmberZclAttributeId_t start;
  uint16_t count;
} EmZclAttributeQueryFilterCountData_t;

typedef struct {
  EmberZclAttributeId_t start;
  EmberZclAttributeId_t end;
} EmZclAttributeQueryFilterRangeData_t;

typedef struct {
  EmZclAttributeQueryFilterType_t type;
  union {
    EmberZclAttributeId_t attributeId;
    EmZclAttributeQueryFilterCountData_t countData;
    EmZclAttributeQueryFilterRangeData_t rangeData;
  } data;
} EmZclAttributeQueryFilter_t;

#define EM_ZCL_ATTRIBUTE_QUERY_FILTER_COUNT_MAX 10

typedef struct {
  // f=
  EmZclAttributeQueryFilter_t filters[EM_ZCL_ATTRIBUTE_QUERY_FILTER_COUNT_MAX];
  uint8_t filterCount;
  // u
  bool undivided;
} EmZclAttributeQuery_t;
#endif

// -----------------------------------------------------------------------------
// Bindings.

typedef struct {
  // From URI.
  EmberZclEndpointId_t endpointId;
  EmberZclClusterSpec_t clusterSpec;

  // From payload.
  EmberZclDestination_t destination;
  EmberZclReportingConfigurationId_t reportingConfigurationId;
} EmberZclBindingEntry_t;

typedef void (*EmberZclBindingResponseHandler)(EmberZclMessageStatus_t status,
                                               const EmberZclBindingContext_t *context,
                                               const EmberZclBindingEntry_t *entry);

// -----------------------------------------------------------------------------
// Commands.

#ifndef DOXYGEN_SHOULD_SKIP_THIS
typedef void (*EmZclRequestHandler)(const EmberZclCommandContext_t *context,
                                    const void *request);
typedef void (*EmZclResponseHandler)(EmberZclMessageStatus_t status,
                                     const EmberZclCommandContext_t *context,
                                     const void *response);
typedef struct {
  const EmberZclClusterSpec_t *clusterSpec;
  EmberZclCommandId_t commandId;
  const ZclipStructSpec *spec;
  EmZclRequestHandler handler;
} EmZclCommandEntry_t;
#endif

// -----------------------------------------------------------------------------
// Groups.

typedef struct {
  EmberZclGroupId_t groupId;
  EmberZclEndpointId_t endpointId;
} EmberZclGroupEntry_t;

// -----------------------------------------------------------------------------
// Reporting.

typedef struct {
  uint16_t backoffMs;
  uint16_t minimumIntervalS;
  uint16_t maximumIntervalS;
} EmberZclReportingConfiguration_t;

// -----------------------------------------------------------------------------
// Internal.

#ifndef DOXYGEN_SHOULD_SKIP_THIS
#define MAX_URI_PATH_SEGMENTS 6 // zcl/[eg]/XX/<cluster>/Y/ZZ
#define MAX_URI_QUERY_SEGMENTS 10 // actual value TBD

typedef struct {
  EmberCoapCode code;
  EmberCoapReadOptions *options;
  const uint8_t *payload;
  uint16_t payloadLength;
  const EmberCoapRequestInfo *info;
  const uint8_t *uriPath[MAX_URI_PATH_SEGMENTS];
  uint16_t uriPathLength[MAX_URI_PATH_SEGMENTS];
  uint8_t uriPathSegments;
  const uint8_t *uriQuery[MAX_URI_PATH_SEGMENTS];
  uint16_t uriQueryLength[MAX_URI_PATH_SEGMENTS];
  uint8_t uriQuerySegments;

  // Values parsed out of the URI Path
  const EmZclEndpointEntry_t *endpoint;
  EmberZclGroupId_t groupId;
  const EmZclAttributeEntry_t *attribute;
  EmberZclBindingId_t bindingId;
  const EmZclCommandEntry_t *command;
  EmberZclReportingConfigurationId_t reportingConfigurationId;

  // Values parsed out of the URI Queries
  EmberZclClusterSpec_t clusterSpec;
  EmberZclDeviceId_t deviceId;
  EmberZclClusterRevision_t clusterRevision;
  EmberZclContextFlags_t flags;
  EmZclAttributeQuery_t attributeQuery;

  EmberZclUid_t uid;
  uint16_t uidBits;
} EmZclContext_t;

typedef bool (*EmZclMultiEndpointHandler)(const EmZclContext_t *context,
                                          CborState *state,
                                          void *data);

typedef EmberStatus (*EmZclCliRequestCommandFunction)(
  const EmberZclDestination_t *destination,
  const void *payloadStruct,
  const EmZclResponseHandler responseHandler);

typedef uint8_t EmZclUriFlag;
enum {
  EM_ZCL_URI_FLAG_METHOD_MASK    = 0x0F,
  EM_ZCL_URI_FLAG_METHOD_GET     = 0x01,
  EM_ZCL_URI_FLAG_METHOD_POST    = 0x02,
  EM_ZCL_URI_FLAG_METHOD_PUT     = 0x04,
  EM_ZCL_URI_FLAG_METHOD_DELETE  = 0x08,
};

typedef bool (EmZclSegmentMatch)(EmZclContext_t *context, void *data, uint8_t depth);
typedef void (EmZclUriAction)(EmZclContext_t *context);

typedef const struct {
  EmZclSegmentMatch *match;
  void *data;
  EmZclSegmentMatch *parse;
} EmZclUriQuery;

typedef const struct {
  uint8_t matchSkip;    // how many entries to skip if the match succeeds
  uint8_t failSkip;     // how many entries to skip if the match fails
  EmZclUriFlag flags;
  EmZclSegmentMatch *match;
  void *data;
  EmZclUriQuery *queries;
  EmZclUriAction *action;
} EmZclUriPath;

// data representation of a URI-reference / context of URI.
// e.g. </zc/e/EE/[sc]CCCC>
typedef struct {
  EmberZclEndpointId_t endpointId;
  EmberZclClusterSpec_t *clusterSpec;
} EmZclUriContext_t;
#endif

#endif // __ZCL_CORE_H__
