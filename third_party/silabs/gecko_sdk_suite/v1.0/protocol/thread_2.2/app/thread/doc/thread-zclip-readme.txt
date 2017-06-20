The ZCL/IP additions to the Silicon Labs Thread framework consists of a set of
plugins that provide ZCL over IP functionality that a user may want in an
application.

ZCL/IP plugins:

Basic Server - implements the Basic cluster server
Bulb User Interface - provides a limited user interface for a light bulb
End Node User Interface - implements a pushbutton and LED blinking based user interface for an end node (sleepy or non sleepy)
Groups Server - implements the Groups cluster server
Identify Server - implements the Identify cluster server
LED Dim PWM - performs translations between dimming level values and HAL PWM functions
LED Rgb PWM - performs translations between RGB color values and HAL PWM functions
LED Temp PWM - performs translations between color temperature values and HAL PWM functions
Level Control Server - implements the Level Control cluster server
On/Off Server - implements the On/Off cluster server
Power Configuration Cluster Server - implements the Power Configuration cluster server
ZCL Core - implements the core ZCL/IP functionality


The ZCL/IP plugins provide CLI commands to the application.  If the plugin is
enabled, the CLI commands will be available.

The ZCL Core plugin contributes the following commands to the application:

zcl info
- Prints information about the ZCL network.

zcl attribute print
- Prints the attribute table.

zcl attribute read <endpoint id:1> <role:1> <manufacturer code:2> <cluster id:2> <attribute id:2>
- Reads a local attribute.
- Role should be 0 for client or 1 for server.
- Example: zcl attribute read 1 1 0 0x0006 0x0000
  - Reads the OnOff attribute from the On/Off server cluster on endpoint 1.

zcl attribute write <endpoint id:1> <role:1> <manufacturer code:2> <cluster id:2> <attribute id:2> <data:n>
- Writes a local attribute.
- Role should be 0 for client or 1 for server.
- Data should be in the endianness of the application.
- Example: zcl attribute write 1 1 0 0x0006 0x0000 {01}
  - Sets the OnOff attribute from the On/Off server cluster on endpoint 1 to
    true.

zcl attribute reset <endpoint id:1>
- Resets all attributes on the given endpoint to the default value.
- Example: zcl attribute reset 1
  - Resets the attributes on endpoint 1.

zcl attribute remote read <role:1> <manufacturer code:2> <cluster id:2> <attribute id:2> [<attribute id:2> ...]
- Reads a attribute from a remote node.
- Role should be 0 for client or 1 for server.
- One of the "zcl send" commands can be used to send the message.
- One or more attributes may be passed as the last argument.
- Example: zcl attribute remote read 1 0 0x0006 0x0000 0x4003
  - Reads the OnOff and StartUpOnOff attribute from the On/Off server cluster on endpoint 1.

zcl attribute remote write <role:1> <manufacturer code:2> <cluster id:2> <attribute id:2> <data:n>
- Writes an attribute on a remote node.
- Role should be 0 for client or 1 for server.
- Data should be in the endianness of the application.
- One of the "zcl send" commands can be used to send the message.
- Example: zcl attribute remote write 1 0 0x0006 0x0000 {01}
  - Sets the OnOff attribute from the On/Off server cluster on endpoint 1 to
    true.

zcl binding add <endpoint id:1> <role:1> <manufacturer code:2> <cluster id:2> <secure:1> <destination address> <destination port:2> <destination endpoint id:1> <destination group id:2> <reporting configuration id:1>
- Adds a local binding.
- Role should be 0 for client or 1 for server.
- Secure should be 0 for unsecured or 1 for secured.
- If destination endpoint is specified, destination group should be 0xFFFF.
- If destination group is specified, destination endpoint should be 0xFF.
- Example: zcl binding add 1 1 0 0x0006 0 "fd31:4159:2653:5897:9323:8462:6433:8327" 5683 2 0xFFFF 0
  - Adds a local binding from the On/Off server cluster on endpoint 1 to
    "fd31..." for endpoint 2.

zcl binding set <endpoint id:1> <role:1> <manufacturer code:2> <cluster id:2> <secure:1> <destination address> <destination port:2> <destination endpoint id:1> <destination group id:2> <reporting configuration id:1> <binding id:1>
- Sets a local binding.
- Role should be 0 for client or 1 for server.
- Secure should be 0 for unsecured or 1 for secured.
- If destination endpoint is specified, destination group should be 0xFFFF.
- If destination group is specified, destination endpoint should be 0xFF.
- Example: zcl binding update 1 1 0 0x0006 0 "fd31:4159:2653:5897:9323:8462:6433:8327" 5683 2 0xFFFF 0 3
  - Sets local binding 3 to be from the On/Off server cluster on endpoint 1 to
    "fd31..." for endpoint 2.

zcl binding remove <binding id:1>
- Removes a local binding.
- Example: zcl binding remove 3
  - Removes local binding 3.

zcl binding clear
- Clears the binding table.

zcl binding print
- Prints the binding table.

zcl binding remote add <role:1> <manufacturer code:2> <cluster id:2> <secure:1> <destination address> <destination port:2> <destination endpoint id:1> <destination group id:2> <reporting configuration id:1>
- Adds a binding on a remote node.
- Role should be 0 for client or 1 for server.
- Secure should be 0 for unsecured or 1 for secured.
- If destination endpoint is specified, destination group should be 0xFFFF.
- If destination group is specified, destination endpoint should be 0xFF.
- One of the "zcl send" commands can be used to send the message.
- Example: zcl binding remote add 1 0 0x0006 0 "fd31:4159:2653:5897:9323:8462:6433:8327" 5683 2 0xFFFF 0
  - Adds a remote binding from the On/Off server cluster on endpoint 1 to
    "fd31..." for endpoint 2.

zcl binding remote update <role:1> <manufacturer code:2> <cluster id:2> <secure:1> <destination address> <destination port:2> <destination endpoint id:1> <destination group id:2> <reporting configuration id:1> <binding id:1>
- Updates a binding on a remote node.
- Role should be 0 for client or 1 for server.
- Secure should be 0 for unsecured or 1 for secured.
- If destination endpoint is specified, destination group should be 0xFFFF.
- If destination group is specified, destination endpoint should be 0xFF.
- One of the "zcl send" commands can be used to send the message.
- Example: zcl binding remote add 1 0 0x0006 0 "fd31:4159:2653:5897:9323:8462:6433:8327" 5683 2 0xFFFF 0
  - Updates remote binding 3 to be from the On/Off server cluster on endpoint 1
    to "fd31..." for endpoint 2.

zcl binding remote remove <role:1> <manufacturer code:2> <cluster id:2> <binding id:1>
- Removes a binding on a remote node.
- Role should be 0 for client or 1 for server.
- One of the "zcl send" commands can be used to send the message.
- Example: zcl binding remote remove 1 0 0x0006 3
  - Removes remote binding 3 from the On/Off server cluster.

zcl send binding <binding id:1>
- Sends an attribute, binding, or command request to the given binding.
- Example: zcl send binding 0
  - Sends a message to the remote node stored at binding index 0.

zcl send endpoint <destination type:1> <destination> <endpoint id:1>
- Sends an attribute, binding, or command request to the given destination and
  endpoint.
- Destination type should be 0 for IPv6 address or 1 for UID.
- Example: zcl send endpoint 0 "fd31:4159:2653:5897:9323:8462:6433:8327" 1
  - Sends a message to IPv6 address "fd31..." for endpoint 1.
- Example: zcl send endpoint 1 {00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff} 1
  - Sends a message to UID {00112233...} for endpoint 1.

zcl send group <address> <group id:2>
- Sends an attribute, binding, or command request to the given address and
  group.
- Example: zcl send group "fd31:4159:2653:5897:9323:8462:6433:8327" 0x5A49
  - Sends a message to "fd31..." for group 0x5A49.

zcl ez-mode start
- Starts EZ Mode commissioning.

zcl ez-mode stop
- Stops EZ Mode commissioning.

zcl discovery cluster <role:1> <manufacturer code:2> <cluster id:2>
- Multicast a service discovery message by cluster information.

zcl discovery endpoint <endpoint:1>
- Multicast a service discovery message by endpoint.

zcl discovery device-type <device type:2>
- Multicast a service discovery message by device type.

zcl discovery uid <uid>
- Multicast a service discovery message by UID.
- Example: zcl discovery uid "36f6e452b1cac02a4f6a6b4ef341af*"
  - Sends a discovery request for UIDs starting with the prefix
    36f6e452b1cac02a4f6a6b4ef341af.

zcl discovery version <version:2>
- Multicast a service discovery message by cluster version.

zcl discovery init
- Initialize internal buffer for tracking service discovery query string before
  they are sent out.

zcl discovery send
- Multicast composed service discovery query.

zcl discovery mode <mode:1>
- Changes the mode of how service discovery messages are multicasted.
- 0: single query (default), 1: multiple queries
- For single query, zcl discovery CLI command will trigger a message to be send
  with one specified query field.
- For multiple queries, each discovery CLI command will append 1 query parameter
  to the overall discovery query string.
- The usage pattern can be used below:
- zcl discovery mode 0/1 # changing mode will automatically trigger an init.
- zcl discovery cluster/device-type/etc...
- zcl send

zcl cache clear
- Clears the address cache.

zcl cache add uid-ipv6 <uid> <address>
- Adds a UID/IPv6 address cache entry.
- Uid is hex binary representation of a 32-byte / 256-bit UID.
- Address is an IPv6 address.
- If a cache entry for the UID does not already exist, a new entry is created.
- If a cache entry for the UID already exists, it is reused. The UID and index
  remain unchanged and the specified address replaces the entry's address.
- Example: zcl cache add uid-ipv6 {00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff} "fd31:4159:2653:5897:9323:8462:6433:8327"
  - Adds an entry in the address cache for the specified UID / address pair.
    An index value for the entry is automatically generated.

zcl cache print <format:1>
- Prints the address cache.
- Format should be 0 for abbreviated UID display or 1 for full UID display.
- Abbreviated UID format shows only the first 15 bytes / 120 bits of the UID:
  "00112233445566778899aabbccddee..."
- Example: zcl cache print 0
  - Displays one line per cache entry showing index, abbreviated UID,
    and address.

zcl cache remove <index:2>
- Removes an entry from the address cache.
- Index is the index value displayed for the cache entry by the print command.
- Example: zcl cache remove 0x0023
  - Removes the address cache entry identified by index 0x0023.


ZCL/IP sample applications demonstrate basic ZCL/IP functionality using the
Silicon Labs Thread stack.

ZCL/IP Sample Applications:

Four sample applications are provided to demonstrate simple ZCL/IP applications
using the Silicon Labs Thread stack.  The light application is a router and
acts as a dimmable light bulb.  It responds to On/Off and Level Control
messages from switches that act as sleepy dimmer switches.  The light and
switch both run on an SoC.  They communicate using the CBOR-encoded payloads
sent over the Constrained Application Protocol (CoAP) at the application layer,
with UDP serving as the transport layer.  Additional information is contained
within each application and is viewable in AppBuilder.

The dimmable light application implements a dimmable light bulb, intended to be
run on the Silicon Labs Dimmable Light reference design hardware.  It implements
the On/Off and Level Control servers and uses a single PWM-controlled LED. This
sample app differs from the light sample application in that it is meant to
represent what a dimmable light would look like as an end product, with
additional complexity and features not present in the light sample application.

The capacitive sensing dimmer switch application implements a dimmer switch,
intended to be run on the Silicon Labs dimmer switch reference design hardware.
It implements the On/Off, Level Control, and Color Control client clusters,
sending various commands over those clusters based on binding table entries.
This sample app differs from the switch sample application in that it is meant
to represent what a sleepy switch would look like as an end product, with
additional complexity and features not present in the switch sample.
