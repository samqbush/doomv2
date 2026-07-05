# Releasing

Downloadable macOS + Linux builds are produced by
[`.github/workflows/release.yml`](../.github/workflows/release.yml), triggered by
pushing a version tag `v*`. This doc is the checklist + the release-notes
template.

## Checklist

1. Make sure the change is **merged to `main`** and CI is **green on `main`**.
   (`main` is branch-protected — you never push commits to it, and you don't need
   to: the release workflow creates the tag + Release for you.)
2. Pick a version (`vMAJOR.MINOR.PATCH`): PATCH = fixes, MINOR = new user-visible
   capability, MAJOR = a break in the WAD/demo data contract or CLI. Pre-releases
   use a `-rcN` / `-beta` suffix.
3. Trigger the release — either way works, both from CI-green `main`:

   **Recommended — the button (no local git):** GitHub → **Actions** →
   **Release** → **Run workflow** → branch `main`, version `v1.0.0`. The workflow
   tags `main`'s HEAD and publishes. Works under branch protection.

   **Alternative — push a tag** (a tag is not the `main` branch, so branch
   protection doesn't block it):
   ```bash
   git fetch origin
   git tag v1.0.0 origin/main
   git push origin v1.0.0
   ```
4. The workflow builds on `ubuntu-22.04` + `macos-latest`, runs the full parity
   gate (a red gate blocks the release), packages each platform's installable
   app, smoke-boots it headlessly, and publishes the GitHub Release with:
   - `DOOM-<tag>-x86_64.AppImage`   (Linux, single-file double-clickable)
   - `doom-<tag>-macos-arm64.dmg`   (macOS, DOOM.app + drag-to-Applications)
   - `doom-<tag>-source.tar.gz`     (GPLv2 corresponding source)
5. Edit the published release body with the notes below.

The **Run workflow** button also serves as a dry-run: supply a version, and it
builds + packages + publishes a **draft** (no git tag) you can inspect/delete.
Only a pushed `v*` tag produces a public, downloadable release.

## Release-notes template

```markdown
## DOOM (Modernized) <version>

Ready-to-run builds of the 1997 id Software DOOM engine on a modern 64-bit
toolchain (C11 + SDL2 + CMake). The software renderer, 35 Hz simulation, and
demo/netgame determinism are preserved; only the dead platform layer was
replaced.

### Downloads
- **macOS (Apple Silicon):** `doom-<version>-macos-arm64.dmg`
  Open the .dmg, drag DOOM.app to Applications, launch it. Unsigned build: on
  first launch right-click DOOM.app -> Open (or `xattr -dr
  com.apple.quarantine /Applications/DOOM.app`).
- **Linux (x86_64):** `DOOM-<version>-x86_64.AppImage`
  `chmod +x DOOM-*.AppImage && ./DOOM-*.AppImage` (needs FUSE; else add
  `--appimage-extract-and-run`).

Each app is self-contained (engine + SDL2 + Freedoom `doom1.wad`). Config and
saves go to a per-user data dir (macOS `~/Library/Application Support/DOOM`,
Linux `${XDG_DATA_HOME:-~/.local/share}/doom`).

### Changes
- <bullet per user-visible change>

### Verification
- Full parity gate green on Linux + macOS (demo-parity `a00552bbf22274a2`,
  2-player `e8ca533e8baf4ad4`).
- Packaged app smoke-booted headlessly in CI.

### Licensing
Engine GPLv2 (source: `doom-<version>-source.tar.gz`); bundled game data is
Freedoom (3-clause BSD, renamed to `doom1.wad`); SDL2 zlib.
```
