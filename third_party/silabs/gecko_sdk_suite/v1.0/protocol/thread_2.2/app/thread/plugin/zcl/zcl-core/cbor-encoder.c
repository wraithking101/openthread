// File: cbor-encoder.c
// Description: Writing out structs as CBOR (RFC 7049).
//
// Copyright 2015 Silicon Laboratories, Inc.                                *80*

#include PLATFORM_HEADER
#include CONFIGURATION_HEADER
#include EMBER_AF_API_STACK
#include "zcl-core.h"

//----------------------------------------------------------------
// Encoding

static uint32_t addCborHeader(CborState *state, uint8_t type, uint32_t length)
{
  uint8_t temp[5];
  uint8_t *finger = temp;

  if (state->finger == NULL || state->end <= state->finger){
    return false;
  }

  if (length <= CBOR_MAX_LENGTH) {
    *finger++ = type | length;
  } else if (length < 1 << 8) {
    *finger++ = type | CBOR_1_BYTE_LENGTH;
    *finger++ = length;
  } else if (length < 1 << 16) {
    *finger++ = type | CBOR_2_BYTE_LENGTH;
    *finger++ = HIGH_BYTE(length);
    *finger++ = LOW_BYTE(length);
  } else {
    *finger++ = type | CBOR_4_BYTE_LENGTH;
    emberStoreHighLowInt32u(finger, length);
    finger += 4;
  }

  uint8_t bytes = finger - temp;
  if (state->finger + bytes <= state->end) {
    MEMCOPY(state->finger, temp, bytes);
    state->finger += bytes;
    return bytes;
  }

  return 0;
}

static uint32_t appendBytes(CborState *state,
                            uint8_t type,
                            const uint8_t *bytes, 
                            uint16_t length)
{
  uint32_t len = addCborHeader(state, type, length);

  if (len && (state->finger + length < state->end)) {
    MEMCOPY(state->finger, bytes, length);
    state->finger += length;
    len += length;
  }
  return len;
}

bool emCborEncodeKey(CborState *state, uint16_t key)
{
  return (addCborHeader(state, CBOR_UNSIGNED, key) != 0);
}

bool emCborEncodeValue(CborState *state,
                       uint8_t type,
                       uint16_t valueLength,
                       const uint8_t *valueLoc)
{
  uint32_t appendedLen = 0;

  if (state->finger == NULL
      || state->end <= state->finger){
    return false;
  }

  switch (type) {
  case EMBER_ZCLIP_TYPE_BOOLEAN:
    if (state->finger < state->end) {
      *state->finger++ = (*((uint8_t *) valueLoc) != 0
                          ? CBOR_TRUE
                          : CBOR_FALSE);
      appendedLen = -1;
    }
    break;

  case EMBER_ZCLIP_TYPE_UNSIGNED_INTEGER:
    appendedLen = addCborHeader(state,
                                CBOR_UNSIGNED, 
                                emFetchInt32uValue(valueLoc, valueLength));
    break;

  case EMBER_ZCLIP_TYPE_INTEGER: {
    int32_t n = emFetchInt32sValue(valueLoc, valueLength);
    if (n < 0) {
      appendedLen = addCborHeader(state, CBOR_NEGATIVE, -1 - n);
    } else {
      appendedLen = addCborHeader(state, CBOR_UNSIGNED, n);
    }
    break;
  }

  case EMBER_ZCLIP_TYPE_FIXED_LENGTH_BINARY:
    appendedLen = appendBytes(state, CBOR_BYTES, valueLoc, valueLength);
    break;

  case EMBER_ZCLIP_TYPE_STRING:
  case EMBER_ZCLIP_TYPE_MAX_LENGTH_STRING: {
    appendedLen = appendBytes(state, CBOR_TEXT, valueLoc, strlen((const char *) valueLoc));
    break;
  }
    
  case EMBER_ZCLIP_TYPE_UINT8_LENGTH_STRING: {
    const uint8_t *data = *((uint8_t **) ((void *) valueLoc));
    appendedLen = appendBytes(state,
                              CBOR_TEXT,
                              data + EMBER_ZCL_STRING_OVERHEAD,
                              emberZclStringLength(data));
    break;
  }

  case EMBER_ZCLIP_TYPE_UINT16_LENGTH_STRING: {
    // TODO: Handle long ZigBee strings.
    appendedLen = 0;
    break;
  }

  default:
    appendedLen = 0;
    break;
  }
  return appendedLen != 0;
}

void emCborEncodeStart(CborState *state, uint8_t *output, uint16_t outputSize)
{
  MEMSET(state, 0, sizeof(CborState));
  state->start = output;
  state->finger = output;
  state->end = output + outputSize;
}

uint32_t emCborEncodeSize(const CborState *state)
{
  return state->finger - state->start;
}

bool emCborEncodeStruct(CborState *state,
                        const ZclipStructSpec *structSpec,
                        const void *theStruct)
{
  ZclipStructData structData;
  uint16_t i;

  if (state->finger == NULL 
      || state->end <= state->finger){
    return false;
  }

  if (!emExpandZclipStructData(structSpec, &structData)){
    return false;
  }

  emCborEncodeIndefiniteMap(state);

  for (i = 0; i < structData.fieldCount; i++) {
    ZclipFieldData fieldData;
    emGetNextZclipFieldData(&structData, &fieldData);
    const uint8_t *valueLoc = (uint8_t *) theStruct + fieldData.valueOffset;
    if (fieldData.name == NULL) {
      addCborHeader(state, CBOR_UNSIGNED, i);
    } else {
      emCborEncodeValue(state,
                        EMBER_ZCLIP_TYPE_STRING,
                        0, // value length - unused
                        (const uint8_t *)fieldData.name);
    }
    emCborEncodeValue(state,
                      fieldData.valueType,
                      fieldData.valueSize,
                      valueLoc);
                   
  }

  emCborEncodeBreak(state);
  return true;
}

uint16_t emCborEncodeOneStruct(uint8_t *output,
                               uint16_t outputSize,
                               const ZclipStructSpec *structSpec,
                               const void *theStruct)
{
  CborState state;
  emCborEncodeStart(&state, output, outputSize);
  emCborEncodeStruct(&state, structSpec, theStruct);
  return emCborEncodeSize(&state);
}

// Maps

bool emCborEncodeMap(CborState *state, uint16_t count)
{
  return (addCborHeader(state, CBOR_MAP, count) != 0);
}

bool emCborEncodeMapStart(CborState *state,
                          uint8_t *output,
                          uint16_t outputSize,
                          uint16_t count)
{
  emCborEncodeStart(state, output, outputSize);
  return emCborEncodeMap(state, count);
}

bool emCborEncodeIndefiniteMapStart(CborState *state,
                                    uint8_t *output,
                                    uint16_t outputSize)
{
  emCborEncodeStart(state, output, outputSize);
  return emCborEncodeIndefiniteMap(state);
}

bool emCborEncodeIndefinite(CborState *state, uint8_t valueType)
{
  if (state->finger == NULL
      || state->end <= state->finger){
    return false;
  } else if (2 <= state->end - state->finger) {
    *state->finger++ = valueType | CBOR_INDEFINITE_LENGTH;
    return true;
  } else {
    return false;
  }
}

bool emCborEncodeBreak(CborState *state)
{
  if (state->finger == NULL 
      || state->end <= state->finger){
    return false;
  }

  if (1 <= state->end - state->finger) {
    *state->finger++ = CBOR_BREAK;
    return true;
  } else {
    return false;
  }
}

bool emCborEncodeMapEntry(CborState *state,
                          uint16_t key,
                          uint8_t valueType,
                          uint16_t valueSize,
                          const uint8_t *valueLoc)
{
  return (emCborEncodeKey(state, key)
          && emCborEncodeValue(state, valueType, valueSize, valueLoc));
}

// Arrays

bool emCborEncodeArray(CborState *state, uint16_t count)
{
  return (addCborHeader(state, CBOR_ARRAY, count) != 0);
}

bool emCborEncodeArrayStart(CborState *state,
                            uint8_t *output,
                            uint16_t outputSize,
                            uint16_t count)
{
  emCborEncodeStart(state, output, outputSize);
  return emCborEncodeArray(state, count);
}

bool emCborEncodeIndefiniteArrayStart(CborState *state,
                                      uint8_t *output,
                                      uint16_t outputSize)
{
  emCborEncodeStart(state, output, outputSize);
  return emCborEncodeIndefiniteArray(state);
}

//----------------------------------------------------------------
// Decoding

void emCborDecodeStart(CborState *state,
                       const uint8_t *input,
                       uint16_t inputSize)
{
  MEMSET(state, 0, sizeof(CborState));
  state->start = (uint8_t *) input;
  state->finger = (uint8_t *) input;
  state->end = (uint8_t *) input + inputSize;
}

static bool peekOrReadCborHeaderLength(CborState *state,
                                       uint8_t b0,
                                       uint32_t *result,
                                       bool read)
{
  uint8_t length = b0 & CBOR_LENGTH_MASK;
  *result = 0;

  uint8_t *finger = state->finger;
  if (finger == NULL) {
    return false;
  }

  if (length == CBOR_INDEFINITE_LENGTH) {
    *result = -1;
  } else if (length <= CBOR_MAX_LENGTH) {
    *result = length;
  } else if (length == CBOR_1_BYTE_LENGTH) {
    *result = *finger++;
  } else if (length == CBOR_2_BYTE_LENGTH) {
    *result = HIGH_LOW_TO_INT(finger[0], finger[1]);
    finger += 2;
  } else {
    *result = emberFetchHighLowInt32u(finger);
    finger += 4;
  }

  if (read) {
    state->finger = finger;
  }

  return (finger <= state->end);
}

static bool readCborHeaderLength(CborState *state,
                                 uint8_t b0,
                                 uint32_t *result)
{
  return peekOrReadCborHeaderLength(state, b0, result, true);
}

static const uint32_t uintMasks[] = {
  0x000000FF,
  0x0000FFFF,
  0,
  0xFFFFFFFF
};

// returns whether read is successful.
static EmZclCoreCborValueReadStatus_t readCborValue(CborState *state,
                                                    ZclipFieldData *fieldData,
                                                    uint8_t *valueLocation)
{
  if (state->finger == NULL
      || state->end <= state->finger){
    return EM_ZCL_CORE_CBOR_VALUE_READ_ERROR;
  }

  uint8_t b0 = *state->finger++;

  switch (fieldData->valueType) {
  case EMBER_ZCLIP_TYPE_BOOLEAN: 
    if (b0 == CBOR_TRUE || b0 == CBOR_FALSE) {
      *valueLocation = (b0 == CBOR_TRUE);
      return EM_ZCL_CORE_CBOR_VALUE_READ_SUCCESS;
    } else {
      return EM_ZCL_CORE_CBOR_VALUE_READ_INVALID_BOOLEAN_VALUE;
    }

  case EMBER_ZCLIP_TYPE_UNSIGNED_INTEGER:
    if ((b0 & CBOR_TYPE_MASK) == CBOR_UNSIGNED) {
      uint32_t value = 0;
      if (readCborHeaderLength(state, b0, &value)) {
        if ((value & uintMasks[fieldData->valueSize - 1]) == value) {
          emStoreInt32uValue(valueLocation, value, fieldData->valueSize);
          return EM_ZCL_CORE_CBOR_VALUE_READ_SUCCESS;
        } else {
          return EM_ZCL_CORE_CBOR_VALUE_READ_VALUE_TOO_LARGE;
        }
      } else {
        return EM_ZCL_CORE_CBOR_VALUE_READ_ERROR;
      }
    } else {
      return EM_ZCL_CORE_CBOR_VALUE_READ_WRONG_TYPE;
    }

  case EMBER_ZCLIP_TYPE_INTEGER:
    if ((b0 & CBOR_TYPE_MASK) == CBOR_UNSIGNED
        || (b0 & CBOR_TYPE_MASK) == CBOR_NEGATIVE) {
      uint32_t value = 0;
      if (readCborHeaderLength(state, b0, &value)) {
        if ((value & (uintMasks[fieldData->valueSize - 1] >> 1)) == value) {
          emStoreInt32uValue(valueLocation,
                             ((b0 & CBOR_TYPE_MASK) == CBOR_UNSIGNED
                              ? value
                              : -1 - (int32_t) value),
                             fieldData->valueSize);
          return EM_ZCL_CORE_CBOR_VALUE_READ_SUCCESS;
        } else {
          return EM_ZCL_CORE_CBOR_VALUE_READ_VALUE_TOO_LARGE;
        }
      } else {
        return EM_ZCL_CORE_CBOR_VALUE_READ_ERROR;
      }
    } else {
      return EM_ZCL_CORE_CBOR_VALUE_READ_WRONG_TYPE;
    }

  case EMBER_ZCLIP_TYPE_FIXED_LENGTH_BINARY:
    if ((b0 & CBOR_TYPE_MASK) == CBOR_BYTES) {
      EmZclCoreCborValueReadStatus_t status;
      uint32_t length = 0;
      if (readCborHeaderLength(state, b0, &length)) {
        if (length == fieldData->valueSize) {
          MEMCOPY(valueLocation, state->finger, length);
          status = EM_ZCL_CORE_CBOR_VALUE_READ_SUCCESS;
        } else if (length < fieldData->valueSize) {
          status = EM_ZCL_CORE_CBOR_VALUE_READ_VALUE_TOO_SMALL;
        } else {
          status = EM_ZCL_CORE_CBOR_VALUE_READ_VALUE_TOO_LARGE;
        }
        state->finger += length;
        return status;
      } else {
        return EM_ZCL_CORE_CBOR_VALUE_READ_ERROR;
      }
    } else {
      return EM_ZCL_CORE_CBOR_VALUE_READ_WRONG_TYPE;
    }

  case EMBER_ZCLIP_TYPE_MAX_LENGTH_STRING:
    if ((b0 & CBOR_TYPE_MASK) == CBOR_TEXT) {
      EmZclCoreCborValueReadStatus_t status;
      uint32_t length = 0;
      if (readCborHeaderLength(state, b0, &length)) {
        if (length + 1 <= fieldData->valueSize) {
          MEMCOPY(valueLocation, state->finger, length);
          valueLocation[length] = 0;
          status = EM_ZCL_CORE_CBOR_VALUE_READ_SUCCESS;
        } else {
          status = EM_ZCL_CORE_CBOR_VALUE_READ_VALUE_TOO_LARGE;
        }
        state->finger += length;
        return status;
      } else {
        return EM_ZCL_CORE_CBOR_VALUE_READ_ERROR;
      }
    } else {
      return EM_ZCL_CORE_CBOR_VALUE_READ_WRONG_TYPE;
    }

  case EMBER_ZCLIP_TYPE_UINT8_LENGTH_STRING:
    if ((b0 & CBOR_TYPE_MASK) == CBOR_TEXT) {
      EmZclCoreCborValueReadStatus_t status;
      uint32_t length = 0;
      if (readCborHeaderLength(state, b0, &length)) {
        if (length <= EMBER_ZCL_STRING_LENGTH_MAX) {
          uint8_t *bytes = state->finger - 1;
          bytes[0] = length;
          *((uint8_t **) ((void *) valueLocation)) = bytes;
          status = EM_ZCL_CORE_CBOR_VALUE_READ_SUCCESS;
        } else {
          status = EM_ZCL_CORE_CBOR_VALUE_READ_VALUE_TOO_LARGE;
        }
        state->finger += length;
        return status;
      } else {
        return EM_ZCL_CORE_CBOR_VALUE_READ_ERROR;
      }
    } else {
      return EM_ZCL_CORE_CBOR_VALUE_READ_WRONG_TYPE;
    }

  case EMBER_ZCLIP_TYPE_UINT16_LENGTH_STRING:
    // TODO: Handle long ZigBee strings.
    return EM_ZCL_CORE_CBOR_VALUE_READ_NOT_SUPPORTED;

  default:
    return EM_ZCL_CORE_CBOR_VALUE_READ_ERROR;
  }
}

static bool cborDecodeStruct(CborState *state,
                             const ZclipStructSpec *structSpec,
                             void *theStruct)
{
  if (state->finger == NULL || state->end <= state->finger) {
    return false;
  }

  ZclipStructData structData;
  if (!emExpandZclipStructData(structSpec, &structData)) {
    return false;
  }

  uint8_t b0 = *state->finger++;

  if ((b0 & CBOR_TYPE_MASK) != CBOR_MAP) {
    return false;
  }

  uint32_t fieldCount = 0;
  if (!readCborHeaderLength(state, b0, &fieldCount)) {
    return false;
  }

  for (uint16_t i = 0; i < fieldCount || fieldCount == ((uint32_t) -1); i++) {
    uint8_t type = emCborDecodePeek(state, NULL);
    if (type == CBOR_BREAK) {
      return (fieldCount == ((uint32_t) -1));
    }

    uint16_t keyIndex = 0;
    uint8_t keyName[8];
    if (type == CBOR_UNSIGNED) {
      emCborDecodeValue(state,
                        EMBER_ZCLIP_TYPE_UNSIGNED_INTEGER,
                        sizeof(keyIndex),
                        (uint8_t *)&keyIndex);
    } else if (type == CBOR_TEXT) {
      emCborDecodeValue(state,
                        EMBER_ZCLIP_TYPE_MAX_LENGTH_STRING,
                        sizeof(keyName),
                        keyName);
    } else {
      // Skip the key and value.
      emCborDecodeSkipValue(state);
      emCborDecodeSkipValue(state);
      continue;
    }

    emResetZclipFieldData(&structData);
    uint16_t j;
    ZclipFieldData fieldData;
    for (j = 0; j < structData.fieldCount; j++) {
      emGetNextZclipFieldData(&structData, &fieldData);
      if ((type == CBOR_UNSIGNED && fieldData.name == NULL && keyIndex == j)
          || (type == CBOR_TEXT
              && fieldData.name != NULL
              && strcmp((const char *)keyName, fieldData.name) == 0)) {
        break;
      }
    }
    if (j == structData.fieldCount) {
      emCborDecodeSkipValue(state);
    } else {
      uint8_t *valueLoc = (uint8_t *) theStruct + fieldData.valueOffset;
      if (readCborValue(state, &fieldData, valueLoc)
          != EM_ZCL_CORE_CBOR_VALUE_READ_SUCCESS){
        return false;
      }
    }
  }
  
  return true;
}

bool emCborDecodeStruct(CborState *state, 
                        const ZclipStructSpec *structSpec,
                        void *theStruct)
{
  return cborDecodeStruct(state, structSpec, theStruct);
}

bool emCborDecodeOneStruct(const uint8_t *input,
                           uint16_t inputSize,
                           const ZclipStructSpec *structSpec,
                           void *theStruct)
{
  CborState state;
  emCborDecodeStart(&state, input, inputSize);
  return cborDecodeStruct(&state, structSpec, theStruct);
}

// Decoding arrays and maps

// Decrement the number of array or map entries.  If we know the number of
// elements we decrement the count.  If it reaches zero we pop off the 
// innermost count.  If there are an indefinite number of elements we 
// peek to see if the next thing is a break, in which case we pop off the
// innermost count.

static bool decrementCount(CborState *state)
{
  if (state->finger == NULL 
      || state->end <= state->finger){
    return false;
  }

  if (state->countDepth != 0) {
    uint32_t count = state->countStack[state->countDepth - 1];
    // fprintf(stderr, "[pop depth %d count %d]\n", 
    //         state->countDepth,
    //         count);
    if (count == (uint32_t) -1) {          // ends with a break
      if (*state->finger == CBOR_BREAK) {
        state->countDepth -= 1;
        state->finger += 1;
        // fprintf(stderr, "[break]\n");
        return false;
      }
    } else if (count == 0) {
      state->countDepth -= 1;
      return false;
    } else {
      state->countStack[state->countDepth - 1] -= 1;
    }
  }
  return true;
}

bool emCborDecodeSequence(CborState *state, uint8_t valueType)
{
  if (state->finger == NULL 
      || state->end <= state->finger
      || !decrementCount(state)) {
    return false;
  }

  if (state->countDepth == MAX_DECODE_NESTING) {
    return false;
  }

  uint8_t b0 = *state->finger++;

  if ((b0 & CBOR_TYPE_MASK) != valueType) {
    return false;
  }

  uint32_t count = 0;
  bool status = readCborHeaderLength(state, b0, &count);
  if (!status){
    return false;
  }

  state->countStack[state->countDepth] = 
    ((count != (uint32_t) -1
      && valueType == CBOR_MAP)
     ? count * 2        // maps have two values (key+value) for each item
     : count);
  state->countDepth += 1;
  return true;
}

uint16_t emCborDecodeKey(CborState *state)
{
  uint16_t key;
  if (emCborDecodeValue(state, 
                        EMBER_ZCLIP_TYPE_UNSIGNED_INTEGER,
                        2,
                        (uint8_t *) &key)) {
    return key;
  } else {
    return -1;
  }
}

bool emCborDecodeValue(CborState *state,
                       uint8_t valueType,
                       uint16_t valueSize,
                       uint8_t *valueLoc)
{
  if (! decrementCount(state)) {
    return false;
  } else {
    ZclipFieldData fieldData;
    fieldData.valueType = valueType;
    fieldData.valueSize = valueSize;
    
    if (readCborValue(state, &fieldData, valueLoc) == EM_ZCL_CORE_CBOR_VALUE_READ_SUCCESS){
      return true;
    } else {
      return false;
    }
  }
}

// Returns the type of the next value.

uint8_t emCborDecodePeek(CborState *state, uint32_t *length)
{
  // TODO: There might be a better value to return here / at least we are not
  // crashing.
  if (state->finger == NULL
      || state->end <= state->finger) {
    return CBOR_BREAK;
  }

  uint8_t b0 = *state->finger;

  if (state->countDepth != 0) {
    uint32_t count = state->countStack[state->countDepth - 1];
    if ((count == (uint32_t) -1
         && b0 == CBOR_BREAK)
        || count == 0) {
      return CBOR_BREAK;
    }
  }

  if (length != NULL && !peekOrReadCborHeaderLength(state, b0, length, false)) {
    return CBOR_BREAK;
  }

  if ((b0 & CBOR_TYPE_MASK) == CBOR_MISC) {
    return b0;
  } else {
    return b0 & CBOR_TYPE_MASK;
  }
}

bool emCborDecodeSkipValue(CborState *state)
{
  if (state->finger == NULL
      || state->end <= state->finger
      || !decrementCount(state)) {
    return false;
  }

  uint32_t needed = 1;
  uint32_t saved[16];
  uint8_t depth = 0;

  while (true) {
    uint8_t b0 = *state->finger++;
    if (needed != (uint32_t) -1) {
      needed -= 1;
    }
    if ((b0 & CBOR_TYPE_MASK) == CBOR_MISC) {
      switch (b0) {
      case CBOR_EXTENDED: state->finger += 1; break;
      case CBOR_FLOAT16:  state->finger += 2; break;
      case CBOR_FLOAT32:  state->finger += 4; break;
      case CBOR_FLOAT64:  state->finger += 8; break;
      case CBOR_BREAK:
        if (needed == (uint32_t) -1) {
          depth -= 1;
          needed = saved[depth];
        }
        break;
      }
    } else {
      uint32_t length = 0;
      bool status = readCborHeaderLength(state, b0, &length);

      if (!status){
        return false;
      }

      switch (b0 & CBOR_TYPE_MASK) {
      case CBOR_UNSIGNED:
      case CBOR_NEGATIVE:
        // nothing to do
        break;
      case CBOR_BYTES:
      case CBOR_TEXT:
        state->finger += length;
        break;
      case CBOR_MAP:
        if (length != (uint32_t) -1) {
          length <<= 1;
        }
        // fall through
      case CBOR_ARRAY:
        if (depth >= 16){
          return false;
        }
        saved[depth] = needed;
        depth += 1;
        needed = length;
        break;
      case CBOR_TAG:
        if (needed != (uint32_t) -1) {
          needed += 1;
        }
        break;
      }
    }  
    if (needed == 0) {
      return true;
    }
  }
}
