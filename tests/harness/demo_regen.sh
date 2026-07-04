#!/usr/bin/env bash
# Phase 2 modernization: prove the frozen demo lump is regenerable from source.
#
# The committed tests/fixtures/parity.lmp is load-bearing (every later lit phase
# diffs against the checksum it produces). This asserts tools/gen_demo.py
# reproduces those exact bytes, so the artifact is never an opaque blob.
#
# Usage: demo_regen.sh <repo-root>
set -euo pipefail

REPO=${1:?usage: demo_regen.sh <repo-root>}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

python3 "$REPO/tools/gen_demo.py" --validate "$TMP/parity.lmp" >/dev/null

if cmp -s "$TMP/parity.lmp" "$REPO/tests/fixtures/parity.lmp"; then
  echo "PASS: demo-regen (gen_demo.py reproduces frozen parity.lmp)"
else
  echo "FAIL: gen_demo.py output differs from committed tests/fixtures/parity.lmp"
  exit 1
fi
