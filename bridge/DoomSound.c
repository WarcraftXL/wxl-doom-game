// DoomSound: a doomgeneric sound_module that feeds Doom's SFX to the module's waveOut mixer (WxlAudio_*).
// Copyright (C) 2026 WarcraftXL. GPLv3.
//
// Built only when FEATURE_SOUND is defined. Reads each SFX lump from the WAD (DMX 8-bit PCM), and hands
// the samples to the C++ mixer in src/DoomAudio.cpp. Music is handled by bridge/DoomMusic.c.

#include <stdio.h>

#include "doomtype.h"
#include "i_sound.h"
#include "w_wad.h"
#include "z_zone.h"

// Config variables normally defined by the (excluded) SDL sound backend. i_sound.c's I_BindSoundVariables
// binds them, so they must exist; we do not resample through libsamplerate.
int   use_libsamplerate   = 0;
float libsamplerate_scale = 0.65f;

// Implemented in src/DoomAudio.cpp.
extern int  WxlAudio_Init(void);
extern void WxlAudio_Shutdown(void);
extern void WxlAudio_Update(void);
extern int  WxlAudio_Play(int slot, const unsigned char* pcm8, int len, int rate, int left, int right);
extern void WxlAudio_Stop(int slot);
extern int  WxlAudio_IsPlaying(int slot);
extern void WxlAudio_SetParams(int slot, int left, int right);

// Split Doom's volume (0..127) and separation (0..254, center 127) into per-side levels (0..127).
static void SplitPan(int vol, int sep, int* left, int* right)
{
    if (sep < 0)   sep = 0;
    if (sep > 254) sep = 254;
    *left  = vol * (254 - sep) / 254;
    *right = vol * sep / 254;
}

static snddevice_t s_sfxDevices[] = { SNDDEVICE_SB, SNDDEVICE_PAS, SNDDEVICE_GENMIDI, SNDDEVICE_AWE32 };

static boolean SFX_Init(boolean use_sfx_prefix) { (void)use_sfx_prefix; return WxlAudio_Init() ? true : false; }
static void    SFX_Shutdown(void) { WxlAudio_Shutdown(); }

static int SFX_GetLumpNum(sfxinfo_t* sfx)
{
    char nb[16];
    snprintf(nb, sizeof(nb), "ds%s", sfx->name);
    return W_CheckNumForName(nb);
}

static void SFX_Update(void) { WxlAudio_Update(); }

static void SFX_UpdateParams(int handle, int vol, int sep)
{
    int l, r;
    SplitPan(vol, sep, &l, &r);
    WxlAudio_SetParams(handle, l, r);
}

static int SFX_Start(sfxinfo_t* sfx, int channel, int vol, int sep)
{
    const unsigned char* data;
    int lump = sfx->lumpnum;
    int len, rate, l, r;

    if (lump < 0) lump = SFX_GetLumpNum(sfx);
    if (lump < 0) return -1;

    data = (const unsigned char*)W_CacheLumpNum(lump, PU_STATIC);
    if (!data) return -1;

    // DMX header: u16 format(3), u16 sample rate, u32 sample count, then 8-bit unsigned PCM.
    rate = data[2] | (data[3] << 8);
    len  = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
    if (len <= 0 || rate <= 0) return -1;

    SplitPan(vol, sep, &l, &r);
    return WxlAudio_Play(channel, data + 8, len, rate, l, r) ? channel : -1;
}

static void    SFX_Stop(int handle) { WxlAudio_Stop(handle); }
static boolean SFX_IsPlaying(int handle) { return WxlAudio_IsPlaying(handle) ? true : false; }
static void    SFX_Cache(sfxinfo_t* sounds, int num) { (void)sounds; (void)num; }

sound_module_t DG_sound_module =
{
    s_sfxDevices, sizeof(s_sfxDevices) / sizeof(s_sfxDevices[0]),
    SFX_Init, SFX_Shutdown, SFX_GetLumpNum, SFX_Update,
    SFX_UpdateParams, SFX_Start, SFX_Stop, SFX_IsPlaying, SFX_Cache
};
