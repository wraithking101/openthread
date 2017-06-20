// Copyright 2016 Silicon Laboratories, Inc.

#include PLATFORM_HEADER
#include CONFIGURATION_HEADER
#include EMBER_AF_API_STACK
#include EMBER_AF_API_BUFFER_MANAGEMENT
#include EMBER_AF_API_BUFFER_QUEUE
#include EMBER_AF_API_EVENT_QUEUE
#include EMBER_AF_API_HAL
#include "thread-callbacks.h"
#include "zcl-core.h"

#define S_TO_MS(s) ((s) * MILLISECOND_TICKS_PER_SECOND)

// 07-5123-06, section 2.5.7.1.6: If [the Maximum Reporting Interval] is set to
// 0xffff, then the device SHALL not issue reports for the specified attribute.
#define MAXIMUM_INTERVAL_S_DISABLE_REPORTING 0xFFFF

// 07-5123-06, section 2.5.11.2.1: If the Maximum Reporting Interval is set to
// 0x0000, there is no periodic reporting, but change based reporting is still
// operational.
#define MAXIMUM_INTERVAL_S_DISABLE_PERIODIC_REPORTING 0x0000

// 07-5123-06, section 2.5.7.1.6: If the Maximum Reporting Interval is set to
// 0x0000 and the minimum reporting interval field equals 0xffff, then the
// device SHALL revert back to its default reporting configuration.
#define MINIMUM_INTERVAL_S_USE_DEFAULT_REPORTING_CONFIGURATION 0xFFFF
#define MAXIMUM_INTERVAL_S_USE_DEFAULT_REPORTING_CONFIGURATION 0x0000

typedef struct {
  EmberZclEndpointId_t endpointId;
  EmberZclClusterSpec_t clusterSpec;
  EmberZclReportingConfigurationId_t reportingConfigurationId;
  size_t sizeReportableChanges;
  size_t sizeLastValues;
  uint32_t lastTimeMs;
  uint16_t backoffMs;
  uint16_t minimumIntervalS;
  uint16_t maximumIntervalS;
} Configuration_t;
#define EMBER_ZCLIP_STRUCT Configuration_t
static const ZclipStructSpec configurationSpec[] = {
  EMBER_ZCLIP_OBJECT(sizeof(EMBER_ZCLIP_STRUCT),
                     3,     // fieldCount
                     NULL), // names
  EMBER_ZCLIP_FIELD_NAMED(EMBER_ZCLIP_TYPE_UNSIGNED_INTEGER, backoffMs, "b"),
  EMBER_ZCLIP_FIELD_NAMED(EMBER_ZCLIP_TYPE_UNSIGNED_INTEGER, minimumIntervalS, "mn"),
  EMBER_ZCLIP_FIELD_NAMED(EMBER_ZCLIP_TYPE_UNSIGNED_INTEGER, maximumIntervalS, "mx"),
};
#undef EMBER_ZCLIP_STRUCT

// TODO: Reporting configurations should be stored persistently in tokens.
static Buffer configurations = NULL_BUFFER;

typedef struct {
  Event event;
  EmberZclEndpointId_t endpointId;
  EmberZclClusterSpec_t clusterSpec;
  EmberZclReportingConfigurationId_t reportingConfigurationId;
  // NULL_BUFFER when a notification should be made and something else when the
  // notification in the Buffer should be sent.
  Buffer notification;
} NotificationEvent;
extern EventQueue emAppEventQueue;
static void eventHandler(const NotificationEvent *event);
static void makeNotification(const NotificationEvent *event);
static void sendNotification(const NotificationEvent *event);
static void eventMarker(NotificationEvent *event);
static bool makeNotificationPredicate(NotificationEvent *event,
                                      const Configuration_t *configuration);
static bool sendNotificationPredicate(NotificationEvent *event,
                                      const Configuration_t *configuration);
static NotificationEvent *cancelMakeNotification(const Configuration_t *configuration);
static NotificationEvent *cancelSendNotification(const Configuration_t *configuration);
static void scheduleMakeNotification(const Configuration_t *configuration,
                                     uint32_t delayMs);
static void scheduleSendNotification(const Configuration_t *configuration,
                                     Buffer notification);
static EventActions actions = {
  .queue = &emAppEventQueue,
  .handler = (void (*)(struct Event_s *))eventHandler,
  .marker = (void (*)(struct Event_s *))eventMarker,
  .name = "zcl core reporting",
};

static bool addReportingConfiguration(Buffer buffer);
static bool decodeReportingConfigurationOta(const EmZclContext_t *context,
                                            Buffer buffer);
static void makeDefaultReportingConfigurations(void);
static Buffer makeReportingConfiguration(EmberZclEndpointId_t endpointId,
                                         const EmberZclClusterSpec_t *clusterSpec);
static void resetToDefaultReportingConfiguration(Configuration_t *configuration);
static void removeAllReportingConfigurations(void);
static void removeReportingConfiguration(const Configuration_t *configuration);
static Configuration_t *findReportingConfiguration(EmberZclEndpointId_t endpointId,
                                                   const EmberZclClusterSpec_t *clusterSpec,
                                                   EmberZclReportingConfigurationId_t reportingConfigurationId);
static bool matchReportingConfiguration(EmberZclEndpointId_t endpointId,
                                        const EmberZclClusterSpec_t *clusterSpec,
                                        EmberZclReportingConfigurationId_t reportingConfigurationId,
                                        const Configuration_t *configuration);

static bool isValidReportingConfiguration(const Configuration_t *configuration);
static bool hasPeriodicReporting(const Configuration_t *configuration);
static bool hasReportableChanges(const Configuration_t *configuration);
static bool hasReportableChange(const uint8_t *oldValue,
                                const uint8_t *reportableChange,
                                const uint8_t *newValue,
                                size_t size);
static bool useDefaultReportingConfiguration(const Configuration_t *configuration);

void emZclReportingMarkApplicationBuffersHandler(void)
{
  emMarkBuffer(&configurations);
}

void emZclReportingNetworkStatusHandler(EmberNetworkStatus newNetworkStatus,
                                        EmberNetworkStatus oldNetworkStatus,
                                        EmberJoinFailureReason reason)
{
  // If the device is no longer associated with a network, its reporting
  // configurations are removed.  If we end up joined and attached and we have
  // no configurations, make the set of default reporting configurations.  It
  // is possible there are no reportable attributes, in which case we'll waste
  // a bit of time trying to make the default reportable connection whenever
  // the node reattaches after losing its connection temporarily.
  if (newNetworkStatus == EMBER_NO_NETWORK) {
    removeAllReportingConfigurations();
  } else if (newNetworkStatus == EMBER_JOINED_NETWORK_ATTACHED
             && configurations == NULL_BUFFER) {
    makeDefaultReportingConfigurations();
  }
}

void emZclReportingPostAttributeChangeHandler(EmberZclEndpointId_t endpointId,
                                              const EmberZclClusterSpec_t *clusterSpec,
                                              EmberZclAttributeId_t attributeId,
                                              const void *buffer,
                                              size_t bufferLength)
{
  // Any change to an attribute forces a reexamination of the report timing.
  Buffer finger = emBufferQueueHead(&configurations);
  while (finger != NULL_BUFFER) {
    const Configuration_t *configuration
      = (const Configuration_t *)emGetBufferPointer(finger);
    if (endpointId == configuration->endpointId
        && emberZclCompareClusterSpec(clusterSpec,
                                      &configuration->clusterSpec)) {
      uint32_t nowMs = halCommonGetInt32uMillisecondTick();
      uint32_t delayMs = UINT32_MAX; // forever
      uint32_t elapsedMs = elapsedTimeInt32u(configuration->lastTimeMs, nowMs);
      if (hasReportableChanges(configuration)) {
        delayMs = (elapsedMs < S_TO_MS(configuration->minimumIntervalS)
                   ? S_TO_MS(configuration->minimumIntervalS) - elapsedMs
                   : 0);
      } else if (hasPeriodicReporting(configuration)) {
        delayMs = (elapsedMs < S_TO_MS(configuration->maximumIntervalS)
                   ? S_TO_MS(configuration->maximumIntervalS) - elapsedMs
                   : 0);
      }
      scheduleMakeNotification(configuration, delayMs);
    }
    finger = emBufferQueueNext(&configurations, finger);
  }
}

bool emZclHasReportingConfiguration(EmberZclEndpointId_t endpointId,
                                    const EmberZclClusterSpec_t *clusterSpec,
                                    EmberZclReportingConfigurationId_t reportingConfigurationId)
{
  return (findReportingConfiguration(endpointId,
                                     clusterSpec,
                                     reportingConfigurationId)
          != NULL);
}

// GET .../r
static void getReportingConfigurationIdsHandler(const EmZclContext_t *context)
{
  CborState state;
  uint8_t buffer[128];
  if (!emCborEncodeIndefiniteArrayStart(&state, buffer, sizeof(buffer))) {
    emZclRespond500InternalServerError();
    return;
  }

  Buffer finger = emBufferQueueHead(&configurations);
  while (finger != NULL_BUFFER) {
    const Configuration_t *configuration
      = (const Configuration_t *)emGetBufferPointer(finger);
    if (context->endpoint->endpointId == configuration->endpointId
        && emberZclCompareClusterSpec(&context->clusterSpec,
                                      &configuration->clusterSpec)
        && !emCborEncodeValue(&state,
                              EMBER_ZCLIP_TYPE_UNSIGNED_INTEGER,
                              sizeof(configuration->reportingConfigurationId),
                              (const uint8_t *)&configuration->reportingConfigurationId)) {
      emZclRespond500InternalServerError();
      return;
    }
    finger = emBufferQueueNext(&configurations, finger);
  }

  if (emCborEncodeBreak(&state)) {
    emZclRespond205ContentCborState(&state);
  } else {
    emZclRespond500InternalServerError();
  }
}

// POST .../r
static void addReportingConfigurationHandler(const EmZclContext_t *context)
{
  Buffer buffer = makeReportingConfiguration(context->endpoint->endpointId,
                                             &context->clusterSpec);
  if (buffer == NULL_BUFFER) {
    emZclRespond500InternalServerError();
    return;
  }

  const Configuration_t *configuration
    = (const Configuration_t *)emGetBufferPointer(buffer);
  if (decodeReportingConfigurationOta(context, buffer)) {
    if (addReportingConfiguration(buffer)) {
      EmberZclApplicationDestination_t destination = {
        .data.endpointId = context->endpoint->endpointId,
        .type = EMBER_ZCL_APPLICATION_DESTINATION_TYPE_ENDPOINT,
      };
      uint8_t uriPath[EMBER_ZCL_URI_PATH_MAX_LENGTH];
      emZclReportingConfigurationIdToUriPath(&destination,
                                             &context->clusterSpec,
                                             configuration->reportingConfigurationId,
                                             uriPath);
      emZclRespond201Created(uriPath);
    } else {
      emZclRespond500InternalServerError();
    }
  }
}

// zcl/e/XX/<cluster>/r:
//   GET: return list of reporting configuration ids.
//   POST: add reporting configuration.
//   OTHER: not allowed.
void emZclUriClusterReportingConfigurationHandler(EmZclContext_t *context)
{
  if (context->groupId != EMBER_ZCL_GROUP_NULL) {
    emZclRespond404NotFound();
  } else if (context->code == EMBER_COAP_CODE_GET) {
    getReportingConfigurationIdsHandler(context);
  } else if (context->code == EMBER_COAP_CODE_POST) {
    addReportingConfigurationHandler(context);
  } else {
    assert(false);
  }
}

// GET .../r/R
static void getReportingConfigurationHandler(const EmZclContext_t *context)
{
  const Configuration_t *configuration
    = findReportingConfiguration(context->endpoint->endpointId,
                                 &context->clusterSpec,
                                 context->reportingConfigurationId);
  if (configuration == NULL) {
    assert(false);
    emZclRespond500InternalServerError();
    return;
  }

  CborState state;
  uint8_t buffer[128];

  if (!emCborEncodeIndefiniteMapStart(&state, buffer, sizeof(buffer))
      || !emCborEncodeValue(&state,
                            EMBER_ZCLIP_TYPE_STRING,
                            0, // size - ignored
                            (const uint8_t *)"b")
      || !emCborEncodeValue(&state,
                            EMBER_ZCLIP_TYPE_UNSIGNED_INTEGER,
                            sizeof(configuration->backoffMs),
                            (const uint8_t *)&configuration->backoffMs)
      || !emCborEncodeValue(&state,
                            EMBER_ZCLIP_TYPE_STRING,
                            0, // size - ignored
                            (const uint8_t *)"mn")
      || !emCborEncodeValue(&state,
                            EMBER_ZCLIP_TYPE_UNSIGNED_INTEGER,
                            sizeof(configuration->minimumIntervalS),
                            (const uint8_t *)&configuration->minimumIntervalS)
      || !emCborEncodeValue(&state,
                            EMBER_ZCLIP_TYPE_STRING,
                            0, // size - ignored
                            (const uint8_t *)"mx")
      || !emCborEncodeValue(&state,
                            EMBER_ZCLIP_TYPE_UNSIGNED_INTEGER,
                            sizeof(configuration->maximumIntervalS),
                            (const uint8_t *)&configuration->maximumIntervalS)
      || !emCborEncodeValue(&state,
                            EMBER_ZCLIP_TYPE_STRING,
                            0, // size - ignored
                            (const uint8_t *)"a")
      || !emCborEncodeIndefiniteMap(&state)) {
    emZclRespond500InternalServerError();
    return;
  }

  uint8_t *reportableChanges = ((uint8_t *)configuration
                                + sizeof(Configuration_t));
  size_t offsetReportableChange = 0;

  for (size_t i = 0; i < EM_ZCL_ATTRIBUTE_COUNT; i++) {
    const EmZclAttributeEntry_t *attribute = &emZclAttributeTable[i];
    if (emberZclCompareClusterSpec(&configuration->clusterSpec,
                                   attribute->clusterSpec)
        && emZclIsAttributeLocal(attribute)
        && emZclIsAttributeReportable(attribute)) {
      if (!emCborEncodeValue(&state,
                             EMBER_ZCLIP_TYPE_UNSIGNED_INTEGER,
                             sizeof(attribute->attributeId),
                             (const uint8_t *)&attribute->attributeId)
          || !emCborEncodeIndefiniteMap(&state)) {
        emZclRespond500InternalServerError();
        return;
      }
      if (emZclIsAttributeAnalog(attribute)) {
        assert(offsetReportableChange + attribute->size
               <= configuration->sizeReportableChanges);
        if (!emCborEncodeValue(&state,
                               EMBER_ZCLIP_TYPE_STRING,
                               0, // size - ignored
                               (const uint8_t *)"r")
            || !emCborEncodeValue(&state,
                                  attribute->type,
                                  attribute->size,
                                  reportableChanges + offsetReportableChange)) {
          emZclRespond500InternalServerError();
          return;
        }
        offsetReportableChange += attribute->size;
      }
      if (!emCborEncodeBreak(&state)) {
        emZclRespond500InternalServerError();
        return;
      }
    }
  }

  // The first break closes the inner map for "a" and the second closes the
  // outer map.
  if (emCborEncodeBreak(&state) && emCborEncodeBreak(&state)) {
    emZclRespond205ContentCborState(&state);
  } else {
    emZclRespond500InternalServerError();
  }
}

// PUT .../r/R
static void updateReportingConfigurationHandler(const EmZclContext_t *context)
{
  Configuration_t *configuration
    = findReportingConfiguration(context->endpoint->endpointId,
                                 &context->clusterSpec,
                                 context->reportingConfigurationId);
  if (configuration == NULL) {
    assert(false);
    emZclRespond500InternalServerError();
    return;
  }

  Buffer buffer = emFillBuffer((const uint8_t *)configuration,
                               (sizeof(Configuration_t)
                                + configuration->sizeReportableChanges
                                + configuration->sizeLastValues));
  if (buffer == NULL_BUFFER) {
    emZclRespond500InternalServerError();
  } else if (decodeReportingConfigurationOta(context, buffer)) {
    MEMCOPY(configuration,
            emGetBufferPointer(buffer),
            emGetBufferLength(buffer));
    scheduleMakeNotification(configuration, 0);
    emZclRespond204Changed();
  }
}

// DELETE .../r/R
static void removeReportingConfigurationHandler(const EmZclContext_t *context)
{
  Configuration_t *configuration
    = findReportingConfiguration(context->endpoint->endpointId,
                                 &context->clusterSpec,
                                 context->reportingConfigurationId);
  if (configuration == NULL) {
    assert(false);
    emZclRespond500InternalServerError();
    return;
  }

  // Deleting a default reporting configuration just means resetting it to its
  // original state.  Any other reporting configuration is permanently deleted.
  if (configuration->reportingConfigurationId
      == EMBER_ZCL_REPORTING_CONFIGURATION_DEFAULT) {
    // Start sending notifications right away for the reset reporting
    // configuration.
    resetToDefaultReportingConfiguration(configuration);
    scheduleMakeNotification(configuration, 0);
  } else {
    removeReportingConfiguration(configuration);
  }
  emZclRespond202Deleted();
}

// zcl/e/XX/<cluster>/r/XX:
//   GET: return reporting configuration.
//   PUT: replace reporting configuration.
//   DELETE: remove reporting configuration.
//   OTHER: not allowed.
void emZclUriClusterReportingConfigurationIdHandler(EmZclContext_t *context)
{
  if (context->groupId != EMBER_ZCL_GROUP_NULL) {
    emZclRespond404NotFound();
  } else if (context->code == EMBER_COAP_CODE_GET) {
    getReportingConfigurationHandler(context);
  } else if (context->code == EMBER_COAP_CODE_PUT) {
    updateReportingConfigurationHandler(context);
  } else if (context->code == EMBER_COAP_CODE_DELETE) {
    removeReportingConfigurationHandler(context);
  } else {
    assert(false);
  }
}

static bool addReportingConfiguration(Buffer buffer)
{
  Configuration_t *configuration
    = (Configuration_t *)emGetBufferPointer(buffer);

  EmberZclReportingConfigurationId_t reportingConfigurationId
    = EMBER_ZCL_REPORTING_CONFIGURATION_DEFAULT;
  bool added = false;

  Buffer tmp = configurations;
  configurations = NULL_BUFFER;

  while (!emBufferQueueIsEmpty(&tmp)) {
    Buffer finger = emBufferQueueRemoveHead(&tmp);
    const Configuration_t *fingee
      = (const Configuration_t *)emGetBufferPointer(finger);

    if (!added
        && configuration->endpointId == fingee->endpointId
        && emberZclCompareClusterSpec(&configuration->clusterSpec,
                                      &fingee->clusterSpec)) {
      if (reportingConfigurationId < fingee->reportingConfigurationId) {
        configuration->reportingConfigurationId = reportingConfigurationId;
        emBufferQueueAdd(&configurations, buffer);
        added = true;
      } else if (reportingConfigurationId
                 < EMBER_ZCL_REPORTING_CONFIGURATION_NULL) {
        reportingConfigurationId++;
      }
    }
    emBufferQueueAdd(&configurations, finger);
  }

  if (!added) {
    if (reportingConfigurationId == EMBER_ZCL_REPORTING_CONFIGURATION_NULL) {
      return false;
    }
    configuration->reportingConfigurationId = reportingConfigurationId;
    emBufferQueueAdd(&configurations, buffer);
  }

  // Start sending notifications right away for this reporting configuration.
  scheduleMakeNotification(configuration, 0);
  return true;
}

static bool decodeReportingConfigurationOta(const EmZclContext_t *context,
                                            Buffer buffer)
{
  Configuration_t *configuration
    = (Configuration_t *)emGetBufferPointer(buffer);
  if ((context->payloadLength != 0
       && !emCborDecodeOneStruct(context->payload,
                                 context->payloadLength,
                                 configurationSpec,
                                 configuration))
      || !isValidReportingConfiguration(configuration)) {
    emZclRespond400BadRequest();
    return false;
  }

  //uint8_t *reportableChanges = ((uint8_t *)configuration
  //                              + sizeof(Configuration_t));
  //size_t offsetReportableChange = 0;
  //for (size_t i = 0; i < EM_ZCL_ATTRIBUTE_COUNT; i++) {
  //  const EmZclAttributeEntry_t *attribute = &emZclAttributeTable[i];
  //  if (emberZclCompareClusterSpec(&context->clusterSpec,
  //                                 attribute->clusterSpec)
  //      && emZclIsAttributeLocal(attribute)
  //      && emZclIsAttributeReportable(attribute)) {
  //    if (emZclIsAttributeAnalog(attribute)) {
  //      assert(offsetReportableChange + attribute->size
  //             <= configuration->sizeReportableChanges);
  //      // TODO: Extract <attribute id>: {"r": <reportable change>} from map
  //      // for every analog attribute.
  //      offsetReportableChange += attribute->size;
  //    }
  //  }
  //}

  return true;
}

static void makeDefaultReportingConfigurations(void)
{
  removeAllReportingConfigurations();
  for (size_t i = 0; i < emZclEndpointCount; i++) {
    const EmZclEndpointEntry_t *endpoint = &emZclEndpointTable[i];
    for (size_t j = 0; endpoint->clusterSpecs[j] != NULL; j++) {
      const EmberZclClusterSpec_t *clusterSpec = endpoint->clusterSpecs[j];
      Buffer buffer = makeReportingConfiguration(endpoint->endpointId,
                                                 clusterSpec);
      if (buffer != NULL_BUFFER) {
        addReportingConfiguration(buffer);
      }
    }
  }
}

static Buffer makeReportingConfiguration(EmberZclEndpointId_t endpointId,
                                         const EmberZclClusterSpec_t *clusterSpec)
{
  size_t sizeReportableChanges = 0;
  size_t sizeLastValues = 0;

  for (size_t i = 0; i < EM_ZCL_ATTRIBUTE_COUNT; i++) {
    const EmZclAttributeEntry_t *attribute = &emZclAttributeTable[i];
    if (emberZclCompareClusterSpec(clusterSpec, attribute->clusterSpec)
        && emZclIsAttributeLocal(attribute)
        && emZclIsAttributeReportable(attribute)) {
      if (emZclIsAttributeAnalog(attribute)) {
        sizeReportableChanges += attribute->size;
      }
      sizeLastValues += attribute->size;
    }
  }

  Buffer buffer = emAllocateBuffer(sizeof(Configuration_t)
                                   + sizeReportableChanges
                                   + sizeLastValues);
  if (buffer == NULL_BUFFER) {
    return NULL_BUFFER;
  }

  Configuration_t *configuration
    = (Configuration_t *)emGetBufferPointer(buffer);
  configuration->endpointId = endpointId;
  configuration->clusterSpec = *clusterSpec;
  configuration->reportingConfigurationId
    = EMBER_ZCL_REPORTING_CONFIGURATION_NULL;
  configuration->sizeReportableChanges = sizeReportableChanges;
  configuration->sizeLastValues = sizeLastValues;
  configuration->lastTimeMs = 0;
  resetToDefaultReportingConfiguration(configuration);
  return buffer;
}

static void resetToDefaultReportingConfiguration(Configuration_t *configuration)
{
  // 13-0402-13, section 6.7: A default report configuration (with a maximum
  // reporting interval either of 0x0000 or in the range 0x003d to 0xfffe)
  // SHALL exist for every implemented attribute that is specified as
  // reportable.
  EmberZclReportingConfiguration_t defaultReportingConfiguration = {
    .backoffMs = 0,             // no backoff
    .minimumIntervalS = 0x0000, // no minimum interval
    .maximumIntervalS = 0xFFFE, // maximum report every 65534 seconds
  };
  emberZclGetDefaultReportingConfigurationCallback(configuration->endpointId,
                                                   &configuration->clusterSpec,
                                                   &defaultReportingConfiguration);
  configuration->backoffMs = defaultReportingConfiguration.backoffMs;
  configuration->minimumIntervalS
    = defaultReportingConfiguration.minimumIntervalS;
  configuration->maximumIntervalS
    = defaultReportingConfiguration.maximumIntervalS;
  assert(isValidReportingConfiguration(configuration));

  uint8_t *reportableChanges = ((uint8_t *)configuration
                                + sizeof(Configuration_t));
  size_t offsetReportableChange = 0;

  uint8_t *lastValues = ((uint8_t *)configuration
                         + sizeof(Configuration_t)
                         + configuration->sizeReportableChanges);
  size_t offsetLastValue = 0;

  for (size_t i = 0; i < EM_ZCL_ATTRIBUTE_COUNT; i++) {
    const EmZclAttributeEntry_t *attribute = &emZclAttributeTable[i];

    if (emberZclCompareClusterSpec(&configuration->clusterSpec,
                                   attribute->clusterSpec)
        && emZclIsAttributeLocal(attribute)
        && emZclIsAttributeReportable(attribute)) {
      if (emZclIsAttributeAnalog(attribute)) {
        assert(offsetReportableChange + attribute->size
               <= configuration->sizeReportableChanges);
        emberZclGetDefaultReportableChangeCallback(configuration->endpointId,
                                                   &configuration->clusterSpec,
                                                   attribute->attributeId,
                                                   (reportableChanges
                                                    + offsetReportableChange),
                                                   attribute->size);
        offsetReportableChange += attribute->size;
      }

      assert(offsetLastValue + attribute->size
             <= configuration->sizeLastValues);
      // TODO: This isn't right for signed integers (should be 0x80...), floats
      // (should be NaN), and data types without a ZigBee-specified invalid
      // value.
      MEMSET(lastValues + offsetLastValue, 0xFF, attribute->size);
      offsetLastValue += attribute->size;
    }
  }
}

static void removeAllReportingConfigurations(void)
{
  configurations = NULL_BUFFER;
  emberFindAllEvents(actions.queue,
                     &actions,
                     NULL,  // no predicate
                     NULL); // no predicate
}

static void removeReportingConfiguration(const Configuration_t *configuration)
{
  Buffer tmp = configurations;
  configurations = NULL_BUFFER;
  while (!emBufferQueueIsEmpty(&tmp)) {
    Buffer finger = emBufferQueueRemoveHead(&tmp);
    const Configuration_t *fingee
      = (const Configuration_t *)emGetBufferPointer(finger);
    if (configuration != fingee) {
      emBufferQueueAdd(&configurations, finger);
    }
  }

  cancelMakeNotification(configuration);
  cancelSendNotification(configuration);

  // 16-07008-021, section 10.5.2.3: For entries that do not represent the
  // default report, a device SHALL permanently remove the attribute report
  // configuration entry. The device SHALL then iterate through each entry in
  // the binding table and update any entry that references deleted attribute
  // report configuration to point to the default report configuration.
  for (EmberZclBindingId_t i = 0; i < EMBER_ZCL_BINDING_TABLE_SIZE; i++) {
    EmberZclBindingEntry_t entry;
    if (emberZclGetBinding(i, &entry)) {
      if (configuration->endpointId == entry.endpointId
          && emberZclCompareClusterSpec(&configuration->clusterSpec,
                                        &entry.clusterSpec)
          && (configuration->reportingConfigurationId
              == entry.reportingConfigurationId)) {
        entry.reportingConfigurationId
          = EMBER_ZCL_REPORTING_CONFIGURATION_DEFAULT;
        emberZclSetBinding(i, &entry);
      }
    }
  }
}

static Configuration_t *findReportingConfiguration(EmberZclEndpointId_t endpointId,
                                                   const EmberZclClusterSpec_t *clusterSpec,
                                                   EmberZclReportingConfigurationId_t reportingConfigurationId)
{
  Buffer finger = emBufferQueueHead(&configurations);
  while (finger != NULL_BUFFER) {
    Configuration_t *configuration
      = (Configuration_t *)emGetBufferPointer(finger);
    if (matchReportingConfiguration(endpointId,
                                    clusterSpec,
                                    reportingConfigurationId,
                                    configuration)) {
      return configuration;
    }
    finger = emBufferQueueNext(&configurations, finger);
  }
  return NULL;
}

static bool matchReportingConfiguration(EmberZclEndpointId_t endpointId,
                                        const EmberZclClusterSpec_t *clusterSpec,
                                        EmberZclReportingConfigurationId_t reportingConfigurationId,
                                        const Configuration_t *configuration)
{
  return (endpointId == configuration->endpointId
          && emberZclCompareClusterSpec(clusterSpec,
                                        &configuration->clusterSpec)
          && (reportingConfigurationId
              == configuration->reportingConfigurationId));
}

static void eventHandler(const NotificationEvent *event)
{
  if (event->notification == NULL_BUFFER) {
    makeNotification(event);
  } else {
    sendNotification(event);
  }
}

static void makeNotification(const NotificationEvent *event)
{
  Configuration_t *configuration
    = findReportingConfiguration(event->endpointId,
                                 &event->clusterSpec,
                                 event->reportingConfigurationId);
  if (configuration == NULL) {
    assert(false);
    return;
  }
  if (configuration->sizeLastValues == 0) {
    assert(false);
    return;
  }

  // If this reporting configuration has periodic reporting, then we want to
  // let this event fire again after the maximum interval.
  scheduleMakeNotification(configuration,
                           (hasPeriodicReporting(configuration)
                            ? (configuration->maximumIntervalS
                               * MILLISECOND_TICKS_PER_SECOND)
                            : UINT32_MAX)); // forever

  // emZclDestinationToUri gives a URI ending with the endpoint id.  We want to
  // add the cluster and /a to that.
  EmberZclDestination_t destination = {
    .network = {
      .scheme = EMBER_ZCL_SCHEME_COAP,
      .data.uid = emZclUid,
      .type = EMBER_ZCL_NETWORK_DESTINATION_TYPE_UID,
      .port = EMBER_COAP_PORT,
    },
    .application = {
      .data.endpointId = configuration->endpointId,
      .type = EMBER_ZCL_APPLICATION_DESTINATION_TYPE_ENDPOINT,
    },
  };
  uint8_t uri[EMBER_ZCL_URI_MAX_LENGTH] = {0};
  uint8_t *finger = uri;
  finger += emZclDestinationToUri(&destination, finger);
  *finger++ = '/';
  emZclAttributeToUriPath(NULL, &configuration->clusterSpec, finger);

  // TODO: 16-07008-026, section 3.14.1 says "t" is included in the map for
  // timestamp, and UID or URI.  The timestamp is problematic, because not all
  // devices track time.  Fortunately, it is optional, so it is simply omitted
  // here.
  CborState state;
  uint8_t buffer[128];
  if (!emCborEncodeIndefiniteMapStart(&state, buffer, sizeof(buffer))
      || !emCborEncodeValue(&state,
                            EMBER_ZCLIP_TYPE_STRING,
                            0, // size - ignored
                            (const uint8_t *)"r")
      || !emCborEncodeValue(&state,
                            EMBER_ZCLIP_TYPE_UNSIGNED_INTEGER,
                            sizeof(configuration->reportingConfigurationId),
                            (const uint8_t *)&configuration->reportingConfigurationId)
      //|| !emCborEncodeValue(&state,
      //                      EMBER_ZCLIP_TYPE_STRING,
      //                      0, // size - ignored
      //                      (const uint8_t *)"t")
      //|| !emCborEncodeValue(&state,
      //                      EMBER_ZCLIP_TYPE_UNSIGNED_INTEGER,
      //                      sizeof(timestamp),
      //                      (const uint8_t *)&timestamp)
      || !emCborEncodeValue(&state,
                            EMBER_ZCLIP_TYPE_STRING,
                            0, // size - ignored
                            (const uint8_t *)"u")
      || !emCborEncodeValue(&state,
                            EMBER_ZCLIP_TYPE_STRING,
                            0, // size - ignored
                            uri)
      || !emCborEncodeValue(&state,
                            EMBER_ZCLIP_TYPE_STRING,
                            0, // size - ignored
                            (const uint8_t *)"a")
      || !emCborEncodeIndefiniteMap(&state)) {
    return;
  }

  uint8_t *lastValues = ((uint8_t *)configuration
                         + sizeof(Configuration_t)
                         + configuration->sizeReportableChanges);
  size_t offsetLastValue = 0;
  for (size_t i = 0; i < EM_ZCL_ATTRIBUTE_COUNT; i++) {
    const EmZclAttributeEntry_t *attribute = &emZclAttributeTable[i];
    if (emberZclCompareClusterSpec(&configuration->clusterSpec,
                                   attribute->clusterSpec)
        && emZclIsAttributeLocal(attribute)
        && emZclIsAttributeReportable(attribute)) {
      assert(offsetLastValue + attribute->size
             <= configuration->sizeLastValues);
      if (!emZclReadEncodeAttributeKeyValue(&state,
                                            configuration->endpointId,
                                            attribute,
                                            lastValues + offsetLastValue,
                                            attribute->size)) {
        return;
      }
      offsetLastValue += attribute->size;
    }
  }

  // The first break closes the inner map for "a" and the second closes the
  // outer map.
  if (emCborEncodeBreak(&state) && emCborEncodeBreak(&state)) {
    configuration->lastTimeMs = halCommonGetInt32uMillisecondTick();
    Buffer notification = emFillBuffer(buffer, emCborEncodeSize(&state));
    if (notification == NULL_BUFFER) {
      return;
    }
    scheduleSendNotification(configuration, notification);
  }
}

static void sendNotification(const NotificationEvent *event)
{
  const Configuration_t *configuration
    = findReportingConfiguration(event->endpointId,
                                 &event->clusterSpec,
                                 event->reportingConfigurationId);
  if (configuration == NULL) {
    assert(false);
    return;
  }
  if (configuration->sizeLastValues == 0) {
    assert(false);
    return;
  }

  for (EmberZclBindingId_t i = 0; i < EMBER_ZCL_BINDING_TABLE_SIZE; i++) {
    EmberZclBindingEntry_t entry = {0};
    if (emberZclGetBinding(i, &entry)
        && event->endpointId == entry.endpointId
        && emberZclCompareClusterSpec(&event->clusterSpec, &entry.clusterSpec)
        && event->reportingConfigurationId == entry.reportingConfigurationId) {

      EmberZclClusterSpec_t clusterSpec;
      emberZclReverseClusterSpec(&event->clusterSpec, &clusterSpec);
      uint8_t uriPath[EMBER_ZCL_URI_PATH_MAX_LENGTH];
      emZclNotificationToUriPath(&entry.destination.application,
                                 &clusterSpec,
                                 uriPath);

      emZclSend(&entry.destination.network,
                EMBER_COAP_CODE_POST,
                uriPath,
                emGetBufferPointer(event->notification),
                emGetBufferLength(event->notification),
                NULL, // handler
                NULL, // application data
                0);   // application data length
    }
  }
}

static void eventMarker(NotificationEvent *event)
{
  emMarkBuffer(&event->notification);
}

static bool makeNotificationPredicate(NotificationEvent *event,
                                      const Configuration_t *configuration)
{
  return (configuration->endpointId == event->endpointId
          && emberZclCompareClusterSpec(&configuration->clusterSpec,
                                        &event->clusterSpec)
          && (configuration->reportingConfigurationId
              == event->reportingConfigurationId)
          && event->notification == NULL_BUFFER);
}

static bool sendNotificationPredicate(NotificationEvent *event,
                                      const Configuration_t *configuration)
{
  return (configuration->endpointId == event->endpointId
          && emberZclCompareClusterSpec(&configuration->clusterSpec,
                                        &event->clusterSpec)
          && (configuration->reportingConfigurationId
              == event->reportingConfigurationId)
          && event->notification != NULL_BUFFER);
}

static NotificationEvent *cancelMakeNotification(const Configuration_t *configuration)
{
  return (NotificationEvent *)emberFindEvent(actions.queue,
                                             &actions,
                                             (EventPredicate)makeNotificationPredicate,
                                             (void *)configuration);
}

static NotificationEvent *cancelSendNotification(const Configuration_t *configuration)
{
  return (NotificationEvent *)emberFindEvent(actions.queue,
                                             &actions,
                                             (EventPredicate)sendNotificationPredicate,
                                             (void *)configuration);
}

static void scheduleMakeNotification(const Configuration_t *configuration,
                                     uint32_t delayMs)
{
  NotificationEvent *event = cancelMakeNotification(configuration);
  if (delayMs != UINT32_MAX && configuration->sizeLastValues != 0) {
    if (event == NULL) {
      Buffer buffer = emAllocateBuffer(sizeof(NotificationEvent));
      if (buffer != NULL_BUFFER) {
        event = (NotificationEvent *)emGetBufferPointer(buffer);
      }
    }
    if (event != NULL) {
      event->event.actions = &actions;
      event->event.next = NULL;
      event->endpointId = configuration->endpointId;
      event->clusterSpec = configuration->clusterSpec;
      event->reportingConfigurationId = configuration->reportingConfigurationId;
      event->notification = NULL_BUFFER;
      emberEventSetDelayMs((Event *)event, delayMs);
    }
  }
}

static void scheduleSendNotification(const Configuration_t *configuration,
                                     Buffer notification)
{
  assert(configuration->sizeLastValues != 0);

  // TODO: What should happen if an existing notification from this reporting
  // configuration is already queued?  If we remove it in favor of this one,
  // the next notification may remove us in favor of itself.  With short
  // intervals and long backoffs, notifications may never be sent.  On the
  // other hand, not removing a pending notification might mean a new one goes
  // before an old one, confusing the other side.
  Buffer buffer = emAllocateBuffer(sizeof(NotificationEvent));
  if (buffer != NULL_BUFFER) {
    NotificationEvent *event = (NotificationEvent *)emGetBufferPointer(buffer);
    event->event.actions = &actions;
    event->event.next = NULL;
    event->endpointId = configuration->endpointId;
    event->clusterSpec = configuration->clusterSpec;
    event->reportingConfigurationId = configuration->reportingConfigurationId;
    event->notification = notification;

    uint32_t backoffMs = halCommonGetRandom() % (configuration->backoffMs + 1);
    emberEventSetDelayMs((Event *)event, backoffMs);
  }
}

static bool isValidReportingConfiguration(const Configuration_t *configuration)
{
  // 07-5123-06, section 2.5.7.3: If the minimum reporting interval field is
  // less than any minimum set by the relevant cluster specification or
  // application, or the value of the maximum reporting interval field is
  // non-zero and is less than that of the minimum reporting interval field,
  // the device SHALL construct an attribute status record with the status
  // field set to INVALID_VALUE.
  if (configuration->maximumIntervalS != 0
      && configuration->maximumIntervalS < configuration->minimumIntervalS) {
    return false;
  }

  // With multiple reporting configuration, it doesn't make sense to support
  // the "use default reporting configuration" settings.
  if (useDefaultReportingConfiguration(configuration)) {
    return false;
  }

  return true;
}

static bool hasPeriodicReporting(const Configuration_t *configuration)
{
  return ((configuration->maximumIntervalS
           != MAXIMUM_INTERVAL_S_DISABLE_REPORTING)
          && (configuration->maximumIntervalS
              != MAXIMUM_INTERVAL_S_DISABLE_PERIODIC_REPORTING));
}

static bool hasReportableChanges(const Configuration_t *configuration)
{
  if (configuration->sizeLastValues == 0) {
    return false;
  }

  const uint8_t *reportableChanges = ((const uint8_t *)configuration
                                      + sizeof(Configuration_t));
  size_t offsetReportableChange = 0;

  const uint8_t *lastValues = ((const uint8_t *)configuration
                               + sizeof(Configuration_t)
                               + configuration->sizeReportableChanges);
  size_t offsetLastValue = 0;

  for (size_t i = 0; i < EM_ZCL_ATTRIBUTE_COUNT; i++) {
    const EmZclAttributeEntry_t *attribute = &emZclAttributeTable[i];
    if (emberZclCompareClusterSpec(&configuration->clusterSpec,
                                   attribute->clusterSpec)
        && emZclIsAttributeLocal(attribute)
        && emZclIsAttributeReportable(attribute)) {

      if (emZclIsAttributeAnalog(attribute)) {
        assert(offsetReportableChange + attribute->size
               <= configuration->sizeReportableChanges);
      }
      assert(offsetLastValue + attribute->size
             <= configuration->sizeLastValues);

      uint8_t newValue[EMBER_ZCL_ATTRIBUTE_MAX_SIZE];
      if ((emZclReadAttributeEntry(configuration->endpointId,
                                   attribute,
                                   newValue,
                                   sizeof(newValue))
           == EMBER_ZCL_STATUS_SUCCESS)
          && hasReportableChange(lastValues + offsetLastValue,
                                 reportableChanges + offsetReportableChange,
                                 newValue,
                                 attribute->size)) {
        return true;
      }

      if (emZclIsAttributeAnalog(attribute)) {
        offsetReportableChange += attribute->size;
      }
      offsetLastValue += attribute->size;
    }
  }

  return false;
}

static bool hasReportableChange(const uint8_t *oldValue,
                                const uint8_t *reportableChange,
                                const uint8_t *newValue,
                                size_t size)
{
  // 07-5123-06, section 2.5.11.2.2: If the attribute has a 'discrete' data
  // type, a report SHALL be generated when the attribute undergoes any change
  // of value.
  // 07-5123-06, section 2.5.11.2.3: If the attribute has an 'analog' data
  // type, a report SHALL be generated when the attribute undergoes a change of
  // value, in a positive or negative direction, equal to or greater than the
  // Reportable Change for that attribute (see 2.5.7.1.7).  The change is
  // measured from the value of the attribute when the Reportable Change is
  // configured, and thereafter from the previously reported value of the
  // attribute.
  // TODO: Handle reportable changes.
  //if (isIntegerDataType() || isUtcTimeDataType()) {
  //  return reportableChange <= abs(oldValue - newValue);
  //} else if (isFloatingPointDataType()) {
  //  ...;
  //} else if (isTimeOfDayDataType()) {
  //  hours/minutes/seconds/hundredths;
  //} else if (isDateDataType()) {
  //  year-1900/month/day of month/day of week;
  //} else {
    return (MEMCOMPARE(oldValue, newValue, size) != 0);
  //}
}

static bool useDefaultReportingConfiguration(const Configuration_t *configuration)
{
  return ((configuration->minimumIntervalS
           == MINIMUM_INTERVAL_S_USE_DEFAULT_REPORTING_CONFIGURATION)
          && (configuration->maximumIntervalS
              == MAXIMUM_INTERVAL_S_USE_DEFAULT_REPORTING_CONFIGURATION));
}
