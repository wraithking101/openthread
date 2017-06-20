/*
 * File: dhcp.h
 * Description: DHCPv6
 *
 * Copyright 2013 Silicon Laboratories, Inc.                                *80*
 */

// Incoming messages.  Returns true if the message was process successfully.
bool emDhcpIncomingMessageHandler(Ipv6Header *ipHeader);

void emNoteGlobalPrefix(const uint8_t *prefix,
                        uint8_t prefixLengthInBits,
                        uint8_t prefixFlags,
                        uint8_t domainId);
void emSendDhcpSolicit(GlobalAddressEntry *entry, EmberNodeId serverNodeId);
bool emSendDhcpAddressRelease(uint16_t shortId, const uint8_t *longId);
bool emSendMyNetworkData(EmberNodeId leader, Buffer myNetworkData);
bool emSendDhcpLeaseQuery(const uint8_t *target);
void emSetGatewayDhcpAddressTimeout(GlobalAddressEntry *entry);

//----------------------------------------------------------------

#define DHCP_CLIENT_PORT 546
#define DHCP_SERVER_PORT 547

//----------------------------------------------------------------
// These are really internal to DHCP but are needed by test code.

// DHCP messages.
#define DHCP_SOLICIT               1
#define DHCP_ADVERTISE             2
#define DHCP_REQUEST               3
#define DHCP_CONFIRM               4
#define DHCP_RENEW                 5
#define DHCP_REBIND                6
#define DHCP_REPLY                 7
#define DHCP_RELEASE               8
#define DHCP_DECLINE               9
#define DHCP_RECONFIGURE          10
#define DHCP_INFORMATION_REQUEST  11
#define DHCP_RELAY_FORW           12
#define DHCP_RELAY_REPL           13
#define DHCP_LEASEQUERY           14
#define DHCP_LEASEQUERY_REPLY     15

#define DHCP_MAX_COMMAND DHCP_LEASEQUERY_REPLY

// DHCP options.
// The names are from RFC 3315, so don't blame me for them.
#define DHCP_CLIENTID_OPTION       1
#define DHCP_SERVERID_OPTION       2
#define DHCP_IA_NA_OPTION          3    // IA_NA = Identity Association
                                        //    for Nontemporary Addresses
#define DHCP_IA_TA_OPTION          4    // Ditto, without the Non-
#define DHCP_IAADDR_OPTION         5
#define DHCP_ORO_OPTION            6    // ORO = Option Request Option
#define DHCP_PREFERENCE_OPTION     7
#define DHCP_ELAPSED_TIME_OPTION   8
#define DHCP_RELAY_MSG_OPTION      9
// There is no 10.
#define DHCP_AUTH_OPTION          11
#define DHCP_UNICAST_OPTION       12
#define DHCP_STATUS_CODE_OPTION   13
#define DHCP_RAPID_COMMIT_OPTION  14
#define DHCP_USER_CLASS_OPTION    15
#define DHCP_VENDOR_CLASS_OPTION  16
#define DHCP_VENDOR_OPTS_OPTION   17
#define DHCP_INTERFACE_ID_OPTION  18
#define DHCP_RECONF_MSG_OPTION    19
#define DHCP_RECONF_ACCEPT_OPTION 20

// From RFC 5007 DHCPv6 Leasequery
#define DHCP_LQ_QUERY_OPTION         44
#define DHCP_CLIENT_DATA_OPTION      45
#define DHCP_CLIENT_LAST_TIME_OPTION 46
#define DHCP_LQ_RELAY_DATA_OPTION    47
#define DHCP_LQ_CLIENT_LINK_OPTION   48

#define DHCP_MAX_OPTION_CODE DHCP_LQ_CLIENT_LINK_OPTION

// Our own options nested in VENDOR_OPTS.
#define DHCP_ASSIGNED_ID_MASK_OPTION    0
#define DHCP_ML_EID_OPTION              1
#define DHCP_ATTACH_TIME_OPTION         2

// Bitmask indexes for the options we actually use.  This can be an
// enum because the values are only used internally.
//
// This must be synchronized with the optionIds[] array in dhcp.c.

enum {
 DHCP_CLIENTID_OPTION_NUMBER,
 DHCP_SERVERID_OPTION_NUMBER,
 DHCP_IA_NA_OPTION_NUMBER,
 DHCP_IAADDR_OPTION_NUMBER,
 DHCP_ORO_OPTION_NUMBER,
 DHCP_ELAPSED_TIME_OPTION_NUMBER,
 DHCP_STATUS_CODE_OPTION_NUMBER,
 DHCP_RAPID_COMMIT_OPTION_NUMBER,
 DHCP_VENDOR_OPTS_OPTION_NUMBER,
 DHCP_LQ_QUERY_OPTION_NUMBER,
 DHCP_CLIENT_DATA_OPTION_NUMBER,
 DHCP_CLIENT_LAST_TIME_OPTION_NUMBER,

 // These indicate the presence of options nested inside other options.
 DHCP_TARGETID_OPTION_NUMBER,      // alias for CLIENTID inside CLIENT_DATA
 DHCP_IA_NA_STATUS_OPTION_NUMBER,  // alias for STATUS_CODE inside IA_NA
 DHCP_IAADDR_STATUS_OPTION_NUMBER, // alias for STATUS_CODE inside IAADDR

 // Our own options nested inside VENDOR_OPTS.
 DHCP_ASSIGNED_ID_MASK_OPTION_NUMBER,
 DHCP_ML_EID_OPTION_NUMBER,
 DHCP_ATTACH_TIME_OPTION_NUMBER
};

// We don't count the internal pseudo options.
#define DHCP_OPTION_NUMBERS_COUNT (DHCP_CLIENT_LAST_TIME_OPTION_NUMBER + 1)

// But do count them for indexes.
#define DHCP_OPTION_INDEX_COUNT (DHCP_ASSIGNED_ID_MASK_OPTION_NUMBER + 1)

#define DHCP_STATUS_SUCCESS         0 // Success.
#define DHCP_STATUS_UNSPEC_FAIL     1 // Failure, reason unspecified;
#define DHCP_STATUS_NO_ADDRS_AVAIL  2 // Server has no addresses available.
#define DHCP_STATUS_NO_BINDING      3 // Client record (binding) unavailable.
#define DHCP_STATUS_NOT_ON_LINK     4 // The prefix is not appropriate.
#define DHCP_STATUS_USE_MULTICAST   5 // Force client to use multicast.

// From RFC 5007 DHCPv6 Leasequery
#define DHCP_UNKNOWN_QUERY_TYPE     7 // Query-type is unknown or not supported.
#define DHCP_MALFORMED_QUERY        8 // Query is not valid.
#define DHCP_NOT_CONFIGURED         9 // Server doesn't have the target address.
#define DHCP_NOT_ALLOWED           10 // Server does not allow the request.

// Two bytes of ID and two bytes of length.
#define DHCP_OPTION_HEADER_LENGTH 4

// DHCP Unique Identifier (DUID)

#define DUID_TYPE_LINK_LAYER_TIME 1  // link-layer address plus timeout
#define DUID_TYPE_ENTERPRISE      2  // enteprise number plus vendor-assigned ID
#define DUID_TYPE_LINK_LAYER      3  // static link-layer address

// See Hardware Types in
// http://www.iana.org/assignments/arp-parameters/arp-parameters.xhtml
#define DUID_HARDWARE_TYPE_EUI64 27

// This is used to mark vendor-specific options.
// From http://www.iana.org/assignments/enterprise-numbers/enterprise-numbers .
// This was Ember's and is now Silicon Labs'.  ZigBee has 37244, should we get
// that far.
#define SILABS_ENTERPRISE_NUMBER 39873

// The therad group enterprise number
#define THREAD_GROUP_ENTERPRISE_NUMBER 44970

// From RFC 5007 DHCPv6 Leasequery
// Query types
#define DHCP_QUERY_BY_ADDRESS  1
#define DHCP_QUERY_BY_CLIENTID 2

//----------------------------------------------------------------

typedef struct {
  uint16_t command;
  uint32_t transactionId;
  uint32_t optionMask;            // which options are present

  uint8_t *optionsStart;          // beginning of options
  uint8_t *optionsEnd;            // pointer to first byte after options

  // Option data
  // There are two client IDs in LQ Responses.
  uint8_t clientLongId[8];        // client ID option
  uint8_t serverLongId[8];        // server ID option
  uint8_t targetLongId[8];        // client ID option inside LQ query and
                                //  client data options

  uint16_t status;                // status option

  uint32_t iaid;                  // IA_NA option
  uint32_t t1;                    // IA_NA option
  uint32_t t2;                    // IA_NA option
  uint16_t iaNaStatus;            // status option inside IA_NA option

  uint8_t *address;               // IAADDR option
  uint32_t preferredLifetime;     // IAADDR option
  uint32_t validLifetime;         // IAADDR option
  uint16_t iaaddrStatus;          // status option inside IAADDR option

  uint8_t *clientDataStart;       // client data option
  uint8_t *clientDataEnd;

  uint8_t assignedIdSequence;     // assigned ID mask option
  uint8_t *assignedIdMask;        // assigned ID mask option

  uint8_t queryType;              // LQ query option

  uint32_t lastTransactionSeconds;// client last transaction time
                                // (DHCP_CLIENT_LAST_TIME_OPTION)
  uint32_t attachTimeSeconds;     // how long client has been attached
} DhcpMessage;

void emAcceptDhcpLeaseQuery(bool yesno);
void emAcceptDhcpSolicit(bool yesno);

#define LQ_QUERY_OPTION_SIZE 21

bool emParseDhcpOptions(DhcpMessage *message,
                        uint8_t parseType,
                        uint8_t *end,
                        uint32_t allowedOptions,
                        uint32_t requiredOptions);
typedef struct {
  Event event;
  PacketHeader message;
  uint32_t transactionId;
  uint8_t command;
  uint8_t retryCount;
} DhcpRetryEvent;

void emDhcpRequestFailed(DhcpRetryEvent *event);

DhcpRetryEvent *emDhcpFindEvent(uint32_t *transactionId);
void emLogDhcp(bool incoming, const uint8_t *ipAddr, uint8_t command);

uint8_t *emAddDhcpHeader(uint8_t *finger,
                         uint8_t command,
                         DhcpMessage *request,
                         uint32_t transactionId);

bool emSendDhcpMessage(const uint8_t *ipDest,
                       uint8_t *message,
                       uint16_t messageLength,
                       Buffer moreMessage,
                       uint16_t sourcePort,
                       uint16_t destinationPort,
                       uint32_t transactionId);

bool emParseDhcpMessage(Ipv6Header *ipHeader,
                        DhcpMessage *message,
                        bool processIt);

void emProcessDhcpLeaseQuery(DhcpMessage *message, uint8_t *ipSource);
void emProcessDhcpSolicit(DhcpMessage *message, uint8_t *ipSource);
void emProcessDhcpRelease(DhcpMessage *message, uint8_t *ipSource);

extern bool emForceRejectDhcp;
