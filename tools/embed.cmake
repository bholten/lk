# Embed a file as a C89 byte array.
#
#   cmake -DINPUT=<file> -DOUTPUT=<file.c> -DSYMBOL=<name> -P tools/embed.cmake
#
# Emits `const unsigned char <SYMBOL>[]` (the bytes plus a trailing NUL,
# so it is also a C string) and `const size_t <SYMBOL>_len` (the byte
# count, NUL excluded).  A byte array, unsigned so UTF-8 needs no sign
# games, rather than a string literal: C89 only guarantees 509 chars.

if(NOT INPUT OR NOT OUTPUT OR NOT SYMBOL)
  message(FATAL_ERROR "embed.cmake: INPUT, OUTPUT and SYMBOL are required")
endif()

file(READ "${INPUT}" _hex HEX)
string(LENGTH "${_hex}" _hexlen)
math(EXPR _len "${_hexlen} / 2")

# "3b3b3b" -> "0x3b,0x3b,0x3b," then wrapped 12 bytes per line.
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _bytes "${_hex}")
string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){12})" "\\1\n  " _bytes "${_bytes}")

get_filename_component(_name "${INPUT}" NAME)
file(WRITE "${OUTPUT}"
"/* Generated from ${_name} by tools/embed.cmake -- do not edit. */

#include <stddef.h>

const unsigned char ${SYMBOL}[] = {
  ${_bytes}0x00};

const size_t ${SYMBOL}_len = ${_len};
")
