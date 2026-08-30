// Pre-JS for the node build of lcl_lk_main (CMake: Emscripten block).
//
// Two things node lacks that the native binary has for free.  SDL's
// Emscripten platform glue reads a few browser globals at init
// (window.matchMedia for the theme-change hook) even with the dummy
// video driver; an empty `window` lets those `if (window.x)` checks
// fall through.  And Emscripten does not import process.env into the
// program's environment, so SDL_VIDEODRIVER=dummy (the whole point of
// running under node) and LK_DSL_FILE are copied across by hand.
if (typeof window === 'undefined') {
  globalThis.window = {};
}

Module['preRun'] = Module['preRun'] || [];
Module['preRun'].push(function () {
  ['SDL_VIDEODRIVER', 'SDL_RENDER_DRIVER', 'LK_DSL_FILE'].forEach(function (k) {
    if (typeof process !== 'undefined' && process.env[k] !== undefined) {
      ENV[k] = process.env[k];
    }
  });
});
