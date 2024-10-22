// File: event-control.c
//
// Description: Functions dealing with events.
//
// Copyright 2014 Silicon Laboratories, Inc.                                *80*

#include PLATFORM_HEADER
#include "stack/include/ember-types.h"
#include "stack/include/event.h"
#include "hal/hal.h"

extern EmberTaskControl emTasks[];
extern PGM uint8_t emTaskCount;

static bool allowIdling = FALSE;
static uint8_t emActiveTaskCount = 0;


//#define DEBUG_EVENTS
#ifdef DEBUG_EVENTS
  void printEvent(EmberEventControl *event, char *txt)
  {
    fprintf(stderr, "%s: ptr=%p now=%d time=%d stat=%d next=%d\n",
                    txt,
                    event,
                    halCommonGetInt32uMillisecondTick(),
                    event->timeToExecute,
                    event->status,
                    emTasks[event->taskid].nextEventTime);
  }
  #define PRINT_EVENT(event, txt) printEvent(event, txt)
  #define EVENT_DEBUG(foo) foo
#else
  #define PRINT_EVENT(event, txt)
  #define EVENT_DEBUG(foo)
#endif

#ifdef EMBER_NO_IDLE_SUPPORT
  #define emTrackNextEvent(event)   do { } while(0)
#else
  static void emTrackNextEvent(EmberEventControl *event)
  {
    EmberTaskId taskid = event->taskid;
    uint32_t eventTime = event->timeToExecute;
    // If an app doesn't support idling, the taskid will just be initialized to
    //   zero.  This often means that the nextEventTime for task 0 will be wrong,
    //   but that is ok since the app likely isn't trying to idle stuff anyway...
    ATOMIC(
      if(timeGTorEqualInt32u(emTasks[taskid].nextEventTime, eventTime)) {
        emTasks[taskid].nextEventTime = eventTime;
      }
    )
  }
#endif //EMBER_NO_IDLE_SUPPORT

void emEventControlSetActive(EmberEventControl *event)
{
  event->timeToExecute = halCommonGetInt32uMillisecondTick();  // "now"
  event->status = EMBER_EVENT_ZERO_DELAY;
  emTrackNextEvent(event);
  PRINT_EVENT(event, "active");
}


void emEventControlSetDelayMS(EmberEventControl*event, uint32_t delay)
{
  event->timeToExecute = halCommonGetInt32uMillisecondTick() + delay;
  event->status = EMBER_EVENT_MS_TIME;
  emTrackNextEvent(event);
  PRINT_EVENT(event, "ms set");
}


uint32_t emEventControlGetRemainingMS(EmberEventControl *event)
{
  if(event->status != EMBER_EVENT_INACTIVE) {
    if(event->status == EMBER_EVENT_ZERO_DELAY) {
      return 0;
    } else { // MS, QS, or MINUTE
      // NOTE: timeToExecute is now always stored in Milliseconds.  The status
      //   field only records the units for backwards compatibility with the
      //   EZSP protocol EZSP_GET_TIMER which returns a 16 bit value and units
      uint32_t nowMS = halCommonGetInt32uMillisecondTick();
      if(timeGTorEqualInt32u(nowMS, event->timeToExecute))
        return 0;  // already pending
      else
        return elapsedTimeInt32u(nowMS, event->timeToExecute);
    }
  }
  return MAX_INT32U_VALUE;
}


#ifndef EMBER_NO_IDLE_SUPPORT
  // If an event was marked as Inactive since the last time emberRunTask was
  //   called, it is possible that this may return a value sooner than the
  //   next real event.  Generally this is safe- just means that an extra
  //   call to emberRunTask may end up being made when not necessary.
  static uint32_t emTaskMsUntilRun(EmberTaskControl *task, uint32_t now)
  {
    if(timeGTorEqualInt32u(now, task->nextEventTime)) {
      return 0;
    } else {
      return elapsedTimeInt32u(now, task->nextEventTime);
    }
  }


  static void emTaskDetermineNextEvent(EmberTaskControl *task)
  {
    uint32_t now = halCommonGetInt32uMillisecondTick();
    ATOMIC(
      // We must use (HALF_MAX_INT32U_VALUE-1) as the max, or else later calls
      //  to determine if now >= nextEventTime will not work properly.
      task->nextEventTime = now + emberMsToNextEvent(task->events,
                                                     (HALF_MAX_INT32U_VALUE-1));
    )
    EVENT_DEBUG(fprintf(stderr,"next=%d\n",task->nextEventTime));
  }
#endif // EMBER_NO_IDLE_SUPPORT


/* Not currently used
uint32_t emberTaskMsUntilRun(EmberTaskId taskid)
{
  EmberTaskControl *task = &(emTasks[taskid]);
  uint32_t now = halCommonGetInt32uMillisecondTick();

  return emTaskMsUntilRun(task, now);
}
*/

// This cannot just operate on task->nextEventTime since it also must
//  work when tasks are not being used.  This also means that it can be
//  used to update task->nextEventTime when necessary.
uint32_t emberMsToNextEventExtended(EmberEventData *events, uint32_t maxMs, uint8_t* returnIndex)
{
  EmberEventData *nextEvent;
  uint32_t nowMS32 = halCommonGetInt32uMillisecondTick();
  uint8_t index = 0;
  if (returnIndex != NULL) {
    *returnIndex = 0xFF;
  }

  for (nextEvent = events; ; nextEvent++) {
    EmberEventControl *control = nextEvent->control;
    if (control == NULL)
      break;

    if (control->status != EMBER_EVENT_INACTIVE) {
      if (control->status == EMBER_EVENT_ZERO_DELAY
          || timeGTorEqualInt32u(nowMS32, control->timeToExecute)) {
        if (returnIndex != NULL) {
          *returnIndex = index;
        }
        return 0;
      } else {
        uint32_t duration = elapsedTimeInt32u(nowMS32, control->timeToExecute);
        if (duration < maxMs) {
          maxMs = duration;
          if (returnIndex != NULL) {
            *returnIndex = index;
          }
        }
      }
    }
    index++;
  }
  return maxMs;
}

uint32_t emberMsToNextEvent(EmberEventData *events, uint32_t maxMs)
{
  return emberMsToNextEventExtended(events, maxMs, NULL);
}

void emberRunEvents(EmberEventData *events)
{
  EmberEventData *nextEvent;
  uint32_t nowMS32 = halCommonGetInt32uMillisecondTick();

  for (nextEvent = events; ; nextEvent++) {
    EmberEventControl *control = nextEvent->control;
    if (control == NULL)
      break;

    if (control->status != EMBER_EVENT_INACTIVE) {
      if (control->status == EMBER_EVENT_ZERO_DELAY
          || timeGTorEqualInt32u(nowMS32, control->timeToExecute)) {
        PRINT_EVENT(control, "running");
        control->status = EMBER_EVENT_ZERO_DELAY;
        ((void (*)(EmberEventControl *))(nextEvent->handler))(control);
      }
    }
  }
}


#ifdef EMBER_NO_IDLE_SUPPORT
  void emberRunTask(EmberTaskId taskid)
  {
    EmberTaskControl *task = &(emTasks[taskid]);
    emberRunEvents(task->events);
  }
#else
  void emberRunTask(EmberTaskId taskid)
  {
    EmberTaskControl *task = &(emTasks[taskid]);
    uint32_t now = halCommonGetInt32uMillisecondTick();

    if(timeGTorEqualInt32u(now, task->nextEventTime)) {
      emberRunEvents(task->events);
      // now that we've gone through and run all the events, we need to
      //  recalculate when the next one is going to occur.
      emTaskDetermineNextEvent(task);
    } // else nothing to do...
  }
#endif //EMBER_NO_IDLE_SUPPORT


// Must be called on a task before it is possible that any of the tasks
//  events may be altered.
EmberTaskId emberTaskInit(EmberEventData *events)
{
  EmberTaskControl *task;
  EmberTaskId id;

  // make sure we have an available task
  assert(emActiveTaskCount < emTaskCount);

  // allocate the next free task
  id = emActiveTaskCount++;

  task = &(emTasks[id]);
  task->events = events;

  #ifndef EMBER_NO_IDLE_SUPPORT
  {
    EmberEventData *nextEvent;
    // assume the task starts out with something to do
    task->busy = TRUE;
    // make all the events reference the task
    for (nextEvent = events; ; nextEvent++) {
      EmberEventControl *control = nextEvent->control;
      if(control == NULL)
        break;
      control->taskid = id;
    }
    // determine when the first event is due
    emTaskDetermineNextEvent(task);
  }
  #endif // EMBER_NO_IDLE_SUPPORT

  return id;
}

// Implement a weak rtos idle handler so that a customer can optionally
// include their own version.
WEAK(bool rtosIdleHandler(uint32_t * msToNextEvent, bool deepOk))
{
  return FALSE;
}

#ifdef EMBER_NO_IDLE_SUPPORT
  bool emberMarkTaskIdle(EmberTaskId taskid)
  {
    INTERRUPTS_ON();
    return FALSE;
  }
#else
  bool emberMarkTaskIdle(EmberTaskId taskid)
  {
    EmberTaskControl *task = &(emTasks[taskid]);
    EmberTaskId id;
    uint32_t now;
    uint32_t msToNextEvent = MAX_INT32U_VALUE;

    if(!allowIdling) {
      INTERRUPTS_ON();
      return FALSE;
    }

#ifdef RTOS
    // This is currently not implemented in RTOS and not clear how to check
    // what this is checking either.
#else
    //  Since the decision on whether or not it is safe to idle is often based on
    //  information that can change in interrupt context, this API should always
    //  be called with interrupts disabled.  If an interrupt is pending, the
    //  processor will not actually idle.
    assert(INTERRUPTS_ARE_OFF());
#endif

    now = halCommonGetInt32uMillisecondTick();

    // Mark the current task as no longer busy
    task->busy = FALSE;

    // Walk all tasks to find out if they have anything to do
    for(id=0; id<emActiveTaskCount; id++) {
      uint32_t thisDuration;

      task = &(emTasks[id]);
      thisDuration = emTaskMsUntilRun(task, now);
      if(task->busy || thisDuration==0) {
        // we found a task that has something to do, so we can't idle
        INTERRUPTS_ON();
        return FALSE;
      } else {
        if(thisDuration < msToNextEvent)
          msToNextEvent = thisDuration;
      }
    }
    //noone was busy... so lets idle for the time to the next event
    // (This API also forcibly re-enables interrupts)
    if (!rtosIdleHandler(&msToNextEvent, false)) {
      halCommonIdleForMilliseconds(&msToNextEvent);
    }

    return TRUE;
  }
#endif //EMBER_NO_IDLE_SUPPORT


void emMarkTaskActive(EmberTaskId taskid)
{
  EmberTaskControl *task = &(emTasks[taskid]);
  task->busy = TRUE;
}


void emTaskEnableIdling(bool allow)
{
  UNUSED_VAR(allowIdling);
  allowIdling = allow;
}
