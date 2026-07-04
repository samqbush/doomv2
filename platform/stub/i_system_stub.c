// Phase 1 modernization stub: system platform layer.
//
// Timer/zone/shutdown. The original i_system.c is already portable POSIX (no
// X11/OSS), so this stub keeps its real behavior rather than no-opping it: a
// working millisecond timer and a malloc'd zone let the core link AND behave
// sanely at startup. SDL timing/input arrives in Phase 2.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <sys/time.h>
#include <unistd.h>

#include "doomdef.h"
#include "m_misc.h"
#include "i_video.h"
#include "i_sound.h"

#include "d_net.h"
#include "g_game.h"

#include "i_system.h"

int	mb_used = 6;

void I_Tactile(int on, int off, int total)
{
    // UNUSED.
    on = off = total = 0;
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
// returns time in 1/70th second tics
//
int I_GetTime(void)
{
    struct timeval	tp;
    struct timezone	tzp;
    int			newtics;
    static int		basetime=0;

    gettimeofday(&tp, &tzp);
    if (!basetime)
	basetime = (int)tp.tv_sec;
    newtics = (int)((tp.tv_sec-basetime)*TICRATE + tp.tv_usec*TICRATE/1000000);
    return newtics;
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
    exit(0);
}

void I_WaitVBL(int count)
{
    usleep(count * (1000000/70));
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
