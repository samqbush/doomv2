#!/usr/bin/env python3
"""Phase 2 modernization: deterministic scripted v110 DOOM demo generator.

Emits a self-contained ``.lmp`` demo lump for the self-frozen parity oracle
(docs/oracle/ORACLE_STRATEGY.md). The bundled Freedoom DEMO1-4 are demo
version 109 and are rejected by this engine (VERSION 110 in doomdef.h), so the
golden master must be produced by *our own* engine. Per the maintainer's
decision the reference is SCRIPTED (no human input) so it is reproducible from
source with zero external dependencies.

Demo wire format (see linuxdoom-1.10/g_game.c G_BeginRecording /
G_ReadDemoTiccmd):

  header (13 bytes):
    VERSION, skill, episode, map, deathmatch, respawn, fast, nomonsters,
    consoleplayer, playeringame[0..3]
  then per tic (4 bytes):
    forwardmove (signed char), sidemove (signed char),
    angleturn>>8 byte (playback reads it back as byte<<8 -- LOSSY, so we
    author the stored byte directly), buttons
  terminated by 0x80 (DEMOMARKER)

The script is authored as a *coverage asset*, not a walk-forward: it exercises
movement+collision, turning, weapon fire (ammo mutation + RNG consumption), and
use lines. See COVERAGE / UNCOVERED below.
"""

import argparse
import sys

VERSION = 110
DEMOMARKER = 0x80

# doomdef.h skill_t: sk_baby=0, sk_easy=1, sk_medium=2, ...
SK_MEDIUM = 2
# d_event.h buttoncode_t
BT_ATTACK = 1
BT_USE = 2

# Vanilla movement magnitudes (g_game.c forwardmove[]/sidemove[]).
FWD_WALK = 25
FWD_RUN = 50
SIDE_WALK = 24

# COVERAGE (systems this demo deliberately drives on Freedoom E1M1):
#   - player movement + wall/thing collision (P_TryMove / P_XYMovement)
#   - view/angle mutation (turning)
#   - weapon fire: pistol -> ammo decrement + P_Random spread/damage (RNG cursor)
#   - use lines: BT_USE against nearby linedefs (door/switch specials)
#   - thinker/mobj advancement + sector specials via leveltime
# UNCOVERED (documented residual, acceptable for the Phase 2 beachhead):
#   - multiplayer / netgame paths
#   - weapon switching beyond the starting pistol
#   - full monster AI kill chains (depends on E1M1 layout; wakeups/RNG are hit)
#   - save/load, intermission scoring

# A "segment" is (forwardmove, sidemove, angleturn_byte, buttons, tics).
SCRIPT = [
    # settle for a few tics (thinkers spin up)
    (0,        0,         0,   0,          8),
    # walk forward into the room (collision + movement)
    (FWD_WALK, 0,         0,   0,          24),
    # turn right while creeping (angle mutation)
    (FWD_WALK, 0,         3,   0,          16),
    # fire the pistol several times (ammo + RNG consumption)
    (0,        0,         0,   BT_ATTACK,  1),
    (0,        0,         0,   0,          6),
    (0,        0,         0,   BT_ATTACK,  1),
    (0,        0,         0,   0,          6),
    (0,        0,         0,   BT_ATTACK,  1),
    (0,        0,         0,   0,          6),
    # turn left back toward start (opposite angle sign)
    (0,        0,         0xfd, 0,         16),   # (-3)<<8 style: 0xfd byte
    # run forward and strafe (run speed + side movement)
    (FWD_RUN,  SIDE_WALK, 0,   0,          24),
    # press use against whatever line is ahead (door/switch specials)
    (FWD_WALK, 0,         0,   BT_USE,     2),
    (FWD_WALK, 0,         0,   0,          12),
    (0,        0,         0,   BT_USE,     2),
    # idle to let any triggered sector specials advance
    (0,        0,         0,   0,          40),
]


def s8(v):
    """Encode a signed value as a two's-complement byte."""
    return v & 0xff


def build_demo():
    b = bytearray()
    # header
    b += bytes([
        VERSION,
        SK_MEDIUM,     # skill
        1,             # episode
        1,             # map
        0,             # deathmatch
        0,             # respawnparm
        0,             # fastparm
        0,             # nomonsters
        0,             # consoleplayer
        1, 0, 0, 0,    # playeringame[0..3]
    ])
    # ticcmd stream
    for fwd, side, angle_byte, buttons, tics in SCRIPT:
        for _ in range(tics):
            b += bytes([s8(fwd), s8(side), angle_byte & 0xff, buttons & 0xff])
    b.append(DEMOMARKER)
    return bytes(b)


def parse_demo(data):
    """Re-parse a generated LMP into (header, ticcmds) exactly as the engine
    reads it, applying the LOSSY angle transform (byte<<8)."""
    if len(data) < 13:
        raise ValueError("demo shorter than header")
    header = {
        "version": data[0], "skill": data[1], "episode": data[2],
        "map": data[3], "deathmatch": data[4], "respawn": data[5],
        "fast": data[6], "nomonsters": data[7], "consoleplayer": data[8],
        "playeringame": list(data[9:13]),
    }
    ticcmds = []
    i = 13
    while i < len(data) and data[i] != DEMOMARKER:
        fwd = data[i]
        side = data[i + 1]
        angle = data[i + 2] << 8          # exactly how G_ReadDemoTiccmd reads it
        buttons = data[i + 3]
        # sign-extend the movement bytes
        if fwd >= 128:
            fwd -= 256
        if side >= 128:
            side -= 256
        ticcmds.append((fwd, side, angle, buttons))
        i += 4
    if i >= len(data) or data[i] != DEMOMARKER:
        raise ValueError("demo not terminated by DEMOMARKER")
    return header, ticcmds


def expected_ticcmds():
    out = []
    for fwd, side, angle_byte, buttons, tics in SCRIPT:
        f = fwd if fwd < 128 else fwd - 256
        s = side if side < 128 else side - 256
        angle = (angle_byte & 0xff) << 8
        for _ in range(tics):
            out.append((f, s, angle, buttons))
    return out


def validate(data):
    header, ticcmds = parse_demo(data)
    assert header["version"] == VERSION, header
    assert header["playeringame"] == [1, 0, 0, 0], header
    exp = expected_ticcmds()
    if ticcmds != exp:
        for idx, (a, e) in enumerate(zip(ticcmds, exp)):
            if a != e:
                raise AssertionError(f"tic {idx}: got {a} expected {e}")
        raise AssertionError(f"length {len(ticcmds)} != {len(exp)}")
    return len(ticcmds)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("output", help="path to write the .lmp (e.g. parity.lmp)")
    ap.add_argument("--validate", action="store_true",
                    help="round-trip validate after writing")
    args = ap.parse_args()

    data = build_demo()
    with open(args.output, "wb") as f:
        f.write(data)

    n = validate(data)
    print(f"wrote {args.output}: {len(data)} bytes, {n} tics"
          f"{' (validated)' if args.validate else ''}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
