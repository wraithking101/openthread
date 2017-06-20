
#ifndef __EMBER_H__
#define __EMBER_H__

#include "ember-types.h"
#include "error.h"
#include "network-management.h"
#if (defined (EMBER_TEST)          \
     || defined(QA_THREAD_TEST)    \
     || defined(EMBER_WAKEUP_STACK)       \
     || defined(RTOS))
  #include "wakeup.h"
#endif
#include "icmp.h"
#include "udp.h"
#include "udp-peer.h"
#include "tcp.h"
#include "event.h"
#include "byte-utilities.h"
#include "stack-info.h"
#include "ember-debug.h"
#include "child.h"
#include "mfglib.h"
#include "coap.h"
#include "coap-diagnostic.h"
#include "app/tmsp/tmsp-enum.h"
#include "config/config.h"

extern const EmberVersion emberVersion;

#endif // __EMBER_H__
