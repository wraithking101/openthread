// Copyright 2015 Silicon Laboratories, Inc.

#include PLATFORM_HEADER
#include CONFIGURATION_HEADER
#include EMBER_AF_API_STACK
#include EMBER_AF_API_EVENT_QUEUE
#include EMBER_AF_API_HAL
#include "thread-bookkeeping.h"

// If serial functionality is enabled, we will initialize the serial ports during
// startup.  This has to happen after the HAL is initialized.
#ifdef EMBER_AF_API_SERIAL
  #include EMBER_AF_API_SERIAL
  #define SERIAL_INIT EMBER_AF_SERIAL_PORT_INIT
#else
  #define SERIAL_INIT()
#endif

// If printing is enabled, we will print some diagnostic information about the
// most recent reset and also during runtime.  On some platforms, extended
// diagnostic information is available.
#if defined(EMBER_AF_API_SERIAL) && defined(EMBER_AF_PRINT_ENABLE)
  #ifdef EMBER_AF_API_DIAGNOSTIC_CORTEXM3
    #include EMBER_AF_API_DIAGNOSTIC_CORTEXM3
  #endif
  static void printResetInformation(void);
  #define PRINT_RESET_INFORMATION printResetInformation
  #define emberAfGuaranteedPrint(...) \
    emberSerialGuaranteedPrintf(APP_SERIAL, __VA_ARGS__)
  #define emberAfGuaranteedPrintln(format, ...) \
    emberSerialGuaranteedPrintf(APP_SERIAL, format "\r\n", __VA_ARGS__);
#else
  #define PRINT_RESET_INFORMATION()
  #define emberAfGuaranteedPrint(...)
  #define emberAfGuaranteedPrintln(...)
#endif

#ifdef EMBER_AF_API_MANAGEMENT_CLIENT
  #include EMBER_AF_API_MANAGEMENT_CLIENT
  #define PROCESS_MANAGEMENT_COMMAND managementCommandTick
#else
  #define PROCESS_MANAGEMENT_COMMAND()
#endif

// Our entry point is typically main(), except during testing.
#ifdef EMBER_TEST
  #define MAIN nodeMain
#else
  #define MAIN main
#endif

static void loop(void);

extern const EmberEventData emAppEvents[];
EmberTaskId emAppTask;
EventQueue emAppEventQueue;

static bool stackIsInitialized;
static bool applicationIsInitialized;
static uint32_t initTimeMs;
#define INIT_TIMEOUT_MS (5 * MILLISECOND_TICKS_PER_SECOND)

int MAIN(MAIN_FUNCTION_PARAMETERS)
{
  // Let the application and plugins do early initialization.  This function is
  // generated.
  emAfMain(MAIN_FUNCTION_ARGUMENTS);

  // Initialize the HAL and enable interrupts.
  halInit();
  INTERRUPTS_ON();

  // Initialize the serial ports.
  SERIAL_INIT();

  // Display diagnostic information about the most recent reset.
  PRINT_RESET_INFORMATION();

  // Initialize a task for the application and plugin events and enable idling.
  emAppTask = emberTaskInit(emAppEvents);
  emberTaskEnableIdling(true);
  emInitializeEventQueue(&emAppEventQueue);

  // Initialize the stack.
  stackIsInitialized = false;
  applicationIsInitialized = false;
  initTimeMs = halCommonGetInt32uMillisecondTick();
  emberInit();

  // Run the application loop, which usually never terminates.
  loop();

  return 0;
}

static void loop(void)
{
  while (true) {
    // Reset the watchdog timer to prevent a timeout.
    halResetWatchdog();

    // Process management commands from the NCP, if applicable.  This is done
    // before checking whether stack initialization has finished, because the
    // initialization status is itself a management command.
    PROCESS_MANAGEMENT_COMMAND();

    // Wait until the stack is initialized before allowing the application and
    // plugins to run, so that so that the application doesn't get ahead of the
    // stack.  If initialization takes too long, we try again.
    if (!stackIsInitialized) {
      uint32_t nowMs = halCommonGetInt32uMillisecondTick();
      if (INIT_TIMEOUT_MS <= elapsedTimeInt32u(initTimeMs, nowMs)) {
        emberAfGuaranteedPrintln("Waiting for NCP...", 0);
        initTimeMs = nowMs;
        emberInit();
      }
      continue;
    }

    // Initialize the application and plugins.  Whenever the stack initializes,
    // these must be reinitialized.  This function is generated.
    if (!applicationIsInitialized) {
      emAfInit();
      applicationIsInitialized = true;
    }

    // Let the stack, application, and plugins run periodic tasks.  This
    // function is generated.
    emAfTick();

    // Run the application and plugin events.
    emberRunTask(emAppTask);
    emberRunEventQueue(&emAppEventQueue);

    simulatedTimePassesMs(emberMsToNextEvent(emAppEvents,
                                             emberMsToNextQueueEvent(&emAppEventQueue)));
  }
}

#ifdef EMBER_AF_PRINT_ENABLE

static void printResetInformation(void)
{
  // Information about the most recent reset is printed during startup to aid
  // in debugging.
  emberAfGuaranteedPrintln("Reset info: 0x%x (%p)",
                           halGetResetInfo(),
                           halGetResetString());
#ifdef EMBER_AF_API_DIAGNOSTIC_CORTEXM3
  emberAfGuaranteedPrintln("Extended reset info: 0x%2x (%p)",
                           halGetExtendedResetInfo(),
                           halGetExtendedResetString());
  if (halResetWasCrash()) {
    halPrintCrashSummary(APP_SERIAL);
    halPrintCrashDetails(APP_SERIAL);
    halPrintCrashData(APP_SERIAL);
  }
#endif // EMBER_AF_API_DIAGNOSTIC_CORTEXM3
}

#endif // EMBER_AF_PRINT_ENABLE

void emberInitReturn(EmberStatus status)
{
  // If initialization fails, we have to assert because something is wrong.
  // Whenever the stack initializes, the application and plugins must be
  // reinitialized.
  emberAfGuaranteedPrintln("Init: 0x%x", status);
  assert(status == EMBER_SUCCESS);
  stackIsInitialized = true;
  applicationIsInitialized = false;
}

// TODO: This should not be necessary in the application.  See EMIPSTACK-324.
bool ipModemLinkPreparingForPowerDown(void)
{
  return false;
}

void emberResetMicroHandler(EmberResetCause resetCause)
{
  // We only print the reset cause for host applications because SoC
  // applications will automatically print the reset cause at startup.
#ifdef EMBER_HOST
  static const char * const resetCauses[] = {
    "UNKNOWN",
    "FIB",
    "BOOTLOADER",
    "EXTERNAL",
    "POWER ON",
    "WATCHDOG",
    "SOFTWARE",
    "CRASH",
    "FLASH",
    "FATAL",
    "FAULT",
    "BROWNOUT",
  };
  emberAfGuaranteedPrintln("Reset info: 0x%x (%p)",
                           resetCause,
                           (resetCause < COUNTOF(resetCauses)
                            ? resetCauses[resetCause]
                            : resetCauses[EMBER_RESET_UNKNOWN]));
#endif // EMBER_HOST
}

void emberMarkApplicationBuffersHandler(void)
{
  // Mark scheduled events in the queue and run the marking functions of those
  // events.
  emberMarkEventQueue(&emAppEventQueue);

  // Let the application and plugins mark their buffers.  This function is
  // generated.
  emAfMarkApplicationBuffers();
}
