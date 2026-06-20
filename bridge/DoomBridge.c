// DoomBridge: implements doomgeneric's DG_* platform callbacks and a small C API the C++ module drives.
// Copyright (C) 2026 WarcraftXL. GPLv3.
//
// doomgeneric renders into DG_ScreenBuffer and pulls input through DG_GetKey; this file wires those to a
// host-fed key ring and exposes WxlDoom_* so the module can boot, tick, read the framebuffer and feed keys.

#include <windows.h>
#include <string.h>

#include "doomgeneric.h"
#include "doomkeys.h"

// ---- input ring (single producer: the window thread; single consumer: the tick thread) ----
#define WXL_DOOM_QCAP 128
static volatile unsigned short g_q[WXL_DOOM_QCAP]; // (pressed << 8) | doomKey
static volatile int g_qHead = 0;                   // next write
static volatile int g_qTail = 0;                   // next read

// Map a Win32 virtual-key code to a Doom keycode using Doom's default control scheme. 0 = unmapped.
static int MapVk(int vk)
{
    switch (vk)
    {
    case VK_LEFT:    return KEY_LEFTARROW;
    case VK_RIGHT:   return KEY_RIGHTARROW;
    case VK_UP:      return KEY_UPARROW;
    case VK_DOWN:    return KEY_DOWNARROW;
    case VK_RETURN:  return KEY_ENTER;
    case VK_ESCAPE:  return KEY_ESCAPE;
    case VK_TAB:     return KEY_TAB;
    case VK_BACK:    return KEY_BACKSPACE;
    case VK_NUMPAD0: return KEY_FIRE;    // fire (numpad 0)
    case VK_CONTROL: return KEY_FIRE;    // fire (alternate)
    case VK_SHIFT:   return KEY_RSHIFT;  // run
    case VK_MENU:    return KEY_RALT;    // strafe
    case VK_SPACE:   return KEY_USE;     // use / open doors
    case VK_OEM_COMMA:  return ',';      // strafe left
    case VK_OEM_PERIOD: return '.';      // strafe right
    case VK_OEM_MINUS:  return KEY_MINUS;
    case VK_OEM_PLUS:   return KEY_EQUALS;
    default:
        if (vk >= 'A' && vk <= 'Z') return vk - 'A' + 'a'; // menu navigation + cheats
        if (vk >= '0' && vk <= '9') return vk;
        return 0;
    }
}

// ---- doomgeneric platform callbacks ----

void DG_Init(void) {}

// The host blits DG_ScreenBuffer itself each frame, so nothing to present here.
void DG_DrawFrame(void) {}

// The host's own frame loop paces Doom; never block the render thread.
void DG_SleepMs(uint32_t ms) { (void)ms; }

uint32_t DG_GetTicksMs(void) { return (uint32_t)GetTickCount(); }

int DG_GetKey(int* pressed, unsigned char* key)
{
    if (g_qHead == g_qTail) return 0; // empty
    unsigned short v = g_q[g_qTail];
    g_qTail = (g_qTail + 1) % WXL_DOOM_QCAP;
    *pressed = (v >> 8) & 1;
    *key = (unsigned char)(v & 0xff);
    return 1;
}

void DG_SetWindowTitle(const char* title) { (void)title; }

// ---- WxlDoom_* API ----

static int  g_started = 0;
static char g_wad[1024];
static char g_arg0[] = "doomgeneric";
static char g_argIwad[] = "-iwad";
static char* g_argv[3]; // doomgeneric keeps this pointer (myargv), so it must outlive the call

int WxlDoom_Start(const char* iwadPath)
{
    size_t n = 0;
    if (g_started) return 0;
    g_started = 1;

    if (iwadPath)
        while (iwadPath[n] && n < sizeof(g_wad) - 1) { g_wad[n] = iwadPath[n]; ++n; }
    g_wad[n] = '\0';

    g_argv[0] = g_arg0;
    g_argv[1] = g_argIwad;
    g_argv[2] = g_wad;
    doomgeneric_Create(3, g_argv);
    return 1;
}

int WxlDoom_Started(void) { return g_started; }

void WxlDoom_Tick(void) { if (g_started) doomgeneric_Tick(); }

const uint32_t* WxlDoom_Framebuffer(int* width, int* height)
{
    if (width)  *width  = DOOMGENERIC_RESX;
    if (height) *height = DOOMGENERIC_RESY;
    return (const uint32_t*)DG_ScreenBuffer;
}

void WxlDoom_PushKey(int pressed, int vk)
{
    int next;
    int doomKey = MapVk(vk);
    if (!doomKey) return;

    next = (g_qHead + 1) % WXL_DOOM_QCAP;
    if (next == g_qTail) return; // full, drop the event
    g_q[g_qHead] = (unsigned short)(((pressed ? 1 : 0) << 8) | (doomKey & 0xff));
    g_qHead = next;
}
