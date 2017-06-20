// File: tftp.h
//
// Description: TFTP Bootloader headers and defines
//
// Copyright 2013 by Silicon Laboratories. All rights reserved.             *80*
//
// Data in this header file is compiled from RFC 1350,
// THE TFTP PROTOCOL (REVISION 2)

// the various types of packets
typedef enum {
  TFTP_READ_REQUEST  = 1,
  TFTP_WRITE_REQUEST = 2,
  TFTP_DATA          = 3,
  TFTP_ACK           = 4,
  TFTP_ERROR         = 5,
  TFTP_OACK          = 6 // option acknowledgement
} TftpPacketType;

#define TFTP_PORT 69
#define TFTP_MAX_BLOCK_SIZE 512

// ---------------------------------------------------------
// TFTP_READ_REQUEST and TFTP_WRITE_REQUEST packet
//
// Filename and Mode are strings without a null-terminator
//
// Opcode = 1 (TFTP_READ_REQUEST) or 2 (TFTP_WRITE_REQUEST)
// Note: only TFTP_WRITE_REQUEST is supported
//
// 2 bytes     string    1 byte     string   1 byte
// --------------------------------------------------
// | Opcode |  Filename  |   0  |    Mode    |   0  |
// --------------------------------------------------

// ---------------------------------------------------------
// TFTP_DATA packet
//
// opcode = 3 (TFTP_DATA)
//
// 2 bytes     2 bytes      n bytes
// ------------------------------------
// | Opcode |   Block #  |   Data     |
// ------------------------------------

// ---------------------------------------------------------
// TFTP_ACK packet
//
// opcode = 4 (TFTP_ACK)
//
// 2 bytes     2 bytes
// -----------------------
// | Opcode |   Block #  |
// -----------------------

// ---------------------------------------------------------
// TFTP_ERROR packet
//
// opcode = 5 (TFTP_ERROR)
//
// 2 bytes     2 bytes      string    1 byte
// -------------------------------------------
// | Opcode |  ErrorCode |   ErrMsg   |   0  |
// -------------------------------------------

#define TFTP_BLOCK_SIZE_STRING "blksize"
#define TFTP_DEFAULT_MODE "octet"

//
// The ACK (for client) and DATA (for server) timeout is 2 seconds
//
#define TFTP_TIMEOUT 2000

void emProcessTftpPacket(const uint8_t *source,
                         uint16_t remotePort,
                         const uint8_t *payload,
                         uint16_t payloadLength);
bool emSendTftpPacket(const uint8_t *payload, uint16_t payloadLength);

#define TFTP_OPCODE_INDEX 1
#define TFTP_FILENAME_INDEX 2

extern uint16_t emTftpRemoteTid;
extern uint16_t emTftpLocalTid;
extern uint16_t emTftpBlockNumber;
extern uint16_t emTftpBlockSize;
extern EmberIpv6Address emTftpRemoteIp;
extern bool emTftpScripting;

void emInitializeTftp(int argc, char **argv);
void emReallyInitializeTftp(int argc, char **argv);

void tftpTick(void);
void emTftpListen(bool randomizeTid);

// generic reset function
void emReallyResetTftp(void);

// implemented for posix or 15-4
void emResetTftp(void);

// a listen occurred
void emTftpListenStatusHandler(uint16_t port, EmberIpv6Address *address);

void emTftpPrintHelp(void);
void emTftpReallyChooseInterface(uint8_t optionalInterfaceNumber,
                                 const uint8_t *optionalPrefix);

// commands
void quitCommand(void);
void chooseInterfaceCommand(void);

#define TFTP_COMMANDS                                                   \
  emberCommand("choose_interface", chooseInterfaceCommand, "u*", NULL), \
  emberCommand("quit",             quitCommand,            "",   NULL), \
  emberCommand("help",             helpCommand,            "",   NULL), \

void emTftpOpenTraceFd(char myTag, char theirTag);

typedef enum {
  TFTP_INTERFACE_CHOSEN
} TftpStatus;

void emTftpStatusHandler(TftpStatus status);
