// Phase 4 modernization: in-process SDL2 audio platform layer.
//
// Replaces the Phase 1 silent stub (platform/stub/i_sound_stub.c) and the dead
// OSS `/dev/dsp` + separate-sndserver original (linuxdoom-1.10/i_sound.c). The
// 8-channel DMX-style software mixer is ported VERBATIM from the 1997 source so
// the sim-facing behavior is byte-identical; only the *output* path changes:
//
//   - I_InitSound   : SDL_OpenAudioDevice (S16LSB / 2ch / 11025, no format
//                     changes) + SDL_PauseAudioDevice(dev,0). Was: open /dev/dsp
//                     and OSS ioctls.
//   - I_SubmitSound : SDL_QueueAudio with a bounded queue. Was: write(audio_fd).
//   - I_ShutdownSound: SDL_CloseAudioDevice. Was: close(audio_fd).
//
// Threading: PUSH model (SDL_QueueAudio). The DMX mixer keeps running on the
// main/sim thread inside I_UpdateSound exactly as before -- there is no audio
// callback and no shared state touched off-thread, so the deterministic parity
// checksum cannot be perturbed by audio.
//
// Headless / oracle safety: when a parity/oracle mode (-checkdemo, -framehash)
// or -nosound/-nosfx is active, audio is fully suppressed -- no device is
// opened, no SFX are pre-cached, and I_UpdateSound/I_SubmitSound are no-ops.
// This keeps the deterministic gates independent of audio-device availability
// (CI runners have none) and prevents queue growth during faster-than-realtime
// demo replay.
//
// Music remains unimplemented (the 1997 source never finished it -- "Still no
// music done"); the music API stays a documented no-op, deferred beyond Phase 4.
//
// All mixer state is file-static: the legacy source declared these as non-static
// globals, but nothing outside this TU needs them, and file scope avoids any
// cross-TU symbol collision under the platform layer's -Werror bar.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <SDL.h>

#include "z_zone.h"

#include "i_system.h"
#include "i_sound.h"
#include "m_argv.h"
#include "w_wad.h"

#include "doomdef.h"
#include "doomstat.h"
#include "sounds.h"

// Needed for calling the actual sound output.
#define SAMPLECOUNT		512
#define NUM_CHANNELS		8
// It is 2 for 16bit, and 2 for two channels.
#define BUFMUL                  4
#define MIXBUFFERSIZE		(SAMPLECOUNT*BUFMUL)

#define SAMPLERATE		11025	// Hz
#define SAMPLESIZE		2   	// 16bit

// One submitted buffer in bytes (SAMPLECOUNT stereo 16-bit frames).
#define SUBMIT_BYTES		(SAMPLECOUNT*BUFMUL)

// Bound the queued audio so faster-than-realtime frames cannot grow it without
// limit; ~4 buffers keeps latency low while tolerating jitter.
#define MAX_QUEUED_BYTES	(SUBMIT_BYTES*4)

// The actual lengths of all sound effects.
static int		lengths[NUMSFX];

// The global mixing buffer. Samples from all active internal channels are
// modified and added, then the leading SUBMIT_BYTES are handed to SDL.
static signed short	mixbuffer[MIXBUFFERSIZE];

// The channel step amount...
static unsigned int	channelstep[NUM_CHANNELS];
// ... and a 0.16 bit remainder of last step.
static unsigned int	channelstepremainder[NUM_CHANNELS];

// The channel data pointers, start and end.
static unsigned char*	channels[NUM_CHANNELS];
static unsigned char*	channelsend[NUM_CHANNELS];

// Time/gametic that the channel started playing, used to determine oldest.
static int		channelstart[NUM_CHANNELS];

// The sound in channel handles, determined on registration.
static int		channelhandles[NUM_CHANNELS];

// SFX id of the playing sound effect. Used to catch duplicates (chainsaw).
static int		channelids[NUM_CHANNELS];

// Pitch to stepping lookup.
static int		steptable[256];

// Volume lookups.
static int		vol_lookup[128*256];

// Hardware left and right channel volume lookup.
static int*		channelleftvol_lookup[NUM_CHANNELS];
static int*		channelrightvol_lookup[NUM_CHANNELS];

// SDL audio device (0 == not open / suppressed).
static SDL_AudioDeviceID	audio_dev = 0;

// When set, audio is fully suppressed (headless/oracle/-nosound).
static int		audio_disabled = 0;


//
// This function loads the sound data from the WAD lump, for a single sound.
// Ported verbatim from the 1997 i_sound.c getsfx().
//
static void*
getsfx
( char*         sfxname,
  int*          len )
{
    unsigned char*      sfx;
    unsigned char*      paddedsfx;
    int                 i;
    int                 size;
    int                 paddedsize;
    char                name[20];
    int                 sfxlump;

    sprintf(name, "ds%s", sfxname);

    // Missing lumps fall back to the pistol (e.g. DOOM II sounds requested by
    // the shareware WAD). Preserved from the original.
    if ( W_CheckNumForName(name) == -1 )
      sfxlump = W_GetNumForName("dspistol");
    else
      sfxlump = W_GetNumForName(name);

    size = W_LumpLength( sfxlump );

    sfx = (unsigned char*)W_CacheLumpNum( sfxlump, PU_STATIC );

    // Pad the sound effect out to a whole number of mixing buffers.
    paddedsize = ((size-8 + (SAMPLECOUNT-1)) / SAMPLECOUNT) * SAMPLECOUNT;

    paddedsfx = (unsigned char*)Z_Malloc( paddedsize+8, PU_STATIC, 0 );

    memcpy(  paddedsfx, sfx, size );
    for (i=size ; i<paddedsize+8 ; i++)
        paddedsfx[i] = 128;

    Z_Free( sfx );

    *len = paddedsize;

    return (void *) (paddedsfx + 8);
}


//
// Adds a sound to the list of currently active sounds, maintained as
// NUM_CHANNELS internal channels. Returns a handle. Ported verbatim.
//
static int
addsfx
( int		sfxid,
  int		volume,
  int		step,
  int		seperation )
{
    static unsigned short	handlenums = 0;

    int		i;
    int		rc = -1;

    int		oldest = gametic;
    int		oldestnum = 0;
    int		slot;

    int		rightvol;
    int		leftvol;

    // Chainsaw troubles. Play these sound effects only one at a time.
    if ( sfxid == sfx_sawup
	 || sfxid == sfx_sawidl
	 || sfxid == sfx_sawful
	 || sfxid == sfx_sawhit
	 || sfxid == sfx_stnmov
	 || sfxid == sfx_pistol	 )
    {
	for (i=0 ; i<NUM_CHANNELS ; i++)
	{
	    if ( (channels[i])
		 && (channelids[i] == sfxid) )
	    {
		channels[i] = 0;
		break;
	    }
	}
    }

    // Loop all channels to find oldest SFX.
    for (i=0; (i<NUM_CHANNELS) && (channels[i]); i++)
    {
	if (channelstart[i] < oldest)
	{
	    oldestnum = i;
	    oldest = channelstart[i];
	}
    }

    // If we found a free channel use it, else overwrite the oldest.
    if (i == NUM_CHANNELS)
	slot = oldestnum;
    else
	slot = i;

    // Set pointer to raw data and to end of raw data.
    channels[slot] = (unsigned char *) S_sfx[sfxid].data;
    channelsend[slot] = channels[slot] + lengths[sfxid];

    if (!handlenums)
	handlenums = 100;

    channelhandles[slot] = rc = handlenums++;

    channelstep[slot] = step;
    channelstepremainder[slot] = 0;
    channelstart[slot] = gametic;

    // Separation, that is, orientation/stereo. range is: 1 - 256
    seperation += 1;

    leftvol =
	volume - ((volume*seperation*seperation) >> 16);
    seperation = seperation - 257;
    rightvol =
	volume - ((volume*seperation*seperation) >> 16);

    if (rightvol < 0 || rightvol > 127)
	I_Error("rightvol out of bounds");

    if (leftvol < 0 || leftvol > 127)
	I_Error("leftvol out of bounds");

    channelleftvol_lookup[slot] = &vol_lookup[leftvol*256];
    channelrightvol_lookup[slot] = &vol_lookup[rightvol*256];

    channelids[slot] = sfxid;

    return rc;
}


//
// SFX API
//
void I_SetChannels()
{
  int		i;
  int		j;

  int*	steptablemid = steptable + 128;

  // This table provides step widths for pitch parameters.
  for (i=-128 ; i<128 ; i++)
    steptablemid[i] = (int)(pow(2.0, (i/64.0))*65536.0);

  // Volume lookup tables, which also turn the unsigned samples into signed.
  for (i=0 ; i<128 ; i++)
    for (j=0 ; j<256 ; j++)
      vol_lookup[i*256+j] = (i*(j-128)*256)/127;
}


void I_SetSfxVolume(int volume)
{
  snd_SfxVolume = volume;
}

void I_SetMusicVolume(int volume)
{
  snd_MusicVolume = volume;
}


//
// Retrieve the raw data lump index for a given SFX name.
//
int I_GetSfxLumpNum(sfxinfo_t* sfx)
{
    char namebuf[9];
    sprintf(namebuf, "ds%s", sfx->name);
    return W_GetNumForName(namebuf);
}


//
// Starting a sound adds it to the current list of active internal channels.
//
int
I_StartSound
( int		id,
  int		vol,
  int		sep,
  int		pitch,
  int		priority )
{
    (void)priority;

    if (audio_disabled)
	return -1;

    return addsfx( id, vol, steptable[pitch], sep );
}


void I_StopSound (int handle)
{
  // Unused in the original mixer (handle-based stop was never implemented).
  (void)handle;
}


int I_SoundIsPlaying(int handle)
{
    return gametic < handle;
}


//
// Loops all active channels, fetches samples from the raw sound data, applies
// per-channel volume/stepping, mixes into the stereo mixbuffer with clamping.
// Ported verbatim (16-bit only). No-op when audio is suppressed.
//
void I_UpdateSound( void )
{
  register unsigned int	sample;
  register int		dl;
  register int		dr;

  signed short*		leftout;
  signed short*		rightout;
  signed short*		leftend;
  int			step;

  int			chan;

  if (audio_disabled)
    return;

  leftout = mixbuffer;
  rightout = mixbuffer+1;
  step = 2;

  leftend = mixbuffer + SAMPLECOUNT*step;

  while (leftout != leftend)
  {
    dl = 0;
    dr = 0;

    for ( chan = 0; chan < NUM_CHANNELS; chan++ )
    {
	if (channels[ chan ])
	{
	    sample = *channels[ chan ];
	    dl += channelleftvol_lookup[ chan ][sample];
	    dr += channelrightvol_lookup[ chan ][sample];
	    channelstepremainder[ chan ] += channelstep[ chan ];
	    channels[ chan ] += channelstepremainder[ chan ] >> 16;
	    channelstepremainder[ chan ] &= 65536-1;

	    if (channels[ chan ] >= channelsend[ chan ])
		channels[ chan ] = 0;
	}
    }

    if (dl > 0x7fff)
	*leftout = 0x7fff;
    else if (dl < -0x8000)
	*leftout = -0x8000;
    else
	*leftout = dl;

    if (dr > 0x7fff)
	*rightout = 0x7fff;
    else if (dr < -0x8000)
	*rightout = -0x8000;
    else
	*rightout = dr;

    leftout += step;
    rightout += step;
  }
}


//
// Hand the freshly mixed buffer to SDL. Push model: the mixer already ran on
// this (the sim) thread in I_UpdateSound. Drop the buffer if the device queue
// is already full so it can never grow unbounded / drift.
//
void
I_SubmitSound(void)
{
  if (audio_disabled || !audio_dev)
    return;

  if (SDL_GetQueuedAudioSize(audio_dev) > (Uint32)MAX_QUEUED_BYTES)
    return;

  SDL_QueueAudio(audio_dev, mixbuffer, SUBMIT_BYTES);
}


void
I_UpdateSoundParams
( int	handle,
  int	vol,
  int	sep,
  int	pitch)
{
  // Unused in the original mixer.
  (void)handle; (void)vol; (void)sep; (void)pitch;
}


void I_ShutdownSound(void)
{
  if (audio_dev)
  {
    SDL_CloseAudioDevice(audio_dev);
    audio_dev = 0;
  }
  if (SDL_WasInit(SDL_INIT_AUDIO))
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}


void
I_InitSound()
{
  SDL_AudioSpec	want;
  SDL_AudioSpec	have;
  int		i;

  // Suppress audio for the deterministic oracle paths (device availability and
  // queue timing must never influence the parity/frame-hash gates) and honor
  // the conventional -nosound/-nosfx switches.
  if ( M_CheckParm("-checkdemo")
       || M_CheckParm("-framehash")
       || M_CheckParm("-nosound")
       || M_CheckParm("-nosfx") )
  {
    audio_disabled = 1;
    fprintf(stderr, "I_InitSound: audio suppressed (oracle/-nosound mode)\n");
    return;
  }

  if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
  {
    audio_disabled = 1;
    fprintf(stderr, "I_InitSound: SDL_InitSubSystem(AUDIO) failed: %s\n",
	    SDL_GetError());
    fprintf(stderr, "I_InitSound: continuing without audio\n");
    return;
  }

  SDL_memset(&want, 0, sizeof(want));
  want.freq = SAMPLERATE;
  want.format = AUDIO_S16LSB;
  want.channels = 2;
  want.samples = SAMPLECOUNT;
  want.callback = NULL;			// push model: we use SDL_QueueAudio

  // No format changes: the queued mixbuffer bytes must match the device format
  // exactly (S16LSB / 2ch / 11025), so the ported mixer output is byte-correct.
  audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  if (audio_dev == 0)
  {
    audio_disabled = 1;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    fprintf(stderr, "I_InitSound: SDL_OpenAudioDevice failed: %s\n",
	    SDL_GetError());
    fprintf(stderr, "I_InitSound: continuing without audio\n");
    return;
  }

  // Pre-cache all sound data (kept static for the session). Ported from the
  // original; sets S_sfx[i].data which s_sound.c and addsfx() consume.
  for (i=1 ; i<NUMSFX ; i++)
  {
    if (!S_sfx[i].link)
    {
      S_sfx[i].data = getsfx( S_sfx[i].name, &lengths[i] );
    }
    else
    {
      S_sfx[i].data = S_sfx[i].link->data;
      lengths[i] = lengths[(S_sfx[i].link - S_sfx)/sizeof(sfxinfo_t)];
    }
  }

  for ( i = 0; i < MIXBUFFERSIZE; i++ )
    mixbuffer[i] = 0;

  // Devices open paused; start playback so the queue actually drains.
  SDL_PauseAudioDevice(audio_dev, 0);

  fprintf(stderr, "I_InitSound: SDL audio ready (%d Hz, %d ch, S16)\n",
	  have.freq, have.channels);
}


//
// MUSIC API.
// Still no music done (unimplemented in the 1997 source; deferred beyond
// Phase 4). Remains dummies.
//
void I_InitMusic(void)		{ }
void I_ShutdownMusic(void)	{ }

static int	looping=0;
static int	musicdies=-1;

void I_PlaySong(int handle, int looping_arg)
{
  (void)handle; (void)looping_arg;
  musicdies = gametic + TICRATE*30;
}

void I_PauseSong (int handle)
{
  (void)handle;
}

void I_ResumeSong (int handle)
{
  (void)handle;
}

void I_StopSong(int handle)
{
  (void)handle;
  looping = 0;
  musicdies = 0;
}

void I_UnRegisterSong(int handle)
{
  (void)handle;
}

int I_RegisterSong(void* data)
{
  (void)data;
  return 1;
}

int I_QrySongPlaying(int handle)
{
  (void)handle;
  return looping || musicdies > gametic;
}
