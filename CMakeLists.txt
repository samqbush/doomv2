cmake_minimum_required(VERSION 3.16)
project(doom C)

#
# DOOM (Modernization) build.
#
# Phase 1 got the portable 1997 core linking 64-bit-clean behind no-op stubs.
# Phase 2 swaps in a real SDL2 platform layer (video blit + timing/input) so the
# engine boots, renders, and replays a demo to a frozen parity checksum.
#
# The dead X11/OSS/BSD platform sources (i_video.c, i_sound.c, i_net.c,
# i_system.c) are excluded from the core glob. Video, system, and (Phase 4)
# in-process audio are provided by platform/sdl/; net remains a no-op stub under
# platform/stub/ until Phase 5. The originals are kept untouched for reference.
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
  ${CMAKE_SOURCE_DIR}/platform/stub/i_net_stub.c)

# Phase 2 SDL2 platform layer: real video (blit) + system (timing/input).
# Phase 4 adds in-process SDL audio (i_sound). Net stays stubbed until Phase 5.
find_package(SDL2 REQUIRED)
set(SDL_SOURCES
  ${CMAKE_SOURCE_DIR}/platform/sdl/i_video_sdl.c
  ${CMAKE_SOURCE_DIR}/platform/sdl/i_system_sdl.c
  ${CMAKE_SOURCE_DIR}/platform/sdl/i_sound_sdl.c)

# Phase 2 demo-parity oracle instrumentation (inert unless -checkdemo).
set(PARITY_SOURCES
  ${CMAKE_SOURCE_DIR}/tests/parity/g_parity.c)

add_executable(doom ${CORE_SOURCES} ${STUB_SOURCES} ${SDL_SOURCES} ${PARITY_SOURCES})

target_include_directories(doom PRIVATE ${LEGACY_DIR} ${CMAKE_SOURCE_DIR}/tests/parity)

# Keep the 1997 Linux source shape (pulls <math.h>, <alloca.h>, mouse config,
# and the NORMALUNIX path/IO code) so we do not silently drift behavior.
target_compile_definitions(doom PRIVATE NORMALUNIX LINUX)

# Dark-regime warning gate: surface 64-bit truncation / UB. Not -Werror yet
# (legacy core is noisy; that is Phase 3, platform files only).
#
# Portable flags apply to every compiler. -Wshorten-64-to-32 is Clang/AppleClang
# only (GCC on the Linux CI runner rejects it), so it is gated by a feature probe
# rather than hard-coded -- keeps the 64-bit truncation gate on macOS without
# breaking the first Linux build.
include(CheckCCompilerFlag)
target_compile_options(doom PRIVATE
  -Wall -Wextra
  -Wpointer-to-int-cast
  -Wint-to-pointer-cast)

check_c_compiler_flag("-Wshorten-64-to-32" HAVE_WSHORTEN_64_TO_32)
if(HAVE_WSHORTEN_64_TO_32)
  target_compile_options(doom PRIVATE -Wshorten-64-to-32)
endif()

# Phase 3: -Werror on the PLATFORM files only (SDL layer + active stubs). These
# are new, modern code we own -- hold them to a zero-warning bar. The legacy
# core stays warn-only (still noisy; promoting it is deferred). Applied per
# source file so the core glob is unaffected.
set_source_files_properties(${SDL_SOURCES} ${STUB_SOURCES}
  PROPERTIES COMPILE_OPTIONS "-Werror")

target_link_libraries(doom m SDL2::SDL2)

#
# Phase 2 demo-parity oracle tests (see docs/oracle/ORACLE_STRATEGY.md).
#
# demo-regen: gen_demo.py reproduces the committed parity.lmp byte-exact.
# demo-parity: replay the scripted v110 demo headless under -checkdemo and
#              assert the frozen canonical world-state checksum.
# frame-smoke: assert the byte-exact indexed screens[0] frame hashes (software
#              drawer) against the frozen frame master.
# palette-lut: validate I_SetPalette's index->ARGB8888 conversion + gamma.
#
enable_testing()

add_test(NAME demo-regen
  COMMAND ${CMAKE_SOURCE_DIR}/tests/harness/demo_regen.sh ${CMAKE_SOURCE_DIR})

add_test(NAME demo-parity
  COMMAND ${CMAKE_SOURCE_DIR}/tests/harness/demo_parity.sh
          $<TARGET_FILE:doom> ${CMAKE_SOURCE_DIR})

add_test(NAME frame-smoke
  COMMAND ${CMAKE_SOURCE_DIR}/tests/harness/frame_smoke.sh
          $<TARGET_FILE:doom> ${CMAKE_SOURCE_DIR})

add_test(NAME palette-lut
  COMMAND ${CMAKE_SOURCE_DIR}/tests/harness/palette_lut.sh
          $<TARGET_FILE:doom> ${CMAKE_SOURCE_DIR})

