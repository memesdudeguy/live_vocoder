#include "vocoder.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace {
constexpr double kPi = 3.14159265358979323846;
}

static void hanning(std::vector<double>& w) {
    const int n = static_cast<int>(w.size());
    if (n <= 1) {
        return;
    }
    for (int i = 0; i < n; ++i) {
        w[static_cast<std::size_t>(i)] =
            0.5 * (1.0 - std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(n - 1)));
    }
}

static inline double cabs2(const fftw_complex& z) {
    return z[0] * z[0] + z[1] * z[1];
}

StreamingVocoderCpp::StreamingVocoderCpp(std::vector<double> carrier, int sample_rate, int n_fft,
                                         int hop_length, double wet_level, int n_bands,
                                         double band_smooth, double mic_preemph,
                                         double mod_presence_db, double mod_presence_hz,
                                         double carrier_mix)
    : sr_(sample_rate),
      n_fft_(n_fft),
      hop_len_(hop_length > 0 ? hop_length : n_fft / 4),
      carrier_mix_(carrier_mix),
      band_smooth_(std::clamp(band_smooth, 0.0, 0.999)),
      mic_preemph_(std::clamp(mic_preemph, 0.0, 0.99)),
      mod_presence_hz_(std::max(200.0, mod_presence_hz)),
      carrier_(std::move(carrier)) {
    wet_level_.store(static_cast<float>(std::clamp(wet_level, 0.05, 3.0)), std::memory_order_relaxed);
    mod_presence_db_.store(static_cast<float>(std::max(0.0, mod_presence_db)), std::memory_order_relaxed);
    if (n_fft_ < 4) {
        throw std::invalid_argument("n_fft too small");
    }
    if (n_fft_ % hop_len_ != 0) {
        throw std::invalid_argument("n_fft must be divisible by hop_length");
    }
    if (carrier_.empty()) {
        throw std::invalid_argument("carrier is empty");
    }
    // Short clips (or bad converts) must cover at least one FFT window; tile by repeating.
    if (static_cast<int>(carrier_.size()) < n_fft_) {
        const std::size_t base = carrier_.size();
        std::vector<double> tiled;
        tiled.reserve(static_cast<std::size_t>(n_fft_));
        while (tiled.size() < static_cast<std::size_t>(n_fft_)) {
            for (std::size_t i = 0; i < base && tiled.size() < static_cast<std::size_t>(n_fft_); ++i) {
                tiled.push_back(carrier_[i]);
            }
        }
        carrier_ = std::move(tiled);
    }

    win_.resize(static_cast<std::size_t>(n_fft_));
    hanning(win_);
    mic_buf_.assign(static_cast<std::size_t>(n_fft_), 0.0);
    synth_buf_.assign(static_cast<std::size_t>(n_fft_), 0.0);
    band_slices_ = log_band_slices(n_fft_, n_bands);
    band_env_.assign(band_slices_.size(), 0.0);

    n_rfft_ = n_fft_ / 2 + 1;
    rfft_freqs_.resize(static_cast<std::size_t>(n_rfft_));
    for (int k = 0; k < n_rfft_; ++k) {
        rfft_freqs_[static_cast<std::size_t>(k)] = static_cast<double>(k) * static_cast<double>(sr_) /
                                                   static_cast<double>(n_fft_);
    }

    work_r_.resize(static_cast<std::size_t>(n_fft_));
    spec_mod_ = static_cast<fftw_complex*>(fftw_malloc(sizeof(fftw_complex) * static_cast<std::size_t>(n_rfft_)));
    spec_car_ = static_cast<fftw_complex*>(fftw_malloc(sizeof(fftw_complex) * static_cast<std::size_t>(n_rfft_)));
    spec_voc_ = static_cast<fftw_complex*>(fftw_malloc(sizeof(fftw_complex) * static_cast<std::size_t>(n_rfft_)));
    if (!spec_mod_ || !spec_car_ || !spec_voc_) {
        throw std::runtime_error("fftw_malloc failed");
    }

    const unsigned flags = FFTW_ESTIMATE;
    plan_r2c_ = fftw_plan_dft_r2c_1d(n_fft_, work_r_.data(), spec_mod_, flags);
    plan_car_ = fftw_plan_dft_r2c_1d(n_fft_, work_r_.data(), spec_car_, flags);
    plan_c2r_ = fftw_plan_dft_c2r_1d(n_fft_, spec_voc_, work_r_.data(), flags);
    if (!plan_r2c_ || !plan_car_ || !plan_c2r_) {
        throw std::runtime_error("fftw_plan failed");
    }
}

StreamingVocoderCpp::~StreamingVocoderCpp() {
    if (plan_r2c_) {
        fftw_destroy_plan(plan_r2c_);
    }
    if (plan_car_) {
        fftw_destroy_plan(plan_car_);
    }
    if (plan_c2r_) {
        fftw_destroy_plan(plan_c2r_);
    }
    fftw_free(spec_mod_);
    fftw_free(spec_car_);
    fftw_free(spec_voc_);
}

void StreamingVocoderCpp::set_wet_level(double wet) {
    wet_level_.store(static_cast<float>(std::clamp(wet, 0.05, 3.0)), std::memory_order_relaxed);
}

void StreamingVocoderCpp::set_mod_presence_db(double db) {
    mod_presence_db_.store(static_cast<float>(std::max(0.0, db)), std::memory_order_relaxed);
}

void StreamingVocoderCpp::modulator_presence(fftw_complex* M) const {
    const float pdb = mod_presence_db_.load(std::memory_order_relaxed);
    if (pdb < 0.05f) {
        return;
    }
    const double nyq = 0.5 * static_cast<double>(sr_);
    const double f0 = mod_presence_hz_;
    const double gfac = std::pow(10.0, static_cast<double>(pdb) / 20.0) - 1.0;
    for (int k = 0; k < n_rfft_; ++k) {
        const double fk = rfft_freqs_[static_cast<std::size_t>(k)];
        double t = (fk - f0) / std::max(nyq - f0, 1.0);
        t = std::clamp(t, 0.0, 1.0);
        const double g = 1.0 + t * gfac;
        const double re = M[k][0];
        const double im = M[k][1];
        const double mag = std::sqrt(re * re + im * im) * g;
        const double ph = std::atan2(im, re);
        M[k][0] = mag * std::cos(ph);
        M[k][1] = mag * std::sin(ph);
    }
}

void StreamingVocoderCpp::vocode_spectrum(const fftw_complex* M_in, const fftw_complex* C,
                                          fftw_complex* V) {
    std::memcpy(V, M_in, sizeof(fftw_complex) * static_cast<std::size_t>(n_rfft_));
    modulator_presence(V);

    V[0][0] = 0.0;
    V[0][1] = 0.0;

    for (std::size_t bi = 0; bi < band_slices_.size(); ++bi) {
        const int a = band_slices_[bi].first;
        const int b = band_slices_[bi].second;
        double sum_m = 0.0;
        double sum_c = 0.0;
        int count = 0;
        for (int j = a; j < b; ++j) {
            sum_m += cabs2(V[j]);
            sum_c += cabs2(C[j]);
            ++count;
        }
        if (count <= 0) {
            continue;
        }
        double env = std::sqrt(sum_m / static_cast<double>(count));
        double car_rms = std::sqrt(sum_c / static_cast<double>(count));
        car_rms = std::max(car_rms, eps_);
        env = std::max(env, eps_);
        band_env_[bi] = band_smooth_ * band_env_[bi] + (1.0 - band_smooth_) * env;
        double scale = (band_env_[bi] / car_rms) * carrier_mix_;
        scale = std::min(scale, 80.0);
        for (int j = a; j < b; ++j) {
            V[j][0] = scale * C[j][0];
            V[j][1] = scale * C[j][1];
        }
    }
}

int StreamingVocoderCpp::process_block(const float* mic, int mic_len, float* out, int out_cap) {
    std::vector<double> micd(static_cast<std::size_t>(mic_len));
    for (int i = 0; i < mic_len; ++i) {
        micd[static_cast<std::size_t>(i)] = static_cast<double>(mic[static_cast<std::size_t>(i)]);
    }
    if (mic_preemph_ > 1e-6) {
        std::vector<double> yin = micd;
        for (int i = 0; i < mic_len; ++i) {
            const double prev = (i == 0) ? pre_x_ : yin[static_cast<std::size_t>(i - 1)];
            micd[static_cast<std::size_t>(i)] = yin[static_cast<std::size_t>(i)] - mic_preemph_ * prev;
        }
        pre_x_ = yin.back();
    }

    int o = 0;
    int i = 0;
    std::vector<double> mod_frame(static_cast<std::size_t>(n_fft_));
    std::vector<double> car_frame(static_cast<std::size_t>(n_fft_));
    std::vector<double> y_frame(static_cast<std::size_t>(n_fft_));

    while (i + hop_len_ <= mic_len) {
        if (o + hop_len_ > out_cap) {
            break;
        }
        for (int k = 0; k < n_fft_ - hop_len_; ++k) {
            mic_buf_[static_cast<std::size_t>(k)] = mic_buf_[static_cast<std::size_t>(k + hop_len_)];
        }
        for (int k = 0; k < hop_len_; ++k) {
            mic_buf_[static_cast<std::size_t>(n_fft_ - hop_len_ + k)] =
                micd[static_cast<std::size_t>(i + k)];
        }

        for (int k = 0; k < n_fft_; ++k) {
            mod_frame[static_cast<std::size_t>(k)] = mic_buf_[static_cast<std::size_t>(k)] *
                                                     win_[static_cast<std::size_t>(k)];
        }

        const int L = static_cast<int>(carrier_.size());
        int end = static_cast<int>(carrier_pos_) + n_fft_;
        if (end <= L) {
            for (int k = 0; k < n_fft_; ++k) {
                car_frame[static_cast<std::size_t>(k)] =
                    carrier_[carrier_pos_ + static_cast<std::size_t>(k)] *
                    win_[static_cast<std::size_t>(k)];
            }
        } else {
            for (int k = 0; k < n_fft_; ++k) {
                const std::size_t idx = (carrier_pos_ + static_cast<std::size_t>(k)) % carrier_.size();
                car_frame[static_cast<std::size_t>(k)] =
                    carrier_[idx] * win_[static_cast<std::size_t>(k)];
            }
        }
        carrier_pos_ = (carrier_pos_ + static_cast<std::size_t>(hop_len_)) % carrier_.size();

        std::memcpy(work_r_.data(), mod_frame.data(), sizeof(double) * static_cast<std::size_t>(n_fft_));
        fftw_execute(plan_r2c_);
        std::memcpy(work_r_.data(), car_frame.data(), sizeof(double) * static_cast<std::size_t>(n_fft_));
        fftw_execute(plan_car_);
        vocode_spectrum(spec_mod_, spec_car_, spec_voc_);
        fftw_execute(plan_c2r_);
        const double inv_n = 1.0 / static_cast<double>(n_fft_);
        for (int k = 0; k < n_fft_; ++k) {
            y_frame[static_cast<std::size_t>(k)] =
                work_r_[static_cast<std::size_t>(k)] * inv_n * win_[static_cast<std::size_t>(k)];
        }

        for (int k = 0; k < n_fft_; ++k) {
            synth_buf_[static_cast<std::size_t>(k)] += y_frame[static_cast<std::size_t>(k)];
        }
        const float wl = wet_level_.load(std::memory_order_relaxed);
        for (int k = 0; k < hop_len_; ++k) {
            double v = synth_buf_[static_cast<std::size_t>(k)] * static_cast<double>(wl);
            out[o + k] = static_cast<float>(std::clamp(v, -1.0, 1.0));
        }
        for (int k = 0; k < n_fft_ - hop_len_; ++k) {
            synth_buf_[static_cast<std::size_t>(k)] = synth_buf_[static_cast<std::size_t>(k + hop_len_)];
        }
        for (int k = n_fft_ - hop_len_; k < n_fft_; ++k) {
            synth_buf_[static_cast<std::size_t>(k)] = 0.0;
        }
        o += hop_len_;
        i += hop_len_;
    }
    return o;
}
