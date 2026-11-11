#ifndef LK_RESOURCES_H
#define LK_RESOURCES_H

#include <lk.h>

int lk_resources_lookup_names(const lk_resources *rs, lk_resource_ref ref,
                              const char **type_name, const char **debug_name);

#endif
