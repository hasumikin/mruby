/*
** refinement.c - Module#refine, Refinement class
**
** See Copyright Notice in mruby.h
*/

#include <mruby.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include <mruby/proc.h>
#include <mruby/variable.h>
#include <mruby/array.h>
#include <mruby/error.h>
#include <mruby/refinements.h>

static void refinement_free(mrb_state *mrb, void *p)
{
  mrb_free(mrb, p);
}

/* Exported so context.c and other files can use mrb_data_get_ptr with this type */
const mrb_data_type mrb_refinement_type = {
  "Refinement", refinement_free
};

static struct mrb_refinement *
refinement_ptr(mrb_state *mrb, mrb_value self)
{
  return (struct mrb_refinement *)mrb_data_get_ptr(mrb, self, &mrb_refinement_type);
}

/* ivar key under which Module stores its refinements array */
static mrb_sym s_refinements_ivar;

MRB_API mrb_value
mrb_refinement_new(mrb_state *mrb, struct RClass *owner, struct RClass *target)
{
  struct mrb_refinement *ref =
    (struct mrb_refinement *)mrb_malloc(mrb, sizeof(struct mrb_refinement));

  /* Anonymous module to hold the refined method table */
  struct RClass *methods = mrb_module_new(mrb);
  methods->flags |= MRB_FL_CLASS_IS_REFINEMENT;

  ref->target_class = target;
  ref->methods = methods;
  ref->owner = owner;

  struct RClass *ref_class = mrb_class_get(mrb, "Refinement");
  return mrb_obj_value(mrb_data_object_alloc(mrb, ref_class, ref, &mrb_refinement_type));
}

/*
 * Module#refine(target_class) { ... }
 */
static mrb_value
mod_refine(mrb_state *mrb, mrb_value self)
{
  mrb_value target_val;
  mrb_value blk;
  mrb_get_args(mrb, "C&!", &target_val, &blk);

  if (mrb_type(target_val) != MRB_TT_CLASS && mrb_type(target_val) != MRB_TT_MODULE) {
    mrb_raise(mrb, E_TYPE_ERROR, "refine target must be a Class or Module");
  }

  struct RClass *owner  = mrb_class_ptr(self);
  struct RClass *target = mrb_class_ptr(target_val);

  mrb_value ref_obj = mrb_refinement_new(mrb, owner, target);
  struct mrb_refinement *ref = refinement_ptr(mrb, ref_obj);

  /* Evaluate block with the refinement module as self/target_class so that
     def inside the block adds methods to the refinement module's mt. */
  mrb_value methods_val = mrb_obj_value(ref->methods);
  mrb_yield_with_class(mrb, blk, 1, &methods_val, methods_val, ref->methods);

  /* Register on owner so GC keeps it alive */
  mrb_value arr = mrb_obj_iv_get(mrb, (struct RObject *)owner, s_refinements_ivar);
  if (mrb_nil_p(arr)) {
    arr = mrb_ary_new(mrb);
    mrb_obj_iv_set(mrb, (struct RObject *)owner, s_refinements_ivar, arr);
  }
  mrb_ary_push(mrb, arr, ref_obj);

  return ref_obj;
}

/*
 * Module#refinements -> Array
 */
static mrb_value
mod_refinements(mrb_state *mrb, mrb_value self)
{
  mrb_value arr = mrb_obj_iv_get(mrb,
    (struct RObject *)mrb_class_ptr(self), s_refinements_ivar);
  if (mrb_nil_p(arr)) return mrb_ary_new(mrb);
  return arr;
}

/* Refinement#target_class */
static mrb_value
ref_target_class(mrb_state *mrb, mrb_value self)
{
  struct mrb_refinement *ref = refinement_ptr(mrb, self);
  return mrb_obj_value(ref->target_class);
}

/* Refinement#owner */
static mrb_value
ref_owner(mrb_state *mrb, mrb_value self)
{
  struct mrb_refinement *ref = refinement_ptr(mrb, self);
  return mrb_obj_value(ref->owner);
}

void
mrb_task_refinements_refinement_init(mrb_state *mrb)
{
  s_refinements_ivar = mrb_intern_cstr(mrb, "__refinements__");

  struct RClass *ref_class = mrb_define_class(mrb, "Refinement", mrb->object_class);
  MRB_SET_INSTANCE_TT(ref_class, MRB_TT_DATA);
  mrb_undef_class_method(mrb, ref_class, "new");

  mrb_define_method(mrb, ref_class, "target_class", ref_target_class, MRB_ARGS_NONE());
  mrb_define_method(mrb, ref_class, "owner",        ref_owner,        MRB_ARGS_NONE());

  mrb_define_method(mrb, mrb->module_class, "refine",
                    mod_refine, MRB_ARGS_REQ(1) | MRB_ARGS_BLOCK());
  mrb_define_method(mrb, mrb->module_class, "refinements",
                    mod_refinements, MRB_ARGS_NONE());
}
