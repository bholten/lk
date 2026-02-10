cmake_minimum_required(VERSION 3.14)

project(lk
  VERSION 0.1.0
  DESCRIPTION "Lk GUI toolkit"
  LANGUAGES C
)

set(CMAKE_C_STANDARD 90)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(LK_SOURCES
  src/core/lk-alloc.c
  src/core/lk-intern.c
  src/core/lk-str.c
  src/core/lk-value.c
  src/core/lk-tree.c
  src/core/lk-ui.c
  src/core/lk-state.c
  src/core/lk-ht.c
  src/core/lk-layout.c
  src/core/lk-render.c
  src/core/lk-event.c
  src/core/lk-widget.c
  src/core/lk-command.c
  src/core/lk-api.c
)

add_library(lk_core STATIC ${LK_SOURCES})

# Shared library for language bindings (Python, etc.)
add_library(lk_shared SHARED ${LK_SOURCES})
target_include_directories(lk_shared
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(lk_shared PRIVATE -Wall -Wextra -pedantic)
set_target_properties(lk_shared PROPERTIES OUTPUT_NAME "lk")

target_include_directories(lk_core
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(lk_core PRIVATE -Wall -Wextra -pedantic)

add_executable(lk_test test/lk-test.c)
target_link_libraries(lk_test PRIVATE lk_core)
target_include_directories(lk_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_options(lk_test PRIVATE -Wall -Wextra -pedantic)

find_package(SDL3 QUIET)
find_package(SDL3_ttf QUIET)

if(SDL3_FOUND AND SDL3_ttf_FOUND)
  message(STATUS "SDL3 found -- building lk_sdl and lk_demo")

  add_library(lk_sdl STATIC src/sdl/lk-sdl.c)
  target_link_libraries(lk_sdl PUBLIC lk_core PRIVATE SDL3::SDL3 SDL3_ttf::SDL3_ttf)
  target_include_directories(lk_sdl
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/sdl
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
