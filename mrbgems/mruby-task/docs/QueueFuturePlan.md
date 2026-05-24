# Task::Queue — Future Plans and Design Notes

## Async IO via Queue: a natural fit for embedded systems

### Background: CRuby's Fiber Scheduler

CRuby (Ruby 3.0+) introduced `Fiber::Scheduler`, which intercepts blocking IO calls (e.g. `IO#read`) and hands them off to an event loop backed by `epoll`, `kqueue`, or `io_uring`. When a Fiber calls a blocking IO method, control is transparently transferred to the scheduler; the Fiber is resumed once the file descriptor is ready. This allows many Fibers to multiplex efficiently on a single OS thread.

### The Queue equivalent for embedded

On embedded targets there are no file descriptors or `epoll`, but there are hardware interrupts: UART receive-complete, SPI transfer-done, GPIO edge, DMA finish, and so on. `Task::Queue`'s blocking `pop` maps onto these events in a structurally identical way:

```
CRuby:  IO#read → Fiber scheduler → epoll → fd ready  → Fiber resumed
mruby:  q.pop   → task WAITING   → ISR   → q.push    → Task resumed
```

A task that calls `q.pop` on an empty queue is moved to `MRB_TASK_STATUS_WAITING` without busy-waiting. When the hardware event fires and something pushes to the queue, `queue_wake_one_waiter` moves the task back to `READY`. The result is cooperative, zero-polling async IO.

### Current limitation: push from an ISR

The existing `queue_push` implementation uses mruby VM operations (`mrb_ary_push`, `mrb_iv_get`, etc.) and can only be called from within the VM execution context. A bare ISR runs outside the VM and cannot safely call these functions.

The practical workaround today is an **IO bridge task**:

```
ISR
 └─► C-level lock-free ring buffer (no heap, no GC)
          └─► IO bridge Task (reads ring buffer, calls q.push)
                   └─► Task::Queue
                            └─► Application Task (blocks on q.pop)
```

This adds one extra layer of indirection but keeps the ISR minimal and safe.

### Proposed: `mrb_task_queue_push_from_isr`

A dedicated C API that can be called directly from an ISR would eliminate the bridge task and reduce latency. The design would mirror the pattern used by embedded RTOSes (FreeRTOS's `xQueueSendFromISR`, Zephyr's `k_msgq_put`):

```c
/*
 * Push a pre-allocated C value into the queue from an ISR context.
 * Uses a lock-free slot rather than mrb_ary_push so the GC is not involved.
 * Sets switching_ so the VM switches to the woken task at the next boundary.
 */
mrb_bool mrb_task_queue_push_from_isr(mrb_state *mrb, mrb_value queue, mrb_value item);
```

Implementation notes:
- The item must be a simple value (fixnum, symbol, a pre-allocated frozen object) to avoid GC interaction inside the ISR.
- A fixed-size C-level slot array (separate from `@items`) could hold values until the VM picks them up on the next timeslice.
- `switching_` can be set atomically from the ISR since it is a single byte.

### Comparison with CRuby's approach

| | CRuby Fiber Scheduler | mruby-task Queue |
|---|---|---|
| IO readiness signal | epoll / kqueue / io_uring | hardware interrupt |
| Transparent to user code | yes (`IO#read` etc.) | no — Queue is explicit |
| Push from outside VM | n/a (OS handles it) | needs ISR-safe API (future) |
| Decoupling of producers/consumers | N:M via fd | N:M via Queue instance |

The explicit nature of the Queue approach is not purely a limitation. On embedded targets it is often desirable to make IO flow visible in the code: which peripheral feeds which Queue, and which task consumes it. Transparency (as in CRuby) trades debuggability for ergonomics; on a microcontroller the trade-off often favors explicitness.

### Example: UART receive with the bridge-task pattern (today)

```ruby
uart_rx = Task::Queue.new

# C side: ISR writes bytes into a C ring buffer.
# Bridge task drains the ring buffer and feeds the Queue.
Task.new(name: "uart-bridge") do
  loop do
    byte = UART.read_byte_nonblock  # returns nil if ring buffer empty
    if byte
      uart_rx.push(byte)
    else
      Task.pass
    end
  end
end

# Application task blocks cleanly, no polling.
Task.new(name: "protocol-handler") do
  loop do
    byte = uart_rx.pop
    handle(byte)
  end
end

Task.run
```

### Example: UART receive with ISR-safe push (proposed)

```ruby
uart_rx = Task::Queue.new
UART.on_receive { |byte| uart_rx.push_from_isr(byte) }  # called from C ISR

Task.new(name: "protocol-handler") do
  loop do
    byte = uart_rx.pop
    handle(byte)
  end
end

Task.run
```

The bridge task disappears; latency drops by one scheduling round-trip.
