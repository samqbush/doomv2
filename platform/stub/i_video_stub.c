// Phase 1 modernization stub: video platform layer.
//
// Replaces the dead X11/XShm 8-bit-PseudoColor i_video.c so the portable core
// links on a modern 64-bit toolchain. No-op only: no window, no blit. The real
// SDL2 backend arrives in Phase 2. Owns exactly the symbols the original
// i_video.c defined (including I_StartFrame/I_StartTic, which i_system.h
// declares but i_video.c historically implemented).

#include "doomdef.h"
#include "i_video.h"

void I_InitGraphics(void)
{
}

void I_ShutdownGraphics(void)
{
}

void I_StartFrame(void)
{
}

void I_StartTic(void)
{
}

void I_UpdateNoBlit(void)
{
}

void I_FinishUpdate(void)
{
}

void I_ReadScreen(byte* scr)
{
    (void)scr;
}

void I_SetPalette(byte* palette)
{
    (void)palette;
}
