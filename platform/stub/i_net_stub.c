// Phase 1 modernization stub: network platform layer.
//
// Replaces the dead BSD-UDP i_net.c. NOT a pure no-op: D_CheckNetGame()
// dereferences doomcom->id immediately after I_InitNetwork(), so this stub must
// establish the single-player doomcom contract (mirrors the original i_net.c
// "single player game" branch). Real transport arrives in Phase 5.

#include "doomdef.h"
#include "doomstat.h"   // doomcom, netbuffer, netgame
#include "d_net.h"      // doomcom_t, DOOMCOM_ID
#include "i_net.h"

#include <stdlib.h>
#include <string.h>

void I_InitNetwork(void)
{
    doomcom = malloc(sizeof(*doomcom));
    memset(doomcom, 0, sizeof(*doomcom));

    // Single player game (no -net in Phase 1).
    netgame = false;
    doomcom->id = DOOMCOM_ID;
    doomcom->numplayers = doomcom->numnodes = 1;
    doomcom->deathmatch = false;
    doomcom->consoleplayer = 0;
    doomcom->ticdup = 1;
    doomcom->extratics = 0;
}

void I_NetCmd(void)
{
}
