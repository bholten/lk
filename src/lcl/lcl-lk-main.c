#include <stdio.h>
#include <stdlib.h>

#include <lcl.h>

#ifdef LK_HAVE_LCL_IO
#include <lcl-io.h>
#endif

#include "lcl-lk.h"
#include "lk-sdl.h"

int main(int argc, char **argv) {
  lcl_interp *interp;
  lcl_value *result = NULL;
  int rc;

  if (argc < 2) {
    fprintf(stderr, "Usage: %s <script.lcl> [args...]\n",
            argc > 0 ? argv[0] : "lcl_lk_main");
    return 1;
  }

  interp = lcl_interp_new();

  if (!interp) {
    fprintf(stderr, "Failed to create interpreter\n");
    return 1;
  }

  lcl_register_core(interp);
  lcl_register_lk(interp);

#ifdef LK_HAVE_LCL_IO
  /* File IO stays a package: core Lcl is IO-free by design and lk is
   * a UI library (docs/weft-surface.md section 3).  The runner is
   * where the io::* procs belong. */
  lcl_register_io(interp);
#endif

#ifdef LK_DSL_PATH
  {
    lcl_value *dsl_result = NULL;
    int dsl_rc = lcl_eval_file(interp, LK_DSL_PATH, &dsl_result);
    if (dsl_result) {
      lcl_ref_dec(dsl_result);
    }
    if (dsl_rc != LCL_RC_OK) {
      fprintf(stderr, "Warning: failed to load DSL prelude (%s)\n",
              LK_DSL_PATH);
    }
  }
#endif

  rc = lcl_eval_file(interp, argv[1], &result);

  if (rc != LCL_RC_OK) {
    const char *file = lcl_interp_error_file(interp);
    int line = lcl_interp_error_line(interp);
    const char *msg = lcl_interp_error_msg(interp);

    fprintf(stderr, "Error");

    if (file) {
      fprintf(stderr, " in %s", file);
    }

    if (line > 0) {
      fprintf(stderr, ":%d", line);
    }

    if (msg) {
      fprintf(stderr, ": %s", msg);
    }

    fprintf(stderr, "\n");
  }

  if (result) {
    lcl_ref_dec(result);
  }
  lcl_interp_free(interp);

  return rc == LCL_RC_OK ? 0 : 1;
}
