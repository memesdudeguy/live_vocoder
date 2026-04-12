#pragma once

#include <portaudio.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "vocoder.hpp"

namespace lv_gui {

inline constexpr int kSampleRate = 48000;

/** Parse once: PortAudio + vocoder hop (must divide 2048). Native Windows defaults to 256; Wine/Linux default 512. */
inline int livevocoder_hop_frames() {
    static const int k_cached = [] {
        constexpr int k_n_fft = 2048;
        constexpr int k_default = 512;
        auto parse_hop = [](const char* e) -> int {
            if (e == nullptr || e[0] == '\0') {
                return -1;
            }
            char* end = nullptr;
            const long x = std::strtol(e, &end, 10);
            if (end == e || x < 64 || x > 512 || k_n_fft % x != 0) {
                return -1;
            }
            return static_cast<int>(x);
        };
        if (const int from_env = parse_hop(std::getenv("LIVE_VOCODER_HOP")); from_env > 0) {
            return from_env;
        }
        const char* live = std::getenv("LIVE_VOCODER_LIVE_MONITORING");
        if (live != nullptr && live[0] != '\0' && !(live[0] == '0' && live[1] == '\0')) {
            if (std::strcmp(live, "false") == 0 || std::strcmp(live, "no") == 0) {
                return k_default;
            }
            return 256;
        }
#if defined(_WIN32)
        /* Native Windows: 256-sample hops by default (~5.3 ms) for tighter monitoring; Wine unchanged. */
        {
            HMODULE ntdll = GetModuleHandleA("ntdll.dll");
            const bool wine = ntdll != nullptr && GetProcAddress(ntdll, "wine_get_version") != nullptr;
            if (!wine) {
                const char* lag = std::getenv("LIVE_VOCODER_HIGH_LATENCY_HOP");
                const bool force512 = lag != nullptr && lag[0] != '\0' && lag[0] != '0' &&
                                      std::strcmp(lag, "false") != 0 && std::strcmp(lag, "no") != 0;
                if (!force512) {
                    return 256;
                }
            }
        }
#endif
        return k_default;
    }();
    return k_cached;
}

/** Cheap comb delay + wet/dry — same behavior as SDL GUI. */
struct LightReverb {
    std::vector<float> buf_{};
    int pos_{0};
    int len_{0};
    float fb_{0.48f};

    void reset() {
        std::fill(buf_.begin(), buf_.end(), 0.f);
        pos_ = 0;
    }

    void configure(int sample_rate, float room) {
        room = std::clamp(room, 0.f, 1.f);
        len_ = std::clamp(static_cast<int>(static_cast<float>(sample_rate) * (0.022f + room * 0.055f)), 400, 4800);
        fb_ = 0.32f + room * 0.52f;
        buf_.assign(static_cast<std::size_t>(len_), 0.f);
        pos_ = 0;
    }

    void process(float* data, int n, float mix) {
        if (mix < 1e-5f || len_ <= 0 || buf_.empty()) {
            return;
        }
        for (int i = 0; i < n; ++i) {
            const float x = data[i];
            const int p = pos_;
            const float d = buf_[static_cast<std::size_t>(p)];
            buf_[static_cast<std::size_t>(p)] = x + fb_ * d;
            pos_ = (p + 1) % len_;
            data[i] = x * (1.f - mix) + d * mix;
        }
    }
};

struct LiveVocoderAudioApp {
    std::unique_ptr<StreamingVocoderCpp> voc;
    LightReverb reverb{};
    std::atomic<bool> clean_mic{false};
    std::atomic<bool> monitor_on{true};
    std::atomic<float> reverb_mix{0.f};
    std::atomic<bool> pulse_virt_sink_output{false};
    std::atomic<float> meter_in_peak{0.f};
    std::atomic<float> meter_out_peak{0.f};
    std::atomic<int> test_beep_frames_left{0};
    float test_beep_phase{0.f};
};

int livevocoder_gui_pa_callback(const void* input_buffer, void* output_buffer, unsigned long frames_per_buffer,
                                const PaStreamCallbackTimeInfo* time_info, PaStreamCallbackFlags status_flags,
                                void* user_data);

}  // namespace lv_gui
