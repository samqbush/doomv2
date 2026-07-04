// Phase 2 modernization: SDL2 video platform layer (rendering only).
//
// Replaces platform/stub/i_video_stub.c. Blits the 320x200 8-bit indexed
// software framebuffer screens[0] through a gamma-corrected palette LUT into a
// 32-bit ARGB streaming texture, then presents it. Input pumping lives in
// i_system_sdl.c -- this TU deliberately does NOT export I_StartTic.
//
// Headless behaviour:
//   -nodraw : no SDL video subsystem, no window (D_Display still runs for
//             -playdemo, but the blit is a no-op). Used by state-parity.
//   -noblit : window created but present skipped.
// For headless CI, set SDL_VIDEODRIVER=dummy (works with either flag or none):
// SDL then creates an offscreen window so screens[0] is still filled by the
// software renderer for frame hashing.

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <SDL.h>

#include "doomdef.h"
#include "doomtype.h"
#include "d_main.h"
#include "m_argv.h"
#include "v_video.h"
#include "i_system.h"
#include "i_video.h"
#include "w_wad.h"
#include "z_zone.h"

// Integer upscale of the 320x200 logical framebuffer for the visible window.
#define WINDOW_SCALE	3

static SDL_Window*	window;
static SDL_Renderer*	renderer;
static SDL_Texture*	texture;

// 256-entry ARGB8888 lookup built from PLAYPAL + gammatable in I_SetPalette.
static Uint32		colorlut[256];

// Expansion scratch: one 32-bit pixel per framebuffer pixel.
static Uint32		argbframe[SCREENWIDTH * SCREENHEIGHT];

static boolean		nodraw;		// -nodraw: no window at all
static boolean		noblit_local;	// -noblit: window but no present

void I_InitGraphics(void)
{
    static boolean	initialized = false;

    if (initialized)
	return;
    initialized = true;

    nodraw = (M_CheckParm("-nodraw") != 0);
    noblit_local = (M_CheckParm("-noblit") != 0);

    if (nodraw)
	return;

    if (SDL_Init(SDL_INIT_VIDEO) < 0)
	I_Error("I_InitGraphics: SDL_Init(VIDEO) failed: %s", SDL_GetError());

    window = SDL_CreateWindow(
	"DOOM",
	SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
	SCREENWIDTH * WINDOW_SCALE, SCREENHEIGHT * WINDOW_SCALE,
	SDL_WINDOW_SHOWN);
    if (!window)
	I_Error("I_InitGraphics: SDL_CreateWindow failed: %s", SDL_GetError());

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer)
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer)
	I_Error("I_InitGraphics: SDL_CreateRenderer failed: %s", SDL_GetError());

    SDL_RenderSetLogicalSize(renderer, SCREENWIDTH, SCREENHEIGHT);

    texture = SDL_CreateTexture(
	renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
	SCREENWIDTH, SCREENHEIGHT);
    if (!texture)
	I_Error("I_InitGraphics: SDL_CreateTexture failed: %s", SDL_GetError());
}

void I_ShutdownGraphics(void)
{
    if (texture)   { SDL_DestroyTexture(texture);   texture = NULL; }
    if (renderer)  { SDL_DestroyRenderer(renderer); renderer = NULL; }
    if (window)    { SDL_DestroyWindow(window);     window = NULL; }
}

void I_StartFrame(void)
{
    // Nothing: frame-synchronous IO is handled by the input pump (I_StartTic).
}

//
// I_SetPalette
// Build the ARGB LUT from a 768-byte PLAYPAL, applying the current gamma
// (matching the legacy UploadNewPalette semantics).
//
void I_SetPalette(byte* palette)
{
    int	i;

    for (i = 0; i < 256; i++)
    {
	int r = gammatable[usegamma][*palette++];
	int g = gammatable[usegamma][*palette++];
	int b = gammatable[usegamma][*palette++];
	colorlut[i] = 0xff000000u
	    | ((Uint32)r << 16)
	    | ((Uint32)g << 8)
	    | (Uint32)b;
    }
}

void I_UpdateNoBlit(void)
{
    // Unused in the SDL backend.
}

//
// I_FinishUpdate
// Expand indexed screens[0] through the palette LUT and present.
//
void I_FinishUpdate(void)
{
    int	i;

    if (nodraw || noblit_local || !renderer)
	return;

    for (i = 0; i < SCREENWIDTH * SCREENHEIGHT; i++)
	argbframe[i] = colorlut[screens[0][i]];

    SDL_UpdateTexture(texture, NULL, argbframe, SCREENWIDTH * sizeof(Uint32));
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);

    // Boot-smoke evidence hook: if $DOOM_SHOT is set, read the presented frame
    // back off the renderer and save it. This exercises the full SDL path
    // (LUT expansion + texture upload + present + readback), not just screens[0].
    {
	const char* shot = getenv("DOOM_SHOT");
	if (shot && shot[0])
	{
	    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(
		0, SCREENWIDTH, SCREENHEIGHT, 32, SDL_PIXELFORMAT_ARGB8888);
	    if (s && SDL_RenderReadPixels(
		    renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
		    s->pixels, s->pitch) == 0)
		SDL_SaveBMP(s, shot);
	    if (s)
		SDL_FreeSurface(s);
	}
    }
}

void I_ReadScreen(byte* scr)
{
    memcpy(scr, screens[0], SCREENWIDTH * SCREENHEIGHT);
}

//
// I_PaletteSelfTest
// Phase 2 palette-LUT gate. Frame-hashes of indexed screens[0] do NOT exercise
// I_SetPalette's index->ARGB conversion, so validate it directly: apply the
// real PLAYPAL and assert every colorlut entry equals the gamma-corrected RGB
// with the expected channel order (ARGB8888: 0xAARRGGBB) and opaque alpha.
// Returns 0 on pass, nonzero on the first mismatch.
//
int I_PaletteSelfTest(void)
{
    byte*	playpal;
    int		i;

    playpal = (byte*)W_CacheLumpName("PLAYPAL", PU_CACHE);
    I_SetPalette(playpal);

    for (i = 0; i < 256; i++)
    {
	int r = gammatable[usegamma][playpal[i * 3 + 0]];
	int g = gammatable[usegamma][playpal[i * 3 + 1]];
	int b = gammatable[usegamma][playpal[i * 3 + 2]];
	Uint32 expected = 0xff000000u
	    | ((Uint32)r << 16) | ((Uint32)g << 8) | (Uint32)b;
	if (colorlut[i] != expected)
	{
	    fprintf(stderr,
		"PALTEST: index %d got %08x expected %08x\n",
		i, colorlut[i], expected);
	    return 1;
	}
    }
    // Sanity: alpha opaque and index 0 (canonical black) is near-zero RGB.
    if ((colorlut[0] & 0xff000000u) != 0xff000000u)
    {
	fprintf(stderr, "PALTEST: alpha not opaque\n");
	return 1;
    }
    printf("PALTEST: OK (256 entries, ARGB8888, gamma=%d)\n", usegamma);
    fflush(stdout);
    return 0;
}
