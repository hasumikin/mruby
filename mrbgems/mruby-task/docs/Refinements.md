# Task-Scoped Refinements

mruby-task ships an optional Refinements subsystem enabled by defining `MRB_USE_TASK_REFINEMENTS` at build time (the gem's `mrbgem.rake` sets this flag automatically). The feature gives every cooperative task its own refinement scope: calling `Task.current.using M` activates the refinements declared in module `M` only for the calling task, with no effect on any other running task.

---

## 1. Ruby-level API

```ruby
module StringExt
  refine String do
    def shout = upcase + "!!"
  end
end

# Activate on a specific task (self only, no cross-task activation):
Task.new do
  Task.current.using StringExt
  puts "hello".shout          # => "HELLO!!"
end

# Or pass refinements at spawn time:
Task.new(using: [StringExt]) do
  puts "hello".shout          # => "HELLO!!"
end
```

| Method | Description |
|--------|-------------|
| `Module#refine(klass) { }` | Declares refined methods for `klass` inside the calling module. |
| `Task#using(mod)` | Activates `mod`'s refinements for the current task. |
| `Task#unusing(mod)` | Deactivates `mod`'s refinements from the current task. |
| `Task#active_refinements` | Returns an Array of active `Refinement` objects. |
| `Task.new(using: [m1, m2]) { }` | Activates a list of modules before the task's first run. |

### Constraint: `using` and `unusing` must be called from within the task itself

`Task#using` and `Task#unusing` can only be called by the task on itself (`Task.current.using M`). Calling them from outside the task — either from the root context or from a different task — raises `RuntimeError`.

```ruby
task = Task.new { Task.current.using Ext }   # ok
task.using Ext                               # RuntimeError: Task#using can only be called from within the task itself
```

This restriction exists because activating a refinement in another task's context mid-execution would invalidate that task's method cache entries from the outside, with no safe synchronization point. The `Task.new(using: [...])` keyword argument is the supported way to pre-activate refinements before a task's first run.

---

## 2. Core data structures

### 2.1 `struct mrb_refinement`  (`include/mruby/refinements.h`)

```c
struct mrb_refinement {
  struct RClass *target_class; /* class being refined          */
  struct RClass *methods;      /* RClass flagged REFINEMENT    */
  struct RClass *owner;        /* module that called #refine   */
};
```

One `mrb_refinement` is created for every `refine TargetClass do ... end` block. The key trick is `methods`: it is an ordinary `RClass` (so all existing `mt_*` method-table helpers work unchanged on it) but has the custom flag `MRB_FL_CLASS_IS_REFINEMENT` set on its `flags` field. Methods defined inside the `refine` block end up in `methods->mt`, not in `target_class->mt`.

The Ruby-level wrapper is an instance of the `Refinement` class (`MRB_TT_DATA`) so the GC can track the struct through its `mrb_data_type`.

### 2.2 `struct mrb_refinement_chain`  (`include/mruby/refinements.h`)

```c
struct mrb_refinement_chain {
  struct mrb_refinement *ref;
  struct mrb_refinement_chain *next;
};
```

A singly-linked list of active refinements. Every task context has its own head pointer. Nodes are owned by the subsystem (heap-allocated, freed on task destruction); the `mrb_refinement` structs they point to are owned by their owner module and stay alive as long as that module's `__refinements__` ivar array holds the Ruby wrapper.

### 2.3 `struct mrb_context` extension  (`include/mruby.h`)

```c
struct mrb_context {
  /* ... existing fields ... */
#ifdef MRB_USE_TASK_REFINEMENTS
  struct mrb_refinement_chain *refinements; /* NULL until first #using */
#endif
};
```

Because `mrb_task` embeds `struct mrb_context` as its last member (`t->c`), adding one pointer here adds exactly one pointer of memory per task. The field starts as `NULL`; the chain is built lazily on the first `#using` call.

### 2.4 Method cache extension  (`include/mruby.h`)

```c
struct mrb_cache_entry {
  struct RClass *c, *c0;
  mrb_sym mid;
#ifdef MRB_USE_TASK_REFINEMENTS
  struct mrb_context *ctx;  /* which task context populated this entry */
#endif
  mrb_method_t m;
};
```

The standard method cache is keyed by `(receiver class, method symbol)`. Under task refinements that key is ambiguous: two tasks calling `"hello".shout` on the same `String` class may resolve to different methods. Adding `ctx` (the current `mrb_context *`) makes each entry task-specific. The hit check in `mrb_vm_find_method` becomes:

```c
if (mc->c == c && mc->mid == mid && mc->ctx == mrb->c) { ... }
```

Refined hits are **not** written to the cache at all (see Section 4) because the chain can change between calls via `#using`/`#unusing`.

---

## 3. Module#refine and GC safety

`Module#refine` is defined on `mrb->module_class` (`refinement.c:mrb_task_refinements_refinement_init`).

```
mod_refine(mrb, self):
  1. Allocate mrb_refinement  (owner=self, target=target_class)
  2. Create an anonymous RClass `methods` flagged MRB_FL_CLASS_IS_REFINEMENT
  3. mrb_gc_register(methods)            -- pin against GC (no Ruby ref yet)
  4. mrb_yield_with_class(blk, methods)  -- run block with `methods` as target
       => def inside the block inserts into methods->mt
  5. Wrap in a Refinement DATA object
  6. Push wrapper into owner->__refinements__ ivar array
  7. Return the wrapper
```

Step 3 is critical: `methods` has no direct Ruby reference at the time of allocation, so the GC would collect it during the block evaluation. `mrb_gc_register` roots it explicitly; the `refinement_free` destructor calls `mrb_gc_unregister` to balance.

The `__refinements__` ivar array on the owner module serves double duty: it keeps the `Refinement` wrappers (and therefore the `mrb_refinement` structs and `methods` RClasses) alive for as long as the module lives, and it is the authoritative list returned by `Module#refinements`.

---

## 4. Method lookup hook

### 4.1 The function pointer

```c
/* include/mruby.h */
typedef int (*mrb_refinement_lookup_fn)(mrb_state *, RClass *, mrb_sym, RClass **, mrb_method_t *);
MRB_API mrb_refinement_lookup_fn mrb_refinement_lookup;
```

`mrb_refinement_lookup` is a global function pointer, initialised to `NULL`. At gem init it is set to `mrb_refinements_find`; at gem final it is cleared back to `NULL`. This lets the feature be compiled into the binary without any hard link dependency from `src/class.c` on the gem's translation units.

### 4.2 Where the hook fires  (`src/class.c: mrb_vm_find_method`)

```c
mrb_bool has_refinements =
  (mrb_refinement_lookup != NULL) && (mrb->c->refinements != NULL);

while (c) {                          /* ancestor walk */
  if (has_refinements) {
    struct RClass *rcp = c;
    if (mrb_refinement_lookup(mrb, c, mid, &rcp, &m)) {
      *cp = rcp;
      return m;                      /* refined hit, not cached */
    }
  }
  /* ... normal mt_get on c->mt ... */
  c = c->super;
}
```

The hook is called **once per step of the ancestor walk**, not once for the whole hierarchy. This matches MRI semantics: a method defined directly on a more-specific class beats a refinement that targets a higher ancestor.

### 4.3 `mrb_refinements_find`  (`src/refinement_lookup.c`)

```c
int
mrb_refinements_find(mrb_state *mrb, struct RClass *c, mrb_sym mid, struct RClass **cp, mrb_method_t *m)
{
  struct mrb_refinement_chain *node = mrb->c->refinements;
  while (node) {
    struct mrb_refinement *ref = node->ref;
    mrb_bool match = (c == ref->target_class) ||
                     (c->tt == MRB_TT_ICLASS && c->c == ref->target_class);
    if (match) {
      if (ref_mt_get(ref->methods->mt, mid, &ptr, &flags)) {
        *cp = ref->methods;
        *m  = ref_make_method(flags, ptr);
        return TRUE;
      }
    }
    node = node->next;
  }
  return FALSE;
}
```

Key points:

- `mrb->c->refinements` is the **current task's** chain (because `mrb->c` is always the running task's context).
- The chain is walked in **LIFO order** (newest `#using` wins), matching MRI's last-in-first-out override semantics.
- The iclass guard (`c->tt == MRB_TT_ICLASS && c->c == ref->target_class`) handles the case where a module is included into a class and appears as an iclass node in the ancestor chain.
- `ref_mt_get` is a local reimplementation of `class.c`'s `mt_get` (which is `static`) performing a linear scan of the method table chain.
- Refined hits are returned immediately without populating the method cache, because the result depends on a mutable per-task chain.

---

## 5. Cache invalidation

When the chain changes (via `#using`, `#unusing`, or task destruction), all cache entries owned by that context must be evicted:

```c
void
mrb_mc_clear_by_ctx(mrb_state *mrb, struct mrb_context *ctx)
{
  struct mrb_cache_entry *mc = mrb->cache;
  for (int i = 0; i < MRB_METHOD_CACHE_SIZE; mc++, i++) {
    if (mc->ctx == ctx) mc->c = NULL;
  }
}
```

Setting `mc->c = NULL` is the sentinel that marks an entry as invalid. The scan is O(MRB_METHOD_CACHE_SIZE) -- typically 256 entries -- and is bounded and fast.

---

## 6. Task lifecycle hooks

The refinements subsystem hooks into the task lifecycle through three function pointers defined in `task.c`:

```c
void (*mrb_task_refinements_on_spawn_fn)(...);
void (*mrb_task_refinements_on_destroy_fn)(...);
void (*mrb_task_refinements_on_init_fn)(...);
```

They are wired to concrete implementations in `mrb_mruby_task_gem_init` only when `MRB_USE_TASK_REFINEMENTS` is defined; otherwise they remain `NULL` and the three call sites in `task.c` are cheap null-pointer checks.

### 6.1 Spawn: `mrb_refinements_on_task_spawn`

```c
void
mrb_refinements_on_task_spawn(mrb_state *mrb, struct mrb_context *child, struct mrb_context *parent)
{
  child->refinements = chain_dup(mrb, parent->refinements);
}
```

A newly spawned task gets a **shallow copy** of the parent's chain. Each node is a fresh allocation but points to the same `mrb_refinement` structs (which are owned by their owner modules and are therefore safe to share). This gives the child the same refinements that were active in the parent **at spawn time**; subsequent `#using`/`#unusing` calls in either task do not affect the other.

### 6.2 Destroy: `mrb_refinements_on_task_destroy`

```c
void
mrb_refinements_on_task_destroy(mrb_state *mrb, struct mrb_context *ctx)
{
  chain_free(mrb, ctx->refinements);
  ctx->refinements = NULL;
  mrb_mc_clear_by_ctx(mrb, ctx);
}
```

Frees all chain nodes and evicts the task's cache entries. Called from `execute_task` when `c->status == MRB_TASK_STOPPED` and from `terminate_task_internal`.

### 6.3 Init: `mrb_refinements_on_task_init`

```c
void
mrb_refinements_on_task_init(mrb_state *mrb, struct mrb_context *ctx, mrb_value mods)
{
  for (mrb_int i = 0; i < RARRAY_LEN(mods); i++) {
    mrb_context_using(mrb, ctx, mrb_ary_ref(mrb, mods, i));
  }
}
```

Handles the `Task.new(using: [...]) { }` keyword argument by activating each module on the new context before the task's first timeslice.

---

## 7. IRQ safety for multi-task mutation

Calls to `mrb_context_using` and `mrb_context_unusing` that target a context other than `mrb->c` (i.e., another task's context) bracket the mutation with `mrb_task_disable_irq` / `mrb_task_enable_irq` to prevent a timer interrupt from switching to the target task mid-update. Mutations of the current context (`ctx == mrb->c`) are safe without a lock because only the running task accesses its own chain.

---

## 8. Summary: the full picture

```
Task.new { Task.current.using Ext1; "hello".shout }
          |
          v
mrb_context_using(mrb, mrb->c, Ext1)
  - reads Ext1->__refinements__ ivar
  - for each Refinement in array:
      chain_push(&mrb->c->refinements, ref)
  - mrb_mc_clear_by_ctx(mrb, mrb->c)

VM executes "hello".shout:
  mrb_vm_find_method(mrb, String, :shout, ...)
    has_refinements = TRUE  (mrb_refinement_lookup != NULL && mrb->c->refinements != NULL)
    ancestor walk: c = String
      mrb_refinements_find(mrb, String, :shout, ...)
        walk chain (LIFO):
          node->ref->target_class == String => match
          ref_mt_get(ref->methods->mt, :shout) => found
          return TRUE, *cp = ref->methods, *m = refined_method
      => return refined_method (NOT cached)
    => "HELLO!!"
```

Another task calling `"hello".shout` without `#using Ext1` has `mrb->c->refinements == NULL`, so `has_refinements` is `FALSE` and the normal ancestor walk proceeds, finding no `shout` and raising `NoMethodError`.
