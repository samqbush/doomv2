#!/usr/bin/env python3
"""Phase 5 modernization: deterministic per-player netgame input generator.

Emits raw scripted-ticcmd streams for the loopback consistency oracle
(docs/oracle/ORACLE_STRATEGY.md, tests/harness/net_loopback.sh). A netgame
cannot use -playdemo (demos are single-player), so each node's LOCAL player is
driven from one of these files via the engine's -scriptcmds flag; the two nodes
exchange commands over real 127.0.0.1 UDP and run the identical lockstep
simulation.

Per the maintainer's self-frozen oracle decision, the scripts are authored (no
human input, no external port) so the reference checksum is reproducible from
source with zero external dependencies.

Wire format consumed by g_parity.c G_NetScriptApply -- raw bytes, 4 per tic:
    forwardmove (signed char), sidemove (signed char),
    angleturn>>8 byte (applied as byte<<8, matching the demo tic encoding),
    buttons (byte)

The two players deliberately move DIFFERENTLY so the frozen world-state
checksum reflects a genuine 2-player game (not two idle drones): player 1 walks
forward and fires; player 2 strafes and turns. Long enough to cover the
-exittic stop point plus the lockstep look-ahead window.
"""

import argparse
import sys

# d_event.h buttoncode_t
BT_ATTACK = 1
BT_USE = 2

# Vanilla movement magnitudes (g_game.c forwardmove[]/sidemove[]).
FWD_WALK = 25
FWD_RUN = 50
SIDE_WALK = 24

# A "segment" is (forwardmove, sidemove, angleturn_byte, buttons, tics).
# angleturn_byte is the stored high byte; playback applies it as (byte<<8).
PLAYER_SCRIPTS = {
    1: [
        # settle, then walk forward, fire the pistol, use a wall.
        (0,          0,          0,   0,        10),
        (FWD_WALK,   0,          0,   0,        20),
        (FWD_RUN,    0,          0,   0,        20),
        (0,          0,          0,   BT_ATTACK, 8),
        (FWD_WALK,   0,          0,   0,        20),
        (0,          0,          0,   BT_USE,    4),
        (0,          0,          0,   0,        40),
    ],
    2: [
        # settle, then turn + strafe + fire on a different cadence.
        (0,          0,          0,   0,        10),
        (0,          SIDE_WALK,  0x02, 0,       20),
        (0,          -SIDE_WALK, 0xFE, 0,       20),
        (FWD_WALK,   0,          0x01, BT_ATTACK, 8),
        (0,          SIDE_WALK,  0x00, 0,       20),
        (0,          0,          0,   BT_USE,    4),
        (0,          0,          0,   0,        40),
    ],
}


def build(player):
    out = bytearray()
    for fwd, side, ang, btn, tics in PLAYER_SCRIPTS[player]:
        for _ in range(tics):
            out.append(fwd & 0xff)
            out.append(side & 0xff)
            out.append(ang & 0xff)
            out.append(btn & 0xff)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--player", type=int, choices=(1, 2), required=True,
                    help="which player's script to emit (1 or 2)")
    ap.add_argument("-o", "--output", help="output file (default: stdout)")
    args = ap.parse_args()

    data = build(args.player)
    if args.output:
        with open(args.output, "wb") as f:
            f.write(data)
    else:
        sys.stdout.buffer.write(data)


if __name__ == "__main__":
    main()
