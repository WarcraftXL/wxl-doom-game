# wxl-doom-game: build the vendored Doom engine (doomgeneric) as a static C library and link it into the
# WarcraftXL DLL. The platform main files and SDL/Allegro audio backends are excluded; bridge/DoomBridge.c
# supplies the DG_* callbacks. Included by the root CMake per-module hook (the WarcraftXL target exists).

enable_language(C)

set(_dg "${CMAKE_CURRENT_LIST_DIR}/vendor/doomgeneric/doomgeneric")
if(NOT EXISTS "${_dg}/doomgeneric.c")
    message(WARNING "wxl-doom-game: doomgeneric sources missing at '${_dg}'. See the module README to fetch them. Skipping.")
    return()
endif()

file(GLOB _doom_src CONFIGURE_DEPENDS "${_dg}/*.c")
# Drop every platform main (each defines its own main + DG_* callbacks) and the SDL/Allegro audio backends.
# What remains is the engine core, exactly the set the project's Windows build compiles.
list(FILTER _doom_src EXCLUDE REGEX
    "/(doomgeneric_(allegro|emscripten|linuxvt|sdl|soso|sosox|win|xlib)|i_sdlsound|i_sdlmusic|i_allegrosound|i_allegromusic)\\.c$")

add_library(wxl_doom STATIC
    ${_doom_src}
    "${CMAKE_CURRENT_LIST_DIR}/bridge/DoomBridge.c"
    "${CMAKE_CURRENT_LIST_DIR}/bridge/DoomSound.c"
    "${CMAKE_CURRENT_LIST_DIR}/bridge/DoomMusic.c")
target_include_directories(wxl_doom PRIVATE "${_dg}" "${CMAKE_CURRENT_LIST_DIR}/bridge/shim")
# FEATURE_SOUND wires Doom's SFX path to DG_sound_module (bridge/DoomSound.c -> the waveOut mixer).
target_compile_definitions(wxl_doom PRIVATE WIN32 NDEBUG _CONSOLE FEATURE_SOUND _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
if(MSVC)
    target_compile_options(wxl_doom PRIVATE /w) # vendored C: silence its own warnings
endif()

target_link_libraries(WarcraftXL PRIVATE wxl_doom winmm) # winmm: waveOut for the Doom mixer
target_include_directories(WarcraftXL PRIVATE "${CMAKE_CURRENT_LIST_DIR}/bridge")

message(STATUS "wxl-doom-game: doomgeneric linked into WarcraftXL")
