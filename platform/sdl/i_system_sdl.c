// Phase 2 modernization: SDL2 system platform layer (timing + input).
//
// Replaces platform/stub/i_system_stub.c. This translation unit is the SOLE
// owner of I_StartTic and the input event pump (the legacy X11 i_video.c
// historically owned input; the SDL split moves it here so exactly one TU
// exports I_StartTic and i_video_sdl.c owns only rendering).
//
// Timing is SDL_GetTicks at TICRATE. Demo replay is `singletics`, so the
// wall-clock reading here gates only frame pacing, never the deterministic
// per-tic playsim -- the frozen parity checksum stays independent of it.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include <SDL.h>

#include "doomdef.h"
#include "doomtype.h"
#include "d_event.h"
#include "d_main.h"
#include "m_argv.h"
#include "m_misc.h"
#include "i_video.h"
#include "i_sound.h"

#include "d_net.h"
#include "g_game.h"

#include "i_system.h"

int	mb_used = 6;

// use_mouse config default (m_misc.c). When 0, mouse events are not posted.
extern int	usemouse;

// Current DOOM mouse button mask (bit0=left, bit1=middle, bit2=right), kept
// across events so motion carries the held buttons -- matches the legacy X11
// ButtonMask semantics without querying global state each event.
static int	mouse_buttons = 0;

// -grabmouse is resolved lazily on the first pump (video is up by then).
static boolean	mouse_grab_checked = false;

void I_Tactile(int on, int off, int total)
{
    // UNUSED.
    (void)on;
    (void)off;
    (void)total;
}

ticcmd_t	emptycmd;
ticcmd_t* I_BaseTiccmd(void)
{
    return &emptycmd;
}

int I_GetHeapSize(void)
{
    return mb_used*1024*1024;
}

byte* I_ZoneBase(int* size)
{
    *size = mb_used*1024*1024;
    return (byte *) malloc(*size);
}

//
// I_GetTime
// returns time in 1/TICRATE second tics
//
int I_GetTime(void)
{
    static Uint32	basetime = 0;
    Uint32		ticks;

    if (SDL_WasInit(SDL_INIT_TIMER) == 0)
	SDL_InitSubSystem(SDL_INIT_TIMER);

    ticks = SDL_GetTicks();
    if (basetime == 0)
	basetime = ticks;
    return (int)((ticks - basetime) * TICRATE / 1000);
}

//
// Translate an SDL keysym into a DOOM keycode (see doomdef.h KEY_*).
//
static int I_TranslateKey(SDL_Keysym* sym)
{
    int rc = sym->sym;

    switch (rc)
    {
      case SDLK_LEFT:		return KEY_LEFTARROW;
      case SDLK_RIGHT:		return KEY_RIGHTARROW;
      case SDLK_DOWN:		return KEY_DOWNARROW;
      case SDLK_UP:		return KEY_UPARROW;
      case SDLK_ESCAPE:		return KEY_ESCAPE;
      case SDLK_RETURN:
      case SDLK_KP_ENTER:	return KEY_ENTER;
      case SDLK_TAB:		return KEY_TAB;
      case SDLK_F1:		return KEY_F1;
      case SDLK_F2:		return KEY_F2;
      case SDLK_F3:		return KEY_F3;
      case SDLK_F4:		return KEY_F4;
      case SDLK_F5:		return KEY_F5;
      case SDLK_F6:		return KEY_F6;
      case SDLK_F7:		return KEY_F7;
      case SDLK_F8:		return KEY_F8;
      case SDLK_F9:		return KEY_F9;
      case SDLK_F10:		return KEY_F10;
      case SDLK_F11:		return KEY_F11;
      case SDLK_F12:		return KEY_F12;
      case SDLK_BACKSPACE:
      case SDLK_DELETE:		return KEY_BACKSPACE;
      case SDLK_PAUSE:		return KEY_PAUSE;
      case SDLK_KP_EQUALS:
      case SDLK_EQUALS:		return KEY_EQUALS;
      case SDLK_KP_MINUS:
      case SDLK_MINUS:		return KEY_MINUS;
      case SDLK_LSHIFT:
      case SDLK_RSHIFT:		return KEY_RSHIFT;
      case SDLK_LCTRL:
      case SDLK_RCTRL:		return KEY_RCTRL;
      case SDLK_LALT:
      case SDLK_RALT:		return KEY_RALT;
      default:
	// Printable ASCII maps straight through; SDL letter keycodes are
	// already lowercase, matching the legacy xlatekey() behaviour.
	if (rc >= SDLK_SPACE && rc <= SDLK_z)
	    return rc;
	return 0;
    }
}

//
// Map an SDL mouse button to the DOOM button-mask bit (L=1, M=2, R=4).
//
static int I_SDLButtonBit(Uint8 button)
{
    switch (button)
    {
      case SDL_BUTTON_LEFT:	return 1;
      case SDL_BUTTON_MIDDLE:	return 2;
      case SDL_BUTTON_RIGHT:	return 4;
      default:			return 0;
    }
}

//
// Resolve -grabmouse once. SDL relative-mouse mode hides the cursor, confines
// it, and feeds relative xrel/yrel deltas -- replacing the legacy X11
// warp-to-center. It may fail under a headless/dummy video driver; that is not
// fatal, we simply run ungrabbed.
//
static void I_UpdateMouseGrab(void)
{
    if (mouse_grab_checked)
	return;
    mouse_grab_checked = true;

    if (M_CheckParm("-grabmouse"))
	SDL_SetRelativeMouseMode(SDL_TRUE);
}

//
// I_StartTic
// Pump SDL events into the DOOM event queue. Sole owner of the input pump.
//
void I_StartTic(void)
{
    SDL_Event	sdlev;
    event_t	event;

    if (SDL_WasInit(SDL_INIT_VIDEO) == 0)
	return;

    I_UpdateMouseGrab();

    while (SDL_PollEvent(&sdlev))
    {
	switch (sdlev.type)
	{
	  case SDL_KEYDOWN:
	    event.type = ev_keydown;
	    event.data1 = I_TranslateKey(&sdlev.key.keysym);
	    if (event.data1)
		D_PostEvent(&event);
	    break;

	  case SDL_KEYUP:
	    event.type = ev_keyup;
	    event.data1 = I_TranslateKey(&sdlev.key.keysym);
	    if (event.data1)
		D_PostEvent(&event);
	    break;

	  case SDL_MOUSEBUTTONDOWN:
	  case SDL_MOUSEBUTTONUP:
	    if (!usemouse)
		break;
	    {
		int bit = I_SDLButtonBit(sdlev.button.button);
		if (sdlev.type == SDL_MOUSEBUTTONDOWN)
		    mouse_buttons |= bit;
		else
		    mouse_buttons &= ~bit;
		event.type = ev_mouse;
		event.data1 = mouse_buttons;
		event.data2 = event.data3 = 0;
		D_PostEvent(&event);
	    }
	    break;

	  case SDL_MOUSEMOTION:
	    if (!usemouse)
		break;
	    if (sdlev.motion.xrel == 0 && sdlev.motion.yrel == 0)
		break;
	    event.type = ev_mouse;
	    event.data1 = mouse_buttons;
	    // Legacy scaled the deltas by <<2; multiply to avoid UB on negative
	    // deltas. SDL y grows downward, DOOM wants up-positive, hence -yrel.
	    event.data2 = sdlev.motion.xrel * 4;
	    event.data3 = -sdlev.motion.yrel * 4;
	    D_PostEvent(&event);
	    break;

	  case SDL_QUIT:
	    I_Quit();
	    break;

	  default:
	    break;
	}
    }
}

//
// I_Init
//
void I_Init(void)
{
    I_InitSound();
    //  I_InitGraphics();
}

//
// I_Quit
//
void I_Quit(void)
{
    D_QuitNetGame();
    I_ShutdownSound();
    I_ShutdownMusic();
    M_SaveDefaults();
    I_ShutdownGraphics();
    SDL_Quit();
    exit(0);
}

void I_WaitVBL(int count)
{
    SDL_Delay(count * 1000 / 70);
}

void I_BeginRead(void)
{
}

void I_EndRead(void)
{
}

byte* I_AllocLow(int length)
{
    byte*	mem;

    mem = (byte *)malloc(length);
    memset(mem,0,length);
    return mem;
}

//
// I_Error
//
extern boolean demorecording;

void I_Error(char *error, ...)
{
    va_list	argptr;

    // Message first.
    va_start(argptr,error);
    fprintf(stderr, "Error: ");
    vfprintf(stderr,error,argptr);
    fprintf(stderr, "\n");
    va_end(argptr);

    fflush(stderr);

    // Shutdown. Here might be other errors.
    if (demorecording)
	G_CheckDemoStatus();

    D_QuitNetGame();
    I_ShutdownGraphics();

    exit(-1);
}
