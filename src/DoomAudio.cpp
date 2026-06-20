// DoomAudio: a small waveOut software mixer that plays Doom's SFX (called from the C sound_module).
// Copyright (C) 2026 WarcraftXL. GPLv3.
//
// 16-bit stereo output at Doom's SFX rate. Each voice is a converted mono sample mixed by a background
// thread into a ring of waveOut buffers. The C sound bridge (bridge/DoomSound.c) drives it through the
// extern "C" WxlAudio_* API.

#include <windows.h>
#include <mmsystem.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace
{
    constexpr int kRate        = 11025; // Doom SFX native rate
    constexpr int kChannels    = 2;     // stereo (panning)
    constexpr int kSlots       = 32;    // concurrent voices
    constexpr int kBuffers     = 4;     // waveOut ring
    constexpr int kBlockFrames = 256;   // ~23 ms per buffer

    struct Voice
    {
        bool     active = false;
        int16_t* data   = nullptr; // converted mono samples (owned)
        int      len    = 0;
        double   pos    = 0.0;
        double   step   = 1.0;
        float    lgain  = 0.0f;
        float    rgain  = 0.0f;
    };

    Voice            g_voice[kSlots];
    CRITICAL_SECTION g_cs;
    HWAVEOUT         g_wo = nullptr;
    WAVEHDR          g_hdr[kBuffers];
    int16_t*         g_buf[kBuffers] = { nullptr };
    HANDLE           g_thread = nullptr;
    volatile bool    g_run = false;
    bool             g_init = false;

    void MixBlock(int16_t* out)
    {
        static int32_t accL[kBlockFrames];
        static int32_t accR[kBlockFrames];
        for (int i = 0; i < kBlockFrames; ++i) { accL[i] = 0; accR[i] = 0; }

        EnterCriticalSection(&g_cs);
        for (int s = 0; s < kSlots; ++s)
        {
            Voice& v = g_voice[s];
            if (!v.active || !v.data) continue;
            for (int i = 0; i < kBlockFrames; ++i)
            {
                int idx = (int)v.pos;
                if (idx >= v.len) { v.active = false; break; }
                int sample = v.data[idx];
                accL[i] += (int32_t)(sample * v.lgain);
                accR[i] += (int32_t)(sample * v.rgain);
                v.pos += v.step;
            }
        }
        LeaveCriticalSection(&g_cs);

        for (int i = 0; i < kBlockFrames; ++i)
        {
            int32_t l = accL[i], r = accR[i];
            if (l >  32767) l =  32767; if (l < -32768) l = -32768;
            if (r >  32767) r =  32767; if (r < -32768) r = -32768;
            out[i * 2]     = (int16_t)l;
            out[i * 2 + 1] = (int16_t)r;
        }
    }

    DWORD WINAPI MixThread(LPVOID)
    {
        while (g_run)
        {
            bool didWork = false;
            for (int b = 0; b < kBuffers; ++b)
            {
                WAVEHDR& h = g_hdr[b];
                const bool inQueue = (h.dwFlags & WHDR_INQUEUE) != 0;
                const bool done    = (h.dwFlags & WHDR_DONE) != 0;
                if (inQueue && !done) continue; // still playing

                MixBlock(g_buf[b]);
                h.lpData         = (LPSTR)g_buf[b];
                h.dwBufferLength = kBlockFrames * kChannels * sizeof(int16_t);
                h.dwFlags &= ~WHDR_DONE;
                waveOutWrite(g_wo, &h, sizeof(WAVEHDR));
                didWork = true;
            }
            if (!didWork) Sleep(2);
        }
        return 0;
    }
}

extern "C"
{
    int WxlAudio_Init(void)
    {
        if (g_init) return 1;

        WAVEFORMATEX wf;
        memset(&wf, 0, sizeof(wf));
        wf.wFormatTag      = WAVE_FORMAT_PCM;
        wf.nChannels       = kChannels;
        wf.nSamplesPerSec  = kRate;
        wf.wBitsPerSample  = 16;
        wf.nBlockAlign     = (WORD)(kChannels * 2);
        wf.nAvgBytesPerSec = kRate * wf.nBlockAlign;

        if (waveOutOpen(&g_wo, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
        {
            g_wo = nullptr;
            return 0;
        }

        InitializeCriticalSection(&g_cs);
        for (int b = 0; b < kBuffers; ++b)
        {
            g_buf[b] = (int16_t*)calloc(kBlockFrames * kChannels, sizeof(int16_t));
            memset(&g_hdr[b], 0, sizeof(WAVEHDR));
            g_hdr[b].lpData         = (LPSTR)g_buf[b];
            g_hdr[b].dwBufferLength = kBlockFrames * kChannels * sizeof(int16_t);
            waveOutPrepareHeader(g_wo, &g_hdr[b], sizeof(WAVEHDR));
            g_hdr[b].dwFlags |= WHDR_DONE; // free for the mix thread to fill
        }

        g_run = true;
        g_thread = CreateThread(nullptr, 0, MixThread, nullptr, 0, nullptr);
        g_init = true;
        return 1;
    }

    void WxlAudio_Shutdown(void)
    {
        if (!g_init) return;
        g_run = false;
        if (g_thread) { WaitForSingleObject(g_thread, 1000); CloseHandle(g_thread); g_thread = nullptr; }
        if (g_wo)
        {
            waveOutReset(g_wo);
            for (int b = 0; b < kBuffers; ++b)
            {
                waveOutUnprepareHeader(g_wo, &g_hdr[b], sizeof(WAVEHDR));
                free(g_buf[b]); g_buf[b] = nullptr;
            }
            waveOutClose(g_wo);
            g_wo = nullptr;
        }
        for (int s = 0; s < kSlots; ++s) { free(g_voice[s].data); g_voice[s].data = nullptr; g_voice[s].active = false; }
        DeleteCriticalSection(&g_cs);
        g_init = false;
    }

    void WxlAudio_Update(void) {} // mixing runs on the background thread

    int WxlAudio_Play(int slot, const unsigned char* pcm8, int len, int rate, int left, int right)
    {
        if (!g_init || slot < 0 || slot >= kSlots || !pcm8 || len <= 0) return 0;

        int16_t* conv = (int16_t*)malloc(sizeof(int16_t) * (size_t)len);
        if (!conv) return 0;
        for (int i = 0; i < len; ++i) conv[i] = (int16_t)((pcm8[i] - 128) << 8);

        EnterCriticalSection(&g_cs);
        Voice& v = g_voice[slot];
        free(v.data);
        v.data   = conv;
        v.len    = len;
        v.pos    = 0.0;
        v.step   = (double)rate / (double)kRate;
        v.lgain  = left  / 127.0f;
        v.rgain  = right / 127.0f;
        v.active = true;
        LeaveCriticalSection(&g_cs);
        return 1;
    }

    void WxlAudio_SetParams(int slot, int left, int right)
    {
        if (!g_init || slot < 0 || slot >= kSlots) return;
        EnterCriticalSection(&g_cs);
        g_voice[slot].lgain = left  / 127.0f;
        g_voice[slot].rgain = right / 127.0f;
        LeaveCriticalSection(&g_cs);
    }

    void WxlAudio_Stop(int slot)
    {
        if (!g_init || slot < 0 || slot >= kSlots) return;
        EnterCriticalSection(&g_cs);
        g_voice[slot].active = false;
        LeaveCriticalSection(&g_cs);
    }

    int WxlAudio_IsPlaying(int slot)
    {
        if (!g_init || slot < 0 || slot >= kSlots) return 0;
        return g_voice[slot].active ? 1 : 0;
    }
}
