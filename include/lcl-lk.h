#ifndef LCL_LK_H
#define LCL_LK_H

#include <lcl.h>

#ifdef __cplusplus
extern "C" {
#endif

void lcl_register_lk(lcl_interp *interp);

void lcl_lk_set_args(int argc, char **argv);

const char *lcl_lk_dsl_source(size_t *len);
lcl_return_code lcl_lk_load_dsl(lcl_interp *interp);

#ifdef LK_HAVE_WINDOW

void lcl_lk_set_run_deferred(int on);
int lcl_lk_run_pending(void);
int lcl_lk_run_step(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
