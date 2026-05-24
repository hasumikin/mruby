# Task::Queue

`Task::Queue` is a thread-safe, blocking FIFO queue designed for cooperative multitasking. A task calling `pop` on an empty queue is suspended without busy-waiting; it is automatically moved back to the READY state the moment another task pushes an item or closes the queue.

---

## 1. Ruby-level API

```ruby
q = Task::Queue.new

# Producer task
Task.new do
  10.times { |i| q.push(i) }
  q.close
end

# Consumer task
Task.new do
  loop do
    item = q.pop     # blocks until an item arrives or the queue closes
    break if item.nil?
    puts item
  end
end

Task.run
```

| Method | Description |
|--------|-------------|
| `push(obj)` / `enq` / `<<` | Enqueue an item. Raises `Task::Error` if the queue is closed. |
| `pop(non_block=false)` / `deq` / `shift` | Dequeue the oldest item. Blocks until one is available (default) or raises `Task::Error` immediately if `non_block` is true. Returns `nil` when the queue is closed and empty. |
| `size` / `length` | Number of items currently in the queue. |
| `empty?` | `true` if the queue holds no items. |
| `clear` | Discard all enqueued items. |
| `close` | Close the queue. Wakes all blocked consumers (they receive `nil`). |
| `closed?` | `true` after `close` has been called. |
| `num_waiting` | Number of tasks currently blocked in `pop`. |

---

## 2. Data structures

### 2.1 C struct  (`src/task_queue.c`)

```c
typedef struct mrb_task_queue {
  uint8_t closed;
} mrb_task_queue;
```

The C struct holds only a single flag. The actual item storage lives in a Ruby Array kept as an instance variable `@items` on the Ruby object. This split keeps the C layer minimal while delegating memory management of arbitrary Ruby values entirely to the GC.

### 2.2 Storage layout

```
Task::Queue Ruby object (MRB_TT_DATA)
  DATA ptr ──► mrb_task_queue { closed: 0|1 }
  @items   ──► Array [ item0, item1, ... ]   (GC-managed)
```

`push` calls `mrb_ary_push` and `pop` calls `mrb_ary_shift`, so the Array acts as a FIFO with O(n) shift (acceptable for the small queues typical in embedded cooperative tasks).

### 2.3 The WAIT_RETRY sentinel

```c
/* Allocated at gem init, stored as Task::Queue::WAIT_RETRY constant */
wait_retry_ = mrb_obj_new(mrb, mrb->object_class, 0, NULL);
mrb_define_const_id(mrb, queue_class, MRB_SYM(WAIT_RETRY), wait_retry_);
```

`WAIT_RETRY` is a unique `Object` instance allocated once at gem init and pinned in the constant table so the GC never collects it. It is used as a private out-of-band return value from `__pop_try` to signal "the current task has been moved to WAITING; please retry after the next wakeup." Because it is compared with `equal?` (identity, not equality), no user-supplied object can accidentally be mistaken for it.

---

## 3. Push

```c
static mrb_value
queue_push(mrb_state *mrb, mrb_value self)
{
  /* ... closed check, get @items ... */
  mrb_ary_push(mrb, items, obj);
  queue_wake_one_waiter(mrb, q);
  return self;
}
```

After appending the item, `queue_wake_one_waiter` scans the global waiting queue for the highest-priority task that is blocked on this specific queue instance and moves it to READY:

```c
static void
queue_wake_one_waiter(mrb_state *mrb, mrb_task_queue *q)
{
  mrb_task_disable_irq();
  mrb_task *curr = q_waiting_;
  while (curr) {
    if (curr->reason == MRB_TASK_REASON_QUEUE && curr->wait.queue == q) {
      q_delete_task(mrb, curr);
      curr->status = MRB_TASK_STATUS_READY;
      curr->reason = MRB_TASK_REASON_NONE;
      curr->wait.queue = NULL;
      q_insert_task(mrb, curr);
      switching_ = TRUE;
      break;           /* wake exactly one */
    }
    curr = curr->next;
  }
  mrb_task_enable_irq();
}
```

Because `q_waiting_` is a priority-sorted list, the first match is always the highest-priority waiter. Setting `switching_ = TRUE` signals the VM to perform a context switch at the next opcode boundary, so the newly ready consumer can run as soon as the producer yields.

---

## 4. Pop: C/Ruby split architecture

The blocking `pop` is implemented across two layers to work around the constraint that a cooperative context switch can only happen at a Ruby opcode boundary, not inside a C function.

### 4.1 `__pop_try` (C) — `queue_pop_try`

`__pop_try` is a private C method that executes exactly one attempt and returns one of three outcomes:

| Condition | Return value |
|-----------|-------------|
| Item available | The dequeued item (`mrb_ary_shift`) |
| Closed and empty | `nil` |
| Empty, non-blocking | raises `Task::Error` |
| Empty, blocking | moves task to WAITING, returns `WAIT_RETRY` |

The blocking path:

```c
/* Move current task to WAITING */
mrb_task *current = MRB2TASK(mrb);
mrb_task_disable_irq();
q_delete_task(mrb, current);
current->status = MRB_TASK_STATUS_WAITING;
current->reason = MRB_TASK_REASON_QUEUE;
current->wait.queue = q;          /* remember which queue */
q_insert_task(mrb, current);
mrb_task_enable_irq();
switching_ = TRUE;

return wait_retry_;               /* sentinel, not nil */
```

The task is removed from the ready queue and re-inserted into the waiting queue before returning. `switching_ = TRUE` causes `mrb_vm_exec` to exit at the next opcode, handing control back to the scheduler. The task will not run again until `queue_wake_one_waiter` or `queue_wake_all_waiters` moves it back to READY.

### 4.2 `pop` (Ruby) — `mrblib/queue.rb`

```ruby
def pop(non_block = false)
  loop do
    v = __pop_try(non_block)
    return v unless v.equal?(WAIT_RETRY)
  end
end
```

The loop is **not** a busy-wait. After `__pop_try` returns `WAIT_RETRY`, the VM exits `mrb_vm_exec` due to `switching_` before the `loop` can iterate again. The next iteration only runs after the scheduler has resumed this task — meaning after a `push` or `close` on the same queue. The loop body therefore executes at most once per actual wakeup event.

The Ruby layer also provides the aliases: `deq`, `shift`.

---

## 5. Close

```c
static mrb_value
queue_close(mrb_state *mrb, mrb_value self)
{
  if (!q->closed) {
    q->closed = 1;
    queue_wake_all_waiters(mrb, q);
  }
  return self;
}
```

`queue_wake_all_waiters` is the same as `queue_wake_one_waiter` but without the `break`, so every task waiting on this queue is moved to READY in one pass. Each woken consumer will call `__pop_try` again; if there are remaining items they will be returned in FIFO order, and once the queue is empty `__pop_try` returns `nil` (the closed-and-empty branch).

Calling `close` a second time is a no-op (guarded by the `!q->closed` check).

---

## 6. num_waiting

```c
static mrb_value
queue_num_waiting(mrb_state *mrb, mrb_value self)
{
  uint32_t count = 0;
  mrb_task_disable_irq();
  mrb_task *curr = q_waiting_;
  while (curr) {
    if (curr->reason == MRB_TASK_REASON_QUEUE && curr->wait.queue == q) {
      count++;
    }
    curr = curr->next;
  }
  mrb_task_enable_irq();
  return mrb_int_value(mrb, (mrb_int)count);
}
```

Walks the global waiting queue under IRQ protection and counts tasks whose `wait.queue` matches this instance. Useful for diagnostics and for producer logic that wants to know whether any consumer is already waiting before deciding to push.

---

## 7. IRQ safety

All mutations that touch the global task queues (`q_waiting_`, `q_ready_`, etc.) are bracketed with `mrb_task_disable_irq` / `mrb_task_enable_irq`. This prevents the timer interrupt from triggering a context switch mid-update and leaving the linked lists in an inconsistent state. Reading `@items` (a Ruby Array) does not require IRQ protection because the Array is only accessed from within the running task's own execution slice.

---

## 8. Summary: blocking pop flow

```
Consumer calls q.pop (empty queue):

  Ruby: loop { v = __pop_try }
          |
          v (C: queue_pop_try)
          queue empty, not closed
          MRB2TASK(mrb)  =>  current task pointer
          disable_irq
            q_delete_task(current)          remove from READY
            current->status = WAITING
            current->reason = QUEUE
            current->wait.queue = q
            q_insert_task(current)          insert into WAITING
          enable_irq
          switching_ = TRUE
          return WAIT_RETRY
          |
          v (VM detects switching_ at next opcode boundary)
          mrb_vm_exec exits => scheduler runs another task

Producer calls q.push(item):

  C: queue_push
    mrb_ary_push(@items, item)
    queue_wake_one_waiter(q)
      disable_irq
        find consumer in q_waiting_ where wait.queue == q
        q_delete_task(consumer)             remove from WAITING
        consumer->status = READY
        consumer->wait.queue = NULL
        q_insert_task(consumer)             insert into READY
        switching_ = TRUE
      enable_irq

Scheduler resumes consumer:

  Ruby loop continues: v = __pop_try
    @items non-empty => mrb_ary_shift => item
    return item (not WAIT_RETRY)
  loop exits, pop returns item
```
