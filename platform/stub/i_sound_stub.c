// Phase 1 modernization stub: sound platform layer.
//
// Replaces the dead OSS `/dev/dsp` + separate-sndserver i_sound.c so the
// portable core links. Silent no-op; in-process SDL audio arrives in Phase 4.
// Signatures mirror i_sound.h exactly to catch contract drift at compile time.

#include "doomdef.h"
#include "i_sound.h"

// doomdef.h defines SNDSERV, so the config table (m_misc.c) and i_sound.h
// reference sndserver_filename, originally owned by the excluded i_sound.c.
// Provide it here so the core links; the separate sndserver is retired in
// Phase 4.
char*	sndserver_filename = "./sndserver ";

void I_InitSound()
{
}

void I_UpdateSound(void)
{
}

void I_SubmitSound(void)
{
}

void I_ShutdownSound(void)
{
}

void I_SetChannels()
{
}

int I_GetSfxLumpNum(sfxinfo_t* sfxinfo)
{
    (void)sfxinfo;
    return 0;
}

int I_StartSound(int id, int vol, int sep, int pitch, int priority)
{
    (void)id; (void)vol; (void)sep; (void)pitch; (void)priority;
    return -1;
}

void I_StopSound(int handle)
{
    (void)handle;
}

int I_SoundIsPlaying(int handle)
{
    (void)handle;
    return 0;
}

void I_UpdateSoundParams(int handle, int vol, int sep, int pitch)
{
    (void)handle; (void)vol; (void)sep; (void)pitch;
}

void I_InitMusic(void)
{
}

void I_ShutdownMusic(void)
{
}

void I_SetMusicVolume(int volume)
{
    (void)volume;
}

void I_PauseSong(int handle)
{
    (void)handle;
}

void I_ResumeSong(int handle)
{
    (void)handle;
}

int I_RegisterSong(void* data)
{
    (void)data;
    return 0;
}

void I_PlaySong(int handle, int looping)
{
    (void)handle; (void)looping;
}

void I_StopSong(int handle)
{
    (void)handle;
}

void I_UnRegisterSong(int handle)
{
    (void)handle;
}
