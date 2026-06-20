// DoomMusic: plays a Doom MIDI song through the Windows MIDI streaming API (midiStream).
// Copyright (C) 2026 WarcraftXL. GPLv3.
//
// WxlMusic_Register parses a Standard MIDI File into a midiStream event list; a background thread
// double-buffers it to the OS synth and loops at the end of the song. Output goes to the system MIDI
// device, so it is independent of the client's muted master volume. The C music bridge
// (bridge/DoomMusic.c) drives it through the extern "C" WxlMusic_* API.

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace
{
    // One midiStream entry: delta ticks, stream id (0), packed MEVT event.
    struct StreamEvt { DWORD delta; DWORD stream; DWORD event; };

    constexpr int kBuffers      = 4;    // event buffers cycled to the stream
    constexpr int kEventsPerBuf = 256;  // events written per buffer

    std::vector<StreamEvt> g_song;       // parsed song, in stream order
    DWORD                  g_division = 96; // ticks per quarter note (from the MThd header)

    HMIDISTRM        g_strm    = nullptr;
    HANDLE           g_thread  = nullptr;
    HANDLE           g_done    = nullptr; // signalled by the stream callback on MOM_DONE
    volatile bool    g_run     = false;
    volatile bool    g_loop    = false;
    CRITICAL_SECTION g_cs;
    bool             g_csInit  = false;
    int              g_volume  = 127;    // 0..127
    size_t           g_pos     = 0;      // feed cursor into g_song

    // ---- Standard MIDI File parsing --------------------------------------

    struct Reader { const uint8_t* p; const uint8_t* end; };

    inline bool     Rok(const Reader& r, int n) { return r.p + n <= r.end; }
    inline uint8_t  R8 (Reader& r)              { return *r.p++; }
    inline uint32_t R16(Reader& r)              { uint32_t v = (uint32_t(r.p[0]) << 8) | r.p[1]; r.p += 2; return v; }
    inline uint32_t R32(Reader& r)              { uint32_t v = (uint32_t(r.p[0]) << 24) | (uint32_t(r.p[1]) << 16) | (uint32_t(r.p[2]) << 8) | r.p[3]; r.p += 4; return v; }

    // Variable length quantity.
    uint32_t Rvlq(Reader& r)
    {
        uint32_t v = 0; uint8_t b;
        do { if (!Rok(r, 1)) return v; b = R8(r); v = (v << 7) | (b & 0x7f); } while (b & 0x80);
        return v;
    }

    // An event placed at an absolute tick; event is the packed MEVT value (top byte = MEVT type).
    struct AbsEvt { uint32_t tick; DWORD event; };

    void ParseTrack(Reader r, std::vector<AbsEvt>& out)
    {
        uint32_t tick   = 0;
        uint8_t  status = 0;

        while (Rok(r, 1))
        {
            tick += Rvlq(r);
            if (!Rok(r, 1)) break;

            uint8_t b = *r.p;
            if (b & 0x80) { status = b; ++r.p; } // new status, else running status keeps the last

            if (status == 0xFF) // meta event
            {
                if (!Rok(r, 1)) break;
                uint8_t  type = R8(r);
                uint32_t len  = Rvlq(r);
                if (!Rok(r, (int)len)) break;
                if (type == 0x51 && len == 3) // set tempo (us per quarter)
                {
                    uint32_t us = (uint32_t(r.p[0]) << 16) | (uint32_t(r.p[1]) << 8) | r.p[2];
                    out.push_back({ tick, (DWORD(0x01) << 24) | (us & 0xFFFFFF) }); // MEVT_TEMPO
                }
                r.p += len; // 0x2F end-of-track and others: ignored, merge handles the end
            }
            else if (status == 0xF0 || status == 0xF7) // sysex
            {
                uint32_t len = Rvlq(r);
                if (!Rok(r, (int)len)) break;
                r.p += len;
            }
            else if (status >= 0x80) // channel voice message
            {
                uint8_t hi     = status & 0xF0;
                int     nbytes = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
                if (!Rok(r, nbytes)) break;
                uint8_t d1 = R8(r);
                uint8_t d2 = (nbytes == 2) ? R8(r) : 0;
                out.push_back({ tick, DWORD(status) | (DWORD(d1) << 8) | (DWORD(d2) << 16) }); // MEVT_SHORTMSG
            }
            else break; // malformed
        }
    }

    bool ParseSmf(const uint8_t* data, int len)
    {
        Reader r{ data, data + len };
        if (!Rok(r, 14) || memcmp(r.p, "MThd", 4) != 0) return false;
        r.p += 4;
        uint32_t hlen   = R32(r);
        /* format */     R16(r);
        uint32_t ntrk   = R16(r);
        uint32_t div    = R16(r);
        r.p += (hlen > 6) ? (hlen - 6) : 0;

        g_division = (div & 0x8000) ? 96 : (div ? div : 96); // metric division only; SMPTE falls back

        std::vector<AbsEvt> all;
        for (uint32_t t = 0; t < ntrk && Rok(r, 8); ++t)
        {
            if (memcmp(r.p, "MTrk", 4) != 0) break;
            r.p += 4;
            uint32_t tlen = R32(r);
            if (!Rok(r, (int)tlen)) break;
            ParseTrack(Reader{ r.p, r.p + tlen }, all);
            r.p += tlen;
        }
        if (all.empty()) return false;

        std::stable_sort(all.begin(), all.end(), [](const AbsEvt& a, const AbsEvt& b) { return a.tick < b.tick; });

        g_song.clear();
        g_song.reserve(all.size());
        uint32_t prev = 0;
        for (const AbsEvt& e : all)
        {
            g_song.push_back({ e.tick - prev, 0, e.event });
            prev = e.tick;
        }
        return true;
    }

    // ---- midiStream playback ---------------------------------------------

    struct Buf { MIDIHDR hdr; std::vector<DWORD> mem; bool prepared = false; };

    void ApplyVolume()
    {
        if (!g_strm) return;
        WORD w = (WORD)((g_volume * 0xFFFF) / 127);
        midiOutSetVolume((HMIDIOUT)g_strm, (DWORD(w) << 16) | w);
    }

    // Fill one buffer with up to kEventsPerBuf events from g_pos. Returns false when the song ended and
    // there is nothing left to queue (no loop).
    bool FillBuffer(Buf& b)
    {
        b.mem.clear();
        int count = 0;
        while (count < kEventsPerBuf)
        {
            if (g_pos >= g_song.size())
            {
                if (g_loop && !g_song.empty()) g_pos = 0;
                else break;
            }
            const StreamEvt& s = g_song[g_pos++];
            b.mem.push_back(s.delta);
            b.mem.push_back(s.stream);
            b.mem.push_back(s.event);
            ++count;
        }
        if (count == 0) return false;

        if (b.prepared) { midiOutUnprepareHeader((HMIDIOUT)g_strm, &b.hdr, sizeof(MIDIHDR)); b.prepared = false; }
        memset(&b.hdr, 0, sizeof(MIDIHDR));
        b.hdr.lpData         = (LPSTR)b.mem.data();
        b.hdr.dwBufferLength = b.hdr.dwBytesRecorded = (DWORD)(b.mem.size() * sizeof(DWORD));
        midiOutPrepareHeader((HMIDIOUT)g_strm, &b.hdr, sizeof(MIDIHDR));
        b.prepared = true;
        midiStreamOut(g_strm, &b.hdr, sizeof(MIDIHDR));
        return true;
    }

    void CALLBACK StreamProc(HMIDIOUT, UINT msg, DWORD_PTR, DWORD_PTR, DWORD_PTR)
    {
        if (msg == MOM_DONE && g_done) SetEvent(g_done);
    }

    DWORD WINAPI PlayThread(LPVOID)
    {
        UINT dev = MIDI_MAPPER;
        if (midiStreamOpen(&g_strm, &dev, 1, (DWORD_PTR)StreamProc, 0, CALLBACK_FUNCTION) != MMSYSERR_NOERROR)
        {
            g_strm = nullptr;
            return 0;
        }

        MIDIPROPTIMEDIV tdiv;
        tdiv.cbStruct  = sizeof(tdiv);
        tdiv.dwTimeDiv = g_division;
        midiStreamProperty(g_strm, (LPBYTE)&tdiv, MIDIPROP_SET | MIDIPROP_TIMEDIV);
        ApplyVolume();

        Buf bufs[kBuffers];
        for (int i = 0; i < kBuffers; ++i) bufs[i].mem.reserve(kEventsPerBuf * 3);

        g_pos = 0;
        int queued = 0;
        for (int i = 0; i < kBuffers; ++i) { if (FillBuffer(bufs[i])) ++queued; else break; }

        midiStreamRestart(g_strm);

        int next = queued % kBuffers; // first buffer to recycle on MOM_DONE
        while (g_run && queued > 0)
        {
            WaitForSingleObject(g_done, 100);
            // Recycle any completed buffers; one MOM_DONE may cover several at low event counts.
            for (int i = 0; i < kBuffers; ++i)
            {
                Buf& b = bufs[next];
                if (b.prepared && (b.hdr.dwFlags & MHDR_DONE))
                {
                    --queued;
                    if (g_run && FillBuffer(b)) ++queued; // refills, or returns false at song end
                }
                next = (next + 1) % kBuffers;
                if (!g_run) break;
            }
        }

        midiStreamStop(g_strm);
        midiOutReset((HMIDIOUT)g_strm);
        for (int i = 0; i < kBuffers; ++i)
            if (bufs[i].prepared) midiOutUnprepareHeader((HMIDIOUT)g_strm, &bufs[i].hdr, sizeof(MIDIHDR));
        midiStreamClose(g_strm);
        g_strm = nullptr;
        return 0;
    }

    void EnsureInit()
    {
        if (!g_csInit) { InitializeCriticalSection(&g_cs); g_csInit = true; }
        if (!g_done)   g_done = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    }

    void StopThread()
    {
        if (!g_thread) return;
        g_run = false;
        if (g_done) SetEvent(g_done);
        WaitForSingleObject(g_thread, 2000);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
}

extern "C"
{
    int WxlMusic_Register(const unsigned char* midi, int len)
    {
        EnsureInit();
        StopThread();
        EnterCriticalSection(&g_cs);
        bool ok = ParseSmf(midi, len);
        LeaveCriticalSection(&g_cs);
        return ok ? 1 : 0;
    }

    void WxlMusic_Unregister(void)
    {
        StopThread();
        if (g_csInit) { EnterCriticalSection(&g_cs); g_song.clear(); LeaveCriticalSection(&g_cs); }
    }

    void WxlMusic_Play(int looping)
    {
        EnsureInit();
        StopThread();
        if (g_song.empty()) return;
        g_loop = looping != 0;
        g_run  = true;
        g_thread = CreateThread(nullptr, 0, PlayThread, nullptr, 0, nullptr);
    }

    void WxlMusic_Stop(void)   { StopThread(); }
    void WxlMusic_Pause(void)  { if (g_strm) midiStreamPause(g_strm); }
    void WxlMusic_Resume(void) { if (g_strm) midiStreamRestart(g_strm); }

    void WxlMusic_SetVolume(int vol)
    {
        if (vol < 0)   vol = 0;
        if (vol > 127) vol = 127;
        g_volume = vol;
        ApplyVolume();
    }

    int WxlMusic_IsPlaying(void) { return (g_thread && g_run) ? 1 : 0; }
}
