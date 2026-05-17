# mruby-task-refinements

Task-scoped dynamic refinements for `mruby-task`.

Each task can activate refined method overrides independently. Refinements
activated in task A are invisible to all other tasks. This lets different tasks
use different implementations of the same method without mutating global class
state.

## Semantics

These refinements are **task-dynamic**, not lexical (unlike CRuby refinements).
Activating a module with `using` applies its refined methods to every dispatch
that happens while the task is the running context, regardless of call depth,
source file, or which class originated the call.

This is a deliberate design choice driven by mruby-task's per-task context
model and the goal of zero compiler changes.

## Setup

Enable `MRB_USE_TASK_REFINEMENTS` and include the gem in your build config:

```ruby
MRuby::Build.new do |conf|
  conf.cc.defines << "MRB_USE_TASK_REFINEMENTS=1"
  conf.gem gemdir: "path/to/mruby-task-refinements"
end
```

The gem depends on `mruby-task` and must be loaded after it.

## Defining Refinements

Use `Module#refine` inside a module body:

```ruby
module MyExt
  refine String do
    def shout
      upcase + "!!"
    end
  end

  refine Integer do
    def double
      self * 2
    end
  end
end
```

## Activating Refinements

### Inside a task

```ruby
Task.new do
  Task.current.using MyExt
  puts "hello".shout  # => "HELLO!!"
  puts 21.double      # => 42
end
```

### Via `Kernel#using` (no explicit receiver)

```ruby
Task.new do
  using MyExt          # same as Task.current.using MyExt
  puts "hello".shout  # => "HELLO!!"
end
```

### At task creation time

```ruby
Task.new(using: [Ext1, Ext2]) do
  puts "hello".shout  # refinements active from the first instruction
end
```

Modules are applied in order; for the same target class the last one wins
(LIFO lookup).

## API

### `Task#using(mod)`

Activate all refinements owned by `mod` in the current task's context.
Raises `TypeError` if called on a dormant task.
Raises `ArgumentError` if called on a task other than `Task.current`.

```ruby
Task.current.using MyExt
```

### `Task#unusing(mod)`

Remove refinements previously activated via `using(mod)`.
Raises `ArgumentError` if `mod` is not currently active.

```ruby
Task.current.unusing MyExt
```

### `Task#active_refinements`

Returns an array of `Refinement` objects currently active in the task.

```ruby
refs = Task.current.active_refinements
puts refs.size  # number of active refinements
```

### `Module#refine(target_class) { ... }`

Defines refined methods on `target_class`. Returns a `Refinement` object.

### `Module#refinements`

Returns the array of `Refinement` objects owned by the module.

### `Refinement#target_class`

Returns the class this refinement targets.

### `Refinement#owner`

Returns the module that defined this refinement.

## Inheritance at Spawn

When a task creates a child task via `Task.new`, the child receives a shallow
copy of the parent's active refinement chain at the moment of creation.
Refinements added to the parent after the child is spawned do not propagate.

```ruby
Task.new do
  Task.current.using Ext1
  child = Task.new do
    # Ext1 is active here (copied from parent at spawn time)
    puts "hello".shout  # => "HELLO!!"
  end
  Task.current.using Ext2  # does NOT affect child
  child.join
end
```

## Multiple Refinements and LIFO

When multiple modules refine the same class and method, the last `using` call
wins (LIFO order):

```ruby
Task.new do
  Task.current.using Ext1   # shout -> "HELLO!!"
  Task.current.using Ext2   # shout -> "HELLO??"
  puts "hello".shout        # => "HELLO??" (Ext2 wins)
end
```

## Known Limitations

- **`super` inside a refined method is not supported** in v1. The lookup hook
  does not track the active refinement frame on the callstack, so `super`
  inside a refined method raises `NoMethodError`.
- **Only classes are accepted as `refine` targets.** Refining modules is not
  supported.
- **No lexical scope.** `using` applies to the entire task context dynamically,
  not just the enclosing file or block.
- **Cross-task `using` is not supported.** You cannot call `other_task.using`
  from a different task; doing so raises `ArgumentError`.
- **mruby/c (FemtoRuby) is not supported** in v1. This gem targets the upstream
  mruby VM only.

## See Also

- `mruby-task`: Cooperative multitasking with preemptive scheduling
