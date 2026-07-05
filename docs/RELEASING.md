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
   gate (a red gate blocks the release), packages each platform tarball, smoke-
   boots the packaged tarball headlessly, and publishes the GitHub Release with:
   - `doom-<tag>-linux-x86_64.tar.gz`
   - `doom-<tag>-macos-arm64.tar.gz`
   - `doom-<tag>-source.tar.gz` (GPLv2 corresponding source)
5. Edit the published release body with the notes below.

The **Run workflow** button also serves as a dry-run: supply a version, and it
builds + packages + publishes without any local git.

## Release-notes template

```markdown
## DOOM (Modernized) <version>

Ready-to-run builds of the 1997 id Software DOOM engine on a modern 64-bit
toolchain (C11 + SDL2 + CMake). The software renderer, 35 Hz simulation, and
demo/netgame determinism are preserved; only the dead platform layer was
replaced.

### Downloads
- **macOS (Apple Silicon):** `doom-<version>-macos-arm64.tar.gz`
- **Linux (x86_64):** `doom-<version>-linux-x86_64.tar.gz`

Each archive is self-contained (engine + SDL2 + Freedoom `doom1.wad`). Extract,
then run `./run-doom.sh`. See `README-RELEASE.txt` inside for platform notes
(incl. the macOS Gatekeeper `xattr` step for this unsigned build).

### Changes
- <bullet per user-visible change>

### Verification
- Full parity gate green on Linux + macOS (demo-parity `a00552bbf22274a2`,
  2-player `e8ca533e8baf4ad4`).
- Packaged tarball smoke-booted headlessly in CI.

### Licensing
Engine GPLv2 (source: `doom-<version>-source.tar.gz`); bundled game data is
Freedoom (3-clause BSD, renamed to `doom1.wad`); SDL2 zlib.
```
