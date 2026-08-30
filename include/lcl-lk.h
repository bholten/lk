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

/* The Layer-2 DSL (lib/lk-dsl.lcl), compiled into the library.
 * lcl_lk_dsl_source returns the text (NUL-terminated; *len is the
 * byte count, NUL excluded; len may be NULL).  lcl_lk_load_dsl
 * evaluates it into `interp` under the name "lk-dsl.lcl": the same
 * result as evaluating the file, with no path involved.  Returns
 * lcl_eval_*'s code; on LCL_RC_ERR the interp error is set. */
const char *lcl_lk_dsl_source(size_t *len);
lcl_return_code lcl_lk_load_dsl(lcl_interp *interp);

#ifdef __cplusplus
}
#endif

#endif /* LCL_LK_H */
