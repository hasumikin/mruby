/*
** lookup.c - Refinement chain walk and method cache helpers
**
** See Copyright Notice in mruby.h
*/

#include <mruby.h>
#include <mruby/class.h>
#include <mruby/proc.h>
#include <mruby/error.h>
#include <mruby/refinements.h>

/* Invalidate all cache entries that belong to ctx */
MRB_API void
mrb_mc_clear_by_ctx(mrb_state *mrb, struct mrb_context *ctx)
{
#ifndef MRB_NO_METHOD_CACHE
  struct mrb_cache_entry *mc = mrb->cache;
  for (int i = 0; i < MRB_METHOD_CACHE_SIZE; mc++, i++) {
    if (mc->ctx == ctx) mc->c = NULL;
  }
#endif
}

/*
 * Local inline version of mt_get (class.c's is static).
 * Walk the mrb_mt_tbl chain doing a linear scan.
 */
static mrb_bool
ref_mt_get(mrb_mt_tbl *t, mrb_sym sym, union mrb_mt_ptr *pp, uint32_t *fp)
{
  while (t) {
    mrb_mt_entry *entries = t->ptr;
    for (int i = 0; i < t->size; i++) {
      if (entries[i].key == sym) {
        if (MRB_MT_REMOVED_P(entries[i])) return FALSE;
        *pp = entries[i].val;
        *fp = entries[i].flags;
        return TRUE;
      }
    }
    t = t->next;
  }
  return FALSE;
}

/* Construct mrb_method_t from flags + ptr (mirrors create_method_value) */
static inline mrb_method_t
ref_make_method(uint32_t flags, union mrb_mt_ptr val)
{
  mrb_method_t m;
  m.flags = flags;
  m.as.proc = val.proc;
  return m;
}

/*
 * Look up a refined method for a SINGLE class `c`.
 * The ancestor walk is the caller's responsibility (mrb_vm_find_method in
 * mruby/src/class.c) so that a method defined directly on a more specific
 * class wins over a refinement targeting a higher ancestor (MRI semantics).
 *
 * Returns TRUE and fills *cp / *m if a refinement in the current task's
 * chain targets `c` (or an iclass wrapping target_class equal to `c->c`)
 * and provides `mid`.
 *
 * Registered into mrb_refinement_lookup at gem init.
 */
int
mrb_refinements_find(mrb_state *mrb, struct RClass *c, mrb_sym mid,
                     struct RClass **cp, mrb_method_t *m)
{
  /* LIFO: first node in the chain wins */
  struct mrb_refinement_chain *node = mrb->c->refinements;
  while (node) {
    struct mrb_refinement *ref = node->ref;
    mrb_bool match = (c == ref->target_class) ||
                     (c->tt == MRB_TT_ICLASS && c->c == ref->target_class);
    if (match) {
      union mrb_mt_ptr ptr;
      uint32_t flags;
      if (ref->methods->mt && ref_mt_get(ref->methods->mt, mid, &ptr, &flags)) {
        if (ptr.proc != 0) {
          *cp = ref->methods;
          *m = ref_make_method(flags, ptr);
          return TRUE;
        }
      }
    }
    node = node->next;
  }
  return FALSE;
}
