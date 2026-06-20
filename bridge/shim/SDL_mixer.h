// Empty shim. doomgeneric's i_sound.c includes <SDL_mixer.h> under FEATURE_SOUND but never uses any SDL
// symbol from it (only the include is present); our sound output is the waveOut mixer, not SDL_mixer.
