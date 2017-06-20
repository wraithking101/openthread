// Copyright 2015 Silicon Laboratories, Inc.

#include PLATFORM_HEADER
#include CONFIGURATION_HEADER
#include "thread-bookkeeping.h"
#include "thread-callbacks.h"
#include "zcl-core.h"

typedef struct {
  EmberZclAttributeContext_t context;
  EmberZclReadAttributeResponseHandler readHandler;
  EmberZclWriteAttributeResponseHandler writeHandler;
} Response;

typedef struct {
  uint16_t count;
  uint16_t usedCounts;
} FilterState;

static size_t attributeSize(const EmZclAttributeEntry_t *attribute,
                            const void *data);
static void *attributeDataLocation(EmberZclEndpointIndex_t endpointIndex,
                                   const EmZclAttributeEntry_t *attribute);
static const void *attributeDefaultMinMaxLocation(const EmZclAttributeEntry_t *attribute,
                                                  EmZclAttributeMask_t dataBit);
static bool isValueLocIndirect(const EmZclAttributeEntry_t *attribute);

static bool isLessThan(const uint8_t *dataA,
                       size_t dataALength,
                       const uint8_t *dataB,
                       size_t dataBLength);

static bool getAttributeIdsHandler(const EmZclContext_t *context,
                                   CborState *state,
                                   void *data);
static bool getAttributeHandler(const EmZclContext_t *context,
                                CborState *state,
                                void *data);
static bool updateAttributeHandler(const EmZclContext_t *context,
                                   CborState *state,
                                   void *data);

static bool filterAttribute(const EmZclContext_t *context,
                            FilterState *state,
                            const EmZclAttributeEntry_t *attribute);
static void readWriteResponseHandler(EmberZclMessageStatus_t status,
                                     EmberCoapCode code,
                                     const uint8_t *payload,
                                     size_t payloadLength,
                                     const void *applicationData,
                                     uint16_t applicationDataLength);
static void handleRead(EmberZclMessageStatus_t status,
                       const Response *response);
static void handleWrite(EmberZclMessageStatus_t status,
                        const Response *response);

static EmberZclStatus_t writeAttribute(EmberZclEndpointIndex_t index,
                                       EmberZclEndpointId_t endpointId,
                                       const EmZclAttributeEntry_t *attribute,
                                       const void *data,
                                       size_t dataLength);

static void callPostAttributeChange(EmberZclEndpointId_t endpointId,
                                    const EmZclAttributeEntry_t *attribute,
                                    const void *data,
                                    size_t dataLength);
static bool callPreAttributeChange(EmberZclEndpointId_t endpointId,
                                   const EmZclAttributeEntry_t *attribute,
                                   const void *data,
                                   size_t dataLength);

static size_t tokenize(const EmZclContext_t *context,
                       void *skipData,
                       uint8_t depth,
                       const uint8_t **tokens,
                       size_t *tokenLengths);

#define oneBitSet(mask) ((mask) != 0 && (mask) == ((mask) & -(mask)))

#define attributeDefaultLocation(a) \
  attributeDefaultMinMaxLocation((a), EM_ZCL_ATTRIBUTE_DATA_DEFAULT)
#define attributeMinimumLocation(a) \
  attributeDefaultMinMaxLocation((a), EM_ZCL_ATTRIBUTE_DATA_MINIMUM)
#define attributeMaximumLocation(a) \
  attributeDefaultMinMaxLocation((a), EM_ZCL_ATTRIBUTE_DATA_MAXIMUM)

// This limit is copied from MAX_ENCODED_URI in coap.c.
#define MAX_ATTRIBUTE_URI_LENGTH 64

void emberZclResetAttributes(EmberZclEndpointId_t endpointId)
{
  // TODO: Handle tokens.
  for (size_t i = 0; i < EM_ZCL_ATTRIBUTE_COUNT; i++) {
    const EmZclAttributeEntry_t *attribute = emZclAttributeTable + i;
    EmberZclEndpointIndex_t index
      = emberZclEndpointIdToIndex(endpointId, attribute->clusterSpec);
    if (index != EMBER_ZCL_ENDPOINT_INDEX_NULL
        && emZclIsAttributeLocal(attribute)) {
      const void *dephault = attributeDefaultLocation(attribute);
      writeAttribute(index, endpointId, attribute, dephault, attribute->size);
      callPostAttributeChange(endpointId, attribute, dephault, attribute->size);
    }
  }
}

EmberZclStatus_t emberZclReadAttribute(EmberZclEndpointId_t endpointId,
                                       const EmberZclClusterSpec_t *clusterSpec,
                                       EmberZclAttributeId_t attributeId,
                                       void *buffer,
                                       size_t bufferLength)
{
  return emZclReadAttributeEntry(endpointId,
                                 emZclFindAttribute(clusterSpec,
                                                    attributeId,
                                                    false), // exclude remote
                                 buffer,
                                 bufferLength);
}

EmberZclStatus_t emberZclWriteAttribute(EmberZclEndpointId_t endpointId,
                                        const EmberZclClusterSpec_t *clusterSpec,
                                        EmberZclAttributeId_t attributeId,
                                        const void *buffer,
                                        size_t bufferLength)
{
  return emZclWriteAttributeEntry(endpointId,
                                  emZclFindAttribute(clusterSpec,
                                                     attributeId,
                                                     false), // exclude remote
                                  buffer,
                                  bufferLength);
}


EmberZclStatus_t emberZclExternalAttributeChanged(EmberZclEndpointId_t endpointId,
                                                  const EmberZclClusterSpec_t *clusterSpec,
                                                  EmberZclAttributeId_t attributeId,
                                                  const void *buffer,
                                                  size_t bufferLength)
{
  if (!emZclEndpointHasCluster(endpointId, clusterSpec)) {
    return EMBER_ZCL_STATUS_UNSUPPORTED_ATTRIBUTE;
  }

  const EmZclAttributeEntry_t *attribute
    = emZclFindAttribute(clusterSpec, attributeId, false); // exclude remote
  if (attribute == NULL) {
    return EMBER_ZCL_STATUS_UNSUPPORTED_ATTRIBUTE;
  }

  if (!emZclIsAttributeExternal(attribute)) {
    return EMBER_ZCL_STATUS_UNSUPPORTED_ATTRIBUTE;
  }

  callPostAttributeChange(endpointId, attribute, buffer, bufferLength);

  return EMBER_ZCL_STATUS_SUCCESS;
}

const EmZclAttributeEntry_t *emZclFindAttribute(const EmberZclClusterSpec_t *clusterSpec,
                                                EmberZclAttributeId_t attributeId,
                                                bool includeRemote)
{
  for (size_t i = 0; i < EM_ZCL_ATTRIBUTE_COUNT; i++) {
    if (emberZclCompareClusterSpec(clusterSpec,
                                   emZclAttributeTable[i].clusterSpec)
        && attributeId == emZclAttributeTable[i].attributeId
        && (includeRemote || emZclIsAttributeLocal(&emZclAttributeTable[i]))) {
      return &emZclAttributeTable[i];
    }
  }
  return NULL;
}

EmberZclStatus_t emZclReadAttributeEntry(EmberZclEndpointId_t endpointId,
                                         const EmZclAttributeEntry_t *attribute,
                                         void *buffer,
                                         size_t bufferLength)
{
  if (attribute == NULL || emZclIsAttributeRemote(attribute)) {
    return EMBER_ZCL_STATUS_UNSUPPORTED_ATTRIBUTE;
  }

  EmberZclEndpointIndex_t index
    = emberZclEndpointIdToIndex(endpointId, attribute->clusterSpec);
  if (index == EMBER_ZCL_ENDPOINT_INDEX_NULL) {
    return EMBER_ZCL_STATUS_UNSUPPORTED_ATTRIBUTE;
  }

  if (emZclIsAttributeExternal(attribute)) {
    return emberZclReadExternalAttributeCallback(endpointId,
                                                 attribute->clusterSpec,
                                                 attribute->attributeId,
                                                 buffer,
                                                 bufferLength);
  }

  // For variable-length attributes, we are a little flexible for buffer sizes.
  // As long as there is enough space in the buffer to store the current value
  // of the attribute, we permit the read, even if the buffer is smaller than
  // the maximum possible size of the attribute.
  void *data = attributeDataLocation(index, attribute);
  size_t size = attributeSize(attribute, data);
  if (bufferLength < size) {
    return EMBER_ZCL_STATUS_INSUFFICIENT_SPACE;
  }

  MEMCOPY(buffer, data, size);
  return EMBER_ZCL_STATUS_SUCCESS;
}

EmberZclStatus_t emZclWriteAttributeEntry(EmberZclEndpointId_t endpointId,
                                          const EmZclAttributeEntry_t *attribute,
                                          const void *buffer,
                                          size_t bufferLength)
{
  if (attribute == NULL || emZclIsAttributeRemote(attribute)) {
    return EMBER_ZCL_STATUS_UNSUPPORTED_ATTRIBUTE;
  }

  EmberZclEndpointIndex_t index
    = emberZclEndpointIdToIndex(endpointId, attribute->clusterSpec);
  if (index == EMBER_ZCL_ENDPOINT_INDEX_NULL) {
    return EMBER_ZCL_STATUS_UNSUPPORTED_ATTRIBUTE;
  }

  // For variable-length attributes, we are a little flexible for buffer sizes.
  // As long as there is enough space in the table to store the new value of
  // the attribute, we permit the write, even if the actual length of buffer
  // containing the new value is larger than what we have space for.
  size_t size = attributeSize(attribute, buffer);
  if (attribute->size < size) {
    return EMBER_ZCL_STATUS_INSUFFICIENT_SPACE;
  }

  if (bufferLength == 0
      || bufferLength > EMBER_ZCL_ATTRIBUTE_MAX_SIZE
      || (emZclIsAttributeBounded(attribute)
          && (isLessThan(buffer,
                         bufferLength,
                         attributeMinimumLocation(attribute),
                         attribute->size)
              || isLessThan(attributeMaximumLocation(attribute),
                            attribute->size,
                            buffer,
                            bufferLength)))) {
    return EMBER_ZCL_STATUS_INVALID_VALUE;
  }

  if (!callPreAttributeChange(endpointId, attribute, buffer, size)) {
    return EMBER_ZCL_STATUS_FAILURE;
  }

  EmberZclStatus_t status = writeAttribute(index,
                                           endpointId,
                                           attribute,
                                           buffer,
                                           size);
  if (status == EMBER_ZCL_STATUS_SUCCESS) {
    callPostAttributeChange(endpointId, attribute, buffer, size);
  }
  return status;
}

bool emZclReadEncodeAttributeKeyValue(CborState *state,
                                      EmberZclEndpointId_t endpointId,
                                      const EmZclAttributeEntry_t *attribute,
                                      void *buffer,
                                      size_t bufferLength)
{
  if (attribute == NULL) {
    return true;
  } else {
    return ((emZclReadAttributeEntry(endpointId,
                                     attribute,
                                     buffer,
                                     attribute->size)
             == EMBER_ZCL_STATUS_SUCCESS)
            && emCborEncodeMapEntry(state,
                                    attribute->attributeId,
                                    attribute->type,
                                    attribute->size,
                                    (isValueLocIndirect(attribute)
                                     ? (const uint8_t *)&buffer
                                     : buffer)));
  }
}

static size_t attributeSize(const EmZclAttributeEntry_t *attribute,
                            const void *data)
{
  if (attribute->type == EMBER_ZCLIP_TYPE_UINT8_LENGTH_STRING) {
    return emberZclStringSize(data);
  } else if (attribute->type == EMBER_ZCLIP_TYPE_UINT16_LENGTH_STRING) {
    return emberZclLongStringSize(data);
  } else {
    return attribute->size;
  }
}

static void *attributeDataLocation(EmberZclEndpointIndex_t endpointIndex,
                                   const EmZclAttributeEntry_t *attribute)
{
  // AppBuilder generates the maximum size for all of the attribute data, so
  // that the app can create a buffer to hold all of the runtime attribute
  // values. This maximum size factors in attributes that need multiple data
  // instances since they exist on multiple endpoints and are not singleton.
  // When an attribute has multiple data instances, the values are stored
  // sequentially in the buffer. AppBuilder also generates the per-attribute
  // offset into the buffer so that it is easy to go from attribute to value.
  static uint8_t attributeData[EM_ZCL_ATTRIBUTE_DATA_SIZE] = EM_ZCL_ATTRIBUTE_DEFAULTS;
  return (attributeData
          + attribute->dataOffset
          + (attribute->size
             * (emZclIsAttributeSingleton(attribute)
                ? 0
                : endpointIndex)));
}

const void *attributeDefaultMinMaxLocation(const EmZclAttributeEntry_t *attribute,
                                           EmZclAttributeMask_t dataBit)
{
  // AppBuilder generates a table of attribute "constants" that are all possible
  // values of defaults, minimums, and maximums that the user has configured
  // their app to use. We don't generate attribute constants that are zero. We
  // always assume that if an attribute doesn't have a bit set for a
  // default/min/max value in its mask, then the value is all zeros.
  //
  // Each attribute uses an index into a lookup table to figure out where each
  // of their default/min/max constants are located in the constant table. The
  // lookup table stores up to 3 indices per attribute (always in this order):
  // a default value index, a minimum value index, and a maximum value index.
  // However, all of these indices are optional. If an attribute does not have
  // a default/min/max value, or the value is all 0's, or the app was
  // configured not to include that constant through AppBuilder (in the case of
  // min/max values), then an index will not be generated.
  assert(READBITS(dataBit, EM_ZCL_ATTRIBUTE_DATA_MASK) && oneBitSet(dataBit));
  if (!READBITS(attribute->mask, dataBit)) {
    const static uint8_t zeros[EMBER_ZCL_ATTRIBUTE_MAX_SIZE] = {0};
    return zeros;
  }

  const size_t *lookupLocation = (emZclAttributeDefaultMinMaxLookupTable
                                  + attribute->defaultMinMaxLookupOffset);
  for (EmZclAttributeMask_t mask = EM_ZCL_ATTRIBUTE_DATA_DEFAULT;
       mask < dataBit;
       mask <<= 1) {
    if (READBITS(attribute->mask, mask)) {
      lookupLocation ++;
    }
  }

  return emZclAttributeDefaultMinMaxData + *lookupLocation;
}

static bool isValueLocIndirect(const EmZclAttributeEntry_t *attribute)
{
  // For the CBOR encoder and decoder, in most cases, valueLoc is a pointer to
  // some data.  For strings, valueLoc is a pointer to a pointer to some data.
  // For commands, this is handled automatically by the structs and specs.  For
  // attributes, it must be done manually.
  switch (attribute->type) {
  case EMBER_ZCLIP_TYPE_UINT8_LENGTH_STRING:
  case EMBER_ZCLIP_TYPE_UINT16_LENGTH_STRING:
    return true;
  default:
    return false;
  }
}

static bool isLessThan(const uint8_t *dataA,
                       size_t dataALength,
                       const uint8_t *dataB,
                       size_t dataBLength)
{
  // This function assumes a few things.
  // - The data* arrays follow the native machine endianness.
  // - The data* arrays represent the same (ZCL) data types.
  // - The data* arrays represent numeric data types.
  // - The data*Length values are not 0.
  // - The data*Length values are no greater than EMBER_ZCL_ATTRIBUTE_MAX_SIZE.
  uint8_t dataABigEndian[EMBER_ZCL_ATTRIBUTE_MAX_SIZE] = {0};
  uint8_t dataBBigEndian[EMBER_ZCL_ATTRIBUTE_MAX_SIZE] = {0};
  size_t dataAOffset = EMBER_ZCL_ATTRIBUTE_MAX_SIZE - dataALength;
  size_t dataBOffset = EMBER_ZCL_ATTRIBUTE_MAX_SIZE - dataBLength;

#if BIGENDIAN_CPU
  MEMMOVE(dataABigEndian + dataAOffset, dataA, dataALength);
  MEMMOVE(dataBBigEndian + dataBOffset, dataB, dataBLength);
#else
  emberReverseMemCopy(dataABigEndian + dataAOffset, dataA, dataALength);
  emberReverseMemCopy(dataBBigEndian + dataBOffset, dataB, dataBLength);
#endif

  return (MEMCOMPARE(dataABigEndian,
                     dataBBigEndian,
                     EMBER_ZCL_ATTRIBUTE_MAX_SIZE)
          < 0);
}

static bool getAttributeIdsHandler(const EmZclContext_t *context,
                                   CborState *state,
                                   void *data)
{
  // If there are no queries, then we return an array of attribute ids.
  // Otherwise, we return a map from the filtered attribute ids to
  // their values.
  const EmZclAttributeQuery_t *query = &context->attributeQuery;
  bool array = (query->filterCount == 0);
  if (array) {
    emCborEncodeIndefiniteArray(state);
  } else {
    emCborEncodeIndefiniteMap(state);
  }

  // TODO: Handle unreadable attributes.
  FilterState filterState = {0};
  for (size_t i = 0; i < EM_ZCL_ATTRIBUTE_COUNT; i++) {
    const EmZclAttributeEntry_t *attribute = emZclAttributeTable + i;
    uint8_t buffer[EMBER_ZCL_ATTRIBUTE_MAX_SIZE];
    if (emberZclCompareClusterSpec(&context->clusterSpec,
                                   attribute->clusterSpec)
        && emZclIsAttributeLocal(attribute)
        && filterAttribute(context, &filterState, attribute)
        && !(array
             ? emCborEncodeValue(state,
                                 EMBER_ZCLIP_TYPE_UNSIGNED_INTEGER,
                                 sizeof(attribute->attributeId),
                                 (const uint8_t *)&attribute->attributeId)
             : emZclReadEncodeAttributeKeyValue(state,
                                                context->endpoint->endpointId,
                                                attribute,
                                                buffer,
                                                sizeof(buffer)))) {
      return false;
    }
  }
  return emCborEncodeBreak(state);
}

static bool getAttributeHandler(const EmZclContext_t *context,
                                CborState *state,
                                void *data)
{
  // TODO: Handle unreadable attributes.
  uint8_t buffer[EMBER_ZCL_ATTRIBUTE_MAX_SIZE];
  return (emCborEncodeIndefiniteMap(state)
          && emZclReadEncodeAttributeKeyValue(state,
                                              context->endpoint->endpointId,
                                              context->attribute,
                                              buffer,
                                              sizeof(buffer))
          && emCborEncodeBreak(state));
}

static bool updateAttributeHandler(const EmZclContext_t *context,
                                   CborState *state,
                                   void *data)
{
  // TODO: support undivided write.
  CborState inState;
  emCborDecodeStart(&inState, context->payload, context->payloadLength);
  emCborEncodeIndefiniteMap(state);
  if (emCborDecodeMap(&inState)) {
    EmberZclAttributeId_t attributeId;
    while (emCborDecodeValue(&inState,
                             EMBER_ZCLIP_TYPE_UNSIGNED_INTEGER,
                             sizeof(attributeId),
                             (uint8_t *)&attributeId)) {
      if (context->attribute == NULL
          || context->attribute->attributeId == attributeId) {
        const EmZclAttributeEntry_t *attribute
          = (context->attribute == NULL
             ? emZclFindAttribute(&context->clusterSpec,
                                  attributeId,
                                  false) // exclude remote
             : context->attribute);

        // TODO: The CBOR decoder doesn't permit skipping over values.  If we
        // don't have the attribute, we don't know what it's encoding is, so we
        // can't read and ignore it either.
        if (attribute == NULL) {
          return false;
        }

        uint8_t buffer[EMBER_ZCL_ATTRIBUTE_MAX_SIZE], *p = buffer;
        if (!emCborDecodeValue(&inState,
                               attribute->type,
                               attribute->size,
                               buffer)) {
          return false;
        }

        EmberZclStatus_t status = EMBER_ZCL_STATUS_READ_ONLY;
        if (emZclIsAttributeWritable(attribute)) {
          status = emZclWriteAttributeEntry(context->endpoint->endpointId,
                                            attribute,
                                            (isValueLocIndirect(attribute)
                                             ? *((uint8_t **)p)
                                             : p),
                                            attribute->size);
        }

        if (!emCborEncodeMapEntry(state,
                                  attribute->attributeId,
                                  EMBER_ZCLIP_TYPE_UNSIGNED_INTEGER,
                                  sizeof(status),
                                  &status)) {
          return false;
        }
      }
    }
  }
  return emCborEncodeBreak(state);
}

static bool filterAttribute(const EmZclContext_t *context,
                           FilterState *state,
                           const EmZclAttributeEntry_t *attribute)
{
  EmberZclAttributeId_t attributeId = attribute->attributeId;
  for (size_t i = 0; i < context->attributeQuery.filterCount; i ++) {
    const EmZclAttributeQueryFilter_t *filter = &context->attributeQuery.filters[i];
    switch (filter->type) {
    case EM_ZCL_ATTRIBUTE_QUERY_FILTER_TYPE_ID:
      if (attributeId == filter->data.attributeId) {
        return true;
      }
      break;

    case EM_ZCL_ATTRIBUTE_QUERY_FILTER_TYPE_COUNT:
      if (attributeId >= filter->data.countData.start) {
        if (filter->data.countData.count > state->count
            && !READBIT(state->usedCounts, i)) {
          state->count = filter->data.countData.count;
        }
        SETBIT(state->usedCounts, i);
      }
      break;

    case EM_ZCL_ATTRIBUTE_QUERY_FILTER_TYPE_RANGE:
      if (attributeId >= filter->data.rangeData.start
          && attributeId <= filter->data.rangeData.end) {
        return true;
      }
      break;

    case EM_ZCL_ATTRIBUTE_QUERY_FILTER_TYPE_WILDCARD:
      return true;

    default:
      assert(false);
    }
  }

  bool ret = (context->attributeQuery.filterCount == 0 || state->count > 0);
  if (state->count > 0) {
    state->count --;
  }
  return ret;
}

EmberStatus emberZclSendAttributeRead(const EmberZclDestination_t *destination,
                                      const EmberZclClusterSpec_t *clusterSpec,
                                      const EmberZclAttributeId_t *attributeIds,
                                      size_t attributeIdsCount,
                                      const EmberZclReadAttributeResponseHandler handler)
{
  // The size of this array is the maximum number of filter range data
  // structures that could possibly be encoded into a URI string of length 64
  // that has been optimized with range formatting. This helps us protect from
  // writing off the end of the filterRangeData array in the second for loop
  // below.
  EmZclAttributeQueryFilterRangeData_t filterRangeData[19];
  EmZclAttributeQueryFilterRangeData_t *filterRangeDatum = filterRangeData;
  for (size_t i = 0; i < COUNTOF(filterRangeData); i++) {
    filterRangeData[i].start = filterRangeData[i].end = EMBER_ZCL_ATTRIBUTE_NULL;
  }
  for (size_t i = 0; i < attributeIdsCount; i++) {
    EmberZclAttributeId_t attributeId = attributeIds[i];
    if (attributeId == filterRangeDatum->end + 1) {
      filterRangeDatum->end = attributeId;
    } else {
      if (filterRangeDatum->start != EMBER_ZCL_ATTRIBUTE_NULL) {
        filterRangeDatum ++;
        if (filterRangeDatum - filterRangeData > sizeof(filterRangeData)) {
          return EMBER_BAD_ARGUMENT;
        }
      }
      filterRangeDatum->start = filterRangeDatum->end = attributeId;
    }
  }

  size_t filterRangeDataCount = filterRangeDatum - filterRangeData + 1;
  uint8_t uri[MAX_ATTRIBUTE_URI_LENGTH];
  uint8_t *uriFinger = uri;
  uriFinger += emZclAttributeToUriPath(&destination->application,
                                       clusterSpec,
                                       uriFinger);
  *uriFinger++ = '?';
  *uriFinger++ = 'f';
  *uriFinger++ = '=';
  for (size_t i = 0; i < filterRangeDataCount; i ++) {
    uint8_t buffer[10];
    uint8_t *bufferFinger = buffer;
    if (i != 0) {
      *bufferFinger++ = ',';
    }
    bufferFinger += emZclIntToHexString(filterRangeData[i].start,
                                        sizeof(EmberZclAttributeId_t),
                                        bufferFinger);
    if (filterRangeData[i].start != filterRangeData[i].end) {
      *bufferFinger++ = '-';
      bufferFinger += emZclIntToHexString(filterRangeData[i].end,
                                          sizeof(EmberZclAttributeId_t),
                                          bufferFinger);
    }

    size_t bufferLength = bufferFinger - buffer;
    MEMMOVE(uriFinger, buffer, bufferLength);
    uriFinger += bufferLength;
    if (uriFinger - uri > sizeof(uri)) {
      return EMBER_BAD_ARGUMENT;
    }
    *uriFinger = '\0';
  }

  Response response = {
    .context = {
      .code = EMBER_COAP_CODE_EMPTY, // filled in when the response arrives
      .groupId
        = ((destination->application.type
            == EMBER_ZCL_APPLICATION_DESTINATION_TYPE_GROUP)
           ? destination->application.data.groupId
           : EMBER_ZCL_GROUP_NULL),
      .endpointId
        = ((destination->application.type
            == EMBER_ZCL_APPLICATION_DESTINATION_TYPE_ENDPOINT)
           ? destination->application.data.endpointId
           : EMBER_ZCL_ENDPOINT_NULL),
      .clusterSpec = clusterSpec,
      .attributeId = EMBER_ZCL_ATTRIBUTE_NULL, // filled in when the response arrives
      .state = NULL, // filled in when the response arrives
    },
    .readHandler = handler,
    .writeHandler = NULL, // unused
  };

  return emZclSend(&destination->network,
                   EMBER_COAP_CODE_GET,
                   uri,
                   NULL, // payload
                   0,    // payload length
                   (handler == NULL ? NULL : readWriteResponseHandler),
                   &response,
                   sizeof(Response));
}

EmberStatus emberZclSendAttributeWrite(const EmberZclDestination_t *destination,
                                       const EmberZclClusterSpec_t *clusterSpec,
                                       const EmberZclAttributeWriteData_t *attributeWriteData,
                                       size_t attributeWriteDataCount,
                                       const EmberZclWriteAttributeResponseHandler handler)
{
  CborState state;
  uint8_t buffer[128];
  emCborEncodeIndefiniteMapStart(&state, buffer, sizeof(buffer));
  for (size_t i = 0; i < attributeWriteDataCount; i++) {
    const EmZclAttributeEntry_t *attribute
      = emZclFindAttribute(clusterSpec,
                           attributeWriteData[i].attributeId,
                           true); // include remote
    if (attribute == NULL) {
      return EMBER_BAD_ARGUMENT;
    } else if (!emCborEncodeMapEntry(&state,
                                     attribute->attributeId,
                                     attribute->type,
                                     attribute->size,
                                     (isValueLocIndirect(attribute)
                                      ? (const uint8_t *)&attributeWriteData[i].buffer
                                      : attributeWriteData[i].buffer))) {
      return EMBER_ERR_FATAL;
    }
  }
  emCborEncodeBreak(&state);

  uint8_t uriPath[EMBER_ZCL_URI_PATH_MAX_LENGTH];
  emZclAttributeToUriPath(&destination->application, clusterSpec, uriPath);

  Response response = {
    .context = {
      .code = EMBER_COAP_CODE_EMPTY, // filled in when the response arrives
      .groupId
        = ((destination->application.type
            == EMBER_ZCL_APPLICATION_DESTINATION_TYPE_GROUP)
           ? destination->application.data.groupId
           : EMBER_ZCL_GROUP_NULL),
      .endpointId
        = ((destination->application.type
            == EMBER_ZCL_APPLICATION_DESTINATION_TYPE_ENDPOINT)
           ? destination->application.data.endpointId
           : EMBER_ZCL_ENDPOINT_NULL),
      .clusterSpec = clusterSpec,
      .attributeId = EMBER_ZCL_ATTRIBUTE_NULL, // filled in when the response arrives
      .state = NULL, // filled in when the response arrives
    },
    .readHandler = NULL, // unused
    .writeHandler = handler,
  };

  return emZclSend(&destination->network,
                   EMBER_COAP_CODE_POST,
                   uriPath,
                   buffer,
                   emCborEncodeSize(&state),
                   (handler == NULL ? NULL : readWriteResponseHandler),
                   &response,
                   sizeof(Response));
}

static void readWriteResponseHandler(EmberZclMessageStatus_t status,
                                     EmberCoapCode code,
                                     const uint8_t *payload,
                                     size_t payloadLength,
                                     const void *applicationData,
                                     uint16_t applicationDataLength)
{
  // We should only be here if the application specified a handler.
  assert(applicationDataLength == sizeof(Response));
  const Response *response = applicationData;
  bool isRead = (*response->readHandler != NULL);
  bool isWrite = (*response->writeHandler != NULL);
  assert(isRead != isWrite);

  ((Response *)response)->context.code = code;

  // TODO: What should happen for failures?
  // TODO: What should happen if the overall payload is missing or malformed?
  // Note that this is a separate issue from how missing or malformed responses
  // from the individual endpoints should be handled.
  if (status == EMBER_ZCL_MESSAGE_STATUS_COAP_RESPONSE
      && emberCoapIsSuccessResponse(code)) {
    CborState state;
    ((Response *)response)->context.state = &state;
    emCborDecodeStart(&state, payload, payloadLength);
    if (response->context.groupId == EMBER_ZCL_GROUP_NULL) {
      (isRead ? handleRead : handleWrite)(status, response);
      return;
    } else if (emCborDecodeMap(&state)) {
      while (emCborDecodeValue(&state,
                               EMBER_ZCLIP_TYPE_UNSIGNED_INTEGER,
                               sizeof(response->context.endpointId),
                               (uint8_t *)&response->context.endpointId)) {
        (isRead ? handleRead : handleWrite)(status, response);
      }
      return;
    }
  }

  if (isRead) {
    (*response->readHandler)(status, &response->context, NULL, 0);
  } else {
    (*response->writeHandler)(status,
                              &response->context,
                              EMBER_ZCL_STATUS_NULL);
  }
}

static void handleRead(EmberZclMessageStatus_t status,
                       const Response *response)
{
  // TODO: If we expect an attribute but it is missing, or it is present but
  // malformed, would should we do?
  if (emCborDecodeMap(response->context.state)) {
    while (emCborDecodeValue(response->context.state,
                             EMBER_ZCLIP_TYPE_UNSIGNED_INTEGER,
                             sizeof(response->context.attributeId),
                             (uint8_t *)&response->context.attributeId)) {
      const EmZclAttributeEntry_t *attribute
        = emZclFindAttribute(response->context.clusterSpec,
                             response->context.attributeId,
                             true); // include remote
      if (attribute == NULL) {
        emCborDecodeSkipValue(response->context.state);
      } else {
        uint8_t buffer[EMBER_ZCL_ATTRIBUTE_MAX_SIZE];
        if (emCborDecodeValue(response->context.state,
                              attribute->type,
                              attribute->size,
                              buffer)) {
          (*response->readHandler)(status,
                                   &response->context,
                                   buffer,
                                   attribute->size);
        }
      }
    }
  }
  (*response->readHandler)(status, &response->context, NULL, 0);
}

static void handleWrite(EmberZclMessageStatus_t status,
                        const Response *response)
{
  // TODO: If we expect an attribute but it is missing, or it is present but
  // malformed, would should we do?
  if (emCborDecodeMap(response->context.state)) {
    while (emCborDecodeValue(response->context.state,
                             EMBER_ZCLIP_TYPE_UNSIGNED_INTEGER,
                             sizeof(response->context.attributeId),
                             (uint8_t *)&response->context.attributeId)) {
      EmberZclStatus_t result;
      if (!emCborDecodeValue(response->context.state,
                             EMBER_ZCLIP_TYPE_UNSIGNED_INTEGER,
                             sizeof(result),
                             (uint8_t *)&result)) {
        break;
      }
      (*response->writeHandler)(status, &response->context, result);
    }
    return;
  }
  (*response->writeHandler)(status, &response->context, EMBER_ZCL_STATUS_NULL);
}

static EmberZclStatus_t writeAttribute(EmberZclEndpointIndex_t index,
                                       EmberZclEndpointId_t endpointId,
                                       const EmZclAttributeEntry_t *attribute,
                                       const void *data,
                                       size_t dataLength)
{
  EmberZclStatus_t status = EMBER_ZCL_STATUS_SUCCESS;
  EmZclAttributeMask_t storageType
    = READBITS(attribute->mask, EM_ZCL_ATTRIBUTE_STORAGE_TYPE_MASK);

  assert(emZclIsAttributeLocal(attribute));

  switch (storageType) {
  case EM_ZCL_ATTRIBUTE_STORAGE_TYPE_EXTERNAL:
    status = emberZclWriteExternalAttributeCallback(endpointId,
                                                    attribute->clusterSpec,
                                                    attribute->attributeId,
                                                    data,
                                                    dataLength);
    break;

  case EM_ZCL_ATTRIBUTE_STORAGE_TYPE_RAM:
    MEMMOVE(attributeDataLocation(index, attribute), data, attribute->size);
    break;

  default:
    assert(false);
  }

  return status;
}

static void callPostAttributeChange(EmberZclEndpointId_t endpointId,
                                    const EmZclAttributeEntry_t *attribute,
                                    const void *data,
                                    size_t dataLength)
{
  if (emZclIsAttributeSingleton(attribute)) {
    for (size_t i = 0; i < emZclEndpointCount; i++) {
      if (emZclEndpointHasCluster(emZclEndpointTable[i].endpointId,
                                  attribute->clusterSpec)) {
        emZclPostAttributeChange(emZclEndpointTable[i].endpointId,
                                 attribute->clusterSpec,
                                 attribute->attributeId,
                                 data,
                                 dataLength);
      }
    }
  } else {
    emZclPostAttributeChange(endpointId,
                             attribute->clusterSpec,
                             attribute->attributeId,
                             data,
                             dataLength);
  }
}

static bool callPreAttributeChange(EmberZclEndpointId_t endpointId,
                                   const EmZclAttributeEntry_t *attribute,
                                   const void *data,
                                   size_t dataLength)
{
  if (emZclIsAttributeSingleton(attribute)) {
    for (size_t i = 0; i < emZclEndpointCount; i++) {
      if (emZclEndpointHasCluster(emZclEndpointTable[i].endpointId,
                                  attribute->clusterSpec)
          && !emZclPreAttributeChange(emZclEndpointTable[i].endpointId,
                                      attribute->clusterSpec,
                                      attribute->attributeId,
                                      data,
                                      dataLength)) {
        return false;
      }
    }
    return true;
  } else {
    return emZclPreAttributeChange(endpointId,
                                   attribute->clusterSpec,
                                   attribute->attributeId,
                                   data,
                                   dataLength);
  }
}

static size_t tokenize(const EmZclContext_t *context,
                       void *skipData,
                       uint8_t depth,
                       const uint8_t **tokens,
                       size_t *tokenLengths)
{
  uint8_t skipLength = strlen((const char *)skipData);
  const uint8_t *bytes = context->uriQuery[depth] + skipLength;
  size_t bytesLength = context->uriQueryLength[depth] - skipLength;
  size_t count = 0;
  const uint8_t *next = NULL, *end = bytes + bytesLength;

  for (; bytes < end; bytes = next + 1) {
    next = memchr(bytes, ',', end - bytes);
    if (next == NULL) {
      next = end;
    } else if (next == bytes || next == end - 1) {
      return 0; // 0 length strings are no good.
    }

    tokens[count] = bytes;
    tokenLengths[count] = next - bytes;
    count ++;
  }

  return count;
}

// .../a?f=
bool emZclAttributeUriQueryFilterParse(EmZclContext_t *context,
                                       void *data,
                                       uint8_t depth)
{
  const uint8_t *tokens[MAX_URI_QUERY_SEGMENTS];
  size_t tokenLengths[MAX_URI_QUERY_SEGMENTS];
  size_t tokenCount = tokenize(context, data, depth, tokens, tokenLengths);
  if (tokenCount == 0) {
    return false;
  }

  for (size_t i = 0; i < tokenCount; i ++) {
    EmZclAttributeQueryFilter_t *filter
      = &context->attributeQuery.filters[context->attributeQuery.filterCount++];
    if (tokenLengths[i] == 1 && tokens[i][0] == '*') {
      // f=*
      filter->type = EM_ZCL_ATTRIBUTE_QUERY_FILTER_TYPE_WILDCARD;
    } else {
      const uint8_t *operator = NULL;
      const uint8_t *now = tokens[i];
      const uint8_t *next = now + tokenLengths[i];
      uintmax_t first, second, length;
      if (((operator = memchr(now, '-', tokenLengths[i])) != NULL
           || (operator = memchr(now, '+', tokenLengths[i])) != NULL)
          && (length = operator - now) > 0
          && length <= sizeof(EmberZclAttributeId_t) * 2 // nibbles
          && emZclHexStringToInt(now, length, &first)
          && (length = next - operator - 1) > 0
          && length <= sizeof(EmberZclAttributeId_t) * 2 // nibbles
          && emZclHexStringToInt(operator + 1, length , &second)) {
        // f=1-2
        // f=3+4
        if (*operator == '-') {
          filter->type = EM_ZCL_ATTRIBUTE_QUERY_FILTER_TYPE_RANGE;
          filter->data.rangeData.start = first;
          filter->data.rangeData.end = second;
          if (filter->data.rangeData.end <= filter->data.rangeData.start) {
            return false;
          }
        } else {
          filter->type = EM_ZCL_ATTRIBUTE_QUERY_FILTER_TYPE_COUNT;
          filter->data.countData.start = first;
          filter->data.countData.count = second;
        }
      } else if (tokenLengths[i] <= sizeof(EmberZclAttributeId_t) * 2
                 && emZclHexStringToInt(now, tokenLengths[i], &first)) {
        // f=5
        filter->type = EM_ZCL_ATTRIBUTE_QUERY_FILTER_TYPE_ID;
        filter->data.attributeId = first;
      } else {
        return false;
      }
    }
  }

  return true;
}

// ...a/?u
bool emZclAttributeUriQueryUndividedParse(EmZclContext_t *context,
                                          void *data,
                                          uint8_t depth)
{
  // Only accept 'u'.
  return (context->attributeQuery.undivided
          = (context->uriQueryLength[depth] == 1));
}

// zcl/[eg]/XX/<cluster>/a:
//   GET:
//     w/ query: read multiple attributes.
//     w/o query: list attributes in cluster.
//   POST:
//     w/ query: update attributes undivided.
//     w/o query: write multiple attributes.
//   OTHER: not allowed.
void emZclUriClusterAttributeHandler(EmZclContext_t *context)
{
  CborState state;
  uint8_t buffer[128];
  emCborEncodeStart(&state, buffer, sizeof(buffer));
  if (context->code == EMBER_COAP_CODE_GET) {
    if (emZclMultiEndpointDispatch(context, getAttributeIdsHandler, &state, NULL)) {
      emZclRespond205ContentCborState(&state);
    } else {
      emZclRespond500InternalServerError();
    }
  } else if (context->code == EMBER_COAP_CODE_POST) {
    if (emZclMultiEndpointDispatch(context, updateAttributeHandler, &state, NULL)) {
      emZclRespond204ChangedCborState(&state);
    } else {
      emZclRespond500InternalServerError();
    }
  } else {
    assert(false);
  }
}

// zcl/[eg]/XX/<cluster>/a/XXXX:
//   GET: read one attribute.
//   PUT: write one attribute.
//   OTHER: not allowed.
void emZclUriClusterAttributeIdHandler(EmZclContext_t *context)
{
  CborState state;
  uint8_t buffer[128];
  emCborEncodeStart(&state, buffer, sizeof(buffer));
  if (context->code == EMBER_COAP_CODE_GET) {
    if (emZclMultiEndpointDispatch(context, getAttributeHandler, &state, NULL)) {
      emZclRespond205ContentCborState(&state);
    } else {
      emZclRespond500InternalServerError();
    }
  } else if (context->code == EMBER_COAP_CODE_PUT) {
    if (emZclMultiEndpointDispatch(context, updateAttributeHandler, &state, NULL)) {
      emZclRespond204ChangedCborState(&state);
    } else {
      emZclRespond500InternalServerError();
    }
  } else {
    assert(false);
  }
}
