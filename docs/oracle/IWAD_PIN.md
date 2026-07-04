# IWAD pin — Phase 0 oracle input

The reference IWAD used as the Phase 0 data/seam baseline and (from Phase 2) the
demo-parity / frame-hash target. Pinned so any drift is detectable.

## Pinned artifact

| Field | Value |
|---|---|
| File (committed) | `wads/freedoom1.wad` |
| Size (bytes) | `28795076` |
| SHA-256 | `7323bcc168c5a45ff10749b339960e98314740a734c30d4b9f3337001f9e703d` |
| WAD ident | `IWAD` |
| Lump count | `3163` |
| Directory offset | `28744468` |

Verify at any time:

```sh
shasum -a 256 wads/freedoom1.wad
# 7323bcc168c5a45ff10749b339960e98314740a734c30d4b9f3337001f9e703d
```

## Upstream provenance

| Field | Value |
|---|---|
| Project | Freedoom (https://github.com/freedoom/freedoom) |
| Release | `v0.13.0` ("Freedoom 0.13.0") |
| Release asset | `freedoom-0.13.0.zip` |
| Asset SHA-256 | `3f9b264f3e3ce503b4fb7f6bdcb1f419d93c7b546f4df3e874dd878db9688f59` |
| Download | https://github.com/freedoom/freedoom/releases/download/v0.13.0/freedoom-0.13.0.zip |
| License | Modified (3-clause) BSD — see `wads/FREEDOOM-COPYING.txt` |
| Attribution | `wads/FREEDOOM-CREDITS.txt` |

The asset SHA-256 matches the upstream signed `freedoom-0.13.0-CHECKSUM`
(PGP-signed `SHA256 (freedoom-0.13.0.zip) = 3f9b264f…`). `freedoom1.wad` is the
single-episode ("Phase 1"/shareware-analog) IWAD extracted from that archive.

## Why Freedoom (not original DOOM)

- **Licensing:** BSD → freely committable to this repo; resolves
  `MODERNIZATION_PLAN.md` §9 (CI IWAD licensing) with no legal ambiguity.
- **We never had original demos anyway** (see `ORACLE_STRATEGY.md`), so losing
  original-DOOM demo compatibility costs the oracle nothing.

## Runtime gotcha to resolve in Phase 2 (not Phase 0)

The legacy engine identifies IWADs by **hard-coded filename** in
`linuxdoom-1.10/d_main.c` (`IdentifyVersion`, ~L563–706) — `doom.wad`,
`doom1.wad`, `doomu.wad`, `doom2.wad`, … — and the filename also sets `gamemode`
(shareware/registered/commercial/retail), which affects episode/map behavior.
`freedoom1.wad` will **not** auto-detect.

**Decision (recorded now, actioned in Phase 2):** keep the committed artifact
named `freedoom1.wad`; at Phase 2 runtime, present it to the engine under a
recognized name — `doom1.wad` (→ shareware `gamemode`, single episode, matching a
`freedoom1.wad` layout) — via copy/symlink or the resolved `-iwad` path. Do not
leave this implicit in Phase 2.
