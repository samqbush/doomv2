#!/usr/bin/env bash
#
# package_release.sh — assemble an installable, downloadable DOOM release.
#
# Produces a ready-to-run app that needs NO system SDL2 install:
#   macOS  ->  doom-<ver>-macos-<arch>.dmg   (DOOM.app + drag-to-Applications)
#   Linux  ->  DOOM-<ver>-<arch>.AppImage    (single-file, double-clickable)
#
# Both bundle: the engine binary, vendored libSDL2 (+ transitive deps), the
# Freedoom Phase 1 data (as the engine's hard-coded doom1.wad name), an app icon,
# and all license files. The engine finds its IWAD by hard-coded filename via
# DOOMWADDIR (NOT -iwad) and writes config to $HOME/.doomrc + savegames to cwd, so
# each app's launcher points DOOMWADDIR at the read-only bundled data and points
# HOME + cwd at a writable per-user data dir.
#
# On macOS, install_name_tool/dylibbundler invalidates Mach-O signatures, so the
# binary + dylibs are ad-hoc re-signed. A dependency audit fails the build if the
# binary still references a build-tree or Homebrew path.
#
# Usage: tools/package_release.sh <version> [build-dir] [output-dir]
#   version    e.g. v1.0.0  (defaults to $GITHUB_REF_NAME, else "dev")
#   build-dir  CMake build dir with ./doom  (default: build)
#   output-dir where the .dmg / .AppImage is written (default: dist)
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
  # macOS: vendor libSDL2 AND all its non-system transitive deps. The release
  # build links REAL SDL2 (built from source; see .github/workflows/release.yml),
  # NOT Homebrew's `sdl2` -- which is now an alias for sdl2-compat, a shim whose
  # libSDL2-2.0.0.dylib dlopens libSDL3 at runtime. dylibbundler only follows
  # link-time deps, so it can't vendor a dlopen'd SDL3; a sdl2-compat bundle then
  # abort()s in SDL2 `dllinit` on any Mac lacking a system SDL3. dylibbundler
  # still walks the whole graph here, copies every non-system dylib into lib/, and
  # rewrites install names to @executable_path/lib (which resolves the same for
  # the executable and every nested dylib, since we always launch ./doom from the
  # bundle root).
  SDL_REF="$(otool -L "$STAGE/doom" | awk '/libSDL2/{print $1; exit}')"
  [ -n "$SDL_REF" ] || { echo "FAIL: binary does not link libSDL2 (otool)"; exit 1; }
  command -v dylibbundler >/dev/null || { echo "FAIL: dylibbundler required on macOS (brew install dylibbundler)"; exit 1; }
  # Resolve the on-disk directory holding the real libSDL2 so dylibbundler can
  # find it. A source-built SDL2 uses an @rpath install name (SDL_REF starts with
  # @rpath/@loader_path), which dylibbundler cannot resolve on its own -- without
  # a `-s` search path it drops into an interactive "does not exist" prompt loop.
  # Probe, in order: an absolute install name, $CMAKE_PREFIX_PATH/lib (set by the
  # build/CI env), then the binary's baked-in LC_RPATH entries (expanding
  # @loader_path/@executable_path relative to the binary's own dir). We do NOT
  # fall back to Homebrew: `brew --prefix sdl2` is sdl2-compat, and bundling it
  # would silently reproduce the SDL3 dllinit abort() this fix exists to prevent.
  SDL_LEAF="$(basename "$SDL_REF")"
  BIN_DIR="$(cd "$(dirname "$STAGE/doom")" && pwd)"
  SDL_LIBDIR=""
  case "$SDL_REF" in
    /*) SDL_LIBDIR="$(dirname "$SDL_REF")" ;;
  esac
  if [ -z "$SDL_LIBDIR" ] && [ -n "${CMAKE_PREFIX_PATH:-}" ] && [ -f "$CMAKE_PREFIX_PATH/lib/$SDL_LEAF" ]; then
    SDL_LIBDIR="$CMAKE_PREFIX_PATH/lib"
  fi
  if [ -z "$SDL_LIBDIR" ]; then
    while read -r rp; do
      [ -n "$rp" ] || continue
      # @loader_path and @executable_path both resolve to the binary's dir here
      # (the binary is the loader); expand them so the file test can succeed.
      case "$rp" in
        @loader_path/*|@executable_path/*) rp="$BIN_DIR/${rp#@*/}" ;;
        @loader_path|@executable_path)     rp="$BIN_DIR" ;;
      esac
      if [ -f "$rp/$SDL_LEAF" ]; then SDL_LIBDIR="$rp"; break; fi
    done < <(otool -l "$STAGE/doom" | awk '/LC_RPATH/{r=1;next} r&&/path/{print $2;r=0}')
  fi
  [ -n "$SDL_LIBDIR" ] || { echo "FAIL: could not locate a self-contained on-disk $SDL_LEAF (ref: $SDL_REF). Set CMAKE_PREFIX_PATH to a real SDL2 (not Homebrew sdl2-compat)."; exit 1; }
  echo "Using SDL2 from: $SDL_LIBDIR/$SDL_LEAF"
  ( cd "$STAGE" && dylibbundler -of -cd -b -x ./doom -d ./lib -p @executable_path/lib/ -s "$SDL_LIBDIR" >/dev/null )
  # dylibbundler rewrote load commands -> every Mach-O signature is now invalid.
  # Ad-hoc re-sign each vendored dylib and the binary, then verify.
  for f in "$STAGE"/lib/*.dylib; do codesign --force --sign - "$f"; done
  codesign --force --sign - "$STAGE/doom"
  codesign --verify --strict "$STAGE/doom" || { echo "FAIL: ad-hoc codesign verify"; exit 1; }
  # SDL2 license: probe common locations relative to the resolved lib dir
  # (source-built SDL2 puts LICENSE.txt at the prefix root; Homebrew keeps it in
  # the keg). Falls back to a pointer file below if none is found.
  SDL_LIC=""
  for cand in "$SDL_LIBDIR/../LICENSE.txt" "$SDL_LIBDIR/../share/licenses/SDL2/LICENSE.txt" "$SDL_LIBDIR/LICENSE.txt"; do
    [ -f "$cand" ] && { SDL_LIC="$cand"; break; }
  done
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

# The per-platform launchers (macOS .app stub, Linux AppRun) are written by the
# format builders below. The engine finds its IWAD by hard-coded filename via
# DOOMWADDIR (not -iwad) and writes its config to $HOME/.doomrc + savegames to the
# cwd, so each launcher points DOOMWADDIR at the read-only bundled data and points
# HOME + cwd at a writable per-user data dir.

# ---- release readme ---------------------------------------------------------
cat > "$STAGE/README-RELEASE.txt" <<EOF
DOOM (Modernized) — ${VERSION} — ${OS}/${ARCH}
================================================================================

How to install & play:
    macOS  — open the .dmg, drag DOOM.app to Applications, then launch it.
             (Unsigned build: on first launch right-click DOOM.app → Open, or run
              'xattr -dr com.apple.quarantine /Applications/DOOM.app' once.)
    Linux  — make the .AppImage executable and run it:
                 chmod +x DOOM-*.AppImage && ./DOOM-*.AppImage

This build is self-contained: the engine, the SDL2 library it needs, and the
game data are all inside the app. No system SDL2 install is required.

Where your saves/config live:
    Config and savegames are written to a per-user data dir so the app itself
    stays read-only:
        macOS  ~/Library/Application Support/DOOM
        Linux  \${XDG_DATA_HOME:-~/.local/share}/doom

Linux note:
    The AppImage is built on the oldest supported Ubuntu LTS runner and needs a
    glibc at least as new as that runner's, plus FUSE to self-mount (most desktop
    distros have it). If FUSE is missing, run './DOOM-*.AppImage --appimage-extract-and-run'.

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

# ---- package into an installable format -------------------------------------
mkdir -p "$OUT_DIR"
VER_NUM="${VERSION#v}"                       # 1.0.0 (Info.plist wants a bare number)
ICON_PNG="$REPO/packaging/doom.png"

if [ "$OS" = macos ]; then
  # -------- macOS: DOOM.app inside a drag-to-Applications .dmg ---------------
  APPROOT="$(mktemp -d)"
  APP="$APPROOT/DOOM.app"
  mkdir -p "$APP/Contents/MacOS/lib" "$APP/Contents/Resources"

  cp "$STAGE/doom"        "$APP/Contents/MacOS/doom"
  cp "$STAGE"/lib/*.dylib "$APP/Contents/MacOS/lib/" 2>/dev/null || true
  cp "$STAGE/doom1.wad"   "$APP/Contents/Resources/doom1.wad"
  cp "$STAGE/README-RELEASE.txt" "$STAGE/LICENSE.TXT" "$STAGE/FREEDOOM-COPYING.txt" \
     "$STAGE/FREEDOOM-CREDITS.txt" "$STAGE/LICENSE-SDL2.txt" "$APP/Contents/Resources/"

  # App icon: build doom.icns from the committed PNG.
  if [ -f "$ICON_PNG" ]; then
    ICONSET="$(mktemp -d)/doom.iconset"; mkdir -p "$ICONSET"
    for s in 16 32 128 256 512; do
      sips -z "$s" "$s"           "$ICON_PNG" --out "$ICONSET/icon_${s}x${s}.png"    >/dev/null
      sips -z "$((s*2))" "$((s*2))" "$ICON_PNG" --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
    done
    iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/doom.icns"
  fi

  # Launcher (CFBundleExecutable). NB: macOS filesystems are case-insensitive, so
  # the launcher must NOT be named "doom"/"DOOM" or it collides with the engine
  # binary "doom" (same file) and exec's itself. Use a distinct name.
  # It sets DOOMWADDIR to read-only Resources and points HOME + cwd at a writable
  # per-user data dir (config -> ~/.doomrc, saves -> cwd).
  cat > "$APP/Contents/MacOS/doom-launcher" <<'LAUNCH'
#!/bin/bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
RES="$(cd "$HERE/../Resources" && pwd)"
DATA="$HOME/Library/Application Support/DOOM"
mkdir -p "$DATA"
export DOOMWADDIR="$RES"
export HOME="$DATA"
cd "$DATA"
exec "$HERE/doom" "$@"
LAUNCH
  chmod +x "$APP/Contents/MacOS/doom-launcher"

  cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key>              <string>DOOM</string>
  <key>CFBundleDisplayName</key>       <string>DOOM</string>
  <key>CFBundleExecutable</key>        <string>doom-launcher</string>
  <key>CFBundleIdentifier</key>        <string>com.github.samqbush.doom</string>
  <key>CFBundleIconFile</key>          <string>doom</string>
  <key>CFBundlePackageType</key>       <string>APPL</string>
  <key>CFBundleShortVersionString</key><string>${VER_NUM}</string>
  <key>CFBundleVersion</key>           <string>${VER_NUM}</string>
  <key>LSMinimumSystemVersion</key>    <string>11.0</string>
  <key>NSHighResolutionCapable</key>   <true/>
  <key>NSPrincipalClass</key>          <string>NSApplication</string>
</dict>
</plist>
PLIST

  # Ad-hoc re-sign inner Mach-O, then the whole bundle (best-effort on the
  # script main executable). Gatekeeper still requires the documented unquarantine
  # step because this is unsigned/un-notarized.
  for f in "$APP"/Contents/MacOS/lib/*.dylib; do codesign --force --sign - "$f" 2>/dev/null || true; done
  codesign --force --sign - "$APP/Contents/MacOS/doom"
  codesign --force --sign - "$APP" 2>/dev/null || echo ">> note: bundle ad-hoc sign skipped (script main executable)"

  # Build the .dmg: DOOM.app + an /Applications symlink for drag-installation.
  DMG_SRC="$(mktemp -d)"
  cp -R "$APP" "$DMG_SRC/DOOM.app"
  ln -s /Applications "$DMG_SRC/Applications"
  DMG="$OUT_DIR/doom-${VERSION}-macos-${ARCH}.dmg"
  rm -f "$DMG"
  hdiutil create -volname "DOOM ${VERSION}" -srcfolder "$DMG_SRC" \
    -ov -format UDZO "$DMG" >/dev/null
  echo ">> wrote $DMG"
  ls -lh "$DMG"

else
  # -------- Linux: single-file AppImage -------------------------------------
  APPDIR="$(mktemp -d)/DOOM.AppDir"
  mkdir -p "$APPDIR/usr/bin/lib" "$APPDIR/usr/share/doom" \
           "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/1024x1024/apps"

  cp "$STAGE/doom"        "$APPDIR/usr/bin/doom"
  cp "$STAGE"/lib/*        "$APPDIR/usr/bin/lib/" 2>/dev/null || true
  cp "$STAGE/doom1.wad"   "$APPDIR/usr/share/doom/doom1.wad"
  cp "$STAGE/README-RELEASE.txt" "$STAGE/LICENSE.TXT" "$STAGE/FREEDOOM-COPYING.txt" \
     "$STAGE/FREEDOOM-CREDITS.txt" "$STAGE/LICENSE-SDL2.txt" "$APPDIR/usr/share/doom/"

  # Icon (top-level + hicolor) — appimagetool wants a top-level .png named after
  # the desktop file's Icon= key.
  if [ -f "$ICON_PNG" ]; then
    cp "$ICON_PNG" "$APPDIR/doom.png"
    cp "$ICON_PNG" "$APPDIR/usr/share/icons/hicolor/1024x1024/apps/doom.png"
  fi

  cat > "$APPDIR/doom.desktop" <<'DESK'
[Desktop Entry]
Type=Application
Name=DOOM
Comment=DOOM (Modernized) — 1997 Linux DOOM 1.10 on SDL2
Exec=doom
Icon=doom
Categories=Game;ActionGame;
Terminal=false
DESK
  cp "$APPDIR/doom.desktop" "$APPDIR/usr/share/applications/doom.desktop"

  # AppRun: DOOMWADDIR -> read-only bundled wad; HOME + cwd -> writable XDG data
  # dir (config -> $HOME/.doomrc, saves -> cwd); $ORIGIN/lib rpath resolves lib/.
  cat > "$APPDIR/AppRun" <<'APPRUN'
#!/bin/bash
set -euo pipefail
HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}/doom"
mkdir -p "$DATA"
export DOOMWADDIR="$HERE/usr/share/doom"
export HOME="$DATA"
cd "$DATA"
exec "$HERE/usr/bin/doom" "$@"
APPRUN
  chmod +x "$APPDIR/AppRun"

  command -v appimagetool >/dev/null || { echo "FAIL: appimagetool required on Linux"; exit 1; }
  APPIMAGE="$OUT_DIR/DOOM-${VERSION}-${ARCH}.AppImage"
  rm -f "$APPIMAGE"
  # ARCH must be exported for appimagetool's runtime selection.
  ARCH="$ARCH" appimagetool "$APPDIR" "$APPIMAGE"
  chmod +x "$APPIMAGE"
  echo ">> wrote $APPIMAGE"
  ls -lh "$APPIMAGE"
fi
