#!/usr/bin/env bash
#
# package_release.sh — assemble a self-contained, downloadable DOOM release.
#
# Produces a tarball that runs on a clean machine with NO system SDL2 install:
#   doom            the built engine binary
#   lib/            vendored libSDL2 (rpath: $ORIGIN/lib on Linux, @loader_path on macOS)
#   doom1.wad       Freedoom Phase 1 data, renamed to the engine's hard-coded IWAD name
#   run-doom.sh     launcher: points DOOMWADDIR + HOME at the bundle dir
#   README-RELEASE.txt, LICENSE.TXT, FREEDOOM-COPYING.txt, FREEDOOM-CREDITS.txt,
#   LICENSE-SDL2.txt
#
# The engine finds its IWAD by hard-coded filename via DOOMWADDIR (NOT -iwad), so we
# present freedoom1.wad as doom1.wad. On macOS, install_name_tool invalidates the Mach-O
# signature, so the binary + dylib are ad-hoc re-signed. A dependency audit fails the
# build if the binary still references a build-tree or Homebrew path.
#
# Usage: tools/package_release.sh <version> [build-dir] [output-dir]
#   version    e.g. v1.0.0  (defaults to $GITHUB_REF_NAME, else "dev")
#   build-dir  CMake build dir with ./doom  (default: build)
#   output-dir where the tarball is written (default: dist)
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${1:-${GITHUB_REF_NAME:-dev}}"
BUILD_DIR="${2:-$REPO/build}"
OUT_DIR="${3:-$REPO/dist}"

BIN="$BUILD_DIR/doom"
[ -x "$BIN" ] || { echo "FAIL: engine binary not found/executable at $BIN"; exit 1; }

# ---- platform + arch labels -------------------------------------------------
case "$(uname -s)" in
  Linux)  OS=linux ;;
  Darwin) OS=macos ;;
  *) echo "FAIL: unsupported OS $(uname -s)"; exit 1 ;;
esac
ARCH="$(uname -m)"   # x86_64, arm64, aarch64

STAGE_NAME="doom-${VERSION}-${OS}-${ARCH}"
STAGE="$(mktemp -d)/${STAGE_NAME}"
mkdir -p "$STAGE/lib"
echo ">> staging $STAGE_NAME"

cp "$BIN" "$STAGE/doom"
chmod +x "$STAGE/doom"

# ---- vendor SDL2 + rewrite rpath -------------------------------------------
FORBIDDEN='/opt/homebrew|/usr/local/opt|/Cellar/|/home/|/Users/'   # leaked absolute paths

if [ "$OS" = linux ]; then
  # Resolve the libSDL2 the binary actually links, copy the real file under its soname.
  SDL_LINE="$(ldd "$STAGE/doom" | awk '/libSDL2/{print $1"|"$3; exit}')"
  [ -n "$SDL_LINE" ] || { echo "FAIL: binary does not link libSDL2 (ldd)"; exit 1; }
  SONAME="${SDL_LINE%%|*}"; SDL_PATH="${SDL_LINE##*|}"
  [ -f "$SDL_PATH" ] || { echo "FAIL: SDL2 lib path not found: $SDL_PATH"; exit 1; }
  cp -L "$SDL_PATH" "$STAGE/lib/$SONAME"
  command -v patchelf >/dev/null || { echo "FAIL: patchelf required on Linux"; exit 1; }
  patchelf --set-rpath '$ORIGIN/lib' "$STAGE/doom"
  # SDL2 copyright (apt ships it here); fall back to a pointer if absent.
  SDL_LIC="$(ls /usr/share/doc/libsdl2-2.0-0/copyright 2>/dev/null || true)"
else
  # macOS: vendor libSDL2 AND all its non-system transitive deps. Homebrew's
  # `sdl2` is now sdl2-compat, whose libSDL2-2.0.0.dylib pulls libSDL3 -- copying
  # only the directly-linked dylib leaves dyld unable to find libSDL3 at runtime
  # (silent load failure). dylibbundler follows the whole graph, copies every
  # non-system dylib into lib/, and rewrites install names to @executable_path/lib
  # (which resolves the same for the executable and every nested dylib, since we
  # always launch ./doom from the bundle root).
  SDL_REF="$(otool -L "$STAGE/doom" | awk '/libSDL2/{print $1; exit}')"
  [ -n "$SDL_REF" ] || { echo "FAIL: binary does not link libSDL2 (otool)"; exit 1; }
  command -v dylibbundler >/dev/null || { echo "FAIL: dylibbundler required on macOS (brew install dylibbundler)"; exit 1; }
  ( cd "$STAGE" && dylibbundler -of -cd -b -x ./doom -d ./lib -p @executable_path/lib/ >/dev/null )
  # dylibbundler rewrote load commands -> every Mach-O signature is now invalid.
  # Ad-hoc re-sign each vendored dylib and the binary, then verify.
  for f in "$STAGE"/lib/*.dylib; do codesign --force --sign - "$f"; done
  codesign --force --sign - "$STAGE/doom"
  codesign --verify --strict "$STAGE/doom" || { echo "FAIL: ad-hoc codesign verify"; exit 1; }
  # SDL2 license: locate the real SDL2 keg (works for sdl2 or sdl2-compat).
  SDL_REAL="$(readlink -f "$SDL_REF" 2>/dev/null || echo "$SDL_REF")"
  SDL_LIC="$(ls "$(dirname "$SDL_REAL")/../LICENSE.txt" 2>/dev/null || true)"
fi

# ---- game data + license files ---------------------------------------------
cp "$REPO/wads/freedoom1.wad"      "$STAGE/doom1.wad"
cp "$REPO/LICENSE.TXT"             "$STAGE/LICENSE.TXT"
cp "$REPO/wads/FREEDOOM-COPYING.txt" "$STAGE/FREEDOOM-COPYING.txt"
cp "$REPO/wads/FREEDOOM-CREDITS.txt" "$STAGE/FREEDOOM-CREDITS.txt"

if [ -n "${SDL_LIC:-}" ] && [ -f "$SDL_LIC" ]; then
  cp "$SDL_LIC" "$STAGE/LICENSE-SDL2.txt"
else
  cat > "$STAGE/LICENSE-SDL2.txt" <<'EOF'
SDL2 (Simple DirectMedia Layer) is bundled with this release under the zlib
license. Full text: https://github.com/libsdl-org/SDL/blob/SDL2/LICENSE.txt
Copyright (C) 1997-2024 Sam Lantinga and the SDL contributors.
EOF
fi

# ---- launcher ---------------------------------------------------------------
cat > "$STAGE/run-doom.sh" <<'EOF'
#!/usr/bin/env bash
# Launch bundled DOOM. The engine finds its IWAD by hard-coded filename via
# DOOMWADDIR (not -iwad); HOME is pointed at the bundle so config/savegames stay
# self-contained. Pass extra engine args through, e.g. ./run-doom.sh -warp 1 1
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export DOOMWADDIR="$HERE"
export HOME="$HERE"
exec "$HERE/doom" "$@"
EOF
chmod +x "$STAGE/run-doom.sh"

# ---- release readme ---------------------------------------------------------
cat > "$STAGE/README-RELEASE.txt" <<EOF
DOOM (Modernized) — ${VERSION} — ${OS}/${ARCH}
================================================================================

Run:
    ./run-doom.sh
(extra engine args pass through, e.g.  ./run-doom.sh -warp 1 1 -skill 4)

This build is self-contained: the engine, the SDL2 library it needs, and the
game data are all inside this folder. No system SDL2 install is required.

macOS note (unsigned build):
    This binary is ad-hoc signed but NOT notarized, so Gatekeeper will block it
    on first launch. Clear the download quarantine once, then run:
        xattr -dr com.apple.quarantine "$(pwd)"
        ./run-doom.sh

Linux note:
    Built on the oldest supported Ubuntu LTS runner. It needs a glibc at least
    as new as that runner's. If you hit a GLIBC_x.xx or missing-library error on
    an older/minimal distro, install SDL2 from your package manager
    (e.g. 'sudo apt-get install libsdl2-2.0-0') and run ./doom directly.

Game data:
    doom1.wad in this folder is Freedoom Phase 1 (a free, BSD-licensed
    replacement for the commercial DOOM IWAD), renamed to doom1.wad because the
    1997 engine searches for that exact filename. It is NOT commercial DOOM data.
    To play with your own DOOM WAD instead, replace doom1.wad with it.

Licenses (this folder):
    LICENSE.TXT           DOOM engine — GNU GPL v2 (Copyright ZeniMax Media Inc.)
    FREEDOOM-COPYING.txt  Freedoom game data — 3-clause BSD (Freedoom is an
                          independent project and does not endorse this build)
    FREEDOOM-CREDITS.txt  Freedoom contributor credits
    LICENSE-SDL2.txt      SDL2 — zlib license

Corresponding source (GPLv2) for this exact build:
    https://github.com/samqbush/doomv2/tree/${VERSION}
    (also attached to the release as doom-${VERSION}-source.tar.gz)
EOF

# ---- dependency audit: no leaked build/Homebrew paths -----------------------
echo ">> dependency audit"
if [ "$OS" = linux ]; then
  AUDIT="$(readelf -d "$STAGE/doom"; ldd "$STAGE/doom" 2>&1 || true)"
  echo "$AUDIT" | grep -q "not found" && { echo "FAIL: unresolved lib in bundle"; echo "$AUDIT"; exit 1; }
else
  # Audit the binary AND every vendored dylib (dylibbundler-copied SDL2/SDL3/...).
  AUDIT="$(otool -L "$STAGE/doom"; otool -l "$STAGE/doom" | grep -A2 LC_RPATH || true)"
  for dy in "$STAGE"/lib/*.dylib; do
    AUDIT="$AUDIT
$(otool -L "$dy")"
  done
fi
if echo "$AUDIT" | grep -Eq "$FORBIDDEN"; then
  echo "FAIL: binary leaks an absolute build/Homebrew path:"
  echo "$AUDIT" | grep -E "$FORBIDDEN"
  exit 1
fi
echo "$AUDIT"

# ---- tar it up --------------------------------------------------------------
mkdir -p "$OUT_DIR"
TARBALL="$OUT_DIR/${STAGE_NAME}.tar.gz"
tar -C "$(dirname "$STAGE")" -czf "$TARBALL" "$STAGE_NAME"
echo ">> wrote $TARBALL"
ls -lh "$TARBALL"
