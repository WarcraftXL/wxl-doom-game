// DoomMusic: a doomgeneric music_module that plays Doom's songs through the Windows MIDI synth.
// Copyright (C) 2026 WarcraftXL. GPLv3.
//
// Built only when FEATURE_SOUND is defined. A song lump is either a Standard MIDI File ("MThd") or a
// MUS lump; MUS is converted to MIDI in memory (mus2mid + memio), then handed to the midiStream player
// in src/DoomMusic.cpp. The synth is a separate OS output, so music keeps playing while the client's
// own master volume is muted.

#include <string.h>

#include "doomtype.h"
#include "i_sound.h"
#include "memio.h"
#include "mus2mid.h"

// MIDI lumps above this size are rejected, matching the reference backend.
#define MAXMIDLENGTH (96 * 1024)

// Implemented in src/DoomMusic.cpp.
extern int  WxlMusic_Register(const unsigned char* midi, int len);
extern void WxlMusic_Unregister(void);
extern void WxlMusic_Play(int looping);
extern void WxlMusic_Stop(void);
extern void WxlMusic_Pause(void);
extern void WxlMusic_Resume(void);
extern void WxlMusic_SetVolume(int vol); // 0..127
extern int  WxlMusic_IsPlaying(void);

static boolean IsMid(const byte* mem, int len) { return len > 4 && !memcmp(mem, "MThd", 4); }

static boolean MUS_Init(void)        { return true; }
static void    MUS_Shutdown(void)    { WxlMusic_Stop(); WxlMusic_Unregister(); }
static void    MUS_SetVolume(int v)  { WxlMusic_SetVolume(v); }
static void    MUS_Pause(void)       { WxlMusic_Pause(); }
static void    MUS_Resume(void)      { WxlMusic_Resume(); }

static void* MUS_Register(void* data, int len)
{
    if (!data || len <= 0) return 0;

    if (IsMid((const byte*)data, len) && len < MAXMIDLENGTH)
    {
        return WxlMusic_Register((const unsigned char*)data, len) ? (void*)1 : 0;
    }

    // Assume a MUS lump and convert it to MIDI in memory.
    {
        MEMFILE* in     = mem_fopen_read(data, len);
        MEMFILE* out    = mem_fopen_write();
        void*    handle = 0;

        if (mus2mid(in, out) == 0)
        {
            void*  buf    = 0;
            size_t buflen = 0;
            mem_get_buf(out, &buf, &buflen);
            handle = WxlMusic_Register((const unsigned char*)buf, (int)buflen) ? (void*)1 : 0;
        }

        mem_fclose(in);
        mem_fclose(out);
        return handle;
    }
}

static void    MUS_Unregister(void* handle)              { (void)handle; WxlMusic_Unregister(); }
static void    MUS_Play(void* handle, boolean looping)   { (void)handle; WxlMusic_Play(looping ? 1 : 0); }
static void    MUS_StopSong(void)                        { WxlMusic_Stop(); }
static boolean MUS_IsPlaying(void)                       { return WxlMusic_IsPlaying() ? true : false; }
static void    MUS_Poll(void)                            {}

static snddevice_t s_musDevices[] = { SNDDEVICE_GENMIDI };

music_module_t DG_music_module =
{
    s_musDevices, sizeof(s_musDevices) / sizeof(s_musDevices[0]),
    MUS_Init, MUS_Shutdown, MUS_SetVolume, MUS_Pause, MUS_Resume,
    MUS_Register, MUS_Unregister, MUS_Play, MUS_StopSong, MUS_IsPlaying, MUS_Poll
};
