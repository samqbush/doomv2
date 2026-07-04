#!/usr/bin/env bash
# Phase 2 modernization: frame-smoke oracle harness.
#
# Complements demo-parity (world-state) with a *rendering* gate: replay the
# scripted v110 demo and assert the byte-EXACT hash of the indexed 320x200
# software framebuffer (screens[0]) at fixed deterministic tics matches the
# frozen master. This exercises the software drawer (R_*), NOT the SDL blit --
# I_SetPalette's ARGB conversion is covered separately by the palette gate.
#
# Determinism: -framehash forces singletics so gametic == frame index; samples
# are taken only at fixed tics, when gamestate==GS_LEVEL and no wipe is active
# (see tests/parity/g_parity.c). -noblit keeps the drawer running while the SDL
# dummy driver keeps this headless. A built-in non-blank guard (>=8 distinct
# palette indices) fails closed if the frame is blank.
#
# Usage: frame_smoke.sh <doom-binary> <repo-root>
set -euo pipefail

DOOM_BIN=${1:?usage: frame_smoke.sh <doom-binary> <repo-root>}
REPO=${2:?usage: frame_smoke.sh <doom-binary> <repo-root>}

IWAD="$REPO/wads/freedoom1.wad"
IWAD_SHA="7323bcc168c5a45ff10749b339960e98314740a734c30d4b9f3337001f9e703d"
DEMO="$REPO/tests/fixtures/parity.lmp"
REF="$REPO/tests/fixtures/parity.frame"

[ -f "$IWAD" ] || { echo "FAIL: missing IWAD $IWAD"; exit 1; }
[ -f "$DEMO" ] || { echo "FAIL: missing demo $DEMO"; exit 1; }
[ -f "$REF" ]  || { echo "FAIL: missing frame reference $REF"; exit 1; }

if command -v shasum >/dev/null 2>&1; then
  GOT_SHA=$(shasum -a 256 "$IWAD" | awk '{print $1}')
else
  GOT_SHA=$(sha256sum "$IWAD" | awk '{print $1}')
fi
if [ "$GOT_SHA" != "$IWAD_SHA" ]; then
  echo "FAIL: IWAD SHA mismatch"
  echo "  expected $IWAD_SHA"
  echo "  got      $GOT_SHA"
  exit 1
fi

RUNDIR=$(mktemp -d)
trap 'rm -rf "$RUNDIR"' EXIT

ln -s "$IWAD" "$RUNDIR/doom1.wad"
cp "$DEMO" "$RUNDIR/parity.lmp"

set +e
( cd "$RUNDIR" && SDL_VIDEODRIVER=dummy DOOMWADDIR="$RUNDIR" HOME="$RUNDIR" \
    "$DOOM_BIN" -playdemo parity -framehash -frameref "$REF" -noblit \
    > "$RUNDIR/out.log" 2>&1 )
RC=$?
set -e

grep -E "PARITY_FRAMEHASH|PARITY: (frame|MATCH|MISMATCH)" "$RUNDIR/out.log" || true

if [ $RC -ne 0 ]; then
  echo "FAIL: frame-smoke exit $RC"
  tail -8 "$RUNDIR/out.log"
  exit $RC
fi
echo "PASS: frame-smoke"
