#ifndef LCL_LK_H
#define LCL_LK_H

#include <lcl.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Register the "Lk" namespace into an LCL interpreter.
 *
 * This adds all Lk:: procs for building UI trees, managing frames,
 * handling commands/translators/state/focus, and (if linked with
 * lk_sdl) creating SDL windows.
 *
 * Usage:
 *   lcl_interp *interp = lcl_interp_new();
 *   lcl_register_core(interp);
 *   lcl_register_lk(interp);
 */
void lcl_register_lk(lcl_interp *interp);

/* Script arguments for Lk::args: the strings AFTER the script path
 * (argc may be 0).  Borrowed, not copied -- the caller keeps them
 * alive for the interpreter's lifetime (main's argv does).  Call
 * before or after lcl_register_lk; Lk::args reads the latest. */
void lcl_lk_set_args(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* LCL_LK_H */
