cmake_minimum_required(VERSION 3.14)

project(lk
  VERSION 0.1.0
  DESCRIPTION "Lk GUI toolkit"
  LANGUAGES C
)

set(CMAKE_C_STANDARD 90)

# include/lk.h is the version of record; keep project(VERSION) in step.
file(STRINGS include/lk.h LK_HEADER_VERSION_LINE
  REGEX "^#define LK_VERSION_STRING \"[0-9.]+\"")
string(REGEX REPLACE ".*\"([0-9.]+)\".*" "\\1" LK_HEADER_VERSION
  "${LK_HEADER_VERSION_LINE}")
if(NOT LK_HEADER_VERSION STREQUAL PROJECT_VERSION)
  message(FATAL_ERROR
    "include/lk.h says LK_VERSION_STRING \"${LK_HEADER_VERSION}\" but "
    "project(VERSION) says ${PROJECT_VERSION} -- update both together")
endif()

include(GNUInstallDirs)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Formatting.  `cmake --build build --target format` rewrites include/,
# src/ and test/ with the repo's .clang-format (identical to Lcl's);
# `format-check` fails when anything would change and is what CI runs.
# LK_FORMAT_CHECK=ON folds the check into every build of this
# directory.  Nothing here touches sources during an ordinary build,
# and none of it exists when lk is embedded (FetchContent) or when
# clang-format is not installed.
option(LK_FORMAT_CHECK "Fail the build when sources are not clang-formatted" OFF)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  find_program(LK_CLANG_FORMAT_EXE clang-format)

  if(LK_CLANG_FORMAT_EXE)
    file(GLOB_RECURSE LK_FORMAT_SOURCES CONFIGURE_DEPENDS
      ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
      ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
      ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
      ${CMAKE_CURRENT_SOURCE_DIR}/test/*.c
      ${CMAKE_CURRENT_SOURCE_DIR}/test/*.h)

    add_custom_target(format
      COMMAND ${LK_CLANG_FORMAT_EXE} -i ${LK_FORMAT_SOURCES}
      COMMENT "clang-format -i include/ src/ test/")

    if(LK_FORMAT_CHECK)
      set(_lk_format_check_all ALL)
    else()
      set(_lk_format_check_all)
    endif()

    add_custom_target(format-check ${_lk_format_check_all}
      COMMAND ${LK_CLANG_FORMAT_EXE} --dry-run --Werror ${LK_FORMAT_SOURCES}
      COMMENT "clang-format --dry-run --Werror include/ src/ test/")
  elseif(LK_FORMAT_CHECK)
    message(FATAL_ERROR "LK_FORMAT_CHECK=ON but clang-format was not found")
  endif()
endif()

option(LK_ENABLE_CLANG_TIDY "Run clang-tidy alongside compilation" OFF)

if(LK_ENABLE_CLANG_TIDY)
  find_program(LK_CLANG_TIDY_EXE clang-tidy)
  if(NOT LK_CLANG_TIDY_EXE)
    message(FATAL_ERROR "LK_ENABLE_CLANG_TIDY=ON but clang-tidy was not found")
  endif()
  # Applied per-target below rather than via CMAKE_C_CLANG_TIDY: the
  # repo-root .clang-tidy pins -std=c89, so only the C89 targets are
  # tidied (SDL C11 targets and the fetched Lcl tree are skipped).
endif()

set(LK_SOURCES
  src/core/lk-alloc.c
  src/core/lk-intern.c
  src/core/lk-str.c
  src/core/lk-value.c
  src/core/lk-tree.c
  src/core/lk-ui.c
  src/core/lk-state.c

  src/core/lk-layout.c
  src/core/lk-utf8.c
  src/core/lk-text-stub.c
  src/core/lk-render.c
  src/core/lk-event.c
  src/core/lk-style.c
  src/core/lk-widget.c
  src/core/lk-text-input.c
  src/core/lk-scroll.c
  src/core/lk-dropdown.c
  src/core/lk-split.c
  src/core/lk-check.c
  src/core/lk-slider.c
  src/core/lk-tabs.c
  src/core/lk-grid.c
  src/core/lk-image.c
  src/core/lk-canvas.c
  src/core/lk-spans.c
  src/core/lk-menu.c
  src/core/lk-list.c
  src/core/lk-styled-text.c
  src/core/lk-overlay.c
  src/core/lk-tooltip.c
  src/core/lk-command.c
  src/core/lk-api.c
  src/core/lk-resources.c

  src/editor/lk-document.c
  src/editor/lk-edit-history.c
  src/editor/lk-editor.c
  src/editor/lk-editor-widget.c
  src/editor/lk-annot-store.c
)

add_library(lk_core STATIC ${LK_SOURCES})

# Shared library for language bindings (Python, etc.)
add_library(lk_shared SHARED ${LK_SOURCES})

target_include_directories(lk_shared
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(lk_shared PRIVATE -Wall -Wextra -pedantic)

set_target_properties(lk_shared PROPERTIES OUTPUT_NAME "lk")

target_include_directories(lk_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(lk_core PRIVATE -Wall -Wextra -pedantic)

add_executable(lk_test test/lk-test.c test/lk-document-test.c
  test/lk-resource-test.c test/lk-editor-test.c test/lk-annot-test.c
  test/lk-present-test.c test/lk-forms-test.c test/lk-image-test.c
  test/lk-canvas-test.c test/lk-focus-test.c
  test/lk-styled-text-test.c test/lk-menu-test.c test/lk-list-test.c)
target_link_libraries(lk_test PRIVATE lk_core)
target_include_directories(lk_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_options(lk_test PRIVATE -Wall -Wextra -pedantic)

if(LK_ENABLE_CLANG_TIDY)
  set_target_properties(lk_core lk_shared lk_test PROPERTIES
    C_CLANG_TIDY "${LK_CLANG_TIDY_EXE}")
endif()

find_package(SDL3 QUIET)
find_package(SDL3_ttf QUIET)
find_package(SDL3_image QUIET)

# Single-binary builds (docs/release.md): link the SDL libraries'
# -static imported targets.  Never the default -- lk stays agnostic
# about how it is linked -- and all-or-nothing: SDL3_ttf/SDL3_image's
# own CMake refuses to pair a static SDL3 with their shared builds, so
# if any static target is missing every SDL library falls back to its
# shared target, with a warning naming the missing one.
option(LK_STATIC_SDL "Link SDL3, SDL3_ttf and SDL3_image statically (needs all their -static targets)" OFF)

set(LK_SDL_USE_STATIC OFF)
if(LK_STATIC_SDL)
  set(LK_SDL_USE_STATIC ON)
  foreach(_t SDL3::SDL3-static SDL3_ttf::SDL3_ttf-static)
    if(NOT TARGET ${_t})
      message(WARNING "LK_STATIC_SDL: ${_t} not available -- linking SDL shared")
      set(LK_SDL_USE_STATIC OFF)
    endif()
  endforeach()
  if(SDL3_image_FOUND AND NOT TARGET SDL3_image::SDL3_image-static)
    message(WARNING "LK_STATIC_SDL: SDL3_image::SDL3_image-static not available -- linking SDL shared")
    set(LK_SDL_USE_STATIC OFF)
  endif()
endif()

function(lk_pick_sdl_target out_var shared_target static_target)
  if(LK_SDL_USE_STATIC)
    set(${out_var} ${static_target} PARENT_SCOPE)
  else()
    set(${out_var} ${shared_target} PARENT_SCOPE)
  endif()
endfunction()

set(LK_HAVE_SDL OFF)
set(LK_HAVE_SDL_IMAGE OFF)

if(SDL3_FOUND AND SDL3_ttf_FOUND)
  message(STATUS "SDL3 found -- building lk_sdl and lk_demo")
  set(LK_HAVE_SDL ON)

  lk_pick_sdl_target(LK_SDL3_TARGET SDL3::SDL3 SDL3::SDL3-static)
  lk_pick_sdl_target(LK_SDL3_TTF_TARGET SDL3_ttf::SDL3_ttf SDL3_ttf::SDL3_ttf-static)

  add_library(lk_sdl STATIC src/sdl/lk-sdl.c)
  target_link_libraries(lk_sdl PUBLIC lk_core PRIVATE ${LK_SDL3_TARGET} ${LK_SDL3_TTF_TARGET})

  # Optional: SDL3_image decodes PNG (and friends) for window icons.
  # Without it, lk_window_set_icon* still accepts BMP via SDL core.
  if(SDL3_image_FOUND)
    message(STATUS "SDL3_image found -- PNG window icons enabled")
    set(LK_HAVE_SDL_IMAGE ON)
    lk_pick_sdl_target(LK_SDL3_IMAGE_TARGET SDL3_image::SDL3_image SDL3_image::SDL3_image-static)
    target_compile_definitions(lk_sdl PRIVATE LK_HAVE_SDL_IMAGE)
    target_link_libraries(lk_sdl PRIVATE ${LK_SDL3_IMAGE_TARGET})
  else()
    message(STATUS "SDL3_image not found -- window icons limited to BMP/raw RGBA")
  endif()
  target_include_directories(lk_sdl
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/sdl>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_compile_options(lk_sdl PRIVATE -Wall -Wextra)
  set_target_properties(lk_sdl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED OFF
    C_EXTENSIONS OFF)

  add_executable(lk_demo src/sdl/demo.c)
  target_link_libraries(lk_demo PRIVATE lk_sdl)
  target_include_directories(lk_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_compile_options(lk_demo PRIVATE -Wall -Wextra)
  set_target_properties(lk_demo PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED OFF
    C_EXTENSIONS OFF)
else()
  message(STATUS "SDL3 not found -- skipping lk_sdl and lk_demo")
endif()

# ---- install / export (docs/release.md) ----
#
# Installs the C library (static + shared), the SDL backend when built,
# and the public headers, with an lk:: CMake package so consumers can
# `find_package(lk)` and link lk::lk_core / lk::lk_sdl.  The Lcl
# bindings are deliberately not installed: scripting hosts embed them
# through FetchContent (the way lk embeds lcl) so the lcl pin travels
# with them.
include(CMakePackageConfigHelpers)

install(TARGETS lk_core lk_shared EXPORT lkTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES include/lk.h include/lk-document.h include/lk-editor.h
  include/lk-annot-store.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(LK_HAVE_SDL)
  install(TARGETS lk_sdl EXPORT lkTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
  install(FILES src/sdl/lk-sdl.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()

install(EXPORT lkTargets NAMESPACE lk::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lk)
configure_package_config_file(cmake/lkConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/lkConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lk)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/lkConfigVersion.cmake
  COMPATIBILITY SameMajorVersion)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/lkConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/lkConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lk)

# ---- Lcl scripting bindings ----
option(LK_BUILD_LCL "Build Lcl scripting bindings" OFF)

if(LK_BUILD_LCL)
  option(LK_LCL_IO "Register the Lcl IO package (Io::*) in lcl_lk_main" ON)
  option(LK_LCL_REGEX "Register the Lcl regex package (Regex::*) in lcl_lk_main" ON)
  option(LK_LCL_RANDOM "Register the Lcl random package (Xoshiro::*) in lcl_lk_main" ON)

  # Lcl's build options must be forced into the cache BEFORE
  # FetchContent_MakeAvailable -- it configures the subproject
  # immediately, so anything set after it is ignored on first run.
  set(LCL_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(LCL_BUILD_STATIC ON CACHE BOOL "" FORCE)
  set(LCL_BUILD_CLI OFF CACHE BOOL "" FORCE)

  if(LK_LCL_IO)
    set(LCL_BUILD_IO ON CACHE BOOL "" FORCE)
  else()
    set(LCL_BUILD_IO OFF CACHE BOOL "" FORCE)
  endif()

  if(LK_LCL_REGEX)
    set(LCL_BUILD_REGEX ON CACHE BOOL "" FORCE)
  else()
    set(LCL_BUILD_REGEX OFF CACHE BOOL "" FORCE)
  endif()

  if(LK_LCL_RANDOM)
    set(LCL_BUILD_RANDOM ON CACHE BOOL "" FORCE)
  else()
    set(LCL_BUILD_RANDOM OFF CACHE BOOL "" FORCE)
  endif()

  if(EMSCRIPTEN)
    # Lcl's own browser host (lcl.mjs, Js::, Dom) is a separate product;
    # lk wants the interpreter and the portable packages only.
    set(LCL_BUILD_WASM OFF CACHE BOOL "" FORCE)
    set(LCL_BUILD_JS OFF CACHE BOOL "" FORCE)
    set(LCL_BUILD_DOM_LIB OFF CACHE BOOL "" FORCE)
  endif()

  include(FetchContent)
  FetchContent_Declare(
    lcl
    GIT_REPOSITORY https://github.com/bholten/lcl.git
    GIT_TAG        846a9b10b3bc5567f5b09bf3abbe346ba567adb6
  )
  FetchContent_MakeAvailable(lcl)

  # The Layer-2 DSL travels inside the library: lib/lk-dsl.lcl is
  # embedded as a byte array (tools/embed.cmake) and evaluated by
  # lcl_lk_load_dsl, so a runner needs no path to the source tree --
  # the first thing an Emscripten build has no use for.  Regenerated
  # whenever the .lcl changes.
  set(_lk_dsl_prelude_c ${CMAKE_CURRENT_BINARY_DIR}/lk-dsl-prelude.c)

  add_custom_command(
    OUTPUT ${_lk_dsl_prelude_c}
    COMMAND ${CMAKE_COMMAND}
      -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/lib/lk-dsl.lcl
      -DOUTPUT=${_lk_dsl_prelude_c}
      -DSYMBOL=lcl_lk_dsl_prelude
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed.cmake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lib/lk-dsl.lcl
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed.cmake
    COMMENT "Embedding lib/lk-dsl.lcl")

  add_library(lcl_lk STATIC src/lcl/lcl-lk.c ${_lk_dsl_prelude_c})

  target_include_directories(lcl_lk
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

  target_link_libraries(lcl_lk PUBLIC lk_core lcl::lcl)
  target_compile_options(lcl_lk PRIVATE -Wall -Wextra -pedantic)

  if(SDL3_FOUND AND SDL3_ttf_FOUND)
    target_compile_definitions(lcl_lk PUBLIC LK_HAVE_SDL)
    target_link_libraries(lcl_lk PUBLIC lk_sdl)

    add_executable(lcl_lk_main src/lcl/lcl-lk-main.c)

    target_link_libraries(lcl_lk_main PRIVATE lcl_lk)

    target_include_directories(lcl_lk_main PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${CMAKE_CURRENT_SOURCE_DIR}/src/sdl)

    if(LK_LCL_IO)
      target_compile_definitions(lcl_lk_main PRIVATE LK_HAVE_LCL_IO)
      target_link_libraries(lcl_lk_main PRIVATE lcl_io)
    endif()

    if(LK_LCL_REGEX)
      target_compile_definitions(lcl_lk_main PRIVATE LK_HAVE_LCL_REGEX)
      target_link_libraries(lcl_lk_main PRIVATE lcl_regex)
    endif()

    if(LK_LCL_RANDOM)
      target_compile_definitions(lcl_lk_main PRIVATE LK_HAVE_LCL_RANDOM)
      target_link_libraries(lcl_lk_main PRIVATE lcl_random)
    endif()

    target_compile_options(lcl_lk_main PRIVATE -Wall -Wextra)

    set_target_properties(lcl_lk_main PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED OFF
      C_EXTENSIONS OFF)
  endif()

  add_executable(lcl_lk_test test/lcl-lk-test.c)

  target_link_libraries(lcl_lk_test PRIVATE lcl_lk)

  target_include_directories(lcl_lk_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

  target_compile_definitions(lcl_lk_test PRIVATE
    TEST_DSL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/lib/lk-dsl.lcl"
    TEST_DOC_LIB_PATH="${lcl_SOURCE_DIR}/lib/doc/src/Doc.lcl"
    TEST_LK_DOCS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/docs/lk.lcl")
  target_compile_options(lcl_lk_test PRIVATE -Wall -Wextra -pedantic)

  if(LK_ENABLE_CLANG_TIDY)
    set_target_properties(lcl_lk lcl_lk_test PROPERTIES
      C_CLANG_TIDY "${LK_CLANG_TIDY_EXE}")
  endif()

endif()

# ---- Emscripten -------------------------------------------------------
#
#   tools/emcc-deps.sh                      # SDL3 + SDL3_ttf -> build-emcc/sdl-prefix
#   emcmake cmake -S . -B build-emcc -DCMAKE_BUILD_TYPE=Release -DLK_BUILD_LCL=ON \
#       -DCMAKE_PREFIX_PATH="$PWD/build-emcc/sdl-prefix" \
#       -DCMAKE_FIND_ROOT_PATH="$PWD/build-emcc/sdl-prefix"
#   cmake --build build-emcc
#   node build-emcc/lk_test.js && node build-emcc/lcl_lk_test.js
#
# (Both PREFIX_PATH and FIND_ROOT_PATH: the Emscripten toolchain
# cross-compiles, so find_package only looks inside its root paths.
# Without the prefix SDL3 is simply not found and the core, the Lcl
# bindings and both test programs still build.)
#
# Executables come out as <name>.js + <name>.wasm.  The test programs
# run under node against the host filesystem (NODERAWFS), the same
# way they run natively.  lcl_lk_main links but wants a browser: SDL's
# Emscripten video driver draws to a canvas, and a page cannot block
# in lk_window_run -- the browser runner drives lk_window_step from
# the frame callback instead.  The flags mirror Lcl's: lcl_int is a
# wasm i64 (WASM_BIGINT), and the interpreter's recursion wants the
# native-sized stack rather than Emscripten's 64 KiB default.
if(EMSCRIPTEN)
  set(_lk_wasm_link -sALLOW_MEMORY_GROWTH=1 -sWASM_BIGINT=1 -sSTACK_SIZE=8388608)

  foreach(_t lk_test lcl_lk_test)
    if(TARGET ${_t})
      target_link_options(${_t} PRIVATE ${_lk_wasm_link} -sNODERAWFS=1
        -sEXIT_RUNTIME=1)
    endif()
  endforeach()

  foreach(_t lk_demo lcl_lk_main)
    if(TARGET ${_t})
      target_link_options(${_t} PRIVATE ${_lk_wasm_link})
    endif()
  endforeach()
endif()
