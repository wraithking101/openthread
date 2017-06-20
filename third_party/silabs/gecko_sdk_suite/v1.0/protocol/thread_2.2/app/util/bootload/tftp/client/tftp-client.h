// File: tftp-client.h
//
// Description: TFTP Client Functionality
//
// Copyright 2013 by Silicon Laboratories. All rights reserved.             *80*

typedef enum {
  NO_CLIENT_STATE = 0,
  SEND_WRITE_REQUEST,
  SEND_FILE
} TftpClientState;

typedef enum {
  TFTP_CLIENT_START_FILE_TRANSMISSION = 0,
  TFTP_CLIENT_FILE_TRANSMISSION_SUCCESS,
  TFTP_CLIENT_FILE_TRANSMISSION_TIMEOUT
} TftpClientStatus;

/*
 * @brief Status handler that is called upon change in TFTP state
 */
void emberTftpClientStatusHandler(TftpClientStatus status);

/*
 * @brief Send a TFTP write request to the TFTP server
 */
bool emSendTftpWriteRequest(void);

/*
 * @brief Process an incoming TFTP packet
 */
void emProcessTftpClientPacket(const uint8_t *sourceAddress,
                               uint16_t sourcePort,
                               const uint8_t *packet,
                               uint16_t payloadLength);

/*
 * @brief Initialize the TFTP client
 */
bool emInitializeTftpBootloaderClient(int argc, char **argv);

/*
 * @brief Send a file to the TFTP server
 */
void emTftpSendFile(const uint8_t *fileName,
                    const uint8_t *fileData,
                    uint32_t fileLength);

/*
 * @brief Cleanup temporary file storage
 */
void emTftpCleanupFile(void);

/*
 * @brief The set_server command
 */
void setServerCommand(void);

/*
 * @brief The send_file command
 */
void sendFileCommand(void);

/*
 * @brief Set the TFTP server
 */
bool emTftpSetServer(const uint8_t *serverAddress);

extern const uint8_t *emTftpFileName;
extern const uint8_t *emTftpFile;
extern EmberIpv6Address myIpAddress;

#define TFTP_CLIENT_COMMANDS                    \
  emberCommand("set_server", setServerCommand, "b",   NULL), \
  emberCommand("send_file",  sendFileCommand,  "bv*", NULL),
