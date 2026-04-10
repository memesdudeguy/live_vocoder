#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

#include <fftw3.h>

#include "bands.hpp"

/**
 * Band-envelope vocoder matching Python StreamingVocoder "bands" mode (simplified: no SOS high-pass).
 */
class StreamingVocoderCpp {
   public:
    StreamingVocoderCpp(std::vector<double> carrier, int sample_rate, int n_fft = 2048,
                        int hop_length = 0, double wet_level = 1.15, int n_bands = 36,
                        double band_smooth = 0.62, double mic_preemph = 0.88,
                        double mod_presence_db = 4.0, double mod_presence_hz = 1800.0,
                        double carrier_mix = 1.0);

    ~StreamingVocoderCpp();

    StreamingVocoderCpp(const StreamingVocoderCpp&) = delete;
    StreamingVocoderCpp& operator=(const StreamingVocoderCpp&) = delete;

    int sample_rate() const { return sr_; }
    int n_fft() const { return n_fft_; }
    int hop_length() const { return hop_len_; }

    /** Process mic mono block; returns number of output samples written (multiple of hop). */
    int process_block(const float* mic, int mic_len, float* out, int out_cap);

    /** Live-adjustable (thread-safe vs PortAudio callback). */
    void set_wet_level(double wet);
    void set_mod_presence_db(double db);

   private:
    void vocode_spectrum(const fftw_complex* M, const fftw_complex* C, fftw_complex* V);
    void modulator_presence(fftw_complex* M) const;

    int sr_;
    int n_fft_;
    int hop_len_;
    std::atomic<float> wet_level_;
    std::atomic<float> mod_presence_db_;
    double carrier_mix_;
    double band_smooth_;
    double mic_preemph_;
    double mod_presence_hz_;
    double eps_{1e-7};

    std::vector<double> carrier_;
    std::size_t carrier_pos_{0};

    std::vector<double> win_;
    std::vector<double> mic_buf_;
    std::vector<double> synth_buf_;
    std::vector<double> band_env_;
    std::vector<std::pair<int, int>> band_slices_;
    std::vector<double> rfft_freqs_;

    double pre_x_{0.0};

    int n_rfft_{0};
    std::vector<double> work_r_;
    fftw_complex* spec_mod_{nullptr};
    fftw_complex* spec_car_{nullptr};
    fftw_complex* spec_voc_{nullptr};
    fftw_plan plan_r2c_{nullptr};
    fftw_plan plan_car_{nullptr};
    fftw_plan plan_c2r_{nullptr};
};
