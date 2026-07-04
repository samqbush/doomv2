#!/usr/bin/env bash
# Phase 5 modernization: 2-node loopback consistency oracle (safety rung L2).
#
# Launches TWO real doom processes that talk over 127.0.0.1 UDP (the actual
# ported platform/posix/i_net_posix.c transport), drives each node's local
# player from a fixed scripted-ticcmd stream, and stops both at the same gametic
# to emit the canonical world-state checksum (g_parity.c). It then asserts:
#
#   * both processes exit 0 -- no I_Error "consistency failure" (lockstep held
#     across the real socket transport, i.e. no packing/endian desync), and
#   * both printed the SAME checksum, and
#   * that checksum matches the frozen self-frozen reference (unless freezing).
#
# Per the self-frozen oracle decision (docs/oracle/ORACLE_STRATEGY.md) the
# reference is produced by our OWN engine -- no external port, no legacy binary.
#
# Anti-hang: D_ArbitrateNetStart has unbounded wait loops, so a misconfigured
# run (bad ports, a crashed child) could hang forever. The whole run is bounded
# by a hard watchdog that kills both children and dumps their logs on timeout.
#
# Usage: net_loopback.sh <doom-binary> <repo-root> [freeze]
#   freeze  -> ignore/write the reference instead of asserting against it.
set -uo pipefail

DOOM_BIN=${1:?usage: net_loopback.sh <doom-binary> <repo-root> [freeze]}
REPO=${2:?usage: net_loopback.sh <doom-binary> <repo-root> [freeze]}
MODE=${3:-check}

# Resolve to absolute paths -- the nodes run from inside their sandbox dirs.
case "$DOOM_BIN" in /*) ;; *) DOOM_BIN="$(pwd)/$DOOM_BIN" ;; esac
case "$REPO" in /*) ;; *) REPO="$(cd "$REPO" && pwd)" ;; esac
[ -x "$DOOM_BIN" ] || { echo "FAIL: doom binary not executable: $DOOM_BIN"; exit 1; }

IWAD="$REPO/wads/freedoom1.wad"
IWAD_SHA="7323bcc168c5a45ff10749b339960e98314740a734c30d4b9f3337001f9e703d"
GEN="$REPO/tools/gen_netscript.py"
REF="$REPO/tests/fixtures/net_loopback.checksum"

EXITTIC=120        # stop point; inside the scripted window + lockstep look-ahead
WATCHDOG=60        # hard wall-clock cap for the whole 2-node run (seconds)

[ -f "$IWAD" ] || { echo "FAIL: missing IWAD $IWAD"; exit 1; }
[ -f "$GEN" ]  || { echo "FAIL: missing generator $GEN"; exit 1; }

# Verify the IWAD content, not just its name (oracle drift must not masquerade
# as an engine regression).
if command -v shasum >/dev/null 2>&1; then
  GOT_SHA=$(shasum -a 256 "$IWAD" | awk '{print $1}')
else
  GOT_SHA=$(sha256sum "$IWAD" | awk '{print $1}')
fi
if [ "$GOT_SHA" != "$IWAD_SHA" ]; then
  echo "FAIL: IWAD SHA mismatch"; echo "  expected $IWAD_SHA"; echo "  got      $GOT_SHA"
  exit 1
fi

PYTHON=$(command -v python3 || command -v python)
[ -n "$PYTHON" ] || { echo "FAIL: python3 not found"; exit 1; }

# Two isolated sandboxes: one discoverable IWAD each, private HOME for .doomrc.
RUN0=$(mktemp -d); RUN1=$(mktemp -d)

PID0=""; PID1=""
cleanup() {
  [ -n "$PID0" ] && kill "$PID0" 2>/dev/null
  [ -n "$PID1" ] && kill "$PID1" 2>/dev/null
  if [ -n "${DOOM_ARTIFACT_DIR:-}" ]; then
    mkdir -p "$DOOM_ARTIFACT_DIR"
    cp "$RUN0/out.log" "$DOOM_ARTIFACT_DIR/net-loopback-node0.log" 2>/dev/null || true
    cp "$RUN1/out.log" "$DOOM_ARTIFACT_DIR/net-loopback-node1.log" 2>/dev/null || true
  fi
  rm -rf "$RUN0" "$RUN1"
}
trap cleanup EXIT

ln -s "$IWAD" "$RUN0/doom1.wad"
ln -s "$IWAD" "$RUN1/doom1.wad"
"$PYTHON" "$GEN" --player 1 -o "$RUN0/p1.cmds" || { echo "FAIL: gen p1"; exit 1; }
"$PYTHON" "$GEN" --player 2 -o "$RUN1/p2.cmds" || { echo "FAIL: gen p2"; exit 1; }

# Distinct ports on 127.0.0.1; SO_REUSEADDR in the transport tolerates reuse.
BASE=$(( 20000 + (($$ + RANDOM) % 20000) ))
PORT0=$BASE
PORT1=$(( BASE + 1 ))

COMMON="-warp 1 1 -skill 3 -nosound -nomusic -checkdemo -exittic $EXITTIC"

# Node 0 = key player (console player 1 -> index 0); peer node 1 at PORT1.
( cd "$RUN0" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    DOOMWADDIR="$RUN0" HOME="$RUN0" \
    "$DOOM_BIN" $COMMON -port "$PORT0" -net 1 "127.0.0.1:$PORT1" \
      -scriptcmds "$RUN0/p1.cmds" > "$RUN0/out.log" 2>&1 ) &
PID0=$!

# Node 1 = console player 2 -> index 1; peer node 0 at PORT0.
( cd "$RUN1" && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    DOOMWADDIR="$RUN1" HOME="$RUN1" \
    "$DOOM_BIN" $COMMON -port "$PORT1" -net 2 "127.0.0.1:$PORT0" \
      -scriptcmds "$RUN1/p2.cmds" > "$RUN1/out.log" 2>&1 ) &
PID1=$!

# Portable watchdog (no dependency on coreutils `timeout`, absent on macOS).
elapsed=0
while kill -0 "$PID0" 2>/dev/null || kill -0 "$PID1" 2>/dev/null; do
  sleep 1
  elapsed=$(( elapsed + 1 ))
  if [ "$elapsed" -ge "$WATCHDOG" ]; then
    echo "FAIL: watchdog timeout (${WATCHDOG}s) -- likely net-start hang"
    kill "$PID0" "$PID1" 2>/dev/null
    echo "--- node0 tail ---"; tail -15 "$RUN0/out.log"
    echo "--- node1 tail ---"; tail -15 "$RUN1/out.log"
    exit 1
  fi
done

wait "$PID0"; RC0=$?
wait "$PID1"; RC1=$?

fail=0
if [ "$RC0" -ne 0 ]; then echo "FAIL: node0 exit $RC0"; tail -8 "$RUN0/out.log"; fail=1; fi
if [ "$RC1" -ne 0 ]; then echo "FAIL: node1 exit $RC1"; tail -8 "$RUN1/out.log"; fail=1; fi
if grep -q "consistency failure" "$RUN0/out.log" "$RUN1/out.log"; then
  echo "FAIL: consistency failure (transport desync)"; fail=1
fi

# Guard against a broken run silently freezing a single-player oracle: both
# nodes must have completed the 2-node handshake.
grep -q "player 1 of 2" "$RUN0/out.log" || { echo "FAIL: node0 not '1 of 2'"; fail=1; }
grep -q "player 2 of 2" "$RUN1/out.log" || { echo "FAIL: node1 not '2 of 2'"; fail=1; }

SUM0=$(grep -oE "PARITY_CHECKSUM [0-9a-f]+" "$RUN0/out.log" | awk '{print $2}' | tail -1)
SUM1=$(grep -oE "PARITY_CHECKSUM [0-9a-f]+" "$RUN1/out.log" | awk '{print $2}' | tail -1)
[ -n "$SUM0" ] || { echo "FAIL: node0 emitted no checksum"; fail=1; }
[ -n "$SUM1" ] || { echo "FAIL: node1 emitted no checksum"; fail=1; }

if [ "$fail" -ne 0 ]; then exit 1; fi

echo "node0 checksum: $SUM0"
echo "node1 checksum: $SUM1"
if [ "$SUM0" != "$SUM1" ]; then
  echo "FAIL: node checksums differ (desync)"; exit 1
fi

if [ "$MODE" = "freeze" ]; then
  echo "$SUM0" > "$REF"
  echo "FROZEN: wrote $REF = $SUM0"
  exit 0
fi

[ -f "$REF" ] || { echo "FAIL: missing reference $REF (run freeze first)"; exit 1; }
WANT=$(tr -d '[:space:]' < "$REF")
if [ "$SUM0" != "$WANT" ]; then
  echo "FAIL: checksum $SUM0 != frozen $WANT"; exit 1
fi
echo "PASS: net-loopback ($SUM0)"
