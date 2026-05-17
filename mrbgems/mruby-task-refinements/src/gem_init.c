/*
** gem_init.c - mruby-task-refinements gem entry point
**
** See Copyright Notice in mruby.h
*/

#include <mruby.h>
#include "task.h"
#include <mruby/refinements.h>

/* Declared in refinement.c and context.c */
void mrb_task_refinements_refinement_init(mrb_state *mrb);
void mrb_task_refinements_context_init(mrb_state *mrb);

/* Function pointer slots declared in task.h, defined in task.c */
/* We set them here so mruby-task calls our hooks at runtime. */

void
mrb_mruby_task_refinements_gem_init(mrb_state *mrb)
{
  /* Wire the method lookup hook into mruby core */
  mrb_refinement_lookup = mrb_refinements_find;

  /* Wire the task lifecycle hooks into mruby-task */
  mrb_task_refinements_on_spawn_fn   = mrb_refinements_on_task_spawn;
  mrb_task_refinements_on_destroy_fn = mrb_refinements_on_task_destroy;

  /* Define Ruby classes and methods */
  mrb_task_refinements_refinement_init(mrb);
  mrb_task_refinements_context_init(mrb);
}

void
mrb_mruby_task_refinements_gem_final(mrb_state *mrb)
{
  mrb_refinement_lookup              = NULL;
  mrb_task_refinements_on_spawn_fn   = NULL;
  mrb_task_refinements_on_destroy_fn = NULL;
}
