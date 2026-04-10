/**
 * Windows + SDL2: libSDL2main provides WinMain → SDL_main. Do not use SDL_MAIN_HANDLED here.
 */
#if defined(_WIN32) && defined(LIVE_VOCODER_HAS_SDL2)

#include <SDL.h>

extern int lv_program_entry(int argc, char** argv);

int main(int argc, char* argv[]) {
    return lv_program_entry(argc, argv);
}

#endif
