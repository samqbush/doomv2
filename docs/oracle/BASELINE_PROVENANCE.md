# Baseline provenance — captured at start of Phase 0

Cheap, useful reference state captured before any modernization work. **Not a
test gate** — just a pinned snapshot of "where we started" so later phases can
diff against it.

## Source tree

| Field | Value |
|---|---|
| Repository | `samqbush/doomv2` |
| Branch (Phase 0) | `phase-0-oracle-baseline` (cut from `main`) |
| HEAD commit | `0c02879ee9686301bd7278150058c8aef639cbe5` |
| `linuxdoom-1.10/` tree hash | `e75a7378737762492a22b81644f856eddd7f5c13` |
| Engine files modified in Phase 0 | **0** (purely additive) |

The `linuxdoom-1.10/` tree hash pins every WAD-independent constant table by
reference (see below) — we freeze these by pinning the source, not by
hand-checksumming each table.

## Toolchain probe (this machine)

| Field | Value |
|---|---|
| Compiler | Apple clang version 21.0.0 (clang-2100.1.1.101) |
| Host OS | Darwin (macOS) |
| Build system present | GNU Make only (no CMake/CI/lint/test yet) |

## Current build failure (the feasibility-spike stop point)

`make` in `linuxdoom-1.10/` fails. Two independent failures observed:

1. **Missing output dir** (trivial): `make` invokes
   `gcc … -c doomdef.c -o linux/doomdef.o` but `linux/` does not exist →
   `unable to open output file 'linux/doomdef.o'`.
2. **Dead header** (the real blocker): any TU that includes `doomtype.h` (or
   `m_bbox.h`) under `-DLINUX` fails on a removed libc5-era header:

   ```
   In file included from z_zone.c:28:
   In file included from ./i_system.h:26:
   In file included from ./d_ticcmd.h:26:
   ./doomtype.h:42:10: fatal error: 'values.h' file not found
   ```

   `#include <values.h>` is guarded by `#ifdef LINUX` (`doomtype.h:39–42`,
   `m_bbox.h:26`); the Makefile defines `-DLINUX`, so the guard fires. `values.h`
   was dropped from modern glibc/musl/macOS. Referenced in exactly **2 files**:
   `doomtype.h`, `m_bbox.h`. Direct confirmation:

   ```
   $ echo '#include <values.h>' | gcc -xc -c - -o /dev/null
   fatal error: 'values.h' file not found
   ```

This matches `MODERNIZATION_PLAN.md` §3.1 and is the first thing Phase 1 fixes
(`values.h` → `<limits.h>`/`<float.h>` + `MAXINT`/`MININT`/`MINSHORT` shims).

## WAD-independent constant tables (inventory, not checksummed)

Frozen by the source tree hash above; listed so frame/playsim regressions have a
known suspect list:

- **RNG:** `m_random.c` `rndtable[256]` — the playsim determinism oracle
  (`SEAM_CONTRACTS.md` §5).
- **Trig/geometry:** `tables.c` (`finesine`, `finecosine`, `finetangent`,
  `tantoangle`) — angle/BAM math driving the renderer and playsim.
- **Gamma/palette handling:** `v_video.c` `gammatable`.
- **Playsim/render definition tables:** `info.c` (`states[]`, `mobjinfo[]`,
  `sprnames[]`), `sounds.c` (`S_sfx[]`, `S_music[]`).

These are **WAD-independent**: they live in source, not the IWAD, so they are not
covered by `wad-directory.tsv` CRCs. No per-table checksum is taken (cheap only if
free; it isn't) — the source tree hash is the freeze.

## How to reproduce this snapshot

```sh
git rev-parse HEAD
git rev-parse HEAD:linuxdoom-1.10
gcc --version
cd linuxdoom-1.10 && gcc -DNORMALUNIX -DLINUX -c z_zone.c -o /tmp/o.o   # values.h failure
shasum -a 256 ../wads/freedoom1.wad                                     # IWAD pin
python3 ../tools/wad_oracle.py ../wads/freedoom1.wad --summary          # WAD/demo baseline
```
