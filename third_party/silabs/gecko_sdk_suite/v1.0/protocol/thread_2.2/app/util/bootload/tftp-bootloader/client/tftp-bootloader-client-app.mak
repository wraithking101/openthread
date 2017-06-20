# This Makefile defines how to build 'tftp-bootloader-client-app' for a unix host.
#

# Variables


GLOBAL_BASE_DIR := ../../platform/base

E_CC ?= gcc
CC = $(E_CC)
LD = $(E_CC)
SHELL = /bin/sh
STD ?= c99

INCLUDES= \
  -I. \
  -I$(GLOBAL_BASE_DIR) \
  -I./stack \
  -I$(GLOBAL_BASE_DIR)/hal

DEFINES= \
  -DBOARD_HEADER=\"$(GLOBAL_BASE_DIR)/hal/micro/unix/host/board/host.h\" \
  -DPLATFORM_HEADER=\"$(GLOBAL_BASE_DIR)/hal/micro/unix/compiler/gcc.h\" \
  -DEMBER_STACK_IP \
  -DHAL_MICRO \
  -DUNIX \
  -DUNIX_HOST \
  -DPHY_NULL \
  -DBOARD_HOST \
  -DCONFIGURATION_HEADER=\"app/util/bootload/tftp-bootloader/client/tftp-bootloader-client-app-configuration.h\" \
  -DAPP_SERIAL=1 \
  -DEMBER_ASSERT_SERIAL_PORT=1 \
  -DEMBER_SERIAL1_BLOCKING \
  -DEMBER_SERIAL1_MODE=EMBER_SERIAL_FIFO \
  -DEMBER_SERIAL1_TX_QUEUE_SIZE=128 \
  -DEMBER_SERIAL1_RX_QUEUE_SIZE=128 \
  -DDEBUG_LEVEL=BASIC_DEBUG

OPTIONS= \
  -std=$(STD) \
  -Wcast-align \
  -Wformat \
  -Wimplicit \
  -Wimplicit-int \
  -Wimplicit-function-declaration \
  -Winline \
  -Wno-long-long \
  -Wmain \
  -Wnested-externs \
  -Wno-import \
  -Wparentheses \
  -Wpointer-arith \
  -Wredundant-decls \
  -Wreturn-type \
  -Wstrict-prototypes \
  -Wswitch \
  -Wunused-label \
  -Wunused-value \
  -ggdb \
  -g \
  -O0

APPLICATION_FILES= \
  $(GLOBAL_BASE_DIR)/hal/micro/generic/crc.c \
  $(GLOBAL_BASE_DIR)/hal/micro/generic/mem-util.c \
  $(GLOBAL_BASE_DIR)/hal/micro/generic/random.c \
  $(GLOBAL_BASE_DIR)/hal/micro/generic/system-timer.c \
  $(GLOBAL_BASE_DIR)/hal/micro/unix/host/micro.c \
  app/test/scan-test-stub.c \
  app/udp/thread-app-stubs.c \
  app/util/bootload/tftp-bootloader/client/tftp-bootloader-client-app.c \
  app/util/bootload/tftp-bootloader/client/tftp-bootloader-client.c \
  app/util/bootload/tftp-bootloader/tftp-bootloader.c \
  app/util/bootload/tftp/client/tftp-client-posix.c \
  app/util/bootload/tftp/client/tftp-client.c \
  app/util/bootload/tftp/client/tftp-file-reader.c \
  app/util/bootload/tftp/tftp-posix.c \
  app/util/bootload/tftp/tftp.c \
  app/util/bootload/tftp/tftp-test-file-generator.c \
  app/util/ip/counters.c \
  app/util/serial/command-interpreter2-binary.c \
  app/util/serial/command-interpreter2-error.c \
  app/util/serial/command-interpreter2-util.c \
  app/util/serial/command-interpreter2.c \
  app/util/serial/simple-linux-serial.c \
  stack/config/ember-ip-configuration.c \
  stack/core/log.c \
  app/host/host-buffer-management.c \
  stack/framework/byte-utilities.c \
  stack/framework/event-control.c \
  stack/framework/event-queue.c \
  stack/ip/host/host-listener-table.c \
  stack/ip/host/host-udp-retry.c \
  stack/ip/host/unix-address.c \
  stack/ip/host/unix-listeners.c \
  stack/ip/host/unix-udp-wrapper.c \
  stack/ip/ip-address.c \
  stack/ip/tls/native-test-util.c \

OUTPUT_DIR= build/tftp-bootloader-client-app
# Build a list of object files from the source file list, but all objects
# live in the $(OUTPUT_DIR) above.  The list of object files
# created assumes that the file part of the filepath is unique
# (i.e. the bar.c of foo/bar.c is unique across all sub-directories included).
APPLICATION_OBJECTS = $(addprefix $(OUTPUT_DIR)/, $(notdir $(APPLICATION_FILES:.c=.o)))
VPATH = $(dir $(APPLICATION_FILES))

APP_FILE= $(OUTPUT_DIR)/tftp-bootloader-client-app

APP_LIBRARIES= \

CPPFLAGS= $(INCLUDES) $(DEFINES) $(OPTIONS)
LINK_FLAGS= \

# Rules

all: $(APP_FILE)

ifneq ($(MAKECMDGOALS),clean)
-include $(APPLICATION_OBJECTS:.o=.d)
endif

$(APP_FILE): $(APPLICATION_OBJECTS) $(APP_LIBRARIES) | $(OUTPUT_DIR)
	$(LD) $^ $(LINK_FLAGS) $(APP_LIBRARIES) -o $(APP_FILE)
	@echo -e '\n$@ build success'

$(OUTPUT_DIR)/%.o: %.c | $(OUTPUT_DIR)
	$(CC) $(CPPFLAGS) -MF $(@:.o=.d) -MMD -MP -c $< -o $@

clean:
	rm -rf $(OUTPUT_DIR)

$(OUTPUT_DIR):
	@mkdir $(OUTPUT_DIR)
