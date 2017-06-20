/** @file tftp-bootloader.h
 * @addtogroup tftp-bootloader
 * @brief TftpBootloader is a bootloading protocol.
 *
 * <!-- Culprit(s): Nate Smith -->
 * <!-- Copyright 2014 by Silicon Laboratories. All rights reserved.    *80* -->
 */

/**
 * @addtogroup tftp-bootloader
 *
 * @{
 */

/**
 * The TftpBootloader port
 */
#define TFTP_BOOTLOADER_PORT 68

/** TftpBootloader is a bootloading protocol. It allows a bootload target to verify an
 incoming bootload request from a bootload server. The target can hold the
 server off while it erases its internal storage area for the bootload image.

 A tftp-bootloader packet follows the format:

 @verbatim
 0               1
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 +       + type  +  payload.........
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 @endverbatim

 where type is @a TftpBootloaderPacketType
 */

/** @name Structs and Packet Types
 * @{
 */

/** An enum describing the different types of TftpBootloader packets.
 */
typedef enum {
  KRAKEN_BOOTLOAD_REQUEST = 1,
  KRAKEN_OKAY_TO_SEND     = 2,
  KRAKEN_ERROR            = 3
} TftpBootloaderPacketType;

/** A struct with fields for a bootload request packet
 */
struct TftpBootloaderBootloadRequest_s {
  /**
   * do we request a resume?
   */
  bool resume;

  /**
   * the manufacturer ID
   */
  uint16_t manufacturerId;

  /**
   * the device type
   */
  uint8_t deviceType;

  /**
   * the version number
   */
  uint32_t versionNumber;

  /**
   * the size of the bootload image
   */
  uint32_t size;
};

typedef struct TftpBootloaderBootloadRequest_s TftpBootloaderBootloadRequest;

/** A struct with fields for an Okay To Send packet
 */
typedef struct {
  /**
   * did we pass authentication?
   */
  bool success;

  /**
   * shall the bootload manager delay until the target has finished
   * erasing its FLASH?
   */
  bool holdOffUntilFurtherNotice;

  /**
   @verbatim
   for resume operations
   0 if resumeRequest is false (see above)
   (if no previous bootload was interrupted)
   > 0 if resumeRequest is true (if a previous bootload was interrupted)
   @endverbatim
   */
  uint32_t resumeOffset;
} TftpBootloaderOkayToSend;

/** A struct with fields for an Error packet
 */
typedef struct {
  /**
   @verbatim
   Error flag values

   +-+-+-+-+-+-+-+-+
   +        A B C D+
   +-+-+-+-+-+-+-+-+

   where A: invalid manufacturer ID
         B: invalid device type
         C: invalid version number
         D: invalid packet
   @endverbatim
   */
  uint8_t flags;
} TftpBootloaderError;

/** @}
 */

/** @}
 */

void emProcessTftpBootloaderPacket(const uint8_t *source,
                                   const uint8_t *payload,
                                   uint16_t payloadLength);

void emTftpBootloaderErrorHandler(const uint8_t *source);
bool emSendTftpBootloaderPacket(const uint8_t *bytes, uint16_t length);

// Buffer -> EmberIpv6Address
extern Buffer emTftpBootloaderRemoteAddress;

// populates emTftpBootloaderRemoteAddress
void emSetTftpBootloaderRemoteAddress(const uint8_t *address);

// verify that the given address matches the stored remote address
bool emVerifyTftpBootloaderRemoteAddress(const uint8_t *source);

const EmberIpv6Address *emGetTftpBootloaderRemoteAddress(void);

void emMarkTftpBootloaderBuffers(void);
void emInitializeBaseTftpBootloaderFunctionality(void);

// the server and client each have a version of this
void emInitializeTftpBootloader(void);

/**
 * @addtogroup tftp-bootloader
 *
 * @{
 */

/** @name Application Functions
 * Implement these functions in your application.
 * @{
 */

/**
 * @brief Verify an incoming bootload request and return true if we will accept
 * it, false otherwise.
 */
bool emberVerifyBootloadRequest(const TftpBootloaderBootloadRequest *request);

/** @}
 */

/** @}
 */
