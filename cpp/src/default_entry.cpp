/**
 * Normal program entry when not using Win32 SDL2main (Linux/macOS, or Windows without SDL GUI).
 */
#if !(defined(_WIN32) && defined(LIVE_VOCODER_HAS_SDL2))

extern int lv_program_entry(int argc, char** argv);

int main(int argc, char** argv) {
    return lv_program_entry(argc, argv);
}

#endif
