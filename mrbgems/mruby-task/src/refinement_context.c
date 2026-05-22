/*
** refinement_context.c - Per-task refinement chain management and
**                        Task Ruby methods (using/unusing/active_refinements)
**
** Compiled into the binary only when MRB_USE_TASK_REFINEMENTS is defined.
**
** See Copyright Notice in mruby.h
*/

#ifdef MRB_USE_TASK_REFINEMENTS

#include <mruby.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include <mruby/array.h>
#include <mruby/variable.h>
#include <mruby/error.h>
#include <stddef.h>
#include <stdint.h>
#include "task.h"
#include <mruby/refinements.h>

/* ivar name shared with refinement.c */
#define REFINEMENTS_IVAR "__refinements__"

/* ------------------------------------------------------------------ */
/* Chain helpers                                                        */
/* ------------------------------------------------------------------ */

static struct mrb_refinement_chain *
chain_push(mrb_state *mrb, struct mrb_refinement_chain *head,
           struct mrb_refinement *ref)
{
  struct mrb_refinement_chain *node =
    (struct mrb_refinement_chain *)mrb_malloc(mrb,
      sizeof(struct mrb_refinement_chain));
  node->ref  = ref;
  node->next = head;
  return node;
}

static void
chain_free(mrb_state *mrb, struct mrb_refinement_chain *node)
{
  while (node) {
    struct mrb_refinement_chain *next = node->next;
    mrb_free(mrb, node);
    node = next;
  }
}

static struct mrb_refinement_chain *
chain_dup(mrb_state *mrb, struct mrb_refinement_chain *src)
{
  if (!src) return NULL;
  struct mrb_refinement_chain *head = NULL;
  struct mrb_refinement_chain **tail = &head;
  while (src) {
    struct mrb_refinement_chain *node =
      (struct mrb_refinement_chain *)mrb_malloc(mrb,
        sizeof(struct mrb_refinement_chain));
    node->ref  = src->ref;
    node->next = NULL;
    *tail = node;
    tail  = &node->next;
    src   = src->next;
  }
  return head;
}

static struct mrb_refinement_chain *
chain_remove_owner(mrb_state *mrb, struct mrb_refinement_chain *head,
                   struct RClass *mod_class, mrb_bool *removed)
{
  *removed = FALSE;
  struct mrb_refinement_chain **pp = &head;
  while (*pp) {
    if ((*pp)->ref->owner == mod_class) {
      struct mrb_refinement_chain *del = *pp;
      *pp = del->next;
      mrb_free(mrb, del);
      *removed = TRUE;
    }
    else {
      pp = &(*pp)->next;
    }
  }
  return head;
}

/* ------------------------------------------------------------------ */
/* C API                                                               */
/* ------------------------------------------------------------------ */

MRB_API mrb_bool
mrb_context_has_refinements(struct mrb_context *c)
{
  return c->refinements != NULL;
}

MRB_API void
mrb_context_using(mrb_state *mrb, struct mrb_context *ctx, mrb_value mod)
{
  if (!mrb_module_p(mod)) {
    mrb_raise(mrb, E_TYPE_ERROR, "argument to using must be a Module");
  }
  struct RClass *mod_class = mrb_class_ptr(mod);
  mrb_sym ivar = mrb_intern_lit(mrb, REFINEMENTS_IVAR);

  mrb_value arr = mrb_obj_iv_get(mrb, (struct RObject *)mod_class, ivar);
  if (mrb_nil_p(arr) || RARRAY_LEN(arr) == 0) return;

  mrb_bool is_current = (ctx == mrb->c);
  if (!is_current) mrb_task_disable_irq();

  for (mrb_int i = 0; i < RARRAY_LEN(arr); i++) {
    mrb_value ref_obj = mrb_ary_ref(mrb, arr, i);
    struct mrb_refinement *ref =
      (struct mrb_refinement *)mrb_data_get_ptr(mrb, ref_obj, &mrb_refinement_type);
    ctx->refinements = chain_push(mrb, ctx->refinements, ref);
  }
  mrb_mc_clear_by_ctx(mrb, ctx);

  if (!is_current) mrb_task_enable_irq();
}

MRB_API void
mrb_context_unusing(mrb_state *mrb, struct mrb_context *ctx, mrb_value mod)
{
  if (!mrb_module_p(mod)) {
    mrb_raise(mrb, E_TYPE_ERROR, "argument to unusing must be a Module");
  }
  struct RClass *mod_class = mrb_class_ptr(mod);

  mrb_bool is_current = (ctx == mrb->c);
  if (!is_current) mrb_task_disable_irq();

  mrb_bool removed = FALSE;
  ctx->refinements = chain_remove_owner(mrb, ctx->refinements, mod_class, &removed);
  if (removed) mrb_mc_clear_by_ctx(mrb, ctx);

  if (!is_current) mrb_task_enable_irq();

  if (!removed) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "module not active in this task");
  }
}

/* ------------------------------------------------------------------ */
/* Lifecycle hooks                                                      */
/* ------------------------------------------------------------------ */

void
mrb_refinements_on_task_spawn(mrb_state *mrb, struct mrb_context *child,
                              struct mrb_context *parent)
{
  child->refinements = chain_dup(mrb, parent->refinements);
}

void
mrb_refinements_on_task_destroy(mrb_state *mrb, struct mrb_context *ctx)
{
  chain_free(mrb, ctx->refinements);
  ctx->refinements = NULL;
  mrb_mc_clear_by_ctx(mrb, ctx);
}

void
mrb_refinements_on_task_init(mrb_state *mrb, struct mrb_context *ctx,
                             mrb_value mods)
{
  if (mrb_nil_p(mods)) return;
  if (!mrb_array_p(mods)) {
    mrb_raise(mrb, E_TYPE_ERROR, "using must be an Array");
  }

  for (mrb_int i = 0; i < RARRAY_LEN(mods); i++) {
    mrb_context_using(mrb, ctx, mrb_ary_ref(mrb, mods, i));
  }
}

/* ------------------------------------------------------------------ */
/* Ruby methods: Task#using, Task#unusing, Task#active_refinements     */
/* ------------------------------------------------------------------ */

static mrb_task *
current_task(mrb_state *mrb)
{
  if (mrb->c == mrb->root_c) {
    return mrb->task.main_task;
  }
  return (mrb_task *)((uint8_t *)mrb->c - offsetof(mrb_task, c));
}

static mrb_task *
get_task_checked(mrb_state *mrb, mrb_value self)
{
  mrb_task *t = (mrb_task *)mrb_data_get_ptr(mrb, self, &mrb_task_type);
  if (!t) mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid task");
  /* Refinements are only meaningful for live task contexts. */
  if (t->status == MRB_TASK_STATUS_DORMANT || t->c.status == MRB_TASK_STOPPED) {
    mrb_raise(mrb, E_TYPE_ERROR, "cannot call using on a dormant task");
  }
  return t;
}

static mrb_value
task_using(mrb_state *mrb, mrb_value self)
{
  mrb_value mod;
  mrb_get_args(mrb, "o", &mod);
  mrb_task *t = get_task_checked(mrb, self);
  if (mrb->c == mrb->root_c) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "Task#using can only be called from within the task itself");
  }
  mrb_task *current = current_task(mrb);
  if (t != current) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "Task#using can only be called from within the task itself");
  }
  mrb_context_using(mrb, mrb->c, mod);
  return self;
}

static mrb_value
task_unusing(mrb_state *mrb, mrb_value self)
{
  mrb_value mod;
  mrb_get_args(mrb, "o", &mod);
  mrb_task *t = get_task_checked(mrb, self);
  if (mrb->c == mrb->root_c) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "Task#unusing can only be called from within the task itself");
  }
  mrb_task *current = current_task(mrb);
  if (t != current) {
    mrb_raise(mrb, E_RUNTIME_ERROR, "Task#unusing can only be called from within the task itself");
  }
  mrb_context_unusing(mrb, mrb->c, mod);
  return self;
}

/*
 * Walk the chain; for each node find the original Ruby Refinement object
 * in the owner module's __refinements__ ivar (avoids creating new wrappers
 * which would double-free the mrb_refinement struct on GC).
 */
static mrb_value
task_active_refinements(mrb_state *mrb, mrb_value self)
{
  mrb_task *t = (mrb_task *)mrb_data_get_ptr(mrb, self, &mrb_task_type);
  if (!t) mrb_raise(mrb, E_ARGUMENT_ERROR, "invalid task");

  mrb_sym ivar = mrb_intern_lit(mrb, REFINEMENTS_IVAR);
  mrb_value result = mrb_ary_new(mrb);

  struct mrb_refinement_chain *node = t->c.refinements;
  while (node) {
    struct mrb_refinement *ref = node->ref;
    mrb_value arr = mrb_obj_iv_get(mrb, (struct RObject *)ref->owner, ivar);
    if (!mrb_nil_p(arr)) {
      for (mrb_int i = 0; i < RARRAY_LEN(arr); i++) {
        mrb_value ref_obj = mrb_ary_ref(mrb, arr, i);
        struct mrb_refinement *r =
          (struct mrb_refinement *)mrb_data_get_ptr(mrb, ref_obj, &mrb_refinement_type);
        if (r == ref) {
          mrb_ary_push(mrb, result, ref_obj);
          break;
        }
      }
    }
    node = node->next;
  }
  return result;
}

void
mrb_task_refinements_context_init(mrb_state *mrb)
{
  struct RClass *task_class = mrb_class_get(mrb, "Task");
  mrb_define_method(mrb, task_class, "using",              task_using,              MRB_ARGS_REQ(1));
  mrb_define_method(mrb, task_class, "unusing",            task_unusing,            MRB_ARGS_REQ(1));
  mrb_define_method(mrb, task_class, "active_refinements", task_active_refinements, MRB_ARGS_NONE());
}

#endif /* MRB_USE_TASK_REFINEMENTS */
