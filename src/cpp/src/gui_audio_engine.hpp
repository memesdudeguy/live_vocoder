#pragma once

#include <portaudio.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

#include "vocoder.hpp"

namespace lv_gui {

inline constexpr int kSampleRate = 48000;
inline constexpr int kHop = 512;

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
