# Interactive-play smoke checklist (Phase 3)

Phase 3 reaches interactive playability. Demo-parity and frame-smoke are
automated, but *"a human can start a level and move"* is inherently manual and
cannot run in headless CI. This checklist is that manual gate: run it locally on
a real desktop (a windowing session — **not** the `SDL_VIDEODRIVER=dummy`
headless path CI uses) and record the result in the evidence table below.

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

`-grabmouse` enables SDL relative-mouse mode (cursor hidden/confined, relative
deltas) so mouse turning works. Omit it to test the ungrabbed default.

## Steps

Default bindings: **arrows** move/turn, **Alt+arrows** or **, / .** strafe,
**Ctrl** fire, **Space** use/open, **Shift** run, **Esc** menu.

| # | Action | Expected | Pass? |
|---|--------|----------|-------|
| 1 | Launch as above | Title screen renders (3D-ish demo loop, no crash) | ☐ |
| 2 | Press a key / Enter through the menu, start a new game (Esc → New Game → skill) | A level loads: 3D view + status bar + HUD | ☐ |
| 3 | Keyboard **↑ / ↓** | Player moves forward / backward | ☐ |
| 4 | Keyboard **← / →** | Player **turns** left / right | ☐ |
| 5 | Keyboard strafe (**,** / **.** or Alt+←/→) | Player strafes left / right | ☐ |
| 6 | **Mouse turn** (move mouse left/right) | View **turns** horizontally (DOOM has no vertical look) | ☐ |
| 7 | **Mouse forward/back** (move mouse up/down) | Player moves forward / backward | ☐ |
| 8 | **Mouse fire** (left button) | Weapon fires | ☐ |
| 9 | Mouse buttons held during motion | Fire + move combine (button mask persists across motion) | ☐ |
| 10 | **Esc** | In-game menu opens; **Esc** again closes it | ☐ |
| 11 | Quit (menu → Quit, or window close) | Clean exit, no hang/crash | ☐ |

## Evidence

Fill in when run:

- Date / commit:
- OS + SDL2 version:
- Runner (name):
- Overall result (all steps pass?):
- Notes / anomalies:

### Automated pre-check (dev session, no display)

The visual steps above require a real windowing session. In a headless dev
session the following were verified as a proxy (they exercise the boot +
mouse-grab code paths and prove determinism is untouched):

- Clean build on macOS/AppleClang; all 4 ctest targets green.
- `-grabmouse -noblit` boot under `SDL_VIDEODRIVER=dummy`: video subsystem
  inits, `SDL_SetRelativeMouseMode` is attempted and its failure under the
  dummy driver is non-fatal, the demo replays, and the world-state checksum
  still `PARITY: MATCH a00552bbf22274a2`.

The interactive table (steps 1–11) still needs a human on a desktop.

A screenshot of step 2 (a loaded level) is encouraged; drop it next to
`docs/oracle/phase2-boot-frame.png` and reference it here.
