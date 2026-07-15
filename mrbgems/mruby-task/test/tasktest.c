#include <time.h>
#include <string.h>
#include <mruby.h>
#include <mruby/class.h>
#include <mruby/string.h>
#include "task.h"

/* Burn CPU until `ms` milliseconds of CPU time have elapsed, then raise.
   Blocking longer than MRB_TICK_UNIT * MRB_TIMESLICE_TICK_COUNT guarantees
   the tick handler has expired the running task's timeslice, so
   mrb->task.switching is pending when mrb_raise unwinds into the VM's
   catch-handler dispatch — the window where a pending switch used to
   swallow a handled exception into the task result. */
static mrb_value
tasktest_block_then_raise(mrb_state *mrb, mrb_value self)
{
  mrb_int ms;
  mrb_get_args(mrb, "i", &ms);
  clock_t end = clock() + (clock_t)(((double)ms / 1000.0) * (double)CLOCKS_PER_SEC);
  while (clock() < end) {
    /* busy-wait; ticks keep firing */
  }
  mrb_raise(mrb, E_RUNTIME_ERROR, "raised after blocking");
  return mrb_nil_value(); /* not reached */
}

/*
 * Minimal native producer exercising the Task::Queue refill / signal_isr
 * API: a fixed FIFO of C strings standing in for an ISR-fed buffer
 * (e.g. radio packets). native_push stores bytes without creating Ruby
 * objects — mirroring what a real ISR is limited to — and the refill
 * hook materializes them as Strings one pop at a time, in VM context.
 */
#define NATIVE_SLOTS 8
#define NATIVE_SLOT_SIZE 32
static char native_buf[NATIVE_SLOTS][NATIVE_SLOT_SIZE];
static size_t native_len[NATIVE_SLOTS];
static int native_head = 0;  /* next write */
static int native_tail = 0;  /* next read */

static mrb_value
tasktest_refill(mrb_state *mrb, void *ud)
{
  (void)ud;
  if (native_tail == native_head) return mrb_nil_value();
  mrb_value item = mrb_str_new(mrb, native_buf[native_tail], native_len[native_tail]);
  native_tail++;
  return item;
}

static mrb_value
tasktest_attach_refill(mrb_state *mrb, mrb_value self)
{
  mrb_value queue;
  mrb_get_args(mrb, "o", &queue);
  mrb_task_queue_set_refill(mrb, queue, tasktest_refill, NULL);
  return queue;
}

static mrb_value
tasktest_native_push(mrb_state *mrb, mrb_value self)
{
  const char *str;
  mrb_int len;
  mrb_get_args(mrb, "s", &str, &len);
  if (native_head >= NATIVE_SLOTS || len > NATIVE_SLOT_SIZE) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "native buffer full");
  }
  memcpy(native_buf[native_head], str, len);
  native_len[native_head] = (size_t)len;
  native_head++;
  return mrb_nil_value();
}

/* Calling the ISR-safe signal from VM context is a superset of the ISR
   case (VM context can only be interrupted less): the wakeup still goes
   through mrb->task.queue_signaled and the next mrb_tick, which is
   exactly the path under test. */
static mrb_value
tasktest_signal_isr(mrb_state *mrb, mrb_value self)
{
  mrb_value queue;
  mrb_get_args(mrb, "o", &queue);
  mrb_task_queue_signal_isr(mrb_task_queue_ptr(mrb, queue));
  return mrb_nil_value();
}

static mrb_value
tasktest_native_reset(mrb_state *mrb, mrb_value self)
{
  native_head = 0;
  native_tail = 0;
  return mrb_nil_value();
}

void
mrb_mruby_task_gem_test(mrb_state* mrb)
{
  struct RClass *tasktest = mrb_define_module(mrb, "TaskTest");
  mrb_define_module_function(mrb, tasktest, "block_then_raise", tasktest_block_then_raise, MRB_ARGS_REQ(1));
  mrb_define_module_function(mrb, tasktest, "attach_refill", tasktest_attach_refill, MRB_ARGS_REQ(1));
  mrb_define_module_function(mrb, tasktest, "native_push", tasktest_native_push, MRB_ARGS_REQ(1));
  mrb_define_module_function(mrb, tasktest, "signal_isr", tasktest_signal_isr, MRB_ARGS_REQ(1));
  mrb_define_module_function(mrb, tasktest, "native_reset", tasktest_native_reset, MRB_ARGS_NONE());
}
