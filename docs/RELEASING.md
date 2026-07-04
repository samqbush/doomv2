# Releasing

Downloadable macOS + Linux builds are produced by
[`.github/workflows/release.yml`](../.github/workflows/release.yml), triggered by
pushing a version tag `v*`. This doc is the checklist + the release-notes
template.

## Checklist

1. Make sure the change is **merged to `main`** and CI is **green on `main`**.
2. Pick a version (`vMAJOR.MINOR.PATCH`): PATCH = fixes, MINOR = new user-visible
   capability, MAJOR = a break in the WAD/demo data contract or CLI. Pre-releases
   use a `-rcN` / `-beta` suffix.
3. Tag from `main` and push:
   ```bash
   git checkout main && git pull
   git tag -a v1.0.0 -m "DOOM (Modernized) v1.0.0"
   git push origin v1.0.0
   ```
4. The workflow builds on `ubuntu-22.04` + `macos-latest`, runs the full parity
   gate (a red gate blocks the release), packages each platform tarball, smoke-
   boots the packaged tarball headlessly, and publishes the GitHub Release with:
   - `doom-<tag>-linux-x86_64.tar.gz`
   - `doom-<tag>-macos-arm64.tar.gz`
   - `doom-<tag>-source.tar.gz` (GPLv2 corresponding source)
5. Edit the published release body with the notes below.

To dry-run without tagging, use the workflow's **Run workflow** button
(`workflow_dispatch`) and supply a version — it builds + packages but you can
delete the resulting draft/assets.

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
