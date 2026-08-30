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

#ifdef LK_HAVE_SDL
/* Hosts that cannot block.  A browser page has no way to sit inside
 * Lk::window_run, so such a host turns deferral on before evaluating
 * the script: Lk::window_run then records the window and the view
 * proc and returns "" at once, and an Lk::window_destroy on that
 * window is held back until the run ends.  The host drives the
 * recorded run from its own frame callback with lcl_lk_run_step, one
 * frame per call, until it returns 0 -- the window stopped (or
 * nothing was recorded) -- at which point the held references are
 * released and the held destroy performed.  One run at a time.
 * Scripts see one difference: what they do after Lk::window_run
 * returns happens before the first frame, not after the last. */
void lcl_lk_set_run_deferred(int on);
int lcl_lk_run_pending(void);
int lcl_lk_run_step(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* LCL_LK_H */
