// wxl-doom-game: runs Doom inside the client, blitted over the frame and driven by the keyboard.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#pragma once

#include "events/EventScript.hpp"
#include "game/gx/Gx.hpp"

namespace wxl::scripts::doom
{
    /**
     * @brief Runs Doom inside the client: ticks the engine each frame, blits its framebuffer over the
     *        whole screen, and forwards the keyboard to it while active. Toggled with F8.
     */
    class DoomModule final : public wxl::events::EventScript
    {
    public:
        DoomModule();

    private:
        /** @brief Ticks Doom and draws its framebuffer over the frame while active. */
        void OnEndScene(const wxl::events::EndSceneArgs& a);
        /** @brief Toggles Doom on F8 and, while active, feeds keys to Doom and swallows them from the game. */
        void OnInput(const wxl::events::InputArgs& a);

        /** @brief Creates the blit texture and pixel shader on first use. */
        bool EnsureResources(void* device);
        /** @brief Copies the current Doom framebuffer into the blit texture. */
        void UploadFrame();
        /** @brief Draws the blit texture as a fullscreen quad. */
        void Blit(wxl::game::gx::Device9 dev);

        bool  active_ = false;
        void* tex_ = nullptr; // IDirect3DTexture9*
        void* ps_  = nullptr; // passthrough pixel shader
        int   texW_ = 0;
        int   texH_ = 0;
        int   diagFrames_ = 0; // one-shot startup diagnostics
    };
}
