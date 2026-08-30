/*
 * lcl-lk-wasm.c -- the browser runner.
 *
 * The Emscripten counterpart of lcl-lk-main.c.  Same interpreter, same
 * packages, same DSL prelude; two things differ because a page cannot
 * block.  The script is not an argv path but /app.lcl, preloaded into
 * the virtual filesystem at link time (CMake: LK_WASM_APP, along with
 * the font at LK_WASM_FONT), and the window loop is not run by the
 * script: Lk::window_run is deferred (include/lcl-lk.h), so the
 * script's `app` returns at once, and the browser's frame callback
 * drives lcl_lk_run_step until the window stops.
 */
#include <stdio.h>
#include <stdlib.h>

#include <emscripten.h>
#include <lcl.h>

#ifdef LK_HAVE_LCL_IO
#include <lcl-io.h>
#endif

#ifdef LK_HAVE_LCL_REGEX
#include <lcl-regex.h>
#endif

#ifdef LK_HAVE_LCL_RANDOM
#include <lcl-random.h>
#endif

#include "lcl-lk.h"

#define LK_WASM_SCRIPT "/app.lcl"

static lcl_interp *g_interp;

static void report(lcl_interp *interp, const char *what) {
  const char *file = lcl_interp_error_file(interp);
  int line = lcl_interp_error_line(interp);
  const char *msg = lcl_interp_error_msg(interp);

  fprintf(stderr, "%s", what);

  if (file) {
    fprintf(stderr, " in %s", file);
  }

  if (line > 0) {
    fprintf(stderr, ":%d", line);
  }

  fprintf(stderr, ": %s\n", msg ? msg : "(no message)");
}

static void tick(void *ud) {
  (void)ud;

  if (!lcl_lk_run_step()) {
    emscripten_cancel_main_loop();
    lcl_interp_free(g_interp);
    g_interp = NULL;
    fprintf(stderr, "lk: window closed\n");
  }
}

int main(void) {
  lcl_value *result = NULL;
  int rc;

  g_interp = lcl_interp_new();

  if (!g_interp) {
    fprintf(stderr, "Failed to create interpreter\n");
    return 1;
  }

  lcl_register_core(g_interp);
  lcl_register_lk(g_interp);
  lcl_lk_set_args(0, NULL);

#ifdef LK_HAVE_LCL_IO
  lcl_register_io(g_interp); /* MEMFS: whatever the link preloaded */
#endif

#ifdef LK_HAVE_LCL_REGEX
  lcl_register_regex(g_interp);
#endif

#ifdef LK_HAVE_LCL_RANDOM
  lcl_register_random(g_interp);
#endif

  if (lcl_lk_load_dsl(g_interp) != LCL_RC_OK) {
    report(g_interp, "Warning: failed to load DSL prelude");
  }

  lcl_lk_set_run_deferred(1);

  rc = lcl_eval_file(g_interp, LK_WASM_SCRIPT, &result);

  if (result) {
    lcl_ref_dec(result);
  }

  if (rc != LCL_RC_OK) {
    report(g_interp, "Error");
  }

  if (!lcl_lk_run_pending()) {
    lcl_interp_free(g_interp);
    return rc == LCL_RC_OK ? 0 : 1;
  }

  /* The first frame right now, synchronously: the canvas is painted
   * before the page yields, rather than after the first animation
   * frame the browser grants (a background tab may grant none). */
  if (!lcl_lk_run_step()) {
    fprintf(stderr, "lk: window closed\n");
    lcl_interp_free(g_interp);
    return 0;
  }

  fprintf(stderr, "lk: first frame rendered; main loop on\n");

  /* 0 fps = requestAnimationFrame; 1 = never return from main (the
   * call unwinds through the JS event loop), so nothing below it may
   * hold state the callback needs. */
  emscripten_set_main_loop_arg(tick, NULL, 0, 1);

  return 0;
}
