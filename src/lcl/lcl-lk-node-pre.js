








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
