# wxl-doom-game

Yes, it runs Doom. A WarcraftXL module that plays Doom inside the running 3.3.5a client.

The module ticks a full Doom engine once per frame, blits its framebuffer over the whole screen, and
feeds it the keyboard. It is the "but can it run Doom?" flex for the framework: a self-contained game,
on top of the live client, through nothing but the SDK's render and input events.

## Getting in

1. Drop a Doom IWAD next to `Wow.exe`. The shareware `doom1.wad` and the freely-licensed
   [Freedoom](https://freedoom.github.io/) WADs (`freedoom1.wad`, `freedoom2.wad`) both work. The module
   looks for `doom.wad`, `doom1.wad`, `doom2.wad`, `tnt.wad`, `plutonia.wad`, `freedoom1.wad`,
   `freedoom2.wad`, `freedm.wad`, in that order.
2. Launch the client and press **F8**. Doom takes over the screen.
3. You start on the title screen. Press **Enter** or **Esc** for the menu, pick **New Game**, choose a
   skill, and you are in.
4. Press **F8** again at any time to drop back to WoW exactly where you left it.

While Doom is active it owns the keyboard, so the client does not also react to your input.

## Controls

| Key | Action |
|---|---|
| **F8** | Toggle Doom on / off (back to WoW) |
| **Arrow keys** | Move forward / back, turn left / right |
| **Numpad 0** *(or **Ctrl**)* | Fire |
| **Space** | Use: open doors, flip switches |
| **Alt** (hold) | Strafe (turn keys strafe instead) |
| **,** / **.** | Strafe left / right |
| **Shift** | Run |
| **1** – **7** | Switch weapon |
| **Esc** | Menu / back |
| **Enter** | Select / confirm |
| **Tab** | Automap |

> Numpad 0 only sends its code when **NumLock is on** (otherwise Windows sends Insert); use Ctrl if in doubt.

## How it works

- The Doom engine is the vendored, portable [doomgeneric](https://github.com/ozkl/doomgeneric).
- `bridge/DoomBridge.c` implements doomgeneric's platform callbacks, maps Win32 keys to Doom keys, and
  exposes a small C API.
- `src/DoomModule.cpp` subscribes to `OnEndScene` (tick + blit) and `OnInput` (toggle + keys), uploads
  the framebuffer into a D3D9 texture, and draws it as a fullscreen quad through the `wxl::game::gx` facade.
- `module.cmake` compiles the engine as a static library and links it into `WarcraftXL.dll`. It builds
  with the rest of the project; no extra step.

## Notes

- Sound: Doom's SFX play through a small built-in waveOut mixer, and its music plays through the system
  MIDI synth (MUS songs are converted to MIDI on the fly). Entering Doom mutes the client's own audio
  (its master volume is set to 0) and restores it on exit; Doom's audio is independent of that, so the
  game is heard while the client is silent.
- The framebuffer is 640x400, point-sampled to the client resolution (so it fills the screen, it is not
  a 640x400 window).
- Quitting Doom from its own menu, or a missing/invalid IWAD, can close the client (Doom exits the
  process on a fatal error). Leave Doom with **F8**, not its quit menu.

## License

This module is GPLv3. doomgeneric and the Doom engine are under `vendor/`, distributed under their own
GPL terms (see `vendor/doomgeneric/`). No game data is included; supply your own IWAD.
