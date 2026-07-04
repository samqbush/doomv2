cmake_minimum_required(VERSION 3.16)
project(doom C)

#
# Phase 1 (Modernization) build.
#
# Goal: compile and link the portable 1997 DOOM core 64-bit-clean on a modern
# toolchain, driven by STUB platform files (no X11/OSS/SDL yet). This produces a
# linkable `doom` binary; it does not render or run a real game (that is Phase 2).
#
# The dead X11/OSS/BSD platform sources (i_video.c, i_sound.c, i_net.c,
# i_system.c) are excluded and replaced by no-op/single-player stubs under
# platform/stub/. The originals are kept untouched for the Phase 2/4 rewrite.
#

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(LEGACY_DIR ${CMAKE_SOURCE_DIR}/linuxdoom-1.10)

# Portable core = every .c in linuxdoom-1.10 EXCEPT the dead platform layer.
file(GLOB CORE_SOURCES ${LEGACY_DIR}/*.c)
list(REMOVE_ITEM CORE_SOURCES
  ${LEGACY_DIR}/i_video.c
  ${LEGACY_DIR}/i_sound.c
  ${LEGACY_DIR}/i_net.c
  ${LEGACY_DIR}/i_system.c)

set(STUB_SOURCES
  ${CMAKE_SOURCE_DIR}/platform/stub/i_video_stub.c
  ${CMAKE_SOURCE_DIR}/platform/stub/i_sound_stub.c
  ${CMAKE_SOURCE_DIR}/platform/stub/i_net_stub.c
  ${CMAKE_SOURCE_DIR}/platform/stub/i_system_stub.c)

add_executable(doom ${CORE_SOURCES} ${STUB_SOURCES})

target_include_directories(doom PRIVATE ${LEGACY_DIR})

# Keep the 1997 Linux source shape (pulls <math.h>, <alloca.h>, mouse config,
# and the NORMALUNIX path/IO code) so we do not silently drift behavior.
target_compile_definitions(doom PRIVATE NORMALUNIX LINUX)

# Dark-regime warning gate: surface 64-bit truncation / UB. Not -Werror yet
# (legacy core is noisy; that is Phase 3, platform files only).
target_compile_options(doom PRIVATE
  -Wall -Wextra
  -Wpointer-to-int-cast
  -Wint-to-pointer-cast
  -Wshorten-64-to-32)

target_link_libraries(doom m)
