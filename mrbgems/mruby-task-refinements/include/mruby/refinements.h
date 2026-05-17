/*
** mruby/refinements.h - Task-scoped refinements API
**
** See Copyright Notice in mruby.h
*/

#ifndef MRUBY_REFINEMENTS_H
#define MRUBY_REFINEMENTS_H

#include <mruby.h>
#include <mruby/data.h>

#ifdef MRB_USE_TASK_REFINEMENTS

/*
 * A Refinement object ties one owner module, one target class, and one
 * method table together.  Represented as an RClass with the REFINEMENT flag
 * so that all existing mt_* helpers work without modification.
 */
#define MRB_FL_CLASS_IS_REFINEMENT (1 << 16)

struct mrb_refinement {
  struct RClass *target_class; /* class being refined          */
  struct RClass *methods;      /* RClass flagged REFINEMENT    */
  struct RClass *owner;        /* module that called #refine   */
};

/*
 * Per-task singly-linked list of active refinements.
 * Nodes are owned by the gem; the mrb_refinement pointers are NOT owned
 * (they stay alive via the owner module's instance variable).
 */
struct mrb_refinement_chain {
  struct mrb_refinement *ref;
  struct mrb_refinement_chain *next;
};

/* Data type descriptor (defined in refinement.c, exported for context.c) */
extern const mrb_data_type mrb_refinement_type;

/* C API */
MRB_API mrb_value mrb_refinement_new(mrb_state *mrb, struct RClass *owner,
                                     struct RClass *target);
MRB_API void      mrb_context_using(mrb_state *mrb, struct mrb_context *ctx,
                                    mrb_value mod);
MRB_API void      mrb_context_unusing(mrb_state *mrb, struct mrb_context *ctx,
                                      mrb_value mod);
MRB_API mrb_bool  mrb_context_has_refinements(struct mrb_context *c);

/* Invalidate all cache entries belonging to ctx */
MRB_API void mrb_mc_clear_by_ctx(mrb_state *mrb, struct mrb_context *ctx);

/* Hook implementation registered into mrb_refinement_lookup */
int mrb_refinements_find(mrb_state *mrb, struct RClass *c,
                         mrb_sym mid, struct RClass **cp,
                         mrb_method_t *m);

/* Lifecycle hooks called from mruby-task */
void mrb_refinements_on_task_spawn(mrb_state *mrb, struct mrb_context *child,
                                   struct mrb_context *parent);
void mrb_refinements_on_task_destroy(mrb_state *mrb, struct mrb_context *ctx);

/* Function pointer slots used by mruby-task (set if gem is loaded) */
extern void (*mrb_task_refinements_on_spawn_fn)(mrb_state *, struct mrb_context *,
                                                struct mrb_context *);
extern void (*mrb_task_refinements_on_destroy_fn)(mrb_state *, struct mrb_context *);

#endif /* MRB_USE_TASK_REFINEMENTS */
#endif /* MRUBY_REFINEMENTS_H */
