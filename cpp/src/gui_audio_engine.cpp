#include "gui_audio_engine.hpp"

#include <algorithm>
#include <cstring>

namespace lv_gui {

int livevocoder_gui_pa_callback(const void* input_buffer, void* output_buffer, unsigned long frames_per_buffer,
                                const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* user_data) {
    auto* app = static_cast<LiveVocoderAudioApp*>(user_data);
    const auto* in_ch = static_cast<const float*>(input_buffer);
    float* out_ch = static_cast<float*>(output_buffer);

    float peak_in = 0.f;
    if (in_ch != nullptr) {
        for (unsigned long i = 0; i < frames_per_buffer; ++i) {
            peak_in = std::max(peak_in, std::fabs(in_ch[i]));
        }
    }
    app->meter_in_peak.store(peak_in, std::memory_order_relaxed);

    auto store_out_peak = [&]() {
        float pk = 0.f;
        for (unsigned long i = 0; i < frames_per_buffer; ++i) {
            pk = std::max(pk, std::fabs(out_ch[i * 2]));
        }
        app->meter_out_peak.store(pk, std::memory_order_relaxed);
    };

    const bool monitoring = app->monitor_on.load(std::memory_order_relaxed);
    const bool sink_for_discord = app->pulse_virt_sink_output.load(std::memory_order_relaxed);
    const int beep_left_in = app->test_beep_frames_left.load(std::memory_order_relaxed);
    const bool allow_out = monitoring || sink_for_discord || beep_left_in > 0;
    if (!allow_out) {
        for (unsigned long i = 0; i < frames_per_buffer; ++i) {
            out_ch[i * 2] = out_ch[i * 2 + 1] = 0.f;
        }
        app->meter_out_peak.store(0.f, std::memory_order_relaxed);
        return paContinue;
    }

    auto mix_test_beep = [&]() {
        int left = app->test_beep_frames_left.load(std::memory_order_relaxed);
        if (left <= 0) {
            return;
        }
        constexpr float kHz = 880.f;
        const float w = 2.f * 3.14159265f * kHz / static_cast<float>(kSampleRate);
        const int n = std::min(left, static_cast<int>(frames_per_buffer));
        for (int i = 0; i < n; ++i) {
            const float t = 0.12f * std::sin(app->test_beep_phase);
            app->test_beep_phase += w;
            if (app->test_beep_phase > 6.2831853f) {
                app->test_beep_phase -= 6.2831853f;
            }
            const auto u = static_cast<unsigned long>(i);
            out_ch[u * 2] += t;
            out_ch[u * 2 + 1] += t;
        }
        app->test_beep_frames_left.fetch_sub(n, std::memory_order_relaxed);
    };

    std::vector<float> mono(frames_per_buffer);
    if (in_ch != nullptr) {
        for (unsigned long i = 0; i < frames_per_buffer; ++i) {
            mono[i] = in_ch[i];
        }
    } else {
        std::fill(mono.begin(), mono.end(), 0.f);
    }

    StreamingVocoderCpp* voc = app->voc.get();
    if (voc == nullptr) {
        const float rm = app->reverb_mix.load(std::memory_order_relaxed);
        if (rm > 1e-5f) {
            app->reverb.process(mono.data(), static_cast<int>(frames_per_buffer), rm);
        }
        for (unsigned long i = 0; i < frames_per_buffer; ++i) {
            const float s = mono[i];
            out_ch[i * 2] = out_ch[i * 2 + 1] = s;
        }
        mix_test_beep();
        store_out_peak();
        return paContinue;
    }

    std::vector<float> vbuf(frames_per_buffer);
    int produced =
        voc->process_block(mono.data(), static_cast<int>(frames_per_buffer), vbuf.data(), static_cast<int>(frames_per_buffer));
    const float rm = app->reverb_mix.load(std::memory_order_relaxed);
    if (rm > 1e-5f && produced > 0) {
        app->reverb.process(vbuf.data(), produced, rm);
    }

    for (unsigned long i = 0; i < static_cast<unsigned long>(produced); ++i) {
        float s = vbuf[i];
        out_ch[i * 2] = out_ch[i * 2 + 1] = s;
    }
    for (unsigned long i = static_cast<unsigned long>(produced); i < frames_per_buffer; ++i) {
        out_ch[i * 2] = out_ch[i * 2 + 1] = 0.f;
    }
    mix_test_beep();
    store_out_peak();
    return paContinue;
}

}  // namespace lv_gui
