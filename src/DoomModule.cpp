// wxl-doom-game: runs Doom inside the client, blitted over the frame and driven by the keyboard.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#include "DoomModule.hpp"

#include "DoomBridge.h"

#include "core/Logger.hpp"
#include "game/sound/Sound.hpp"

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>

namespace wxl::scripts::doom
{
    namespace gx  = wxl::game::gx;
    namespace ev  = wxl::events;
    namespace snd = wxl::game::sound;

    namespace
    {
        // Standard D3D9 vtable slots and enum values (public D3D constants, not client offsets).
        constexpr unsigned kVtCreateTexture   = 23; // IDirect3DDevice9::CreateTexture
        constexpr unsigned kVtTexLockRect     = 19; // IDirect3DTexture9::LockRect
        constexpr unsigned kVtTexUnlockRect   = 20; // IDirect3DTexture9::UnlockRect
        constexpr unsigned kVtSetSamplerState = 69; // IDirect3DDevice9::SetSamplerState

        constexpr unsigned kFmtX8R8G8B8 = 22;   // D3DFMT_X8R8G8B8
        constexpr unsigned kUsageDynamic = 0x200; // D3DUSAGE_DYNAMIC (CPU-updated each frame)
        constexpr unsigned kPoolDefault  = 0;     // D3DPOOL_DEFAULT
        constexpr unsigned kLockDiscard  = 0x2000; // D3DLOCK_DISCARD
        constexpr unsigned kSampAddressU = 1, kSampAddressV = 2, kSampMagFilter = 5, kSampMinFilter = 6;
        constexpr unsigned kAddressClamp = 3, kFilterPoint = 1;

        constexpr int kToggleVk = VK_F8;

        using CreateTexFn = long(__stdcall*)(void*, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, void**, void*);
        using LockFn      = long(__stdcall*)(void*, unsigned, void*, const void*, unsigned);
        using UnlockFn    = long(__stdcall*)(void*, unsigned);
        using SetSampFn   = long(__stdcall*)(void*, unsigned, unsigned, unsigned);
        struct D3DLockedRect { int pitch; void* bits; };

        // Passthrough: sample stage 0 and return it.
        const char* kBlitHLSL =
            "sampler2D s0 : register(s0);\n"
            "float4 main(float2 uv : TEXCOORD0) : COLOR0 { return tex2D(s0, uv); }\n";

        // The first IWAD found next to Wow.exe, or empty if none is present.
        std::string FindWad()
        {
            char exe[MAX_PATH];
            DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
            std::string dir(exe, n);
            size_t slash = dir.find_last_of("\\/");
            dir = (slash == std::string::npos) ? std::string(".") : dir.substr(0, slash);

            const char* names[] = {
                "doom.wad", "doom1.wad", "doom2.wad", "tnt.wad", "plutonia.wad",
                "freedoom1.wad", "freedoom2.wad", "freedm.wad", nullptr
            };
            for (int i = 0; names[i]; ++i)
            {
                std::string p = dir + "\\" + names[i];
                DWORD attr = GetFileAttributesA(p.c_str());
                if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
                    return p;
            }
            return std::string();
        }
    }

    DoomModule::DoomModule()
    {
        on<&DoomModule::OnEndScene>(ev::Event::OnEndScene);
        on<&DoomModule::OnInput>(ev::Event::OnInput);
    }

    bool DoomModule::EnsureResources(void* device)
    {
        if (!ps_)
            ps_ = gx::CompilePixelShader(gx::Device9(device), kBlitHLSL, "ps_2_0");

        if (!tex_)
        {
            int w = 0, h = 0;
            WxlDoom_Framebuffer(&w, &h);
            void* t = nullptr;
            auto create = gx::Vtbl<CreateTexFn>(device, kVtCreateTexture);
            if (create(device, (unsigned)w, (unsigned)h, 1, kUsageDynamic, kFmtX8R8G8B8, kPoolDefault, &t, nullptr) < 0 || !t)
                return false;
            tex_ = t; texW_ = w; texH_ = h;
        }
        return ps_ && tex_;
    }

    void DoomModule::UploadFrame()
    {
        if (!tex_) return;

        int w = 0, h = 0;
        const uint32_t* src = WxlDoom_Framebuffer(&w, &h);
        if (!src) return;

        D3DLockedRect lr{};
        auto lock = gx::Vtbl<LockFn>(tex_, kVtTexLockRect);
        if (lock(tex_, 0, &lr, nullptr, kLockDiscard) < 0 || !lr.bits) return;

        auto* dst = static_cast<uint8_t*>(lr.bits);
        for (int y = 0; y < h; ++y)
            std::memcpy(dst + (size_t)y * lr.pitch, src + (size_t)y * w, (size_t)w * 4);

        gx::Vtbl<UnlockFn>(tex_, kVtTexUnlockRect)(tex_, 0);
    }

    void DoomModule::Blit(gx::Device9 dev)
    {
        const unsigned sZE = dev.GetRenderState(gx::rs::kZEnable);
        const unsigned sAB = dev.GetRenderState(gx::rs::kAlphaBlend);
        const unsigned sCU = dev.GetRenderState(gx::rs::kCullMode);

        dev.SetRenderState(gx::rs::kZEnable, 0);
        dev.SetRenderState(gx::rs::kAlphaBlend, 0);
        dev.SetRenderState(gx::rs::kCullMode, gx::cull::kNone);

        auto samp = gx::Vtbl<SetSampFn>(dev.raw(), kVtSetSamplerState);
        samp(dev.raw(), 0, kSampMagFilter, kFilterPoint);
        samp(dev.raw(), 0, kSampMinFilter, kFilterPoint);
        samp(dev.raw(), 0, kSampAddressU, kAddressClamp);
        samp(dev.raw(), 0, kSampAddressV, kAddressClamp);

        dev.SetVertexShader(nullptr);
        dev.SetTexture(0, tex_);
        dev.SetPixelShader(ps_);
        gx::DrawFullscreenQuad(dev);

        dev.SetPixelShader(nullptr);
        dev.SetTexture(0, nullptr);
        dev.SetRenderState(gx::rs::kZEnable, sZE);
        dev.SetRenderState(gx::rs::kAlphaBlend, sAB);
        dev.SetRenderState(gx::rs::kCullMode, sCU);
    }

    void DoomModule::OnEndScene(const ev::EndSceneArgs& a)
    {
        if (!active_) return;
        gx::Device9 dev(a.device);
        if (!dev) return;

        if (!WxlDoom_Started())
        {
            std::string wad = FindWad();
            if (wad.empty())
            {
                WLOG_INFO("[doom] no IWAD next to Wow.exe. Drop doom1.wad or freedoom1.wad there, then press F8 again.");
                snd::SetMasterVolume(savedVol_);
                active_ = false;
                return;
            }
            WLOG_INFO("[doom] booting with IWAD '%s'", wad.c_str());
            WxlDoom_Start(wad.c_str());
        }

        WxlDoom_Tick();

        if (!EnsureResources(dev.raw()))
        {
            if (diagFrames_ < 3) { WLOG_INFO("[doom] diag: EnsureResources failed (ps=%p tex=%p)", ps_, tex_); ++diagFrames_; }
            return;
        }

        if (diagFrames_ < 3)
        {
            int w = 0, h = 0;
            const uint32_t* fb = WxlDoom_Framebuffer(&w, &h);
            uint32_t center = (fb && w > 0 && h > 0) ? fb[(h / 2) * w + w / 2] : 0;
            uint32_t any = 0;
            if (fb) for (int i = 0; i < w * h; i += 991) any |= fb[i];
            WLOG_INFO("[doom] diag frame=%d fb=%p %dx%d center=0x%08X nonzero=%u tex=%p ps=%p",
                      diagFrames_, (void*)fb, w, h, center, any ? 1u : 0u, tex_, ps_);
            ++diagFrames_;
        }

        UploadFrame();
        Blit(dev);
    }

    void DoomModule::OnInput(const ev::InputArgs& a)
    {
        const unsigned msg = a.message;
        const bool down = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
        const bool up   = (msg == WM_KEYUP   || msg == WM_SYSKEYUP);
        if (!down && !up) return;

        const int vk = (int)a.wparam;

        // F8 toggles Doom. Act on the first press only (ignore auto-repeat), and swallow it either way.
        if (vk == kToggleVk)
        {
            if (down && !(a.lparam & (1 << 30)))
            {
                active_ = !active_;
                if (active_) { savedVol_ = snd::MasterVolume(); snd::SetMasterVolume(0.0f); }
                else         { snd::SetMasterVolume(savedVol_); }
                WLOG_INFO("[doom] %s (client vol %.2f)", active_ ? "on" : "off", savedVol_);
            }
            if (a.handled) *a.handled = true;
            return;
        }

        if (!active_) return;

        // Doom owns the keyboard while active: forward the key and keep the game from also reacting.
        WxlDoom_PushKey(down ? 1 : 0, vk);
        if (a.handled) *a.handled = true;
    }

    // Self-registration: the file-scope instance binds its handlers at DLL load via the EventScript ctor.
    DoomModule g_doom;
}
