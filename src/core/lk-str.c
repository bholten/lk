#include <stdlib.h>
#include <string.h>

#include <lk.h>

lk_str lk_str_c(const char *cstr) {
  lk_str s;
  s.ptr = cstr;
  s.len = (lk_u32)(cstr ? strlen(cstr) : 0);

  return s;
}

int lk_str_cmp(lk_str a, lk_str b) {
  if (a.len != b.len) {
    return 0;
  }

  if (a.len == 0) {
    return 1;
  }

  return memcmp(a.ptr, b.ptr, a.len) == 0;
}
