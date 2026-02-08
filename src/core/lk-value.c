#include "lk-data.h"

/* Constructors */
lk_value lk_v_none(void) {
  lk_value v;
  v.tag = UIV_NONE;

  return v;
}

lk_value lk_v_bool(int b) {
  lk_value v;
  v.tag = UIV_BOOL;
  v.as.b = (lk_u8)(b ? 1 : 0);

  return v;
}

lk_value lk_v_i32(lk_i32 i) {
  lk_value v;
  v.tag = UIV_I32;
  v.as.i = i;

  return v;
}

lk_value lk_v_str(lk_intern *it, lk_str s) {
  lk_value v;
  v.tag = UIV_STR;
  v.as.str_id = lk_intern_id(it, s);

  return v;
}

lk_value lk_v_cstr(lk_intern *it, const char *cstr) {
  return lk_v_str(it, lk_str_c(cstr));
}
