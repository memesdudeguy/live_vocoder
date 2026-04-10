/**
 * Main executable: SDL2 + PortAudio vocoder GUI by default (no GTK/Tk/web/Python GUI from this binary).
 * Author: memesdudeguy.
 * Optional: --validate-carrier path [sample_rate] — load .f32 (or ffmpeg-convert), build vocoder, exit (no audio).
 * Optional headless path: --minimal-cpp carrier [sample_rate]  (any audio ffmpeg reads, or raw .f32)
 *
 *   ./LiveVocoder.exe --minimal-cpp song.mp3
 *
 * For the Python app (GTK/web/Tk), run: python3 live_vocoder.py …
 */

#include <portaudio.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <process.h>
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

#include <filesystem>

#include "carrier_convert.hpp"
#include "linux_pulse_env.hpp"
#include "pa_duplex.hpp"
#include "vocoder.hpp"

#ifdef LIVE_VOCODER_HAS_SDL2
#include "sdl_gui.hpp"
#endif

namespace {

constexpr int kSr = 48000;
constexpr int kHop = 512;

struct App {
    std::unique_ptr<StreamingVocoderCpp> voc;
};

int pa_callback(const void* in, void* out, unsigned long frames, const PaStreamCallbackTimeInfo*,
                PaStreamCallbackFlags, void* user) {
    auto* app = static_cast<App*>(user);
    const auto* in_ch = static_cast<const float*>(in);
    float* out_ch = static_cast<float*>(out);

    std::vector<float> mono(frames);
    if (in_ch) {
        for (unsigned long i = 0; i < frames; ++i) {
            mono[i] = in_ch[i];
        }
    } else {
        std::fill(mono.begin(), mono.end(), 0.f);
    }

    std::vector<float> vbuf(frames);
    int produced =
        app->voc->process_block(mono.data(), static_cast<int>(frames), vbuf.data(), static_cast<int>(frames));

    for (unsigned long i = 0; i < static_cast<unsigned long>(produced); ++i) {
        float s = vbuf[i];
        out_ch[i * 2] = out_ch[i * 2 + 1] = s;
    }
    for (unsigned long i = static_cast<unsigned long>(produced); i < frames; ++i) {
        out_ch[i * 2] = out_ch[i * 2 + 1] = 0.f;
    }
    return paContinue;
}

std::vector<double> load_carrier_f32(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error(std::string("open carrier: ") + path);
    }
    f.seekg(0, std::ios::end);
    const auto sz = static_cast<std::size_t>(f.tellg());
    f.seekg(0);
    if (sz % sizeof(float) != 0) {
        throw std::runtime_error("carrier file size not multiple of float32");
    }
    const std::size_t n = sz / sizeof(float);
    std::vector<float> tmp(n);
    f.read(reinterpret_cast<char*>(tmp.data()), static_cast<std::streamsize>(sz));
    std::vector<double> c(n);
    for (std::size_t i = 0; i < n; ++i) {
        c[i] = static_cast<double>(tmp[i]);
    }
    return c;
}

/** Log PortAudio's default host API and default in/out devices (runtime; on Windows usually WASAPI). */
void log_portaudio_default_routing() {
    const PaHostApiIndex n_hosts = Pa_GetHostApiCount();
    if (n_hosts < 0) {
        return;
    }
    const PaHostApiIndex def_host = Pa_GetDefaultHostApi();
    if (def_host >= 0) {
        const PaHostApiInfo* h = Pa_GetHostApiInfo(def_host);
        if (h != nullptr) {
            std::printf("PortAudio default host API: %s\n", h->name);
        }
    }
    const PaDeviceIndex in_def = Pa_GetDefaultInputDevice();
    const PaDeviceIndex out_def = Pa_GetDefaultOutputDevice();
    if (in_def >= 0) {
        const PaDeviceInfo* di = Pa_GetDeviceInfo(in_def);
        if (di != nullptr) {
            std::printf("PortAudio default input: %s\n", di->name);
        }
    }
    if (out_def >= 0) {
        const PaDeviceInfo* d = Pa_GetDeviceInfo(out_def);
        if (d != nullptr) {
            std::printf("PortAudio default output: %s\n", d->name);
        }
    }
}

std::filesystem::path resolve_exe_dir(char* argv0) {
#if defined(_WIN32)
    wchar_t wbuf[MAX_PATH];
    if (GetModuleFileNameW(nullptr, wbuf, MAX_PATH) != 0U) {
        return std::filesystem::path(wbuf).parent_path();
    }
#elif defined(__linux__)
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[static_cast<size_t>(n)] = '\0';
        return std::filesystem::path(buf).parent_path();
    }
#endif
    std::error_code ec;
    std::filesystem::path p = std::filesystem::weakly_canonical(std::filesystem::path(argv0), ec);
    if (!ec) {
        return p.parent_path();
    }
    return std::filesystem::path(argv0).parent_path();
}

#if defined(_WIN32)
/** True if argv has no real subcommand (Explorer sometimes passes extra empty strings). */
static bool windows_argv_looks_like_gui_launch_only(int argc, char** argv) {
    if (argc <= 1) {
        return true;
    }
    for (int i = 1; i < argc; ++i) {
        if (argv[i] != nullptr && argv[i][0] != '\0') {
            return false;
        }
    }
    return true;
}
#endif

/** Load carrier samples (ffmpeg → temp .f32 when needed). On failure prints ``err_out`` and removes temp file. */
static bool prepare_carrier_samples(const char* carrier_path, int sample_rate, std::vector<double>& carrier,
                                    std::filesystem::path& tmp_f32, std::string& err_out) {
    tmp_f32.clear();
    err_out.clear();
    std::filesystem::path use_path = carrier_path;
    if (!carrier_path_is_raw_f32(use_path)) {
#if defined(_WIN32)
        const unsigned pid_u = static_cast<unsigned>(_getpid());
#else
        const unsigned pid_u = static_cast<unsigned>(getpid());
#endif
        tmp_f32 = std::filesystem::temp_directory_path() /
                  ("live_vocoder_carrier_" + std::to_string(pid_u) + ".f32");
        if (!carrier_ffmpeg_to_f32(sample_rate, use_path, tmp_f32, err_out)) {
            return false;
        }
        use_path = tmp_f32;
    }

    try {
        carrier = load_carrier_f32(use_path.string().c_str());
    } catch (const std::exception& e) {
        err_out = e.what();
        if (!tmp_f32.empty()) {
            std::error_code ec;
            std::filesystem::remove(tmp_f32, ec);
        }
        return false;
    }
    return true;
}

/** Exit 0 if carrier loads and vocoder constructs (no PortAudio / SDL). For CI, Wine, and VM smoke tests. */
static int run_validate_carrier_only(const char* carrier_path, int sample_rate) {
    std::vector<double> carrier;
    std::filesystem::path tmp_f32;
    std::string err;
    if (!prepare_carrier_samples(carrier_path, sample_rate, carrier, tmp_f32, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    try {
        StreamingVocoderCpp test_voc(std::move(carrier), sample_rate, 2048, kHop);
        (void)test_voc;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "vocoder: %s\n", e.what());
        if (!tmp_f32.empty()) {
            std::error_code ec;
            std::filesystem::remove(tmp_f32, ec);
        }
        return 1;
    }
    if (!tmp_f32.empty()) {
        std::error_code ec;
        std::filesystem::remove(tmp_f32, ec);
    }
    std::printf("validate-carrier: OK (%d Hz, vocoder ready)\n", sample_rate);
    return 0;
}

int run_minimal_cpp_vocoder(char* argv0, const char* carrier_path, int sample_rate) {
    (void)argv0;
    std::filesystem::path tmp_f32;
    std::vector<double> carrier;
    std::string load_err;
    if (!prepare_carrier_samples(carrier_path, sample_rate, carrier, tmp_f32, load_err)) {
        std::fprintf(stderr, "%s\n", load_err.c_str());
        return 1;
    }

    App app;
    try {
        app.voc = std::make_unique<StreamingVocoderCpp>(std::move(carrier), sample_rate, 2048, kHop);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "vocoder: %s\n", e.what());
        if (!tmp_f32.empty()) {
            std::error_code ec;
            std::filesystem::remove(tmp_f32, ec);
        }
        return 1;
    }

    lv_linux_ensure_pipewire_virt_mic_stack();
    lv_apply_pulse_sink_env_before_portaudio();

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::fprintf(stderr, "PortAudio: %s\n", Pa_GetErrorText(err));
        if (!tmp_f32.empty()) {
            std::error_code ec;
            std::filesystem::remove(tmp_f32, ec);
        }
        return 1;
    }
    pa_log_all_devices_if_requested(stderr);
    {
        const std::string pl = lv_linux_pulse_virt_mic_status_line();
        if (!pl.empty()) {
            std::fprintf(stderr, "[LiveVocoder] %s\n", pl.c_str());
        }
        const std::string pa = pa_portaudio_virt_capture_hint();
        if (!pa.empty()) {
            std::fprintf(stderr, "[LiveVocoder] %s\n", pa.c_str());
        }
    }
    log_portaudio_default_routing();
#if defined(_WIN32)
    std::printf(
        "Using Windows default devices via PortAudio (typically WASAPI). "
        "Change mic/speakers in Windows Sound settings if needed.\n");
#endif

    PaStream* stream = nullptr;
    err = pa_open_livevocoder_duplex(&stream, static_cast<double>(sample_rate),
                                     static_cast<unsigned long>(kHop), pa_callback, &app);
    if (err != paNoError) {
        std::fprintf(stderr, "pa_open_livevocoder_duplex: %s\n", Pa_GetErrorText(err));
        Pa_Terminate();
        if (!tmp_f32.empty()) {
            std::error_code ec;
            std::filesystem::remove(tmp_f32, ec);
        }
        return 1;
    }
    pa_log_stream_devices(stream);

    err = Pa_StartStream(stream);
    if (err != paNoError) {
        std::fprintf(stderr, "Pa_StartStream: %s\n", Pa_GetErrorText(err));
        Pa_CloseStream(stream);
        Pa_Terminate();
        if (!tmp_f32.empty()) {
            std::error_code ec;
            std::filesystem::remove(tmp_f32, ec);
        }
        return 1;
    }
#if defined(__linux__)
    lv_linux_move_livevocoder_sink_input_after_pa_start();
#elif defined(_WIN32)
    lv_linux_wine_move_livevocoder_sink_input_after_pa_start();
#endif

    std::printf("C++ minimal vocoder (Ctrl+D or Enter to stop). Not the full Python app.\n");
    std::cin.get();

    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    if (!tmp_f32.empty()) {
        std::error_code ec;
        std::filesystem::remove(tmp_f32, ec);
    }
    return 0;
}

void print_usage(const char* argv0) {
    std::fprintf(stderr,
                  "Usage:\n"
                  "  %s --validate-carrier <audio|.f32> [sample_rate=%d]\n"
                  "                            Exit 0 if carrier loads (ffmpeg if needed) and vocoder builds; no audio.\n",
                  argv0, kSr);
    std::fprintf(stderr,
                  "  %s --minimal-cpp carrier [sample_rate=%d]\n"
                  "                            Standalone C++ PortAudio vocoder (ffmpeg converts audio → f32le).\n",
                  argv0, kSr);
    std::fprintf(stderr, "  Requires ffmpeg on PATH for non-.f32 files.\n");
#ifdef LIVE_VOCODER_HAS_SDL2
    std::fprintf(stderr,
                  "  %s                        (no args) SDL2 + PortAudio vocoder GUI.\n"
                  "  %s <audio|.f32>          Same, with carrier path (ffmpeg if needed).\n"
                  "  %s --sdl-gui [path]      SDL GUI explicitly.\n",
                  argv0, argv0, argv0);
    std::fprintf(stderr, "  For GTK/web/Tk, run: python3 live_vocoder.py …\n");
#else
    std::fprintf(stderr,
                  "This build was configured with LIVE_VOCODER_CPP_SDL_GUI=OFF; only --minimal-cpp is available.\n"
                  "Reconfigure CMake with -DLIVE_VOCODER_CPP_SDL_GUI=ON (needs SDL2 + SDL2_ttf).\n");
#endif
}

}  // namespace

int lv_program_entry(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "--validate-carrier") == 0) {
        if (argc < 3) {
            print_usage(argv[0]);
            return 2;
        }
        const int sr = (argc >= 4) ? std::atoi(argv[3]) : kSr;
        return run_validate_carrier_only(argv[2], sr);
    }
    if (argc >= 2 && std::strcmp(argv[1], "--minimal-cpp") == 0) {
        if (argc < 3) {
            print_usage(argv[0]);
            return 2;
        }
        const int sr = (argc >= 4) ? std::atoi(argv[3]) : kSr;
        return run_minimal_cpp_vocoder(argv[0], argv[2], sr);
    }

#ifdef LIVE_VOCODER_HAS_SDL2
    if (argc >= 2 && std::strcmp(argv[1], "--sdl-gui") == 0) {
        const char* c = (argc >= 3) ? argv[2] : nullptr;
        return run_sdl_gui(argv[0], c);
    }
    if (argc == 2) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(argv[1], ec)) {
            return run_sdl_gui(argv[0], argv[1]);
        }
    }
#endif

#if defined(_WIN32)
    if (windows_argv_looks_like_gui_launch_only(argc, argv)) {
#ifdef LIVE_VOCODER_HAS_SDL2
        return run_sdl_gui(argv[0], nullptr);
#else
        print_usage(argv[0]);
        return 2;
#endif
    }
#else
    if (argc == 1) {
#ifdef LIVE_VOCODER_HAS_SDL2
        return run_sdl_gui(argv[0], nullptr);
#else
        print_usage(argv[0]);
        return 2;
#endif
    }
#endif
    print_usage(argv[0]);
    return 1;
}
