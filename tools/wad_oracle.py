#!/usr/bin/env python3
"""wad_oracle.py -- static WAD/demo seam-contract capture for DOOM Phase 0.

Dependency-free (Python stdlib only). Runs NO engine: everything here is derived
statically from the WAD bytes + the documented on-disk formats, so it is a valid
"dark"-regime oracle (a reproducible data/seam baseline, not a behavioral golden
master -- see docs/oracle/ORACLE_STRATEGY.md).

Formats implemented (cross-referenced to the legacy source):
  * WAD header + directory      -- linuxdoom-1.10/w_wad.h (wadinfo_t, filelump_t)
  * Demo header + tic stream    -- linuxdoom-1.10/g_game.c (G_BeginRecording,
                                    G_ReadDemoTiccmd, DEMOMARKER=0x80)
  * Engine demo VERSION gate     -- linuxdoom-1.10/doomdef.h (VERSION=110)

Outputs (deterministic):
  --dir-tsv PATH     lump directory as TSV (index, name, offset, size, crc32)
  --demo-md PATH     demo-lump contract as Markdown
  --summary          human-readable summary to stdout

Duplicate lump names are preserved and disambiguated by index (WADs legitimately
contain repeated names, e.g. map markers); nothing is de-duplicated or reordered.
"""

import argparse
import struct
import sys
import zlib

# Engine demo version gate: linuxdoom-1.10/doomdef.h -> enum { VERSION = 110 }
ENGINE_VERSION = 110
# linuxdoom-1.10/g_game.c:1488 -> #define DEMOMARKER 0x80
DEMOMARKER = 0x80
# linuxdoom-1.10/doomdef.h:119 -> #define MAXPLAYERS 4
MAXPLAYERS = 4
# Demo header size = 9 fixed bytes + MAXPLAYERS playeringame bytes.
#   G_BeginRecording writes: VERSION, skill, episode, map, deathmatch,
#   respawnparm, fastparm, nomonsters, consoleplayer, then MAXPLAYERS bytes.
DEMO_HEADER_LEN = 9 + MAXPLAYERS
# G_ReadDemoTiccmd consumes 4 bytes/tic: forwardmove, sidemove, angleturn, buttons.
DEMO_BYTES_PER_TIC = 4

SKILL_NAMES = {0: "ITYTD", 1: "HNTR", 2: "HMP", 3: "UV", 4: "NM"}


class WadError(Exception):
    pass


def read_wad(path):
    with open(path, "rb") as fh:
        data = fh.read()
    if len(data) < 12:
        raise WadError("file too small to be a WAD")
    ident, numlumps, infotableofs = struct.unpack_from("<4sii", data, 0)
    ident = ident.decode("ascii", "replace")
    if ident not in ("IWAD", "PWAD"):
        raise WadError("bad WAD identification %r (want IWAD/PWAD)" % ident)
    lumps = []
    for i in range(numlumps):
        off = infotableofs + i * 16
        if off + 16 > len(data):
            raise WadError("directory entry %d runs past EOF" % i)
        filepos, size, raw_name = struct.unpack_from("<ii8s", data, off)
        name = raw_name.split(b"\x00", 1)[0].decode("ascii", "replace")
        in_bounds = size == 0 or (0 <= filepos and filepos + size <= len(data))
        crc = None
        if in_bounds and size > 0:
            crc = zlib.crc32(data[filepos:filepos + size]) & 0xFFFFFFFF
        lumps.append({
            "index": i, "name": name, "offset": filepos, "size": size,
            "crc32": crc, "in_bounds": in_bounds, "raw": raw_name,
        })
    return {
        "ident": ident, "numlumps": numlumps, "infotableofs": infotableofs,
        "filesize": len(data), "lumps": lumps, "_data": data,
    }


def classify_demo(lump, data):
    """Classify a DEMO* lump vs the engine's demo-version gate."""
    if not lump["in_bounds"] or lump["size"] <= 0:
        return {"status": "absent", "reason": "lump empty or out of bounds"}
    blob = data[lump["offset"]:lump["offset"] + lump["size"]]
    if len(blob) < DEMO_HEADER_LEN + 1:
        return {"status": "malformed",
                "reason": "shorter than a demo header (%d bytes)" % len(blob)}
    ver = blob[0]
    (skill, episode, gmap, deathmatch, respawn, fast, nomon,
     console) = blob[1:9]
    playeringame = list(blob[9:9 + MAXPLAYERS])
    body = blob[DEMO_HEADER_LEN:]
    marker_idx = body.find(bytes([DEMOMARKER]))
    if marker_idx < 0 or marker_idx % DEMO_BYTES_PER_TIC != 0:
        # No terminator on a 4-byte boundary -> not this engine's demo layout.
        status = "malformed"
        tics = None
    else:
        tics = marker_idx // DEMO_BYTES_PER_TIC
        status = "compatible" if ver == ENGINE_VERSION else "incompatible_version"
    return {
        "status": status, "version": ver, "skill": skill,
        "skill_name": SKILL_NAMES.get(skill, "?"), "episode": episode,
        "map": gmap, "deathmatch": deathmatch, "respawnparm": respawn,
        "fastparm": fast, "nomonsters": nomon, "consoleplayer": console,
        "playeringame": playeringame, "tics": tics, "byte_length": lump["size"],
        "crc32": lump["crc32"],
    }


def write_dir_tsv(wad, path):
    lines = ["index\tname\toffset\tsize\tcrc32\tin_bounds"]
    for lp in wad["lumps"]:
        crc = "" if lp["crc32"] is None else "%08x" % lp["crc32"]
        lines.append("%d\t%s\t%d\t%d\t%s\t%s" % (
            lp["index"], lp["name"], lp["offset"], lp["size"], crc,
            "yes" if lp["in_bounds"] else "NO"))
    _write(path, "\n".join(lines) + "\n")


def write_demo_md(wad, path):
    data = wad["_data"]
    demos = [lp for lp in wad["lumps"] if lp["name"].upper().startswith("DEMO")]
    out = []
    out.append("# Demo-lump contract (static capture)\n")
    out.append("Generated by `tools/wad_oracle.py` -- no engine executed.\n")
    out.append("Engine demo-version gate: `VERSION = %d` "
               "(`linuxdoom-1.10/doomdef.h`); rejected otherwise in "
               "`g_game.c:G_DoPlayDemo`.\n" % ENGINE_VERSION)
    out.append("Header = %d bytes (9 fixed + %d `playeringame`); "
               "%d bytes/tic; terminator `0x%02X`.\n"
               % (DEMO_HEADER_LEN, MAXPLAYERS, DEMO_BYTES_PER_TIC, DEMOMARKER))
    if not demos:
        out.append("\n**No `DEMO*` lumps present in this IWAD.** "
                   "Demo-parity seed is therefore **deferred to Phase 2**, "
                   "where a demo will be recorded against the modernized "
                   "engine and frozen as the provisional golden master.\n")
        _write(path, "\n".join(out))
        return {"count": 0, "compatible": 0}
    compatible = 0
    for lp in demos:
        c = classify_demo(lp, data)
        out.append("\n## `%s` (index %d)\n" % (lp["name"], lp["index"]))
        out.append("- status: **%s**" % c["status"])
        if c["status"] in ("absent",) or "version" not in c:
            out.append("- reason: %s" % c.get("reason", ""))
            continue
        if c["status"] == "compatible":
            compatible += 1
        crc = "" if c["crc32"] is None else "%08x" % c["crc32"]
        out.append("- version byte: %d (engine wants %d)"
                   % (c["version"], ENGINE_VERSION))
        out.append("- skill: %d (%s), episode: %d, map: %d"
                   % (c["skill"], c["skill_name"], c["episode"], c["map"]))
        out.append("- deathmatch: %d, respawn: %d, fast: %d, nomonsters: %d, "
                   "consoleplayer: %d" % (c["deathmatch"], c["respawnparm"],
                   c["fastparm"], c["nomonsters"], c["consoleplayer"]))
        out.append("- playeringame: %s" % c["playeringame"])
        out.append("- tic count: %s" % ("?" if c["tics"] is None else c["tics"]))
        out.append("- byte length: %d, crc32: %s" % (c["byte_length"], crc))
    _write(path, "\n".join(out) + "\n")
    return {"count": len(demos), "compatible": compatible}


def _write(path, text):
    with open(path, "w") as fh:
        fh.write(text)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("wad", help="path to the IWAD/PWAD")
    ap.add_argument("--dir-tsv", help="write lump directory TSV to this path")
    ap.add_argument("--demo-md", help="write demo contract Markdown to this path")
    ap.add_argument("--summary", action="store_true",
                    help="print a summary to stdout")
    args = ap.parse_args(argv)

    try:
        wad = read_wad(args.wad)
    except (WadError, OSError) as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 2

    playpal = next((l for l in wad["lumps"] if l["name"].upper() == "PLAYPAL"), None)
    colormap = next((l for l in wad["lumps"] if l["name"].upper() == "COLORMAP"), None)

    if args.dir_tsv:
        write_dir_tsv(wad, args.dir_tsv)
    demo_stats = None
    if args.demo_md:
        demo_stats = write_demo_md(wad, args.demo_md)

    if args.summary or (not args.dir_tsv and not args.demo_md):
        print("WAD: %s" % args.wad)
        print("  ident=%s  numlumps=%d  infotableofs=%d  filesize=%d"
              % (wad["ident"], wad["numlumps"], wad["infotableofs"], wad["filesize"]))
        oob = [l for l in wad["lumps"] if not l["in_bounds"]]
        print("  out-of-bounds lumps: %d" % len(oob))
        if playpal:
            n = playpal["size"] / 768.0
            print("  PLAYPAL: size=%d (%s768 => %.1f palettes of 256*RGB)"
                  % (playpal["size"], "" if playpal["size"] % 768 == 0 else "!=", n))
        else:
            print("  PLAYPAL: MISSING")
        if colormap:
            maps = colormap["size"] / 256.0
            print("  COLORMAP: size=%d (%s256 => %.1f maps of 256 bytes; "
                  "vanilla = 34)"
                  % (colormap["size"], "" if colormap["size"] % 256 == 0 else "!=", maps))
        else:
            print("  COLORMAP: MISSING")
        demos = [l for l in wad["lumps"] if l["name"].upper().startswith("DEMO")]
        print("  DEMO* lumps: %d" % len(demos))
        if demo_stats:
            print("  DEMO* compatible with VERSION %d: %d/%d"
                  % (ENGINE_VERSION, demo_stats["compatible"], demo_stats["count"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
