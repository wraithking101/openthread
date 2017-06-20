tftp-bootloader-client-app is a bootloading application.  It runs on a Unix
host, and interfaces directly with POSIX sockets.

tftp-bootloader-client-app bootloads an EBL file to a single target at a time.
It allows bootload resuming via the --resume argument (see below).  It has no
knowledge of Thread networks, so you will need to be connected to a Thread
gateway to bootload a target on a Thread network.

tftp-bootloader-client-app and the tftp-bootloader server, running on a
bootload target, utilize the tftp-bootloader protocol. The tftp-bootloader
protocol is comprised of two components: TFTP and a synchronization protocol.
The synchronization protocol serves a few purposes.  It allows the bootload
target to hold off the bootload process while it erases its flash storage.  It
allows resuming if the bootload process fails due to communication errors, and
it also allows security checking via the manufacturer ID, device type, and
version number arguments (see below).

Implementation and use example is described below.

Required:

1. Silicon Labs TFTP client you can compile for your host:
   a. Install Silicon Labs Thread in THREAD_HOME on your Unix host
   b. Compilate:
      cd THREAD_HOME
      make -f app/util/bootload/tftp-bootloader/client/tftp-bootloader-client-app.mak

2. TFTP-enabled application:
   a. Select an application or local storage bootloader
   b. Enable the TFTP Bootload Target plugin
   c. Disable the UDP Debug plugin
   d. Enable the emberUdpHandler and emberVerifyBootloadRequest callbacks
   e. Add the lines in below note 1 in your implementation

3. Save the compiled EBL files (IAR ARM - debug directory in the project) to
   your host filesystem.

4. First flashing should be done based on binary that has TFTP plugin.

OTA example:
For this example, the same application has been compiled two times in order to
generate an S37 file and a second EBL file.  Difference between both
compilations is only a printf line that says "image 1" and "image 2" on the
console in order to check OTA was correct.

First, flash is based on the binary generating "image 1."  To OTA the binary
printing "image 2" you need to execute on the host from THREAD_HOME directory
the following command:

sudo ./build/tftp-bootloader-client-app/tftp-bootloader-client-app --target "aaaa::6aaa:7e53:7b17:516b" --file image2.ebl

aaaa::6aaa:7e53:7b17:516b being the address of the node we wanted to OTA and
image2.ebl being the file integrating the "image 2" printf for my example.

Note that during the execution client ask you to choose the interface to be
used from a list.  For example:

[0]: ::1 on interface lo
[1]: fd51:13eb:7491::1 on interface wlan0
[2]: bbbb::1 on interface wlan0
[3]: fe80::76da:38ff:fe2e:12f2 on interface wlan0
[4]: aaaa::4b80:7820:633c:9c4 on interface tun0
Choice:

Enter the number from the displayed menu that corresponds to the interface you
are using.  In this example, we would choose 4.

The manufacturer_id, device_type, and version_number arguments are verified on
the bootload target in the emberVerifyBootloadRequest() function.


-------------------------------------------------------

Note 1:

#include EMBER_AF_API_TFTP
#include EMBER_AF_API_TFTP_BOOTLOADER

[...]

void emberUdpHandler(const uint8_t *destination,
                     const uint8_t *source,
                     uint16_t localPort,
                     uint16_t remotePort,
                     const uint8_t *payload,
                     uint16_t payloadLength)
{
  // UDP packets for TFTP bootloading are passed through.  Everything else is
  // ignored.

  if (localPort == emTftpLocalTid) {
    emProcessTftpPacket(source, remotePort, payload, payloadLength);
  } else if (localPort == TFTP_BOOTLOADER_PORT) {
    emProcessTftpBootloaderPacket(source, payload, payloadLength);
  }
}

bool emberVerifyBootloadRequest(const TftpBootloaderBootloadRequest *request)
{
  // A real implementation should verify a bootload request to ensure it is
  // valid.  This sample application simply accepts any request.

  return true;
}
