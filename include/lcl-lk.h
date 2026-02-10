#ifndef LCL_LK_H
#define LCL_LK_H

#include <lcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Register the "lk" namespace into an LCL interpreter.
 *
 * This adds all lk:: procs for building UI trees, managing frames,
 * handling commands/translators/state/focus, and (if linked with
 * lk_sdl) creating SDL windows.
 *
 * Usage:
 *   lcl_interp *interp = lcl_interp_new();
 *   lcl_register_core(interp);
 *   lcl_register_lk(interp);
 */
void lcl_register_lk(lcl_interp *interp);

#ifdef __cplusplus
}
#endif

#endif /* LCL_LK_H */
