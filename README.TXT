================================================================================
 DOOM — Modernized (2026)
================================================================================

This is the 1997 id Software DOOM source release, brought up to a buildable,
runnable, maintainable state on a modern 64-bit toolchain — without changing how
the game plays. The software renderer, the 35 Hz deterministic simulation, the
WAD data format, and demo/netgame determinism are all preserved byte-for-byte.
Only the dead platform layer (X11 8-bit video, OSS `/dev/dsp` audio, the DOS
drivers) was replaced, with a cross-platform SDL2 backend and a portable POSIX
network transport.

Original source (id Software, released 1997-12-23):
    https://github.com/id-Software/DOOM

The bundled game data (freedoom1.wad) is from the Freedoom project — a free,
BSD-licensed replacement for the commercial DOOM IWAD, so this build is playable
out of the box without owning a copy of DOOM:
    https://github.com/freedoom/freedoom


--------------------------------------------------------------------------------
 Before / After — what was modernized
--------------------------------------------------------------------------------

  Area          | Before (1997, Linux 1.10)        | After (2026)
  --------------+----------------------------------+-------------------------------
  Language      | K&R-era C, compiled with gcc     | C11 (C_STANDARD 11)
  Build system  | GNU Make, Linux-only             | CMake (cmake -B build && build)
  Compiles?     | No — dies on <values.h>, libc5   | Yes — 64-bit clean, Linux+macOS
  Video         | X11 Xlib + MIT-SHM, 8-bit        | SDL2 window; index->ARGB8888
                | PseudoColor 320x200 (dead)       | palette LUT + gamma
  Audio         | OSS /dev/dsp via a SEPARATE       | In-process SDL2 push audio;
                | sndserver process (popen)        | DMX mixer ported verbatim
  Networking    | BSD UDP (Linux) + DOS IPX and    | Portable POSIX BSD sockets
                | serial drivers                   | (platform/posix), Linux+macOS
  Platforms     | Linux only (32-bit era)          | Linux + macOS, 64-bit
  Word size     | 32-bit / pointer assumptions     | 64-bit correct
  Tests         | None                             | 5-target ctest parity gate
  CI            | None                             | GitHub Actions (Linux + macOS)
  Warnings      | -Wall only, noisy                | -Werror on all platform files
  Releases      | Source tarball only              | Downloadable Mac + Linux builds

Kept exactly as-is (this was NOT a rewrite of the game):
  * Software renderer          r_* (BSP, segs, planes, things, draw)
  * Playsim / game logic        p_*, g_game, info
  * WAD + zone memory           w_wad, z_zone
  * Fixed-point math and RNG    m_fixed, m_random, lookup tables
  * 35 Hz tic model + ticcmd streams -> demos and netplay stay deterministic
  * WAD lump format + 320x200 8-bit paletted framebuffer data contract

Dropped / archived:
  * The separate OSS `sndserver` process (folded in-process; moved to legacy/)
  * DOS real-mode `ipx/` and `sersrc/` drivers (unbuildable today; archived)


--------------------------------------------------------------------------------
 Correctness: how we know the modernization didn't change the game
--------------------------------------------------------------------------------

We deliberately used NO external source port (no Chocolate Doom, Crispy, PrBoom,
etc.) and no period-correct legacy binary as an oracle. Instead the port is
validated against ITSELF: our own engine records a native demo, and its
world-state consistency checksum plus byte-exact software-framebuffer hashes are
frozen and re-checked on every change. `ctest` runs five gates:

  demo-parity   world-state consistency checksum   a00552bbf22274a2
  frame-smoke   byte-exact indexed screens[0] hash  3e61b0f0c5dfd943
  palette-lut   I_SetPalette index->ARGB8888 + gamma
  demo-regen    tools/gen_demo.py reproduces the demo lump byte-exact
  net-loopback  two real processes over 127.0.0.1    e8ca533e8baf4ad4


--------------------------------------------------------------------------------
 Build & run from source
--------------------------------------------------------------------------------

Requirements: a C11 compiler, CMake >= 3.16, and SDL2.
    Linux:  sudo apt-get install -y build-essential cmake libsdl2-dev
    macOS:  brew install cmake sdl2

Build:
    cmake -B build
    cmake --build build

Run (the engine finds its IWAD by hard-coded filename via DOOMWADDIR, NOT via
-iwad). Present the bundled Freedoom WAD as doom1.wad:
    mkdir -p run && ln -sf "$PWD/wads/freedoom1.wad" run/doom1.wad
    DOOMWADDIR="$PWD/run" ./build/doom

Test:
    ctest --test-dir build --output-on-failure


--------------------------------------------------------------------------------
 Downloadable releases
--------------------------------------------------------------------------------

Prebuilt, ready-to-run macOS and Linux builds are published on the Releases page
for each tagged version. Each archive bundles the engine, the required SDL2
library, and Freedoom's game data (as doom1.wad) so it runs on download — see
the README-RELEASE.txt inside each archive for platform notes (including the
macOS Gatekeeper unquarantine step for unsigned builds).

    https://github.com/samqbush/doomv2/releases


--------------------------------------------------------------------------------
 Licensing
--------------------------------------------------------------------------------

This project aggregates three independently-licensed works:

  * The DOOM engine — GNU General Public License, version 2 (see LICENSE.TXT).
    Copyright (c) ZeniMax Media Inc. Original release by id Software.
  * The Freedoom game data (freedoom1.wad) — 3-clause BSD license
    (see wads/FREEDOOM-COPYING.txt and wads/FREEDOOM-CREDITS.txt). Freedoom is
    an independent project and does not endorse this build.
  * SDL2 (bundled in binary releases) — zlib license.

The engine binary is a GPLv2 work; the WAD is runtime data and SDL2 is a
separate library, so they are combined only as an aggregate. Full corresponding
source for every binary release is this repository at the matching tag.
