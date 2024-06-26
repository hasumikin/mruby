/*
** enum.c - Enumerable module
**
** See Copyright Notice in mruby.h
*/

#include <mruby.h>
#include <mruby/proc.h>
#include <mruby/presym.h>

/* internal method `__update_hash(oldhash, index, itemhash)` */
static mrb_value
enum_update_hash(mrb_state *mrb, mrb_value self)
{
  mrb_int hash;
  mrb_int index;
  mrb_int hv;

  mrb_get_args(mrb, "iii", &hash, &index, &hv);
  hash ^= ((uint32_t)hv << (index % 16));

  return mrb_int_value(mrb, hash);
}

void
mrb_init_enumerable(mrb_state *mrb)
{
  struct RClass *enumerable = mrb_define_module_id(mrb, MRB_SYM(Enumerable));  /* 15.3.2 */
  mrb_define_module_function_id(mrb, enumerable, MRB_SYM(__update_hash), enum_update_hash, MRB_ARGS_REQ(3));
}
