The Silicon Labs Thread framework consists of a set of plugins and libraries
that provide functionality that a user may want in an application.  Plugins and
libraries are roughly divided into three categories: HAL, stack, and
application.  Some plugins and libraries are also offered as stubs that satisfy
dependencies but otherwise provide no functionality.

HAL plugins and libraries:

Accelerometer LED - provides wake-up on motion and status LED support
ADC - provides sample API functions for the analog-to-digital converter
Antenna - provides antenna configuration functionality
ASHv3 - provides reliable serial communication for UART NCPs
Battery Monitor - provides a method to measure battery voltage during radio transmit
bulb-pwm-driver - Generic PWM drive routines for LED bulb
Button - provides sample API functions for operating buttons
Button Interface - provides callbacks for different types of button presses (short, long, and press and hold)
Buzzer - provides sample API functions for operating buzzers
Coexistence Configuration - provides an interfaceto configure coexistence GPIO
Diagnostic - provides program counter diagnostic functions
External Device GPIO Driver - drives GPIO on an external device
FEM Control - provides FEM configuration for the EFR32
Gateway MQTT Transport - Implements the generic transport layer between a gateway and a broker using MQTT
Generic GPIO Interrupt Controller - provides an interface into the interrupt controller of the hardware
GPIO Sensor Interface - provides an interface for a generic binary GPIO sensor
Graphics Library - provides API functions for operating the sample TFT display
HAL Library - provides common functionality for EFR32 and EM3xx SoCs
HAL NCP Library - provides common functionality for EFR32 and EM3xx NCPs
I2C Driver - provides an I2C driver for use by the application or other plugins
Infrared LED - drives an infrared LED
Key Matrix - provides API functions for operating key-matrix scanning
LED - provides sample API functions for operating LEDs
Led Blinking - provides simple API functions for blinking an LED with custom patterns
Linked List - provides a linked list utility for EZSP hosts running on linux
Microphone Codec MSADPCM - provides a microphone interface using hardware codecs
Occupancy PYD-1698 - provides APIs and callbacks for dealing with a pyd1698 occupancy sensor
Occupancy PYD-1698 Stub - provides a APIs and callbacks for simulating an occupancy sensor when no hardware is available
Microphone IMAADPCM - provides a microphone interface using software codecs
Multiprotocol Stack Interface (MPSI) - THIS IS NOT YET SUPPORTED FOR THIS STACK.  Provides an interface for sending multi protocol stack messages
MPSI Storage - THIS IS NOT YET SUPPORTED FOR THIS STACK.  Handles reading and writing of multi protocl stack messages to flash storage
NCP SPI Link - provides SPI support for EFR32 and EM3xx NCPs
NCP UART Link - provides UART support for EFR32 and EM3xx NCPs
Paho MQTT - provides Paho MQTT for host applications running on Linux
PS Store - manages the data in the flash memory of the EFR32 devices
RAIL Library - provides the radio abstraction interface layer (RAIL) for stacks
SB1 Gesture Sensor - provides a driver for an EFM8SB1 Capacative Touch Gesture Sensor
Simulated EEPROM - provides a simulated EEPROM for persistent storage
Slot Manager - THIS IS NOT YET SUPPORTED FOR THIS STACK. Manages slots for the external SPI flash used with common bootloader
Tamper Switch Interface- provides an interface for using a button as an enclosure tamper detection switch
Unix Library - provides common functions for Unix hosts

Stack plugins and libraries:

DHCP Library - provides support for DHCP
Host Network Management - provides support for running host applications
Manufacturing Library - provides support for various manufacturing test APIs
NCP Library - provides functionality for an NCP to communicate with a host
mbedTLS Library - provides support for TLS/J-PAKE and cryptographic operations
Thread Stack Library - provides the Thread networking layer for SoCs and NCPs

Application plugins and libraries:

General functionality:
Button Press - interprets button presses as either single or double presses
CLI - prints a prompt and drives the Command Interpreter
CoAP Dispatch - dispatches CoAP requests to different handlers based on URIs
Command Interpreter - processes commands on the application serial port
Debug Print - provides APIs for debug printing
DHCP Client - requests addresses when DHCP servers become available
Idle/Sleep - implements power saving via idling and sleeping
Main - initializes the HAL, stack, and application and runs the application loop
Polling - periodically sends a data poll to the parent
Serial - provides serial input and output functions
SLAAC Client - requests addresses when SLAAC servers become available
TFTP Bootload Target - receives bootloader images from TFTP client applications

Debug and test functionality:
Address Configuration Debug - prints address information for debugging
Bootload NCP CLI - provides CLI commands for NCP bootloading
CoAP CLI - provides CLI commands for sending CoAP request to another node
CoAP Debug - prints incoming CoAP messages for debugging
Global Address/Prefix Debug - prints global addresses and prefixes for debugging
Heartbeat - blinks an LED periodically to indicate the application is running
Heartbeat (Node Type) - blinks an LED based on the node type
ICMP ClI - provides CLI commands for pinging another node
ICMP Debug - prints incoming ICMP messages for debugging
Network Management CLI - provides CLI commands for forming and joining networks
Scan Debug - prints scanning data for debugging
UDP CLI - provides CLI commands for sending UDP messages to another node
UDP Debug - prints incoming UDP messages for debugging
Version Debug - prints version information for debugging


Several plugins provide CLI commands to the application.  If the plugin is
enabled, the CLI commands will be available.  The application can also provide
its own CLI commands using AppBuilder.

The Bootload NCP CLI plugin contributes the following commands to the
application:

bootloader info
- Print the version of the standalone bootloader installed on the NCP as well
  as the NCP's platform, micro and phy values.

bootloader launch
- Inform the NCP to launch the standalone bootloader.

bootloader load-image <image-path> <begin-offset> <length> [serial options]
- Runs the sample applications for serial bootloading an NCP over SPI or UART.
- The sample applications are bootload-ncp-spi-app and bootload-ncp-uart-app.
- serial options are optional and may be omitted.
- Details of command line arguments can be obtained by running the sample
  applications.
- The sample applications can be built as follows:
  - make -f app/util/bootload/serial-bootloader/bootload-ncp-uart-app.mak
  - make -f app/util/bootload/serial-bootloader/bootload-ncp-spi-app.mak
- Example: bootloader load-image "./bootload-ncp-uart-app" "./em3588-ncp-uart.ebl" 0 0xFFFFFFFF "-p /dev/ttyUSB0"
- Example: bootloader load-image "./bootload-ncp-spi-app" "./efr32-ncp-spi.ebl" 0 0xFFFFFFFF

The Command Interpreter plugin contributes the following commands to the
application:

help
- Prints the available CLI commands.

The CLI plugin contributes the following commands to the application:

reset
- Resets the node.

The CoAP CLI plugin contributes the following commands to the application:

coap listen <address>
- Sets up a listener for CoAP messages for the given address.
- Example: coap listen "fd31:4159:2653:5897:9323:8462:6433:8327"
  - Sets up "fd31..." as a listener for CoAP messages.

coap get <destination> <path>
- Sends a GET request to an IPv6 address with the given parameters.
- Example: coap get "fd31:4159:2653:5897:9323:8462:6433:8327" "example/get"
  - Sends a GET request to "fd31..." for the "example/get" path.

coap post <destination> <path> [<payload>]
- Sends a POST request to an IPv6 address with the given parameters.
- payload is optional and may be omitted.
- An empty payload (e.g., {}) means no payload.
- Example: coap post "fd31:4159:2653:5897:9323:8462:6433:8327" "example/post" {01}
  - Sends a POST request to "fd31..." with payload 0x01 to the "example/post"
    path.

coap put <destination> <path> [<payload>]
- Sends a PUT request to an IPv6 address with the given parameters.
- payload is optional and may be omitted.
- An empty payload (e.g., {}) means no payload.
- Example: coap put "fd31:4159:2653:5897:9323:8462:6433:8327" "example/put" {01}
  - Sends a PUT request to "fd31..." with payload 0x01 to the "example/put"
    path.

coap delete <destination> <path>
- Sends a DELETE request to an IPv6 address with the given parameters.
- Example: coap delete "fd31:4159:2653:5897:9323:8462:6433:8327" "example/delete"
  - Sends a DELETE request to "fd31..." for the "example/delete" path.

The Host Network Management plugin contributes the following commands to the
application:

exit
- Terminates the host process.

The ICMP CLI plugin contributes the following commands to the application:

icmp listen <address>
- Sets up a listener for ICMP messages for the given address.
- Example: icmp listen "fd31:4159:2653:5897:9323:8462:6433:8327"
  - Sets up "fd31..." as a listener for ICMP messages.

icmp ping <destination> [<id:2> <sequence:2> <length:2> <hop limit:2>]
- Pings an IPv6 address with the given parameters.
- id, sequence, length, and hop limit are optional and may be omitted.  If one
  is specified, all must be specified.
- Example: icmp ping "fd31:4159:2653:5897:9323:8462:6433:8327" 0x0102 0x0304 1 0
  - Pings "fd31..." with id 0x0102, sequence number 0x0304, payload length of 1
    byte, with no hops.

The Network Management CLI plugin contributes the following commands to the
application:

info
- Prints information about the network, including network state, node type, and
  IPv6 address(es).

network-management form <channel:1> <power:1> <node type:1> [<network id> [<ula prefix>]]
- Forms a new network with the given channel, TX power, node type, network id,
  and ULA prefix.
- Channel 0 means any channel.
- Network id and ULA prefix are optional and may be omitted.  If ULA is
  specified, network id must be specified too, but may be empty (e.g., "").
- Example: network-management form 11 3 2 "example-id" "fd31:4159:2653:5897::/64"
  - Forms a network "example-id" with ULA "fd31..." on channel 11, with TX
    power 3 dBm, as a router.

network-management join <channel:1> <power:1> <node type:1> <network id> <extended pan id:8> <pan id:2> <join key>
- Joins a new network with the given channel, TX power, node type, extended pan
  id, pan id, and join key.
- Channel 0 means any channel.
- An empty network id (e.g., "") means any network id.
- An empty extend PAN id  (e.g., {}) means any extended PAN id.
- PAN id 0xFFFF means any PAN id.
- Join key is the key shared with the commissioner.
- Example: network-management join 0 3 2 "" {} 0xFFFF "JOIN_KEY"
  - Joins any network using the key "JOIN_KEY" as a router with TX power 3 dBm.

network-management attach
- Attaches with any available router-eligible devices in the network.

network-management commission <preferred channel:1> <fallback channel mask:4> <network id:0--16> <ula prefix> <extended pan id:8> <key:16> [<pan id:2> [<key sequence:4>]]
- Commissions the stack for commissioned joining.
- Channel 0 means any channel.
- PAN id 0xFFFF means any PAN id.
- PAN id and key sequence are optional and may be omitted.  If key sequence is
  specified, PAN id must be specified too, but may be 0xFFFF.
- Example: network-management commission 0 0 "example-id" "fd31:4159:2653:5897::/64" {0102030405060708} {656D62657220454D3235302063686970}
  - Commissions the stack to join network "example-id" with ULA "fd31..." and
    extended PAN id {01...} on any channel using key {65...}.

network-management join-commissioned <power:1> <node type:1> [<require connectivity:1>]
- Joins an already-commissioned network.
- Require connectivity is optional and may be omitted.
- Example: network-management join-commissioned 3 2 0
  - Join the already-commissioned network as a router with TX power 3 dBm
    without starting a new fragment.

network-management resume
- Resumes network operation after the node resets.

network-management reset
- Erases the network state stored in nonvolatile memory.

network-management set-master-key <network key:16>
- Sets a specific key as the master key.
- Must be called prior to forming or joining.
- Example: network-management set-master-key {656D62657220454D3235302063686970}
  - Sets the master key to {65...}.

network-management set-join-key <join key> [<eui64:8>]
- Supplies the commissioner with the key for a joining device.
- EUI64 is optional and may be omitted.
- Example: network-management set-join-key "JOIN_KEY" {0102030405060708}
  - Sets the join key to "JOIN_KEY", specifically for {01...}.

network-management commissioning start <commissioner id>
- Petitions to make this device the commissioner for the network.
- Example: network-management commissioning start "My smartphone"
  - Becomes the commissioner with the friendly name "My smartphone".

network-management commissioning stop
- Stops being the commissioner.

network-management set-joining-mode <mode:1> <length:1>
- Sets the joining mode.
- Example: network-management set-joining-mode 1 1
  - Sets the joining mode to allow steering without steering information.

network-management steering add <eui64:8>
- Adds a node to the steering data.
- Example: network-management steering add {0102030405060708}
  - Adds {01...} to the steering data.

network-management steering send
- Sends steering data to the network and enable joining.

network-management scan active [<channel:1> [<duration:1>]]
- Starts an active scan for available networks.
- Channel 0 means all channels.
- Duration is the exponent of the number of scan periods, where a scan period
  is 960 symbols, and a symbol is 16 microseconds.  The scan will occur for
  ((2^duration) + 1) scan periods.
- Duration 0 means default duration.
- Channel and duration are optional and may be omitted.  If duration is
  specified, channel must be specified too, but may be zero.
- Example: network-management scan active 11 2
  - Scans channel 11 for available networks for 77 milliseconds.

network-management scan energy [<channel:1> [<duration:1>]]
- Starts an energy scan for RSSI values.
- Channel 0 means all channels.
- Duration is the exponent of the number of scan periods, where a scan period
  is 960 symbols, and a symbol is 16 microseconds.  The scan will occur for
  ((2^duration) + 1) scan periods.
- Duration 0 means default duration.
- Channel and duration are optional and may be omitted.  If duration is
  specified, channel must be specified too, but may be zero.
- Example: network-management scan energy 11 2
  - Scans channel 11 for its RSSI value for 77 milliseconds.

network-management scan stop
- Terminates a scan in progress.

network-management gateway <border router flags:1> <is stable:1> <prefix> <domain id:1> <preferred lifetime:4> <valid lifetime:4>
- Configures the node as a gateway.
- Prefix {} means no prefix.
- Preferred lifetime and valid lifetime are measured in seconds.
- Example: network-management gateway 0x32 1 "fd31:4159:2653:5897::/64" 0 0 0
  - Configures the node as a stable gateway for the prefix "fd31..." with
    provisioning domain 0 using SLAAC.

network-management global-addresses [<prefix>]
- Returns the list of global addresses configured on the device.
- Prefix is optional and may be omitted.
- Example: network-management global-addresses "fd31:4159:2653:5897::/64"
  - Returns the list of global addresses configured for the prefix "fd31..." on
    the device.

network-management global-prefixes
- Returns the list of global prefixes known to the device.

network-management listeners
- Returns the list of listeners on the device.

versions
- Gets version information from the stack.

The UDP CLI plugin contributes the following commands to the application:

udp listen <port:2> <address>
- Sets up a listener for UDP messages for the given port and address.
- Example: udp listen 49152 "fd31:4159:2653:5897:9323:8462:6433:8327"
  - Sets up port 49152 on "fd31..." as a listener for UDP messages.

udp send <destination> <source port:2> <destination port:2> <payload>
- Sends a UDP message to an IPv6 address with the given parameters.
- Example: udp send "fd31:4159:2653:5897:9323:8462:6433:8327" 49152 65535 "example message"
  - Sends "example message" to port 65535 on "fd31..." from port 49152.


Sample applications are provided to demonstrate how plugins, libraries, and
custom application code can be combined to develop complete applications.

Client Server Sample Applications:

Four sample applications are provided to demonstrate a simple wireless sensor
network using the Silicon Labs Thread stack.  The server application is a
router and acts as a data sink.  It collects information from client nodes that
act as sensors.  The server can be run on either an SoC or on a host connected
to an NCP.  There are two types of client nodes, both of which run on an SoC:
routers and sleepy end devices.  The client and server communicate using the
Constrained Application Protocol (CoAP) at the application layer, with UDP
serving as the transport layer.  Additional information is contained within
each application and is viewable in AppBuilder.

Border Router Sample Applications:

Two sample applications are provided to demonstrate a border router.  The
border router application runs on a Raspberry Pi.  It is designed to work with
the sensor/actuator application that runs on an SoC.  When connected to the
border router the sensor/actuator node will send information about button
presses and temperature.  It can also receive commands to update the LED state
and activate the buzzer.  Additional information is contained within each
application and is viewable in AppBuilder.

NCP Sample Applications:

Three sample applications are provided to demonstrate how to create a custom
application image to run on a network coprocessor (NCP): SPI, UART with HW flow
control, and UART with SW flow control.  These sample applications are used to
create the pre-built NCP images included in the installer.  They may be
modified to adapt the application to custom hardware.  Additional information
is contained within each application and is viewable in AppBuilder.
