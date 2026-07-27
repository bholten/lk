#ifndef LK_RESOURCES_H
#define LK_RESOURCES_H

#include <lk.h>

/* Internal (lk-tree.c dump): resolve a ref to its type name and debug
 * name without a type descriptor.  Returns 1 when the ref is live and
 * current (the strings stay valid until the next register/release on
 * rs, and are never NULL); 0 for a NULL table, null ref, out-of-range
 * id, or stale generation. */
int lk_resources_lookup_names(const lk_resources *rs, lk_resource_ref ref,
                              const char **type_name,
                              const char **debug_name);

#endif /* LK_RESOURCES_H */
