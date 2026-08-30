#!/bin/sh
# Build SDL3 and SDL3_ttf for Emscripten into a prefix that lk's
# emcmake configure can find:
#
#   tools/emcc-deps.sh [PREFIX] [WORKDIR]
#   emcmake cmake -S . -B build-emcc -DCMAKE_BUILD_TYPE=Release -DLK_BUILD_LCL=ON \
#       -DCMAKE_PREFIX_PATH="$PWD/build-emcc/sdl-prefix" \
#       -DCMAKE_FIND_ROOT_PATH="$PWD/build-emcc/sdl-prefix"
#
# Emscripten's ports carry SDL2 only, so SDL3 comes from source, the
# same tags the CI `sdl` job builds natively.  FreeType is SDL_ttf's
# vendored copy (a git submodule); HarfBuzz and PlutoSVG are off --
# lk shapes nothing and draws no colour emoji.  Static only: there
# are no shared libraries in a wasm module.  Re-running skips the
# clones and lets ninja decide what is stale.
set -e

PREFIX=${1:-$PWD/build-emcc/sdl-prefix}
WORK=${2:-$PWD/build-emcc/deps}
SDL_TAG=${SDL_TAG:-release-3.2.20}
SDL_TTF_TAG=${SDL_TTF_TAG:-release-3.2.2}

mkdir -p "$WORK"

if [ ! -d "$WORK/sdl" ]; then
  git clone --depth 1 --branch "$SDL_TAG" https://github.com/libsdl-org/SDL.git "$WORK/sdl"
fi

emcmake cmake -S "$WORK/sdl" -B "$WORK/sdl/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
  -DSDL_INSTALL_DOCS=OFF
cmake --build "$WORK/sdl/build"
cmake --install "$WORK/sdl/build"

if [ ! -d "$WORK/sdl_ttf" ]; then
  git clone --depth 1 --branch "$SDL_TTF_TAG" --recurse-submodules \
    --shallow-submodules https://github.com/libsdl-org/SDL_ttf.git "$WORK/sdl_ttf"
fi

emcmake cmake -S "$WORK/sdl_ttf" -B "$WORK/sdl_ttf/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PREFIX" \
  -DCMAKE_FIND_ROOT_PATH="$PREFIX" \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" -DBUILD_SHARED_LIBS=OFF \
  -DSDLTTF_VENDORED=ON -DSDLTTF_HARFBUZZ=OFF -DSDLTTF_PLUTOSVG=OFF \
  -DSDLTTF_SAMPLES=OFF -DSDLTTF_INSTALL_DOCS=OFF
cmake --build "$WORK/sdl_ttf/build"
cmake --install "$WORK/sdl_ttf/build"

echo "emcc-deps: SDL3 + SDL3_ttf installed under $PREFIX"
