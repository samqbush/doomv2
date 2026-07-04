# Oracle strategy — how DOOM behavior is pinned across the port

This is the single reference for **what our correctness oracle is, when each layer
lands, and what it does *not* cover.** It records decisions made with the
maintainer that refine `MODERNIZATION_PLAN.md` Phase 0.

## Regime

Phase 0 is **pre-testability ("dark")**: the engine does not build yet, so there
is no test gate. Phase 0 exits on a **safety-ladder rung** — a captured, purely
additive data/seam snapshot — not on a green test suite.

**Rung achieved: L2-data / seam baseline.** Deliberately *not* called an "L2
golden master": Phase 0 captures **no running behavior**. WAD CRC-32s and
source-derived seam contracts are a reproducible *data* baseline, not a
*behavioral* one.

## Decision 1 — IWAD = Freedoom, no original DOOM data

`wads/freedoom1.wad` (Freedoom v0.13.0, BSD-licensed), pinned in `IWAD_PIN.md`.
Chosen to resolve the §9 licensing blocker (freely committable). We never had
original-DOOM demos, so nothing behavioral is lost by not using original data.

## Decision 2 — **No external source port** (maintainer's explicit choice)

We do **not** install or run Chocolate Doom or any other reimplementation as an
independent reference. This is the defining constraint on the oracle and forces a
**layered, degradable** model:

### Layer 1 — Static seam contracts (captured NOW, Phase 0)

Everything derivable from the data file + source **without executing an engine**:

- WAD lump directory with per-lump **CRC-32** (`wad-directory.tsv`).
- Demo LMP byte-stream structure + per-lump classification (`demo-contract.md`).
- 320×200 8-bit framebuffer + `PLAYPAL`/`COLORMAP` sizes (`SEAM_CONTRACTS.md` §4).
- `ticcmd_t`, savegame layout (as intent), RNG table + 35 Hz tic invariants.

This is the **bedrock** oracle: independent of any binary, and it de-risks the
WAD parser (endian/offset/duplicate handling), accidental IWAD drift, and
palette/framebuffer assumptions in the coming SDL2 port.

### Layer 3 — Self-frozen behavioral golden master (DEFERRED to end of Phase 2)

The **first time our own modernized engine boots and replays a demo cleanly**
(end of Phase 2 — the core's Testability Milestone), we record *that* run's
end-of-demo **consistency checksum** + a few **frame hashes** and freeze them as
the **provisional golden master**. Every later *lit* phase diffs against it
(`ctest -R demo-parity`, `ctest -R frame-smoke`).

**Named, accepted residual risk:** with no independent reference, the *initial*
port cannot be behaviorally blessed — Layer 3 only guarantees **self-consistency
of all subsequent refactors**, not first-boot correctness. Layer 1 partially
offsets this (a corrupt WAD read or wrong palette size is caught statically), but
a subtle playsim regression baked in before the first frozen frame would be
invisible. This is the exact risk `MODERNIZATION_PLAN.md` Phase 0 already names;
here it is the **primary** model, not a fallback.

## Decision 3 — The demo oracle is vaporware until Phase 2 (and here's why)

Empirically confirmed by `tools/wad_oracle.py`: the pinned IWAD's `DEMO1`–`DEMO4`
are **version 109** (vanilla 1.9), but this engine's `VERSION` is **110**
(`doomdef.h`), so `G_DoPlayDemo` **rejects all four**. Therefore:

- No demo can be replayed or checksummed in Phase 0 — there is nothing to run and
  nothing that would be accepted even if we could run it.
- The **demo-parity seed is deferred to Phase 2**, where we will **record a fresh
  demo** against the modernized engine (a native version-110 demo) and freeze it
  as the Layer-3 master.
- Because a single bundled demo also under-exercises the engine, Phase 2/3 should
  seed a **small deterministic suite** (title loop, E1M1-style gameplay for N
  tics, doors/lifts/platforms, enemy/projectile interaction, item pickup,
  intermission transition) rather than trusting one demo.

## What Phase 0 deliberately does NOT do

- Run any engine, port, or period binary.
- Capture any frame or checksum from execution.
- Claim byte-exact savegame preservation (documented as intent only —
  `SEAM_CONTRACTS.md` §6).
- Modify a single line of engine code (assert: purely additive — only `wads/`,
  `tools/`, `docs/oracle/`).

## Layer summary

| Layer | What | When | Independence |
|---|---|---|---|
| 1 — static seam/data baseline | WAD CRCs, seam contracts, RNG/tic invariants | **Phase 0 (now)** | Independent of any binary |
| 2 — original-binary capture | run the actual legacy binary in a period env | *not pursued* (maintainer declined external ports) | Independent |
| 3 — self-frozen golden master | first-good-boot demo checksum + frame hashes | **end of Phase 2** | Self-referential (named risk) |
