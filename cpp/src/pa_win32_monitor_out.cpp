#include "pa_win32_monitor_out.hpp"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <pa_win_wasapi.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>

namespace {

static bool mon_env_truthy(const char* v) {
    if (v == nullptr || v[0] == '\0' || (v[0] == '0' && v[1] == '\0')) {
        return false;
    }
    if (std::strcmp(v, "false") == 0 || std::strcmp(v, "no") == 0) {
        return false;
    }
    return true;
}

/** Match pa_duplex: WASAPI can advertise multi‑second defaultLowOutputLatency. */
static void mon_cap_monitor_suggested_latency(PaStreamParameters& outp, double sample_rate, unsigned long hop) {
    if (mon_env_truthy(std::getenv("LIVE_VOCODER_PA_DISABLE_SUGGESTED_CAP"))) {
        return;
    }
    const double hop_sec = static_cast<double>(hop) / sample_rate;
    double cap_sec = 0.048;
    if (const char* e = std::getenv("LIVE_VOCODER_PA_MAX_SUGGESTED_LATENCY_SEC"); e != nullptr && e[0] != '\0') {
        char* end = nullptr;
        const double v = std::strtod(e, &end);
        if (end != e && v >= 0.005 && v <= 0.5) {
            cap_sec = v;
        }
    }
    if (cap_sec < hop_sec * 2.0) {
        cap_sec = hop_sec * 2.0;
    }
    const double was = outp.suggestedLatency;
    outp.suggestedLatency = std::clamp(was, hop_sec, cap_sec);
    if (was > cap_sec + 1e-4) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            std::fprintf(stderr,
                         "[LiveVocoder] Monitor speaker stream: capping suggested latency to ~%.0f ms (device reported "
                         "%.3f s).\n",
                         cap_sec * 1000.0, was);
        }
    }
}

static bool mon_ci_hay_contains(const char* hay, const char* needle) {
    if (needle == nullptr || needle[0] == '\0') {
        return true;
    }
    if (hay == nullptr || hay[0] == '\0') {
        return false;
    }
    const std::size_t nlen = std::strlen(needle);
    for (const char* p = hay; *p != '\0'; ++p) {
        std::size_t j = 0;
        for (; j < nlen; ++j) {
            const unsigned char c1 = static_cast<unsigned char>(p[j]);
            const unsigned char c2 = static_cast<unsigned char>(needle[j]);
            if (c1 == '\0') {
                return false;
            }
            if (std::tolower(c1) != std::tolower(c2)) {
                break;
            }
        }
        if (j == nlen) {
            return true;
        }
    }
    return false;
}

static bool name_is_win_virt_playback(const char* nm) {
    if (nm == nullptr || nm[0] == '\0') {
        return false;
    }
    if (mon_ci_hay_contains(nm, "vb-audio") || mon_ci_hay_contains(nm, "vb audio") ||
        mon_ci_hay_contains(nm, "vbaudio")) {
        return true;
    }
    if (mon_ci_hay_contains(nm, "voicemeeter")) {
        return true;
    }
    if (mon_ci_hay_contains(nm, "virtual cable")) {
        return true;
    }
    return false;
}

static int mon_host_api_score(PaHostApiIndex api_ix) {
    if (api_ix < 0) {
        return 0;
    }
    const PaHostApiInfo* a = Pa_GetHostApiInfo(api_ix);
    if (a == nullptr || a->name == nullptr) {
        return 0;
    }
    if (mon_ci_hay_contains(a->name, "wasapi")) {
        return 3;
    }
    if (mon_ci_hay_contains(a->name, "directsound")) {
        return 2;
    }
    if (mon_ci_hay_contains(a->name, "mme")) {
        return 1;
    }
    return 0;
}

static PaDeviceIndex pick_physical_speaker(PaDeviceIndex duplex_out) {
    const PaDeviceInfo* dup = Pa_GetDeviceInfo(duplex_out);
    const PaHostApiIndex pref_api = dup != nullptr ? dup->hostApi : static_cast<PaHostApiIndex>(-1);
    const char* sub = std::getenv("LIVE_VOCODER_WIN_MONITOR_DEVICE");
    const PaDeviceIndex def_out = Pa_GetDefaultOutputDevice();
    const int n = Pa_GetDeviceCount();
    PaDeviceIndex best = paNoDevice;
    int best_sc = -1;
    for (PaDeviceIndex i = 0; i < n; ++i) {
        const PaDeviceInfo* inf = Pa_GetDeviceInfo(i);
        if (inf == nullptr || inf->name == nullptr || inf->maxOutputChannels < 2) {
            continue;
        }
        if (name_is_win_virt_playback(inf->name)) {
            continue;
        }
        if (sub != nullptr && sub[0] != '\0' && !mon_ci_hay_contains(inf->name, sub)) {
            continue;
        }
        int sc = mon_host_api_score(inf->hostApi);
        if (pref_api >= 0 && inf->hostApi == pref_api) {
            sc += 20;
        }
        if (i == def_out) {
            sc += 5;
        }
        if (sc > best_sc) {
            best_sc = sc;
            best = i;
        }
    }
    return best;
}

static std::mutex g_q_mu;
static std::deque<float> g_q;
/* Cap queue so speaker monitor stays tight (~21 ms max @ 48 kHz stereo vs ~1 s before). */
static constexpr std::size_t k_max_samples = 2048u;
static std::atomic<bool> g_feed_armed{false};

static int monitor_callback(const void*, void* output, unsigned long frames, const PaStreamCallbackTimeInfo*,
                            PaStreamCallbackFlags, void*) {
    auto* o = static_cast<float*>(output);
    std::lock_guard<std::mutex> lock(g_q_mu);
    for (unsigned long i = 0; i < frames; ++i) {
        if (g_q.size() >= 2) {
            o[i * 2] = g_q.front();
            g_q.pop_front();
            o[i * 2 + 1] = g_q.front();
            g_q.pop_front();
        } else {
            o[i * 2] = o[i * 2 + 1] = 0.f;
        }
    }
    return paContinue;
}

}  // namespace

PaError pa_win32_monitor_output_start(PaStream** out_stream, double sample_rate, unsigned long hop,
                                      PaDeviceIndex duplex_output_device) {
    if (out_stream == nullptr) {
        return paInvalidFlag;
    }
    pa_win32_monitor_output_stop(*out_stream);
    *out_stream = nullptr;
    g_feed_armed.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(g_q_mu);
        g_q.clear();
    }

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll != nullptr && GetProcAddress(ntdll, "wine_get_version") != nullptr) {
        return paNoError;
    }

    const PaDeviceIndex sp = pick_physical_speaker(duplex_output_device);
    if (sp < 0) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            std::fprintf(stderr,
                         "[LiveVocoder] Monitor: no non-virtual stereo output device for speaker preview "
                         "(set LIVE_VOCODER_WIN_MONITOR_DEVICE=name substring).\n");
        }
        return paNoError;
    }
    const PaDeviceInfo* oi = Pa_GetDeviceInfo(sp);
    if (oi == nullptr) {
        return paInvalidDevice;
    }

    PaStreamParameters outp{};
    outp.device = sp;
    outp.channelCount = 2;
    outp.sampleFormat = paFloat32;
    const char* hi = std::getenv("LIVE_VOCODER_WIN_MONITOR_HIGH_LATENCY");
    const bool want_high = hi != nullptr && hi[0] != '\0' && hi[0] != '0' &&
                           std::strcmp(hi, "false") != 0 && std::strcmp(hi, "no") != 0;
    outp.suggestedLatency = want_high ? oi->defaultHighOutputLatency : oi->defaultLowOutputLatency;
    if (!want_high) {
        mon_cap_monitor_suggested_latency(outp, sample_rate, hop);
    }

    PaStream* s = nullptr;
    PaError e;
    const PaHostApiInfo* host_api = Pa_GetHostApiInfo(oi->hostApi);
    const bool try_wasapi = !want_high && host_api != nullptr && host_api->type == paWASAPI &&
                            !mon_env_truthy(std::getenv("LIVE_VOCODER_PA_DISABLE_WASAPI_STREAMINFO"));
    PaWasapiStreamInfo wo{};
    auto open_mon = [&]() {
        return Pa_OpenStream(&s, nullptr, &outp, sample_rate, hop, paClipOff, monitor_callback, nullptr);
    };
    if (try_wasapi) {
        wo.size = sizeof(PaWasapiStreamInfo);
        wo.hostApiType = paWASAPI;
        wo.version = 1;
        wo.flags = paWinWasapiThreadPriority;
        wo.threadPriority = eThreadPriorityProAudio;
        wo.streamCategory = eAudioCategoryMedia;
        wo.streamOption =
            mon_env_truthy(std::getenv("LIVE_VOCODER_PA_WASAPI_RAW")) ? eStreamOptionRaw : eStreamOptionNone;
        outp.hostApiSpecificStreamInfo = &wo;
        e = open_mon();
        if (e != paNoError && wo.streamOption == eStreamOptionRaw) {
            wo.streamOption = eStreamOptionNone;
            e = open_mon();
        }
        if (e != paNoError) {
            outp.hostApiSpecificStreamInfo = nullptr;
            e = open_mon();
        }
    } else {
        outp.hostApiSpecificStreamInfo = nullptr;
        e = open_mon();
    }
    if (e != paNoError) {
        return e;
    }
    e = Pa_StartStream(s);
    if (e != paNoError) {
        Pa_CloseStream(s);
        return e;
    }
    *out_stream = s;
    g_feed_armed.store(true, std::memory_order_release);
    static bool logged_ok = false;
    if (!logged_ok) {
        logged_ok = true;
        std::fprintf(stderr, "[LiveVocoder] Monitor: duplicating output to speakers \"%s\" (device %d).\n",
                     oi->name != nullptr ? oi->name : "?", static_cast<int>(sp));
    }
    return paNoError;
}

void pa_win32_monitor_output_stop(PaStream* stream) {
    g_feed_armed.store(false, std::memory_order_release);
    if (stream != nullptr) {
        Pa_StopStream(stream);
        Pa_CloseStream(stream);
    }
    std::lock_guard<std::mutex> lock(g_q_mu);
    g_q.clear();
}

void pa_win32_monitor_output_feed(bool duplex_targets_virt_route, bool monitor_on, const float* interleaved_stereo_lr,
                                  unsigned long frames) {
    if (!duplex_targets_virt_route || !monitor_on || !g_feed_armed.load(std::memory_order_acquire) ||
        interleaved_stereo_lr == nullptr || frames == 0) {
        return;
    }
    /* Blocking lock: try_lock drops whole duplex blocks → clicks; brief wait keeps audio continuous. */
    std::lock_guard<std::mutex> lock(g_q_mu);
    const std::size_t add = static_cast<std::size_t>(frames) * 2u;
    while (g_q.size() + add > k_max_samples && g_q.size() >= 2) {
        g_q.pop_front();
        g_q.pop_front();
    }
    for (unsigned long i = 0; i < frames; ++i) {
        g_q.push_back(interleaved_stereo_lr[i * 2]);
        g_q.push_back(interleaved_stereo_lr[i * 2 + 1]);
    }
}

#endif  // _WIN32
