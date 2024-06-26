#include <string.h>
#include <mruby.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include <mruby/string.h>

/* no argument */
static mrb_value
vf_s_format_0(mrb_state *mrb, mrb_value klass)
{
  mrb_value fmt_str;
  mrb_get_args(mrb, "S", &fmt_str);
  const char *fmt = RSTRING_CSTR(mrb, fmt_str);
  return mrb_format(mrb, fmt);
}

/* c char */
static mrb_value
vf_s_format_c(mrb_state *mrb, mrb_value klass)
{
  mrb_value fmt_str, arg_str;

  mrb_get_args(mrb, "SS", &fmt_str, &arg_str);
  const char *fmt = RSTRING_CSTR(mrb, fmt_str);
  char c = RSTRING_CSTR(mrb, arg_str)[0];

  return mrb_format(mrb, fmt, c);
}

/* d int */
static mrb_value
vf_s_format_d(mrb_state *mrb, mrb_value klass)
{
  mrb_value fmt_str;
  mrb_int i;

  mrb_get_args(mrb, "Si", &fmt_str, &i);
  const char *fmt = RSTRING_CSTR(mrb, fmt_str);
  int d = (int)i;
  return mrb_format(mrb, fmt, d);
}

#ifndef MRB_NO_FLOAT
/* f float */
static mrb_value
vf_s_format_f(mrb_state *mrb, mrb_value klass)
{
  mrb_value fmt_str;
  mrb_float f;

  mrb_get_args(mrb, "Sf", &fmt_str, &f);
  const char *fmt = RSTRING_CSTR(mrb, fmt_str);
  return mrb_format(mrb, fmt, f);
}
#endif

/* i fixnum */
static mrb_value
vf_s_format_i(mrb_state *mrb, mrb_value klass)
{
  mrb_value fmt_str;
  mrb_int i;

  mrb_get_args(mrb, "Si", &fmt_str, &i);
  const char *fmt = RSTRING_CSTR(mrb, fmt_str);
  return mrb_format(mrb, fmt, i);
}

/* l char*, size_t */
static mrb_value
vf_s_format_l(mrb_state *mrb, mrb_value klass)
{
  mrb_value fmt_str, arg_str;
  mrb_int i;

  mrb_get_args(mrb, "SSi", &fmt_str, &arg_str, &i);
  const char *fmt = RSTRING_CSTR(mrb, fmt_str);
  const char *s = RSTRING_PTR(arg_str);
  size_t len = (size_t)i;
  if (len > (size_t)RSTRING_LEN(arg_str)) len = (size_t)RSTRING_LEN(arg_str);

  return mrb_format(mrb, fmt, s, len);
}

/* n symbol */
static mrb_value
vf_s_format_n(mrb_state *mrb, mrb_value klass)
{
  mrb_value fmt_str;
  mrb_sym sym;

  mrb_get_args(mrb, "Sn", &fmt_str, &sym);
  const char *fmt = RSTRING_CSTR(mrb, fmt_str);
  return mrb_format(mrb, fmt, sym);
}

/* s char* */
static mrb_value
vf_s_format_s(mrb_state *mrb, mrb_value klass)
{
  mrb_value fmt_str, arg_str;
  mrb_get_args(mrb, "SS", &fmt_str, &arg_str);
  const char *fmt = RSTRING_CSTR(mrb, fmt_str);
  const char *s = RSTRING_CSTR(mrb, arg_str);
  return mrb_format(mrb, fmt, s);
}

/* C RClass */
static mrb_value
vf_s_format_C(mrb_state *mrb, mrb_value klass)
{
  mrb_value fmt_str, arg_cls;

  mrb_get_args(mrb, "SC", &fmt_str, &arg_cls);
  const char *fmt = RSTRING_CSTR(mrb, fmt_str);
  struct RClass *c = mrb_class_ptr(arg_cls);
  return mrb_format(mrb, fmt, c);
}

/* v value */
static mrb_value
vf_s_format_v(mrb_state *mrb, mrb_value klass)
{
  mrb_value fmt_str, arg_v;

  mrb_get_args(mrb, "So", &fmt_str, &arg_v);
  const char *fmt = RSTRING_CSTR(mrb, fmt_str);
  return mrb_format(mrb, fmt, arg_v);
}

void
mrb_init_test_vformat(mrb_state *mrb)
{
  struct RClass *vf = mrb_define_module(mrb, "TestVFormat");
  mrb_define_class_method(mrb, vf, "z", vf_s_format_0, MRB_ARGS_REQ(1));

#define VF_DEFINE_FORMAT_METHOD(t) VF_DEFINE_FORMAT_METHOD_n(t,2)
#define VF_DEFINE_FORMAT_METHOD_n(t,n) mrb_define_class_method(mrb, vf, #t, vf_s_format_##t, MRB_ARGS_REQ(n));

  VF_DEFINE_FORMAT_METHOD(c);
  VF_DEFINE_FORMAT_METHOD(d);
#ifndef MRB_NO_FLOAT
  VF_DEFINE_FORMAT_METHOD(f);
#endif
  VF_DEFINE_FORMAT_METHOD(i);
  VF_DEFINE_FORMAT_METHOD_n(l,3);
  VF_DEFINE_FORMAT_METHOD(n);
  VF_DEFINE_FORMAT_METHOD(s);
  VF_DEFINE_FORMAT_METHOD(C);
  VF_DEFINE_FORMAT_METHOD(v);
}
