#include <lk.h>

lk_value lk_v_none(void) {
  lk_value v;
  v.tag = UIV_NONE;
  /* Zero the union too so reading .as.i on a NONE value is defined
   * (0) instead of uninitialized memory. */
  v.as.i = 0;

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

lk_value lk_v_resource(lk_resource_ref ref) {
  lk_value v;
  v.tag = UIV_RESOURCE;
  v.as.res.id = ref.id;
  v.as.res.gen = ref.generation;

  return v;
}

lk_resource_ref lk_v_resource_ref(lk_value v) {
  lk_resource_ref ref;
  ref.id = 0;
  ref.generation = 0;

  if (v.tag == UIV_RESOURCE) {
    ref.id = v.as.res.id;
    ref.generation = v.as.res.gen;
  }

  return ref;
}
