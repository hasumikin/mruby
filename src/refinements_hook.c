/*
** refinements_hook.c - Global hook for task-scoped refinements
**
** See Copyright Notice in mruby.h
*/

#ifdef MRB_USE_TASK_REFINEMENTS

#include <mruby.h>

/* Default NULL; assigned by mruby-task-refinements gem init */
mrb_refinement_lookup_fn mrb_refinement_lookup = NULL;

#endif /* MRB_USE_TASK_REFINEMENTS */
