#!/usr/bin/env bash
# Phase 2 modernization: palette-LUT gate.
#
# Frame hashes cover the indexed software framebuffer but NOT I_SetPalette's
# index->ARGB8888 conversion (channel order, opaque alpha, gamma table). This
# runs the engine's -paltest self-check, which applies the real PLAYPAL and
# asserts every colorlut entry equals the gamma-corrected RGB in ARGB8888
# order. Headless: no SDL window is created.
#
# Usage: palette_lut.sh <doom-binary> <repo-root>
set -euo pipefail

DOOM_BIN=${1:?usage: palette_lut.sh <doom-binary> <repo-root>}
REPO=${2:?usage: palette_lut.sh <doom-binary> <repo-root>}

IWAD="$REPO/wads/freedoom1.wad"
IWAD_SHA="7323bcc168c5a45ff10749b339960e98314740a734c30d4b9f3337001f9e703d"

[ -f "$IWAD" ] || { echo "FAIL: missing IWAD $IWAD"; exit 1; }

if command -v shasum >/dev/null 2>&1; then
  GOT_SHA=$(shasum -a 256 "$IWAD" | awk '{print $1}')
else
  GOT_SHA=$(sha256sum "$IWAD" | awk '{print $1}')
fi
if [ "$GOT_SHA" != "$IWAD_SHA" ]; then
  echo "FAIL: IWAD SHA mismatch"; exit 1
fi

RUNDIR=$(mktemp -d)
# Preserve the run log for CI before deleting the sandbox (see demo_parity.sh).
cleanup() {
  if [ -n "${DOOM_ARTIFACT_DIR:-}" ] && [ -f "$RUNDIR/out.log" ]; then
    mkdir -p "$DOOM_ARTIFACT_DIR"
    cp "$RUNDIR/out.log" "$DOOM_ARTIFACT_DIR/palette-lut.log" 2>/dev/null || true
  fi
  rm -rf "$RUNDIR"
}
trap cleanup EXIT
ln -s "$IWAD" "$RUNDIR/doom1.wad"

set +e
( cd "$RUNDIR" && SDL_VIDEODRIVER=dummy DOOMWADDIR="$RUNDIR" HOME="$RUNDIR" \
    "$DOOM_BIN" -paltest > "$RUNDIR/out.log" 2>&1 )
RC=$?
set -e

grep -E "PALTEST" "$RUNDIR/out.log" || true

if [ $RC -ne 0 ]; then
  echo "FAIL: palette-lut exit $RC"
  tail -5 "$RUNDIR/out.log"
  exit $RC
fi
echo "PASS: palette-lut"
