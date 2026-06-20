// DoomBridge: a tiny C API over doomgeneric so the C++ module drives Doom without touching its internals.
// Copyright (C) 2026 WarcraftXL. GPLv3.

#ifndef WXL_DOOM_BRIDGE_H
#define WXL_DOOM_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Boots the Doom engine with the given absolute IWAD path. Boots only on the first call.
 * @param iwadPath  absolute path to a Doom IWAD (e.g. doom1.wad or freedoom1.wad).
 * @return 1 if this call booted the engine, 0 if it was already running.
 */
int WxlDoom_Start(const char* iwadPath);

/** @brief Returns 1 once the engine has been booted. */
int WxlDoom_Started(void);

/** @brief Advances Doom by one frame: runs game logic and renders into the internal framebuffer. */
void WxlDoom_Tick(void);

/**
 * @brief Returns the current framebuffer (XRGB8888, row-major, tightly packed).
 * @param width   receives the framebuffer width.
 * @param height  receives the framebuffer height.
 * @return the pixel buffer, or null before the engine has booted.
 */
const uint32_t* WxlDoom_Framebuffer(int* width, int* height);

/**
 * @brief Pushes a keyboard event into Doom's input queue.
 * @param pressed  1 for key-down, 0 for key-up.
 * @param vk       a Win32 virtual-key code; unmapped keys are ignored.
 */
void WxlDoom_PushKey(int pressed, int vk);

#ifdef __cplusplus
}
#endif

#endif // WXL_DOOM_BRIDGE_H
