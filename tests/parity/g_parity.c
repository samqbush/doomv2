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
#include "doomstat.h"
#include "m_argv.h"
#include "r_state.h"
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

static unsigned long long G_ParityChecksum(void)
{
    thinker_t* th;
    int i;

    fnv = FNV64_OFFSET;

    // Level / progression identity.
    h_u32((unsigned int)gameepisode);
    h_u32((unsigned int)gamemap);
    h_u32((unsigned int)gametic);
    h_u32((unsigned int)leveltime);
    // RNG cursor: any divergence in RNG consumption shifts this.
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

void G_ParityCheckAndExit(void)
{
    unsigned long long sum;
    char got[32];
    int p;

    if (!G_ParityEnabled())
        return;

    sum = G_ParityChecksum();
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
