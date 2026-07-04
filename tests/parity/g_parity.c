// Phase 2 modernization: demo-parity oracle hook (implementation).
//
// Canonical world-state digest = a stable-ordered, explicitly-serialized,
// fixed-endianness FNV-1a/64 hash. We deliberately DO NOT hash raw structs
// (padding / pointer bits / host endianness would poison determinism); every
// field is fed big-endian through h_u32/h_u16. Coverage is intentionally broad
// (all live mobjs/thinkers, all players, all sectors + the RNG cursor) so that
// monster / projectile / sector-special regressions cannot pass green -- the
// narrow player-only digest that a first cut might use would miss them.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomtype.h"
#include "doomdef.h"
#include "doomstat.h"
#include "d_ticcmd.h"
#include "m_argv.h"
#include "r_state.h"
#include "v_video.h"
#include "p_local.h"
#include "info.h"
#include "g_parity.h"

// ---- FNV-1a/64 over an explicit big-endian byte stream --------------------

static unsigned long long fnv;

#define FNV64_OFFSET 14695981039346656037ULL
#define FNV64_PRIME  1099511628211ULL

static void h_u8(unsigned int v)
{
    fnv ^= (unsigned char)(v & 0xff);
    fnv *= FNV64_PRIME;
}

static void h_u16(unsigned int v)
{
    h_u8((v >> 8) & 0xff);
    h_u8(v & 0xff);
}

static void h_u32(unsigned int v)
{
    h_u8((v >> 24) & 0xff);
    h_u8((v >> 16) & 0xff);
    h_u8((v >> 8) & 0xff);
    h_u8(v & 0xff);
}

// ---- world-state serialization --------------------------------------------

static void h_mobj(mobj_t* mo)
{
    h_u32((unsigned int)mo->type);
    h_u32((unsigned int)mo->x);
    h_u32((unsigned int)mo->y);
    h_u32((unsigned int)mo->z);
    h_u32((unsigned int)mo->angle);
    h_u32((unsigned int)mo->momx);
    h_u32((unsigned int)mo->momy);
    h_u32((unsigned int)mo->momz);
    h_u32((unsigned int)mo->health);
    h_u32((unsigned int)mo->flags);
    h_u32((unsigned int)mo->tics);
    // State identity as a stable index into the global states[] table
    // (pointers themselves are non-deterministic across runs).
    h_u32((unsigned int)(mo->state ? (int)(mo->state - states) : -1));
    h_u32((unsigned int)mo->movedir);
    h_u32((unsigned int)mo->movecount);
    h_u32((unsigned int)mo->reactiontime);
}

static void h_player(player_t* pl)
{
    int i;

    h_u32((unsigned int)pl->playerstate);
    h_u32((unsigned int)pl->health);
    h_u32((unsigned int)pl->armorpoints);
    h_u32((unsigned int)pl->armortype);
    h_u32((unsigned int)pl->readyweapon);
    h_u32((unsigned int)pl->pendingweapon);
    for (i = 0; i < NUMPOWERS; i++)
        h_u32((unsigned int)pl->powers[i]);
    for (i = 0; i < NUMWEAPONS; i++)
        h_u8(pl->weaponowned[i] ? 1 : 0);
    for (i = 0; i < NUMAMMO; i++)
    {
        h_u32((unsigned int)pl->ammo[i]);
        h_u32((unsigned int)pl->maxammo[i]);
    }
    h_u32((unsigned int)pl->killcount);
    h_u32((unsigned int)pl->itemcount);
    h_u32((unsigned int)pl->secretcount);
    if (pl->mo)
        h_mobj(pl->mo);
    else
        h_u32(0xffffffffu);
}

// prndindex is the *playsim* RNG cursor (P_Random); rndindex is the *cosmetic*
// RNG cursor (M_Random -- sound pitch, status-bar face, menu). See
// m_random.c: P_Random advances prndindex, M_Random advances rndindex. Only
// prndindex is deterministic across lockstep nodes; rndindex depends on the
// listener (consoleplayer) because sound pitch/rejection is distance-gated.
extern int prndindex;

static unsigned long long G_ParityChecksumEx(int netmode)
{
    thinker_t* th;
    int i;

    fnv = FNV64_OFFSET;

    // Level / progression identity.
    h_u32((unsigned int)gameepisode);
    h_u32((unsigned int)gamemap);
    h_u32((unsigned int)gametic);
    h_u32((unsigned int)leveltime);
    // RNG cursor. In single-player demo mode (Phase 2) we hash the historical
    // rndindex to preserve the frozen master. In netgame loopback mode we hash
    // the PLAYSIM cursor (prndindex) instead and deliberately omit rndindex,
    // which legitimately diverges per node (listener-dependent sound pitch) and
    // is cosmetic -- never fed back into the playsim.
    if (netmode)
        h_u32((unsigned int)prndindex);
    else
        h_u32((unsigned int)rndindex);

    // Players, in fixed slot order.
    for (i = 0; i < MAXPLAYERS; i++)
    {
        h_u8(playeringame[i] ? 1 : 0);
        if (playeringame[i])
            h_player(&players[i]);
    }

    // All live mobjs, in thinker-list (spawn) order -- deterministic given
    // deterministic play. Only P_MobjThinker thinkers are map objects.
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
        if (th->function.acp1 == (actionf_p1)P_MobjThinker)
            h_mobj((mobj_t*)th);
    }
    // Sentinel between the mobj stream and the sector stream so the two
    // variable-length regions cannot alias.
    h_u32(0xdeadbeefu);

    // All sectors: moving floors/ceilings + specials show up here.
    for (i = 0; i < numsectors; i++)
    {
        h_u32((unsigned int)sectors[i].floorheight);
        h_u32((unsigned int)sectors[i].ceilingheight);
        h_u16((unsigned int)(unsigned short)sectors[i].special);
        h_u16((unsigned int)(unsigned short)sectors[i].tag);
        h_u16((unsigned int)(unsigned short)sectors[i].lightlevel);
        // Non-null specialdata => an active platform/door/etc. thinker.
        h_u8(sectors[i].specialdata ? 1 : 0);
    }

    return fnv;
}

int G_ParityEnabled(void)
{
    return M_CheckParm("-checkdemo") != 0;
}

// ---- frame hashing (software-renderer output, pre-SDL-blit) ----------------
//
// We hash the indexed 320x200 screens[0] byte-EXACT (no tolerance): it is the
// deterministic software-renderer output, independent of SDL. Determinism
// requires singletics (gametic == frame index), forced by D_DoomMain when
// -framehash is set, plus sampling only outside the level-start melt wipe (the
// wipe loop is driven by wall-clock I_GetTime).

static const int frame_target_tics[] = { 40, 80, 120, 160 };
#define NUM_FRAME_TARGETS ((int)(sizeof(frame_target_tics)/sizeof(frame_target_tics[0])))

static unsigned long long frame_digest = FNV64_OFFSET;
static int frame_captured = 0;
static int frame_last_tic = -1;
static int frame_min_distinct = 1 << 30;   // fewest distinct indices seen

int G_ParityFrameHashEnabled(void)
{
    return M_CheckParm("-framehash") != 0;
}

static void G_ParityFrameDump(const char* path)
{
    // Write screens[0] as a binary PGM (indices as gray) for visual boot-smoke
    // evidence -- structure is recognisable even without the palette applied.
    FILE* f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P5\n%d %d\n255\n", SCREENWIDTH, SCREENHEIGHT);
    fwrite(screens[0], 1, SCREENWIDTH * SCREENHEIGHT, f);
    fclose(f);
}

void G_ParityFrameSample(int wipe_in_progress)
{
    int i;
    const byte* fb;

    if (!G_ParityFrameHashEnabled())
        return;
    if (wipe_in_progress)
        return;
    if (gamestate != GS_LEVEL || gametic == 0)
        return;
    if (gametic == frame_last_tic)
        return;                 // one sample per tic
    frame_last_tic = gametic;

    for (i = 0; i < NUM_FRAME_TARGETS; i++)
    {
        int seen[256];
        int distinct = 0;
        int k, n;

        if (gametic != frame_target_tics[i])
            continue;

        fb = screens[0];
        n = SCREENWIDTH * SCREENHEIGHT;

        // Boot-smoke guard: a real rendered frame has many distinct palette
        // indices. A blank/stale buffer would have ~1 and must NOT pass green.
        memset(seen, 0, sizeof(seen));
        for (k = 0; k < n; k++)
        {
            if (!seen[fb[k]])
            {
                seen[fb[k]] = 1;
                distinct++;
            }
        }
        if (distinct < frame_min_distinct)
            frame_min_distinct = distinct;

        // Fold (gametic, full frame) into the rolling digest, in ascending
        // tic order (guaranteed by singletics).
        fnv = frame_digest;
        h_u32((unsigned int)gametic);
        for (k = 0; k < n; k++)
            h_u8(fb[k]);
        frame_digest = fnv;
        frame_captured++;

        // Optional visual dump of the last target frame.
        {
            int p = M_CheckParm("-framedump");
            if (p && p < myargc - 1 && gametic == frame_target_tics[NUM_FRAME_TARGETS - 1])
                G_ParityFrameDump(myargv[p + 1]);
        }
        break;
    }
}

static void G_ParityFrameFinishAndExit(void)
{
    char got[32];
    int p;

    if (frame_captured != NUM_FRAME_TARGETS)
    {
        fprintf(stderr, "PARITY: framehash captured %d/%d target frames\n",
                frame_captured, NUM_FRAME_TARGETS);
        exit(4);
    }

    // Boot-smoke: refuse to bless a blank/stale framebuffer.
    if (frame_min_distinct < 8)
    {
        fprintf(stderr, "PARITY: frame appears blank (min distinct indices %d)\n",
                frame_min_distinct);
        exit(5);
    }
    fprintf(stderr, "PARITY: frame non-blank (min distinct indices %d)\n",
            frame_min_distinct);

    snprintf(got, sizeof(got), "%016llx", frame_digest);
    printf("PARITY_FRAMEHASH %s\n", got);
    fflush(stdout);

    p = M_CheckParm("-frameref");
    if (p && p < myargc - 1)
    {
        FILE* f = fopen(myargv[p + 1], "r");
        char ref[64];
        if (!f)
        {
            fprintf(stderr, "PARITY: cannot open frameref %s\n", myargv[p + 1]);
            exit(2);
        }
        ref[0] = '\0';
        if (fscanf(f, "%63s", ref) != 1)
        {
            fprintf(stderr, "PARITY: empty frameref %s\n", myargv[p + 1]);
            fclose(f);
            exit(2);
        }
        fclose(f);
        if (strcmp(ref, got) != 0)
        {
            fprintf(stderr, "PARITY: FRAME MISMATCH expected %s got %s\n",
                    ref, got);
            exit(3);
        }
        printf("PARITY: FRAME MATCH %s\n", got);
        fflush(stdout);
    }
}

void G_ParityCheckAndExit(void)
{
    unsigned long long sum;
    char got[32];
    int p;

    if (!G_ParityEnabled() && !G_ParityFrameHashEnabled())
        return;

    if (G_ParityFrameHashEnabled())
        G_ParityFrameFinishAndExit();

    if (!G_ParityEnabled())
        exit(0);

    sum = G_ParityChecksumEx(G_NetScriptEnabled());
    snprintf(got, sizeof(got), "%016llx", sum);
    printf("PARITY_CHECKSUM %s\n", got);
    fflush(stdout);

    p = M_CheckParm("-parityref");
    if (p && p < myargc - 1)
    {
        FILE* f = fopen(myargv[p + 1], "r");
        char ref[64];
        if (!f)
        {
            fprintf(stderr, "PARITY: cannot open reference %s\n", myargv[p + 1]);
            exit(2);
        }
        ref[0] = '\0';
        if (fscanf(f, "%63s", ref) != 1)
        {
            fprintf(stderr, "PARITY: empty reference %s\n", myargv[p + 1]);
            fclose(f);
            exit(2);
        }
        fclose(f);
        if (strcmp(ref, got) != 0)
        {
            fprintf(stderr, "PARITY: MISMATCH expected %s got %s\n", ref, got);
            exit(3);
        }
        printf("PARITY: MATCH %s\n", got);
        fflush(stdout);
    }

    exit(0);
}

// ---- Phase 5: deterministic netgame scripted input + fixed-tic exit --------
//
// A netgame cannot use -playdemo (demos are single-player), so the loopback
// consistency test drives each node's LOCAL player from a scripted ticcmd
// stream and stops both nodes at a fixed gametic to emit the world-state
// checksum. Both are pure test instrumentation, inert unless their flags are
// passed, so normal play and the Phase 2 demo-parity checksum are unchanged.
//
// Script file format (raw bytes, 4 per tic, matching the demo tic encoding in
// g_game.c G_ReadDemoTiccmd):
//     forwardmove (signed char), sidemove (signed char),
//     angleturn>>8 (byte, stored value used directly),
//     buttons (byte)
// The stream is consumed one entry per G_BuildTiccmd call; past the end the
// local player idles (all zeros).

static byte*  script_buf = NULL;
static long   script_len = 0;
static long   script_cursor = 0;
static int    script_loaded = 0;

int G_NetScriptEnabled(void)
{
    int p = M_CheckParm("-scriptcmds");
    return (p != 0 && p < myargc - 1);
}

static void G_NetScriptLoad(void)
{
    int   p;
    FILE* f;
    long  n;

    script_loaded = 1;
    p = M_CheckParm("-scriptcmds");
    if (!p || p >= myargc - 1)
        return;

    f = fopen(myargv[p + 1], "rb");
    if (!f)
    {
        fprintf(stderr, "PARITY: cannot open scriptcmds %s\n", myargv[p + 1]);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0)
    {
        fprintf(stderr, "PARITY: bad scriptcmds %s\n", myargv[p + 1]);
        fclose(f);
        exit(2);
    }
    script_buf = malloc((size_t)n ? (size_t)n : 1);
    if (!script_buf)
    {
        fprintf(stderr, "PARITY: out of memory loading scriptcmds %s\n", myargv[p + 1]);
        fclose(f);
        exit(2);
    }
    if (n && fread(script_buf, 1, (size_t)n, f) != (size_t)n)
    {
        fprintf(stderr, "PARITY: short read on scriptcmds %s\n", myargv[p + 1]);
        fclose(f);
        exit(2);
    }
    fclose(f);
    script_len = n;
}

// Overwrite ONLY the local player's control fields from the script. The
// caller (G_BuildTiccmd) has already set cmd->consistancy from the local
// consistency ring; we deliberately leave it untouched so G_Ticker's
// cross-node consistency check remains a real transport test.
void G_NetScriptApply(ticcmd_t* cmd)
{
    if (!script_loaded)
        G_NetScriptLoad();

    if (script_cursor + 4 <= script_len)
    {
        cmd->forwardmove = (signed char)script_buf[script_cursor + 0];
        cmd->sidemove    = (signed char)script_buf[script_cursor + 1];
        cmd->angleturn   = (short)(((unsigned char)script_buf[script_cursor + 2]) << 8);
        cmd->buttons     = (byte)script_buf[script_cursor + 3];
    }
    else
    {
        cmd->forwardmove = 0;
        cmd->sidemove    = 0;
        cmd->angleturn   = 0;
        cmd->buttons     = 0;
    }
    cmd->chatchar = 0;
    script_cursor += 4;
}

// Called from TryRunTics immediately after a completed game tic (gametic has
// just been incremented). When gametic reaches the -exittic target, emit the
// world-state checksum and exit -- the one deterministic, per-node-identical
// point at which both lockstep nodes hold the same state. Inert unless
// -exittic was passed.
void G_ParityExitTicCheck(void)
{
    int p = M_CheckParm("-exittic");
    int target;

    if (!p || p >= myargc - 1)
        return;
    target = atoi(myargv[p + 1]);
    if (target <= 0)
    {
        fprintf(stderr, "PARITY: -exittic requires a positive integer, got '%s'\n",
                myargv[p + 1]);
        exit(2);
    }
    if (gametic >= target)
        G_ParityCheckAndExit();
}

