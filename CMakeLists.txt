cmake_minimum_required(VERSION 3.14)

project(lk
  VERSION 0.1.0
  DESCRIPTION "Dagwood shell orchestration"
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
  src/core/lk-ht.c
)

add_library(lk_core STATIC ${LK_SOURCES})
target_include_directories(lk_core PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_options(lk_core PRIVATE -Wall -Wextra -pedantic)

add_executable(lk_test src/core/lk-test.c)
target_link_libraries(lk_test PRIVATE lk_core)
target_include_directories(lk_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_options(lk_test PRIVATE -Wall -Wextra -pedantic)
