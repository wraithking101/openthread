// Copyright 2016 Silicon Laboratories, Inc.

#include PLATFORM_HEADER
#include CONFIGURATION_HEADER
#include <stdio.h>
#include "zcl-core.h"

// Copies "src," including the NUL terminator, to "dst."  The number of
// characters written, not including the NUL, is returned.
static size_t writeString(uint8_t *dst, const char *src)
{
  uint8_t *finger = dst;
  while ((*finger = *src) != '\0') {
    finger++;
    src++;
  }
  return finger - dst;
}

// Yields '0' through '9' or 'a' through 'f' only.
static uint8_t nibbleToHexChar(uint8_t nibble)
{
  assert(nibble <= 0x0F);
  return nibble + (nibble < 10 ? '0' : 'a' - 10);
}

// Accepts '0' through '9' and 'a' through 'f' only.  'A' through 'F' and any
// non-alphanumeric characters result in a outside the range 0x00--0x0F.
static uint8_t hexCharToNibble(char c)
{
  return ('0' <= c && c <= '9'
          ? c - '0'
          : ('a' <= c && c <= 'f'
             ? c - ('a' - 10)
             : (uint8_t)-1));
}

// Writes the integer "value" as a hexadecimal string to "result."  "size" is
// the number of bytes in "value."  Leading zeros are omitted from the string
// and lowercase letters are used for nibbles with values from 0xA to 0xF.  The
// string is NUL terminated.  The number of characters written, not including
// the NUL, is returned.  "result" is assumed to be adequately sized.
//   0x0000: returns 1 and sets "result" to "0"
//   0x0001: returns 1 and sets "result" to "1"
//   0xDEAD: returns 4 and sets "result" to "dead"
uint8_t emZclIntToHexString(uintmax_t value, size_t size, uint8_t *result)
{
  assert(size <= sizeof(value));
  assert(value <= (1 << (8 * size)) - 1);

  uint8_t *finger = result;
  bool printing = false;
  const size_t length = 2 * size; // bytes to nibbles
  for (uint8_t i = 0; i < length; i++) {
    uint8_t shift = (4 * (length - i - 1));
    uint8_t nibble = (value >> shift) & 0x0F;
    if (printing || nibble != 0 || i == length - 1) {
      *finger++ = nibbleToHexChar(nibble);
      printing = true;
    }
  }
  *finger = '\0';
  return finger - result;
}

// Converts the hexadecimal string "chars" to the integer "result."  "length"
// is the number of characters to read.  Strings with leading zeros, uppercase
// letters, or any non-alphanumeric characters are rejected as invalid.
//   "0": returns true and sets "result" to 0x0
//   "1": returns true and sets "result" to 0x1
//   "01": returns false
//   "dead": returns true and sets "result" to 0xDEAD
bool emZclHexStringToInt(const uint8_t *chars, size_t length, uintmax_t *result)
{
  if (sizeof(*result) < (length + 1) / 2) {
    return false;
  }

  uintmax_t tmp = 0;
  for ( ; length; chars++, length--) {
    uint8_t nibble = hexCharToNibble(*chars);
    if (0x0F < nibble || (nibble == 0 && tmp == 0 && length != 1)) {
      return false;
    } else {
      tmp = (tmp << 4) + nibble;
    }
  }
  *result = tmp;
  return true;
}

size_t emZclClusterToString(const EmberZclClusterSpec_t *clusterSpec,
                            uint8_t *result)
{
  uint8_t *finger = result;
  *finger++ = (clusterSpec->role == EMBER_ZCL_ROLE_CLIENT ? 'c' : 's');
  if (clusterSpec->manufacturerCode != EMBER_ZCL_MANUFACTURER_CODE_NULL) {
    finger += emZclIntToHexString(clusterSpec->manufacturerCode,
                                  sizeof(clusterSpec->manufacturerCode),
                                  finger);
    *finger++ = EMBER_ZCL_URI_PATH_MANUFACTURER_CODE_CLUSTER_ID_SEPARATOR;
  }
  finger += emZclIntToHexString(clusterSpec->id,
                                sizeof(clusterSpec->id),
                                finger);
  // emZclIntToHexString adds a null terminator, so we don't need to add one
  // here.
  return finger - result;
}

bool emZclStringToCluster(const uint8_t *chars,
                          size_t length,
                          EmberZclClusterSpec_t *clusterSpec)
{
  EmberZclRole_t role;
  uintmax_t manufacturerCode;
  uintmax_t clusterId;

  const uint8_t *finger = chars;

  // Cluster ids start with c for client or s for server.  No other values are
  // valid.
  if (*finger == 'c') {
    role = EMBER_ZCL_ROLE_CLIENT;
  } else if (*finger == 's') {
    role = EMBER_ZCL_ROLE_SERVER;
  } else {
    return false;
  }
  finger++;
  length--;

  // Next is either the manufacturer code and cluster id joined together or
  // just the cluster id by itself.  Both are two-byte values.
  const uint8_t *separator
    = memchr(finger,
             EMBER_ZCL_URI_PATH_MANUFACTURER_CODE_CLUSTER_ID_SEPARATOR,
             length);
  if (separator == NULL) {
    manufacturerCode = EMBER_ZCL_MANUFACTURER_CODE_NULL;
  } else {
    uint16_t sublength = separator - finger;
    if (! (sublength <= sizeof(clusterSpec->manufacturerCode) * 2 // bytes to nibbles
           && emZclHexStringToInt(finger, sublength, &manufacturerCode)
           && manufacturerCode != EMBER_ZCL_MANUFACTURER_CODE_NULL)) {
      return false;
    }
    finger = separator + 1;
    length = length - sublength - 1;
  }
  if (! (length <= sizeof(clusterSpec->id) * 2 // bytes to nibbles
         && emZclHexStringToInt(finger, length, &clusterId))) {
    return false;
  }

  clusterSpec->role = role;
  clusterSpec->manufacturerCode = manufacturerCode;
  clusterSpec->id = clusterId;
  return true;
}
// It converts the endpoint or group to equivalent null terminated uri string
// destination  : input that has the endpoint or group value and type
// result       : output collecter - caller ensures the memeory is allocated
// returns      : size of the output
static size_t applicationDestinationToUriPath(const EmberZclApplicationDestination_t *destination,
                                              uint8_t *result)
{
  uint8_t *finger = result;
  if (destination->type == EMBER_ZCL_APPLICATION_DESTINATION_TYPE_ENDPOINT) {
    finger += writeString(finger, "zcl/e/");
    finger += emZclIntToHexString(destination->data.endpointId,
                                  sizeof(destination->data.endpointId),
                                  finger);
  } else {
    finger += writeString(finger, "zcl/g/");
    finger += emZclIntToHexString(destination->data.groupId,
                                  sizeof(destination->data.groupId),
                                  finger);
  }

  // emZclIntToHexString adds a null terminator, so we don't need to add one
  // here.
  return finger - result;
}

static size_t clusterToUriPath(const EmberZclApplicationDestination_t *destination,
                               const EmberZclClusterSpec_t *clusterSpec,
                               uint8_t *result)
{
  uint8_t *finger = result;
  if (destination != NULL) {
    finger += applicationDestinationToUriPath(destination, finger);
    *finger++ = '/';
  }
  finger += emZclClusterToString(clusterSpec, finger);
  // emZclClusterToString adds a null terminator, so we don't need to add one
  // here.
  return finger - result;
}

size_t emZclThingToUriPath(const EmberZclApplicationDestination_t *destination,
                           const EmberZclClusterSpec_t *clusterSpec,
                           char thing,
                           uint8_t *result)
{
  uint8_t *finger = result;
  finger += clusterToUriPath(destination, clusterSpec, finger);
  *finger++ = '/';
  *finger++ = thing;
  *finger = '\0';
  return finger - result;
}

size_t emZclThingIdToUriPath(const EmberZclApplicationDestination_t *destination,
                             const EmberZclClusterSpec_t *clusterSpec,
                             char thing,
                             uintmax_t thingId,
                             size_t size,
                             uint8_t *result)
{
  uint8_t *finger = result;
  finger += emZclThingToUriPath(destination, clusterSpec, thing, finger);
  *finger++ = '/';
  finger += emZclIntToHexString(thingId, size, finger);
  // emZclIntToHexString adds a null terminator, so we don't need to add one
  // here.
  return finger - result;
}

size_t emZclDestinationToUri(const EmberZclDestination_t *destination,
                             uint8_t *result)
{
  uint8_t *finger = result;

  if (destination->network.scheme == EMBER_ZCL_SCHEME_COAP) {
    finger += writeString(finger, "coap://");
  } else if (destination->network.scheme == EMBER_ZCL_SCHEME_COAPS) {
    finger += writeString(finger, "coaps://");
  } else {
    assert(false);
  }

  switch (destination->network.type) {
  case EMBER_ZCL_NETWORK_DESTINATION_TYPE_ADDRESS:
    *finger++ = '[';
    if (!emberIpv6AddressToString(&destination->network.data.address,
                                  finger,
                                  EMBER_IPV6_ADDRESS_STRING_SIZE)) {
      return 0;
    }
    finger += strlen((const char *)finger);
    *finger++ = ']';
    break;
  case EMBER_ZCL_NETWORK_DESTINATION_TYPE_UID:
    finger += writeString(finger, "nih:sha-256;");
    finger += emZclUidToString(&destination->network.data.uid,
                               EMBER_ZCL_UID_BITS,
                               finger);
    break;
  default:
    assert(false);
    return 0;
  }

  if ((destination->network.scheme == EMBER_ZCL_SCHEME_COAP
       && destination->network.port != EMBER_COAP_PORT)
      || (destination->network.scheme == EMBER_ZCL_SCHEME_COAPS
          && destination->network.port != EMBER_COAP_SECURE_PORT)) {
    finger += sprintf((char *)finger, ":%u", destination->network.port);
  }

  // applicationDestinationToUriPath adds a null terminator, so we don't need
  // to add one here.
  *finger++ = '/';
  finger += applicationDestinationToUriPath(&destination->application, finger);
  return finger - result;
}

bool emZclUriToDestination(const uint8_t *uri, EmberZclDestination_t *result)
{
  return emZclUriToDestinationAndCluster(uri,
                                         result,
                                         NULL); // cluster spec
}

bool emZclUriToDestinationAndCluster(const uint8_t *uri,
                                     EmberZclDestination_t *result,
                                     EmberZclClusterSpec_t *clusterSpec)
{
  const uint8_t *finger = uri;
  if (strncmp((const char *)finger, "coap://", 7) == 0) {
    finger += 7;
    result->network.scheme = EMBER_ZCL_SCHEME_COAP;
    result->network.port = EMBER_COAP_PORT;
  } else if (strncmp((const char *)finger, "coaps://", 8) == 0) {
    finger += 8;
    result->network.scheme = EMBER_ZCL_SCHEME_COAPS;
    result->network.port = EMBER_COAP_SECURE_PORT;
  } else {
    return false;
  }

  if (*finger == '[') {
    finger++;
    const uint8_t *end = (const uint8_t *)strchr((const char *)finger, ']');
    if (end == NULL || EMBER_IPV6_ADDRESS_STRING_SIZE <= end - finger) {
      return false;
    }
    uint8_t address[EMBER_IPV6_ADDRESS_STRING_SIZE] = {0};
    MEMCOPY(address, finger, end - finger);
    if (!emberIpv6StringToAddress(address, &result->network.data.address)) {
      return false;
    }
    finger = end + 1;
    result->network.type = EMBER_ZCL_NETWORK_DESTINATION_TYPE_ADDRESS;
  } else if (strncmp((const char *)finger, "nih:sha-256;", 12) == 0) {
    finger += 12;
    uint16_t uidBits;
    if (!emZclStringToUid(finger,
                          EMBER_ZCL_UID_STRING_LENGTH,
                          &result->network.data.uid,
                          &uidBits)) {
      return false;
    }
    if (uidBits != EMBER_ZCL_UID_BITS) {
      return false;
    }
    finger += EMBER_ZCL_UID_STRING_LENGTH;
    result->network.type = EMBER_ZCL_NETWORK_DESTINATION_TYPE_UID;
  } else {
    return false;
  }

  if (*finger == ':') {
    finger++;
    result->network.port = 0;
    do {
      uint8_t value = emberHexToInt(*finger);
      if (9 < value) {
        return false;
      }
      uintmax_t port = 10 * result->network.port + value;
      if (UINT16_MAX < port) {
        return false;
      }
      result->network.port = (uint16_t)port;
      finger++;
    } while (*finger != '/');
  }

  if (*finger != '/') {
    return false;
  }
  finger++;

  if (strncmp((const char *)finger, "zcl/e/", 6) == 0
      || strncmp((const char *)finger, "zcl/g/", 6) == 0) {
    finger += 6;
    const uint8_t *end = (const uint8_t *)strchr((const char *)finger, '/');
    if (end == NULL) {
      end = finger + strlen((const char *)finger);
    }
    size_t length = end - finger;
    uintmax_t id;
    if (!emZclHexStringToInt(finger, length, &id)) {
      return false;
    }
    if (finger[-2] == 'e') {
      result->application.data.endpointId = (EmberZclEndpointId_t)id;
      result->application.type = EMBER_ZCL_APPLICATION_DESTINATION_TYPE_ENDPOINT;
    } else {
      result->application.data.groupId = (EmberZclGroupId_t)id;
      result->application.type = EMBER_ZCL_APPLICATION_DESTINATION_TYPE_GROUP;
    }
    finger = end;
  } else {
    return false;
  }

  if (clusterSpec == NULL) {
    return (*finger == '\0');
  }

  if (*finger != '/') {
    return false;
  }
  finger++;

  const uint8_t *end = (const uint8_t *)strchr((const char *)finger, '/');
  if (end == NULL) {
    end = finger + strlen((const char *)finger);
  }
  size_t length = end - finger;

  if (!emZclStringToCluster(finger, length, clusterSpec)) {
    return false;
  }
  finger = end;

  return true;
}

size_t emZclUidToString(const EmberZclUid_t *uid,
                        uint16_t uidBits,
                        uint8_t *result)
{
  assert(uidBits <= EMBER_ZCL_UID_BITS);

  // We can only send whole nibbles over the air, so the number of bits
  // requested by the user has to be massaged into a multiple of four.
  uidBits &= 0xFFFC;

  uint8_t *finger = result;
  for (size_t i = 0; i < uidBits; i += 4) { // bytes to bits
    uint8_t byte = uid->bytes[i / 8];
    uint8_t nibble = (byte >> (4 - (i % 8))) & 0x0F;
    // TODO: Should we print uppercase hexits in the UID?
    *finger++ = nibbleToHexChar(nibble);
  }
  *finger = '\0';
  return finger - result;
}

bool emZclStringToUid(const uint8_t *uid,
                      size_t length,
                      EmberZclUid_t *result,
                      uint16_t *resultBits)
{
  if (EMBER_ZCL_UID_STRING_LENGTH < length) {
    return false;
  }

  MEMSET(result, 0, sizeof(EmberZclUid_t));
  *resultBits = 0;

  const uint8_t *finger = uid;
  for (size_t i = 0; i < length; i++) {
    // TODO: Should we accept uppercase hexits in the UID?
    uint8_t nibble = hexCharToNibble(*finger++);
    if (0x0F < nibble) {
      return false;
    }
    result->bytes[*resultBits / 8] += (nibble << ((*resultBits + 4) % 8));
    *resultBits += 4;
  }
  return true;
}
