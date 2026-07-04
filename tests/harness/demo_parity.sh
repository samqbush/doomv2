#!/usr/bin/env bash
# Phase 2 modernization: IWAD + demo harness for the demo-parity oracle.
#
# The 1997 engine identifies IWADs by HARD-CODED filename via $DOOMWADDIR
# (linuxdoom-1.10/d_main.c IdentifyVersion) -- `-iwad` is ignored -- and the
# filename also selects `gamemode`. Our committed IWAD is wads/freedoom1.wad;
# presenting it as `doom1.wad` yields the shareware gamemode we froze against.
#
# This script builds an isolated, sanitized run directory containing ONLY the
# doom1.wad symlink and the demo lump, verifies the IWAD by SHA-256 (so oracle
# drift can't masquerade as an engine regression), then runs the demo headless
# under `-checkdemo`, asserting the frozen world-state checksum.
#
# Usage: demo_parity.sh <doom-binary> <repo-root>
set -euo pipefail

DOOM_BIN=${1:?usage: demo_parity.sh <doom-binary> <repo-root>}
REPO=${2:?usage: demo_parity.sh <doom-binary> <repo-root>}

IWAD="$REPO/wads/freedoom1.wad"
IWAD_SHA="7323bcc168c5a45ff10749b339960e98314740a734c30d4b9f3337001f9e703d"
DEMO="$REPO/tests/fixtures/parity.lmp"
REF="$REPO/tests/fixtures/parity.checksum"

[ -f "$IWAD" ] || { echo "FAIL: missing IWAD $IWAD"; exit 1; }
[ -f "$DEMO" ] || { echo "FAIL: missing demo $DEMO"; exit 1; }
[ -f "$REF" ]  || { echo "FAIL: missing reference $REF"; exit 1; }

# Verify the IWAD content, not just its name.
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

# Sanitized run dir: exactly one discoverable IWAD, exactly the frozen demo.
ln -s "$IWAD" "$RUNDIR/doom1.wad"
cp "$DEMO" "$RUNDIR/parity.lmp"

# HOME is redirected so the engine's ~/.doomrc write lands in the sandbox.
# -nodraw: state parity needs the ticker, not the drawer (D_Display is skipped).
set +e
( cd "$RUNDIR" && DOOMWADDIR="$RUNDIR" HOME="$RUNDIR" \
    "$DOOM_BIN" -playdemo parity -checkdemo -parityref "$REF" -nodraw \
    > "$RUNDIR/out.log" 2>&1 )
RC=$?
set -e

grep -E "PARITY_CHECKSUM|PARITY: (MATCH|MISMATCH)" "$RUNDIR/out.log" || true

if [ $RC -ne 0 ]; then
  echo "FAIL: demo-parity exit $RC"
  tail -5 "$RUNDIR/out.log"
  exit $RC
fi
echo "PASS: demo-parity"
