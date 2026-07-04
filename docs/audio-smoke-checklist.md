# Audio smoke checklist (Phase 4)

Phase 4 replaces the dead OSS `/dev/dsp` + separate `sndserver` process with
in-process SDL2 audio, keeping the original 8-channel DMX software mixer. There
is **no automated audio oracle** (this is the plan's blessed **L1 / perceptual**
downgrade): *"a human hears SFX play in sync"* is inherently manual and cannot
run in headless CI. This checklist is that manual gate — run it locally on a
real desktop **with a working audio device** (not the `SDL_AUDIODRIVER=dummy` /
oracle path, where audio is deliberately suppressed) and record the result in
the evidence table below.

What automation already guarantees (so this checklist only has to cover
*perception*):

- The full 4-target parity gate stays green (`ctest`), proving the mixer running
  on the sim thread does **not** perturb the deterministic world state.
- A dev-session boot opens the SDL device at the exact requested format and
  pre-caches all SFX without crashing (see pre-check below).

## Build

```sh
cmake -B build && cmake --build build
```

## Launch

The 1997 engine finds its IWAD by **hard-coded filename** via `DOOMWADDIR`
(`-iwad` is ignored), and the filename also selects the game mode. Present the
committed shareware `freedoom1.wad` as `doom1.wad`:

```sh
rundir=$(mktemp -d)
ln -s "$PWD/wads/freedoom1.wad" "$rundir/doom1.wad"
DOOMWADDIR="$rundir" HOME="$rundir" ./build/doom -grabmouse
```

Audio is **on by default**. To force it off (and confirm the game still runs
silent), add `-nosound` or `-nosfx`. `stderr` should show
`I_InitSound: SDL audio ready (11025 Hz, 2 ch, S16)` on a normal run, or
`I_InitSound: audio suppressed ...` / `continuing without audio` otherwise.

## Steps

Default bindings: **arrows** move/turn, **Ctrl** fire, **Space** use/open,
**Esc** menu.

| # | Action | Expected | Pass? |
|---|--------|----------|-------|
| 1 | Launch as above; watch `stderr` | `I_InitSound: SDL audio ready (11025 Hz, 2 ch, S16)` printed; no crash | ☐ |
| 2 | Navigate the menu (arrows / Enter) | Menu move/select **beeps** (`pstop`/`pistol` UI SFX) | ☐ |
| 3 | Start a new game | Level loads; door/lift ambient SFX audible where present | ☐ |
| 4 | **Ctrl** (fire pistol) | Pistol shot plays promptly, **in sync** with the muzzle flash (no obvious lag) | ☐ |
| 5 | Fire repeatedly / rapidly | Shots overlap cleanly; no runaway latency buildup, no crackling stall | ☐ |
| 6 | Trigger several SFX at once (fire while a door opens near enemies) | Sounds mix (up to 8 channels); loudest/nearest dominate, no dropouts to silence | ☐ |
| 7 | **Space** on a door | Door open/close SFX plays with correct left/right bias as you strafe past | ☐ |
| 8 | Pick up an item | Item pickup SFX plays | ☐ |
| 9 | Play for ~1–2 min continuously | Audio stays in sync with action; latency does **not** grow over time (queue cap holds) | ☐ |
| 10 | Relaunch with `-nosound` | Game runs normally, completely silent, no errors | ☐ |
| 11 | Quit (menu → Quit, or window close) | Clean exit, no hang/crash on `I_ShutdownSound` | ☐ |

## Evidence

Fill in when run:

- Date / commit:
- OS + SDL2 version:
- Runner (name):
- Overall result (all steps pass?):
- Notes / anomalies:

A short screen recording (with audio) of steps 4–6 is encouraged.

### Automated pre-check (dev session)

The perceptual steps above require a human with speakers/headphones. In a dev
session the following were verified as a proxy (they exercise the device-open +
SFX-precache + mixer code paths and prove determinism is untouched):

- Clean build on macOS/AppleClang; `i_sound_sdl.c` compiles under `-Werror`
  with zero warnings.
- All 4 ctest targets green (`demo-parity`, `frame-smoke`, `palette-lut`,
  `demo-regen`) — audio does not change the sim; world-state checksum still
  `a00552bbf22274a2`.
- Non-oracle boot under `SDL_VIDEODRIVER=dummy` with a real audio driver:
  `SDL_OpenAudioDevice` succeeds at the requested `11025 Hz / 2 ch / S16LSB`
  (no format change), all SFX pre-cache, and the engine advances to `ST_Init`
  without crashing.
- Oracle paths (`-checkdemo` / `-framehash`) print `audio suppressed` and open
  no device, keeping the deterministic gates independent of audio hardware.

The perceptual table (steps 1–11) still needs a human on a desktop.
