/*
 * File: thread-key-stretch.c
 * Description: Key stretching for the Thread commissioning key (PSKc).
 *
 * Copyright 2015 Silicon Laboratories, Inc.                                *80*
 */

// For strnlen(3) in glibc.
#define _GNU_SOURCE
 
#include "core/ember-stack.h"
#include "platform/micro/aes.h"
#include "stack/ip/tls/dtls-join.h"

// A commissioner authenticates itself to a border router by establishing
// a J-PAKE/DTLS connection using a key called the PSKc.  The PSKc is
// derived from a user passphrase using the PBKDF2 algorithm combined
// with the AES-CMAC-PRF-128 keyed hash function.  The different parts of
// this are defined in various RFCs: 
//   PBKDF2:           RFC 2898
//   AES-CMAC-PRF-128: RFC 4615
//   AES-CMAC:         RFC 4493

// Byte Ordering
// 
// All of the algorithms use big-endian byte ordering on strings of
// bytes.  In order to avoid complications, especially when using test
// vectors, the bytes are stored with the high order byte at index
// zero: 'x[0]' is the high-order byte of byte sequence 'x'.  That way,
// when a test value "2b7e1516 28aed2a6 abf71588 09cf4f3c" it can be
// stored as "{ 0x2B, 0x7E, 0x15, 0x16, 0x28, ... }".

// Variables names are taken from the algorithm descriptions in the RFCs.
// Don't blame the messenger.

//----------------------------------------------------------------
// This uses AES128 to generate two new keys from one old one.

static void aesCmacKeyGen(const uint8_t *k, uint8_t *k1, uint8_t *k2)
{
  uint8_t l[16];
  MEMSET(l, 0, 16);
  int i;
  const uint8_t *from = l;
  uint8_t *to = k1;

  emLoadKeyIntoCore(k);
  emStandAloneEncryptBlock(l);

  for (i = 0; i < 2; i++) {
    int j;
    uint8_t x = (from[0] << 1) & 0xFE;
    for (j = 1; j < 16; j++) {
      to[j - 1] = x | (from[j] >> 7);
      x = (from[j] << 1) & 0xFE;
    }
    to[15] = ((from[0] & 0x80) == 0
              ? x
              : x ^ 0x87);
    from = k1;
    to = k2;
  }
}

#ifdef EMBER_TEST
// Test vectors from RFC 4493

static const uint8_t aesCmacTestKey[] = {
  0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
  0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C
};

static const uint8_t aesCmacTestKey1[] = {
  0xFB, 0xEE, 0xD6, 0x18, 0x35, 0x71, 0x33, 0x66,
  0x7C, 0x85, 0xE0, 0x8F, 0x72, 0x36, 0xA8, 0xDE 
};

static const uint8_t aesCmacTestKey2[] = {
  0xF7, 0xDD, 0xAC, 0x30, 0x6A, 0xE2, 0x66, 0xCC,
  0xF9, 0x0B, 0xC1, 0x1E, 0xE4, 0x6D, 0x51, 0x3B
};

static void aesCmacKeyGenTest(void)
{
  uint8_t k1[16];
  uint8_t k2[16];
  aesCmacKeyGen(aesCmacTestKey, k1, k2);
  assert(MEMCOMPARE(k1, aesCmacTestKey1, 16) == 0);
  assert(MEMCOMPARE(k2, aesCmacTestKey2, 16) == 0);
}
#endif

// Utility to xor one 16-byte array with another.
static void xor16(uint8_t *x, const uint8_t *y)
{
  uint8_t i;
  for (i = 0; i < 16; i++) {
    x[i] ^= y[i];
  }
}

// 'k' is a 16-byte key that is used with AES128 to hash the 'len'
// bytes of 'm' into 't'.

static void aesCmac(const uint8_t *k,
                    const uint8_t *m,
                    int16u len,
                    uint8_t *t)
{
  uint8_t k1[16];
  uint8_t k2[16];
  uint8_t mLast[16];
  int i;
  aesCmacKeyGen(k, k1, k2);
  uint16_t n = (len + 15) >> 4;

  bool flag;
  if (n == 0) {
    n = 1;
    flag = false;
  } else {
    flag = (len & 0x0F) == 0;
  }

  if (flag) {
    MEMCOPY(mLast, m + (len - 16), 16);
    xor16(mLast, k1);
  } else {
    MEMSET(mLast, 0, 16);
    MEMCOPY(mLast, m + ((n - 1) << 4), len & 0x0F);
    mLast[len & 0x0F] = 0x80;
    xor16(mLast, k2);
  }

  MEMSET(t, 0, 16);
  for (i = 0; i < n - 1; i++) {
    xor16(t, m + (i << 4));
    emStandAloneEncryptBlock(t);
  }

  xor16(t, mLast);
  emStandAloneEncryptBlock(t);
}

#ifdef EMBER_TEST
// Test vectors from RFC 4493

static const uint8_t aesCmacTestMessage[] = {
  0x6B, 0xC1, 0xBE, 0xE2, 0x2E, 0x40, 0x9F, 0x96,
  0xE9, 0x3D, 0x7E, 0x11, 0x73, 0x93, 0x17, 0x2A,
  0xAE, 0x2D, 0x8A, 0x57, 0x1E, 0x03, 0xAC, 0x9C,
  0x9E, 0xB7, 0x6F, 0xAC, 0x45, 0xAF, 0x8E, 0x51,
  0x30, 0xC8, 0x1C, 0x46, 0xA3, 0x5C, 0xE4, 0x11,
  0xE5, 0xFB, 0xC1, 0x19, 0x1A, 0x0A, 0x52, 0xEF,
  0xF6, 0x9F, 0x24, 0x45, 0xDF, 0x4F, 0x9B, 0x17,
  0xAD, 0x2B, 0x41, 0x7B, 0xE6, 0x6C, 0x37, 0x10
};

static const uint8_t aesCmacTestResult0[] = {
  0xBB, 0x1D, 0x69, 0x29, 0xE9, 0x59, 0x37, 0x28, 
  0x7F, 0xA3, 0x7D, 0x12, 0x9B, 0x75, 0x67, 0x46, 
};

static const uint8_t aesCmacTestResult16[] = {
  0x07, 0x0A, 0x16, 0xB4, 0x6B, 0x4D, 0x41, 0x44, 
  0xF7, 0x9B, 0xDD, 0x9D, 0xD0, 0x4A, 0x28, 0x7C, 
};

static const uint8_t aesCmacTestResult40[] = {
  0xDF, 0xA6, 0x67, 0x47, 0xDE, 0x9A, 0xE6, 0x30, 
  0x30, 0xCA, 0x32, 0x61, 0x14, 0x97, 0xC8, 0x27, 
};

static const uint8_t aesCmacTestResult64[] = {
  0x51, 0xF0, 0xBE, 0xBF, 0x7E, 0x3B, 0x9D, 0x92, 
  0xFC, 0x49, 0x74, 0x17, 0x79, 0x36, 0x3C, 0xFE, 
};

static void aesCmacTest(void)
{
  uint8_t t[16];
  aesCmac(aesCmacTestKey, aesCmacTestMessage, 0, t);
  assert(MEMCOMPARE(t, aesCmacTestResult0, 16) == 0);
  aesCmac(aesCmacTestKey, aesCmacTestMessage, 16, t);
  assert(MEMCOMPARE(t, aesCmacTestResult16, 16) == 0);
  aesCmac(aesCmacTestKey, aesCmacTestMessage, 40, t);
  assert(MEMCOMPARE(t, aesCmacTestResult40, 16) == 0);
  aesCmac(aesCmacTestKey, aesCmacTestMessage, 64, t);
  assert(MEMCOMPARE(t, aesCmacTestResult64, 16) == 0);
}
#endif

#ifdef EMBER_TEST

// Use AES CMAC to hash the message, first using it to hash the key if the key
// is too long or two short.  This is only used during testing.  It is 
// in-lined in pbkdf2() to avoid recomputing the key on every iteration.
// The tests for it did reveal a bug in aesCmacKeyGen(), so it's worth
// keeping around.

// This is just used for testing.  For actual use this has been integrated
// into pbkdf2 to avoid hashing the key a zillion times.

static void aesCmacPrf128(const uint8_t *varKey,
                          uint16_t varKeyLen,
                          const uint8_t *message,
                          uint16_t messageLen,
                          uint8_t *result)
{
  uint8_t key[16];
  if (varKeyLen == 16) {
    MEMCOPY(key, varKey, 16);
  } else {
    uint8_t zero[16];
    MEMSET(zero, 0, 16);
    aesCmac(zero, varKey, varKeyLen, key);
  }
  aesCmac(key, message, messageLen, result);
}

static const uint8_t aesCmacPrf128TestKey[] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
  0xED, 0xCB
};

static const uint8_t aesCmacPrf128TestMessage[] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
  0x10, 0x11, 0x12, 0x13
};

static const uint8_t aesCmacPRF128TestResult18[] = {
  0x84, 0xA3, 0x48, 0xA4, 0xA4, 0x5D, 0x23, 0x5B,
  0xAB, 0xFF, 0xFC, 0x0D, 0x2B, 0x4D, 0xA0, 0x9A 
};

static const uint8_t aesCmacPRF128TestResult16[] = {
  0x98, 0x0A, 0xE8, 0x7B, 0x5F, 0x4C, 0x9C, 0x52,
  0x14, 0xF5, 0xB6, 0xA8, 0x45, 0x5E, 0x4C, 0x2D
};

static const uint8_t aesCmacPRF128TestResult10[] = {
  0x29, 0x0D, 0x9E, 0x11, 0x2E, 0xDB, 0x09, 0xEE,
  0x14, 0x1F, 0xCF, 0x64, 0xC0, 0xB7, 0x2F, 0x3D
};
  
static void aesCmacPrf128Test(void)
{
  uint8_t t[16];
  aesCmacPrf128(aesCmacPrf128TestKey, 18, aesCmacPrf128TestMessage, 20, t);
  assert(MEMCOMPARE(t, aesCmacPRF128TestResult18, 16) == 0);
  aesCmacPrf128(aesCmacPrf128TestKey, 16, aesCmacPrf128TestMessage, 20, t);
  assert(MEMCOMPARE(t, aesCmacPRF128TestResult16, 16) == 0);
  aesCmacPrf128(aesCmacPrf128TestKey, 10, aesCmacPrf128TestMessage, 20, t);
  assert(MEMCOMPARE(t, aesCmacPRF128TestResult10, 16) == 0);
}
#endif

// Iteratively hash a password and a salt together to get a key.  The
// idea is to make the iteration count high enough to discourage
// people from brute force attempts to derive the password from the
// key.
//
// Thread uses this for the user's commissioning password.  Thread
// devices store the resulting key, called the PSKc.  A commissioning
// device gets the password from the user, uses PBKDF2 to get the key,
// and then uses the key as a J-PAKE passphrase to connect to a border
// router.  The rationale for the PBKDF2 hashing is that it protects
// users who use the same password in multiple places.  Extracting the
// PSKc from a Thread device does not get you in to the user's bank
// or facebook account.

// aesCmacPrf128() is inlined so that we don't have to repeat the key
// derivation thousands of times.

static void pbkdf2(const uint8_t *password,
                   int16_t passwordLen,
                   uint8_t *salt,       // must have at least four extra bytes
                   int16_t saltLen,
                   uint16_t count,
                   uint8_t *result)
{
  // dump("password", password, passwordLen);

  uint8_t key[16];
  if (passwordLen == 16) {
    MEMCOPY(key, password, 16);
  } else {
    uint8_t zero[16];
    MEMSET(zero, 0, 16);
    aesCmac(zero, password, passwordLen, key);
  }

  // dump("key", key, 16);

  salt[saltLen] = 0;
  salt[saltLen + 1] = 0;
  salt[saltLen + 2] = 0;
  salt[saltLen + 3] = 1;

  // dump("salt", salt, saltLen + 4);

  aesCmac(key, salt, saltLen + 4, result);

  // dump("u0", result, 16);

  uint8_t lastU[16];
  uint16_t i;
  MEMCOPY(lastU, result, 16);
  for (i = 1; i < count; i++) {
    uint8_t nextU[16];
    aesCmac(key, lastU, 16, nextU);
    xor16(result, nextU);
    MEMCOPY(lastU, nextU, 16);
  }
}

#ifdef EMBER_TEST

// The test from the Thread spec.

// Commissioner Credential: "12SECRETPASSWORD34"
static const uint8_t pbkdf2Password[] = {
  0x31, 0x32, 0x53, 0x45, 0x43, 0x52, 0x45, 0x54, 
  0x50, 0x41, 0x53, 0x53, 0x57, 0x4F, 0x52, 0x44, 
  0x33, 0x34
};

// Network Name: "Test Network\0"
static const uint8_t pbkdf2NetworkName[] = {
 0x54, 0x65, 0x73, 0x74, 0x20, 0x4E, 0x65, 0x74,
 0x77, 0x6F, 0x72, 0x6B, 0x00
};

// Extended PAN ID
static const uint8_t pbkdf2ExtendedPanId[] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
};

// This result is corrected from the one in the original Thread spec.
static const uint8_t pbkdf2Result[] = {
  0xC3, 0xF5, 0x93, 0x68, 0x44, 0x5A, 0x1B, 0x61,
  0x06, 0xBE, 0x42, 0x0A, 0x70, 0x6D, 0x4C, 0xC9
};

#endif // EMBER_TEST

// salt = ”Thread” || <Extended PAN ID> || <Network Name>

void emDerivePskc(const uint8_t *passphrase,
                  int16_t passphraseLen,
                  const uint8_t *extendedPanId,
                  const uint8_t *networkName,
                  uint8_t *result)
{
  uint8_t salt[6 + 8 + 16 + 4];         // 4 is for uint32_t added by pbkdf2
  uint16_t nameLength = strnlen(networkName, 16);
  MEMCOPY(salt, "Thread", 6);
  MEMCOPY(salt + 6, extendedPanId, 8);
  MEMCOPY(salt + 14, networkName, nameLength);
  pbkdf2(passphrase,
         passphraseLen,
         salt,
         nameLength + 14,
         16384,         // iteration count
         result);
}

// int main(void)
// {
//   uint8_t result[16];
//   emDerivePskc(pbkdf2Password,
//                sizeof(pbkdf2Password),
//                pbkdf2ExtendedPanId,
//                pbkdf2NetworkName,
//                result);
//   assert(MEMCOMPARE(result, pbkdf2Result, 16) == 0);
//   return 0;
// }
