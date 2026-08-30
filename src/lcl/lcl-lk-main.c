#include <stdio.h>
#include <stdlib.h>

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
  lcl_lk_set_args(argc - 2, argv + 2); /* everything after the script */

#ifdef LK_HAVE_LCL_IO
  /* File IO stays a package: core Lcl is IO-free by design and lk is
   * a UI library (docs/weft-surface.md section 3).  The runner is
   * where the Io::* procs belong. */
  lcl_register_io(interp);
#endif

#ifdef LK_HAVE_LCL_REGEX
  lcl_register_regex(interp);
#endif

#ifdef LK_HAVE_LCL_RANDOM
  /* xoshiro128** streams (Xoshiro::new/int/float/shuffle) -- runner
   * only, like Io:: and Regex::; scripts probe with catch. */
  lcl_register_random(interp);
#endif

  /* The DSL prelude ships inside lcl_lk.  LK_DSL_FILE=<path> evaluates
   * a file instead -- editing lib/lk-dsl.lcl without a rebuild. */
  {
    const char *dsl_file = getenv("LK_DSL_FILE");
    int dsl_rc;

    if (dsl_file && dsl_file[0]) {
      lcl_value *dsl_result = NULL;

      dsl_rc = lcl_eval_file(interp, dsl_file, &dsl_result);

      if (dsl_result) {
        lcl_ref_dec(dsl_result);
      }
    } else {
      dsl_file = "embedded lib/lk-dsl.lcl";
      dsl_rc = lcl_lk_load_dsl(interp);
    }

    if (dsl_rc != LCL_RC_OK) {
      const char *msg = lcl_interp_error_msg(interp);

      fprintf(stderr, "Warning: failed to load DSL prelude (%s): %s\n",
              dsl_file, msg ? msg : "(no message)");
    }
  }

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
