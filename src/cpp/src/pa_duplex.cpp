#include "pa_duplex.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <pa_win_wasapi.h>

/** True when running under Wine (Linux/macOS host) — skip Windows-only virtual cable auto-pick. */
static bool lv_windows_is_wine_host() {
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (ntdll == nullptr) {
        return false;
    }
    return GetProcAddress(ntdll, "wine_get_version") != nullptr;
}
#endif

static bool lv_ci_hay_contains(const char* hay, const char* needle) {
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

#if defined(_WIN32)
/** Prefer WASAPI > DirectSound > MME (Wine Pulse + native VB-Cable / WASAPI stability). */
static int lv_win32_portaudio_host_api_score(PaHostApiIndex api_ix) {
    if (api_ix < 0) {
        return 0;
    }
    const PaHostApiInfo* a = Pa_GetHostApiInfo(api_ix);
    if (a == nullptr || a->name == nullptr) {
        return 0;
    }
    if (lv_ci_hay_contains(a->name, "wasapi")) {
        return 3;
    }
    if (lv_ci_hay_contains(a->name, "directsound")) {
        return 2;
    }
    if (lv_ci_hay_contains(a->name, "mme")) {
        return 1;
    }
    if (lv_ci_hay_contains(a->name, "wdm") || lv_ci_hay_contains(a->name, "kernel") ||
        lv_ci_hay_contains(a->name, "ks")) {
        return 2;
    }
    return 0;
}

/** PortAudio device name suggests VB-Audio / virtual cable (spacing and wording vary by host API). */
static bool lv_win32_pa_name_hints_vb_virtual_cable(const char* nm) {
    if (nm == nullptr || nm[0] == '\0') {
        return false;
    }
    if (lv_ci_hay_contains(nm, "vb-audio")) {
        return true;
    }
    if (lv_ci_hay_contains(nm, "vb audio")) {
        return true;
    }
    if (lv_ci_hay_contains(nm, "vbaudio")) {
        return true;
    }
    if (lv_ci_hay_contains(nm, "virtual cable")) {
        return true;
    }
    return false;
}

/**
 * Wine exposes host Pulse as MME/DirectSound/WASAPI devices containing "PulseAudio" in the name.
 * PULSE_SINK is honored by Wine's pulse driver for those, not for "Wine Sound Mapper".
 */
static PaDeviceIndex lv_wine_pick_pulseaudio_duplex_device(bool want_output) {
    if (!lv_windows_is_wine_host()) {
        return paNoDevice;
    }
    const PaDeviceIndex n = static_cast<PaDeviceIndex>(Pa_GetDeviceCount());
    if (n <= 0) {
        return paNoDevice;
    }
    PaDeviceIndex best = paNoDevice;
    int best_score = -1;
    for (PaDeviceIndex i = 0; i < n; ++i) {
        const PaDeviceInfo* inf = Pa_GetDeviceInfo(i);
        if (inf == nullptr || inf->name == nullptr) {
            continue;
        }
        if (want_output) {
            if (inf->maxOutputChannels < 2) {
                continue;
            }
        } else if (inf->maxInputChannels < 1) {
            continue;
        }
        if (!lv_ci_hay_contains(inf->name, "pulseaudio")) {
            continue;
        }
        const int sc = lv_win32_portaudio_host_api_score(inf->hostApi);
        if (sc > best_score) {
            best_score = sc;
            best = i;
        }
    }
    return best;
}

/**
 * Native Windows: VB-Audio Virtual Cable playback (CABLE Input). Prefer WASAPI when duplicated.
 * If only_host_api >= 0, only consider devices on that API (duplex streams cannot mix MME + WASAPI, etc.).
 */
static PaDeviceIndex lv_native_windows_pick_vb_cable_output_device_filtered(PaDeviceIndex n_dev,
                                                                            PaHostApiIndex only_host_api) {
    PaDeviceIndex best_named = paNoDevice;
    int best_named_score = -1;
    PaDeviceIndex any_vb = paNoDevice;
    int any_vb_score = -1;
    for (PaDeviceIndex i = 0; i < n_dev; ++i) {
        const PaDeviceInfo* inf = Pa_GetDeviceInfo(i);
        if (inf == nullptr || inf->name == nullptr || inf->maxOutputChannels < 2) {
            continue;
        }
        if (only_host_api >= 0 && inf->hostApi != only_host_api) {
            continue;
        }
        const char* nm = inf->name;
        if (!lv_win32_pa_name_hints_vb_virtual_cable(nm)) {
            continue;
        }
        const int sc = lv_win32_portaudio_host_api_score(inf->hostApi);
        const bool named_cable_in = (lv_ci_hay_contains(nm, "cable") && lv_ci_hay_contains(nm, "input")) ||
                                    lv_ci_hay_contains(nm, "cable input");
        if (named_cable_in) {
            if (sc > best_named_score) {
                best_named_score = sc;
                best_named = i;
            }
        } else {
            if (sc > any_vb_score) {
                any_vb_score = sc;
                any_vb = i;
            }
        }
    }
    return (best_named >= 0) ? best_named : any_vb;
}

static PaDeviceIndex lv_native_windows_pick_vb_cable_output_device(PaDeviceIndex n_dev) {
    return lv_native_windows_pick_vb_cable_output_device_filtered(n_dev, static_cast<PaHostApiIndex>(-1));
}

/** Microphone on a specific host API (for duplex alignment). Skips VB-Audio *capture* of cable output. */
static PaDeviceIndex lv_native_windows_pick_input_on_host(PaDeviceIndex n_dev, PaHostApiIndex api_ix) {
    if (api_ix < 0) {
        return paNoDevice;
    }
    const PaHostApiInfo* pa = Pa_GetHostApiInfo(api_ix);
    if (pa != nullptr && pa->defaultInputDevice >= 0) {
        const PaDeviceInfo* di = Pa_GetDeviceInfo(pa->defaultInputDevice);
        if (di != nullptr && di->hostApi == api_ix && di->maxInputChannels >= 1 && di->name != nullptr) {
            const bool vb_cap = lv_win32_pa_name_hints_vb_virtual_cable(di->name) && lv_ci_hay_contains(di->name, "output");
            if (!vb_cap) {
                return pa->defaultInputDevice;
            }
        }
    }
    for (PaDeviceIndex i = 0; i < n_dev; ++i) {
        const PaDeviceInfo* inf = Pa_GetDeviceInfo(i);
        if (inf == nullptr || inf->name == nullptr || inf->hostApi != api_ix || inf->maxInputChannels < 1) {
            continue;
        }
        if (lv_win32_pa_name_hints_vb_virtual_cable(inf->name) && lv_ci_hay_contains(inf->name, "output")) {
            continue;
        }
        return i;
    }
    return paNoDevice;
}
#endif

static PaDeviceIndex g_duplex_out_dev = paNoDevice;

static bool pa_output_name_is_virt_sink_discord_route(const char* name) {
    if (name == nullptr || name[0] == '\0') {
        return false;
    }
    if (lv_ci_hay_contains(name, "live_vocoder") || lv_ci_hay_contains(name, "livevocoder")) {
        return true;
    }
    if (lv_ci_hay_contains(name, "null") && lv_ci_hay_contains(name, "output")) {
        return true;
    }
    return false;
}

#if defined(_WIN32)
/** WASAPI/MME playback to common virtual cable drivers — treat like Linux null sink for Monitor routing. */
static bool pa_output_name_is_windows_virtual_audio_route(const char* name) {
    if (name == nullptr || name[0] == '\0') {
        return false;
    }
    if (lv_win32_pa_name_hints_vb_virtual_cable(name)) {
        return true;
    }
    if (lv_ci_hay_contains(name, "voicemeeter")) {
        return true;
    }
    if (lv_ci_hay_contains(name, "virtual cable")) {
        return true;
    }
    return false;
}
#endif

static bool lv_env_pulse_sink_targets_virt_route() {
    const char* ps = std::getenv("PULSE_SINK");
    if (ps != nullptr && ps[0] != '\0') {
        return true;
    }
    const char* lv = std::getenv("LIVE_VOCODER_PULSE_SINK");
    return lv != nullptr && lv[0] != '\0';
}

/** PortAudio+ALSA builds use pcm "pulse" / "pipewire"; libpulse honors PULSE_SINK (null sink). */
static bool pa_output_device_is_pulse_pcm_virt_route(const char* name) {
    if (name == nullptr || name[0] == '\0') {
        return false;
    }
    if (std::strcmp(name, "pulse") == 0 || std::strcmp(name, "pipewire") == 0) {
        return lv_env_pulse_sink_targets_virt_route();
    }
    return false;
}

#if defined(__linux__)
static bool lv_input_name_looks_virtual(const char* name) {
    if (name == nullptr || name[0] == '\0') {
        return false;
    }
    return lv_ci_hay_contains(name, "monitor") || lv_ci_hay_contains(name, "_mic") ||
           lv_ci_hay_contains(name, "virtual") || lv_ci_hay_contains(name, "remap") ||
           lv_ci_hay_contains(name, "live_vocoder") || lv_ci_hay_contains(name, "livevocoder");
}

static PaHostApiIndex lv_find_pulse_host_api() {
    const PaHostApiIndex n = Pa_GetHostApiCount();
    for (PaHostApiIndex i = 0; i < n; ++i) {
        const PaHostApiInfo* a = Pa_GetHostApiInfo(i);
        if (a != nullptr && a->name != nullptr && lv_ci_hay_contains(a->name, "pulse")) {
            return i;
        }
    }
    return -1;
}

static bool lv_env_wants_pulse_virt_route() {
    return lv_env_pulse_sink_targets_virt_route();
}

static PaDeviceIndex lv_alsa_io_exact_name(int n_dev, const char* exact, bool want_output, int min_out_ch) {
    for (PaDeviceIndex i = 0; i < static_cast<PaDeviceIndex>(n_dev); ++i) {
        const PaDeviceInfo* inf = Pa_GetDeviceInfo(i);
        if (inf == nullptr || inf->name == nullptr) {
            continue;
        }
        if (std::strcmp(inf->name, exact) != 0) {
            continue;
        }
        if (want_output) {
            if (inf->maxOutputChannels >= min_out_ch) {
                return i;
            }
        } else if (inf->maxInputChannels >= 1) {
            return i;
        }
    }
    return paNoDevice;
}
#endif

namespace {

bool env_truthy(const char* v) {
    if (v == nullptr || v[0] == '\0') {
        return false;
    }
    return v[0] == '1' || v[0] == 'y' || v[0] == 'Y' || v[0] == 't' || v[0] == 'T';
}

bool ci_hay_contains_needle(const char* hay, const char* needle) {
    return lv_ci_hay_contains(hay, needle);
}

int parse_device_index_env(const char* name) {
    const char* v = std::getenv(name);
    if (v == nullptr || v[0] == '\0') {
        return -1;
    }
    char* end = nullptr;
    const long x = std::strtol(v, &end, 10);
    if (end == v || x < 0) {
        return -1;
    }
    return static_cast<int>(x);
}

#if defined(_WIN32)
/** PortAudio rejects one duplex stream when input and output use different Windows host APIs (e.g. MME + WASAPI). */
static void lv_win32_align_duplex_devices_native(PaDeviceIndex& in_dev, PaDeviceIndex& out_dev) {
    if (lv_windows_is_wine_host()) {
        return;
    }
    const int n_raw = Pa_GetDeviceCount();
    if (n_raw <= 0 || in_dev < 0 || out_dev < 0) {
        return;
    }
    const PaDeviceIndex n_dev = static_cast<PaDeviceIndex>(n_raw);
    const PaDeviceInfo* ii = Pa_GetDeviceInfo(in_dev);
    const PaDeviceInfo* oi = Pa_GetDeviceInfo(out_dev);
    if (ii == nullptr || oi == nullptr || ii->hostApi == oi->hostApi) {
        return;
    }
    if (parse_device_index_env("LIVE_VOCODER_PA_INPUT_INDEX") >= 0 ||
        parse_device_index_env("LIVE_VOCODER_PA_OUTPUT_INDEX") >= 0) {
        return;
    }
    const char* in_sub = std::getenv("LIVE_VOCODER_PA_INPUT");
    const char* out_sub = std::getenv("LIVE_VOCODER_PA_OUTPUT");
    if ((in_sub != nullptr && in_sub[0] != '\0') || (out_sub != nullptr && out_sub[0] != '\0')) {
        return;
    }

    const PaHostApiIndex in_api = ii->hostApi;
    const PaHostApiIndex out_api = oi->hostApi;
    const PaDeviceIndex alt_out = lv_native_windows_pick_vb_cable_output_device_filtered(n_dev, in_api);
    if (alt_out >= 0) {
        static bool logged_match_out = false;
        if (!logged_match_out) {
            logged_match_out = true;
            const PaHostApiInfo* ai = Pa_GetHostApiInfo(in_api);
            std::fprintf(stderr,
                         "[LiveVocoder] Native Windows: microphone and VB-Cable used different PortAudio host APIs "
                         "(illegal duplex). Using CABLE Input on the same API as the mic [%s].\n",
                         ai != nullptr && ai->name != nullptr ? ai->name : "?");
        }
        out_dev = alt_out;
        return;
    }
    const PaDeviceIndex alt_in = lv_native_windows_pick_input_on_host(n_dev, out_api);
    if (alt_in >= 0) {
        static bool logged_match_in = false;
        if (!logged_match_in) {
            logged_match_in = true;
            const PaHostApiInfo* ao = Pa_GetHostApiInfo(out_api);
            std::fprintf(stderr,
                         "[LiveVocoder] Native Windows: microphone and VB-Cable used different PortAudio host APIs "
                         "(illegal duplex). Using a microphone on the same API as CABLE Input [%s].\n",
                         ao != nullptr && ao->name != nullptr ? ao->name : "?");
        }
        in_dev = alt_in;
        return;
    }
    std::fprintf(stderr,
                 "[LiveVocoder] Native Windows: input/output are on different PortAudio host APIs and no aligned pair "
                 "was found — open may fail with \"Illegal combination\". Install VB-Cable for every listed API or set "
                 "LIVE_VOCODER_PA_INPUT / LIVE_VOCODER_PA_OUTPUT to names from the same API "
                 "(LIVE_VOCODER_PA_LIST_DEVICES=1).\n");
}
#endif

PaDeviceIndex pick_input_device() {
    const int n_dev = Pa_GetDeviceCount();
    const int by_ix = parse_device_index_env("LIVE_VOCODER_PA_INPUT_INDEX");
    if (by_ix >= 0 && n_dev > 0 && by_ix < n_dev) {
        const PaDeviceInfo* inf = Pa_GetDeviceInfo(by_ix);
        if (inf != nullptr && inf->maxInputChannels >= 1) {
            return static_cast<PaDeviceIndex>(by_ix);
        }
    }
    const char* sub = std::getenv("LIVE_VOCODER_PA_INPUT");
    if (sub != nullptr && sub[0] != '\0' && n_dev > 0) {
        for (PaDeviceIndex i = 0; i < static_cast<PaDeviceIndex>(n_dev); ++i) {
            const PaDeviceInfo* inf = Pa_GetDeviceInfo(i);
            if (inf == nullptr || inf->maxInputChannels < 1) {
                continue;
            }
            if (ci_hay_contains_needle(inf->name, sub)) {
                return i;
            }
        }
    }
#if defined(__linux__)
    // PULSE_SINK only affects libpulse. Duplex must use Pulse devices, not ALSA "default",
    // or audio never reaches the null sink / virtual mic chain.
    if (lv_env_wants_pulse_virt_route() && n_dev > 0) {
        const PaHostApiIndex pulse = lv_find_pulse_host_api();
        if (pulse >= 0) {
            for (PaDeviceIndex i = 0; i < static_cast<PaDeviceIndex>(n_dev); ++i) {
                const PaDeviceInfo* inf = Pa_GetDeviceInfo(i);
                if (inf == nullptr || inf->hostApi != pulse || inf->maxInputChannels < 1) {
                    continue;
                }
                if (lv_input_name_looks_virtual(inf->name)) {
                    continue;
                }
                return i;
            }
            const PaHostApiInfo* pa = Pa_GetHostApiInfo(pulse);
            if (pa != nullptr && pa->defaultInputDevice >= 0) {
                const PaDeviceInfo* di = Pa_GetDeviceInfo(pa->defaultInputDevice);
                if (di != nullptr && di->maxInputChannels >= 1 && !lv_input_name_looks_virtual(di->name)) {
                    return pa->defaultInputDevice;
                }
            }
        }
        PaDeviceIndex ap = lv_alsa_io_exact_name(n_dev, "pulse", false, 0);
        if (ap >= 0) {
            return ap;
        }
        ap = lv_alsa_io_exact_name(n_dev, "pipewire", false, 0);
        if (ap >= 0) {
            return ap;
        }
    }
    const char* ps = std::getenv("PULSE_SINK");
    if (ps != nullptr && ps[0] != '\0' && n_dev > 0) {
        const PaDeviceIndex def_in = Pa_GetDefaultInputDevice();
        if (def_in >= 0) {
            const PaDeviceInfo* dinf = Pa_GetDeviceInfo(def_in);
            if (dinf != nullptr && lv_input_name_looks_virtual(dinf->name)) {
                for (PaDeviceIndex i = 0; i < static_cast<PaDeviceIndex>(n_dev); ++i) {
                    const PaDeviceInfo* inf = Pa_GetDeviceInfo(i);
                    if (inf == nullptr || inf->maxInputChannels < 1) {
                        continue;
                    }
                    if (lv_input_name_looks_virtual(inf->name)) {
                        continue;
                    }
                    return i;
                }
            }
        }
    }
#endif
#if defined(_WIN32)
    if (lv_windows_is_wine_host() && lv_env_pulse_sink_targets_virt_route() && n_dev > 0) {
        const PaDeviceIndex w = lv_wine_pick_pulseaudio_duplex_device(false);
        if (w >= 0) {
            return w;
        }
    }
    if (!lv_windows_is_wine_host() && !env_truthy(std::getenv("LIVE_VOCODER_DISABLE_VB_CABLE")) && n_dev > 0) {
        const PaDeviceIndex def_in = Pa_GetDefaultInputDevice();
        if (def_in >= 0) {
            const PaDeviceInfo* dinf = Pa_GetDeviceInfo(def_in);
            if (dinf != nullptr && dinf->name != nullptr && lv_ci_hay_contains(dinf->name, "vb-audio")) {
                // CABLE Output is the capture side; use a real mic for the vocoder input when possible.
                if (lv_ci_hay_contains(dinf->name, "output")) {
                    for (PaDeviceIndex i = 0; i < static_cast<PaDeviceIndex>(n_dev); ++i) {
                        const PaDeviceInfo* inf = Pa_GetDeviceInfo(i);
                        if (inf == nullptr || inf->name == nullptr || inf->maxInputChannels < 1) {
                            continue;
                        }
                        if (lv_ci_hay_contains(inf->name, "vb-audio")) {
                            continue;
                        }
                        static bool logged_win_in = false;
                        if (!logged_win_in) {
                            logged_win_in = true;
                            std::fprintf(stderr,
                                         "[LiveVocoder] Native Windows: default input was VB-Audio capture (\"%s\"). "
                                         "Using \"%s\" (%d) as microphone — playback still goes to CABLE Input when "
                                         "auto-routed.\n",
                                         dinf->name, inf->name, static_cast<int>(i));
                        }
                        return i;
                    }
                }
            }
        }
    }
#endif
    return Pa_GetDefaultInputDevice();
}

PaDeviceIndex pick_output_device() {
    const int n_dev = Pa_GetDeviceCount();
    const int by_ix = parse_device_index_env("LIVE_VOCODER_PA_OUTPUT_INDEX");
    if (by_ix >= 0 && n_dev > 0 && by_ix < n_dev) {
        const PaDeviceInfo* inf = Pa_GetDeviceInfo(by_ix);
        if (inf != nullptr && inf->maxOutputChannels >= 2) {
            return static_cast<PaDeviceIndex>(by_ix);
        }
    }
    const char* sub = std::getenv("LIVE_VOCODER_PA_OUTPUT");
    if (sub != nullptr && sub[0] != '\0' && n_dev > 0) {
        for (PaDeviceIndex i = 0; i < static_cast<PaDeviceIndex>(n_dev); ++i) {
            const PaDeviceInfo* inf = Pa_GetDeviceInfo(i);
            if (inf == nullptr || inf->maxOutputChannels < 2) {
                continue;
            }
            if (ci_hay_contains_needle(inf->name, sub)) {
                return i;
            }
        }
    }
#if defined(_WIN32)
    // Before matching PULSE_SINK as a PortAudio device substring: names like "live_vocoder" match
    // unrelated MME devices and skip the real "Speakers (PulseAudio Output)" path.
    if (lv_windows_is_wine_host() && lv_env_pulse_sink_targets_virt_route() && n_dev > 0) {
        const PaDeviceIndex w = lv_wine_pick_pulseaudio_duplex_device(true);
        if (w >= 0) {
            static bool logged_wine_pa_out = false;
            if (!logged_wine_pa_out) {
                logged_wine_pa_out = true;
                const PaDeviceInfo* inf = Pa_GetDeviceInfo(w);
                std::fprintf(stderr,
                             "[LiveVocoder] Wine: PULSE_SINK set — using \"%s\" (device %d) for playback "
                             "(host PulseAudio honors PULSE_SINK → null sink / *_mic).\n",
                             inf != nullptr && inf->name != nullptr ? inf->name : "?",
                             static_cast<int>(w));
            }
            return w;
        }
    }
#endif
    const char* pulse_sub = std::getenv("LIVE_VOCODER_PULSE_SINK");
    if (pulse_sub == nullptr || pulse_sub[0] == '\0') {
        pulse_sub = std::getenv("PULSE_SINK");
    }
    if (pulse_sub != nullptr && pulse_sub[0] != '\0' && n_dev > 0) {
        for (PaDeviceIndex i = 0; i < static_cast<PaDeviceIndex>(n_dev); ++i) {
            const PaDeviceInfo* inf = Pa_GetDeviceInfo(i);
            if (inf == nullptr || inf->maxOutputChannels < 2) {
                continue;
            }
            if (ci_hay_contains_needle(inf->name, pulse_sub)) {
                return i;
            }
        }
#if defined(__linux__)
        const PaHostApiIndex pulse = lv_find_pulse_host_api();
        if (pulse >= 0) {
            for (PaDeviceIndex i = 0; i < static_cast<PaDeviceIndex>(n_dev); ++i) {
                const PaDeviceInfo* inf = Pa_GetDeviceInfo(i);
                if (inf == nullptr || inf->hostApi != pulse || inf->maxOutputChannels < 2) {
                    continue;
                }
                if (ci_hay_contains_needle(inf->name, pulse_sub)) {
                    return i;
                }
            }
            const PaHostApiInfo* pa = Pa_GetHostApiInfo(pulse);
            if (pa != nullptr && pa->defaultOutputDevice >= 0) {
                const PaDeviceInfo* inf = Pa_GetDeviceInfo(pa->defaultOutputDevice);
                if (inf != nullptr && inf->maxOutputChannels >= 2) {
                    static bool logged_pulse_default = false;
                    if (!logged_pulse_default) {
                        logged_pulse_default = true;
                        std::fprintf(stderr,
                                     "[LiveVocoder] PULSE_SINK set: using PulseAudio default output (device %d) so "
                                     "playback goes through PipeWire/Pulse (null sink). Override: "
                                     "LIVE_VOCODER_PA_OUTPUT / _INDEX.\n",
                                     static_cast<int>(pa->defaultOutputDevice));
                    }
                    return pa->defaultOutputDevice;
                }
            }
        }
        PaDeviceIndex ap = lv_alsa_io_exact_name(n_dev, "pulse", true, 2);
        if (ap >= 0) {
            static bool logged_alsa_pulse = false;
            if (!logged_alsa_pulse) {
                logged_alsa_pulse = true;
                std::fprintf(stderr,
                             "[LiveVocoder] PULSE_SINK set: using ALSA device \"pulse\" (libpulse applies PULSE_SINK → "
                             "null sink for virtual mic).\n");
            }
            return ap;
        }
        ap = lv_alsa_io_exact_name(n_dev, "pipewire", true, 2);
        if (ap >= 0) {
            static bool logged_alsa_pw = false;
            if (!logged_alsa_pw) {
                logged_alsa_pw = true;
                std::fprintf(stderr,
                             "[LiveVocoder] PULSE_SINK set: using ALSA device \"pipewire\" for PipeWire routing.\n");
            }
            return ap;
        }
#endif
    }
#if defined(_WIN32)
    if (!env_truthy(std::getenv("LIVE_VOCODER_DISABLE_VB_CABLE")) && !lv_windows_is_wine_host()) {
        const char* pa_out = std::getenv("LIVE_VOCODER_PA_OUTPUT");
        if ((pa_out == nullptr || pa_out[0] == '\0') && parse_device_index_env("LIVE_VOCODER_PA_OUTPUT_INDEX") < 0) {
            const PaDeviceIndex cab =
                lv_native_windows_pick_vb_cable_output_device(static_cast<PaDeviceIndex>(n_dev));
            if (cab >= 0) {
                static bool logged_vb = false;
                if (!logged_vb) {
                    logged_vb = true;
                    const PaDeviceInfo* inf = Pa_GetDeviceInfo(cab);
                    std::fprintf(stderr,
                                 "[LiveVocoder] Native Windows: playback → VB-Audio cable (CABLE Input) \"%s\" "
                                 "(device %d). In Discord/OBS set mic to CABLE Output. Overrides: "
                                 "LIVE_VOCODER_PA_OUTPUT / _INDEX; LIVE_VOCODER_DISABLE_VB_CABLE=1 for speakers.\n",
                                 inf != nullptr && inf->name != nullptr ? inf->name : "?",
                                 static_cast<int>(cab));
                }
                return cab;
            }
            static bool logged_no_vb = false;
            if (!logged_no_vb) {
                logged_no_vb = true;
                std::fprintf(stderr,
                             "[LiveVocoder] Native Windows: no VB-Virtual-Cable playback device in PortAudio — "
                             "install VB-Audio Virtual Cable or set LIVE_VOCODER_PA_OUTPUT. "
                             "LIVE_VOCODER_PA_LIST_DEVICES=1 lists devices.\n");
            }
        }
    }
#endif
#if defined(_WIN32)
    if (lv_windows_is_wine_host()) {
        static bool logged_wine_pulse = false;
        if (!logged_wine_pulse) {
            logged_wine_pulse = true;
            if (lv_env_pulse_sink_targets_virt_route()) {
                std::fprintf(stderr,
                             "[LiveVocoder] Wine: PULSE_SINK is set but no \"Speakers (PulseAudio Output)\" device was "
                             "found; playback may not reach your null sink. LIVE_VOCODER_PA_LIST_DEVICES=1 lists "
                             "devices; try LIVE_VOCODER_PA_OUTPUT=PulseAudio.\n");
            } else {
                std::fprintf(stderr,
                             "[LiveVocoder] Wine on Linux: using default output (Wine → host Pulse/PipeWire). "
                             "For null-sink capture, set PULSE_SINK / LIVE_VOCODER_PULSE_SINK (e.g. live_vocoder) "
                             "before starting Wine, or rely on installer auto-setup. "
                             "LIVE_VOCODER_PA_LIST_DEVICES=1 lists Wine device names.\n");
            }
        }
    }
#endif
    return Pa_GetDefaultOutputDevice();
}

}  // namespace

void pa_log_all_devices_if_requested(FILE* out) {
    if (!env_truthy(std::getenv("LIVE_VOCODER_PA_LIST_DEVICES"))) {
        return;
    }
    const PaHostApiIndex n_apis = Pa_GetHostApiCount();
    std::fprintf(out, "[LiveVocoder] PortAudio devices (set LIVE_VOCODER_PA_INPUT / _OUTPUT substring or _INDEX):\n");
    const int n_raw = Pa_GetDeviceCount();
    if (n_raw <= 0) {
        std::fprintf(out, "  (no devices)\n");
        return;
    }
    const PaDeviceIndex n = static_cast<PaDeviceIndex>(n_raw);
    for (PaDeviceIndex i = 0; i < n; ++i) {
        const PaDeviceInfo* inf = Pa_GetDeviceInfo(i);
        if (inf == nullptr) {
            continue;
        }
        const PaHostApiInfo* api = nullptr;
        if (inf->hostApi >= 0 && inf->hostApi < n_apis) {
            api = Pa_GetHostApiInfo(inf->hostApi);
        }
        std::fprintf(out, "  [%d] %s  in_ch=%d out_ch=%d  (%s)\n", static_cast<int>(i), inf->name,
                     inf->maxInputChannels, inf->maxOutputChannels, api != nullptr ? api->name : "?");
    }
    std::fprintf(out, "  default in=%d out=%d\n", static_cast<int>(Pa_GetDefaultInputDevice()),
                 static_cast<int>(Pa_GetDefaultOutputDevice()));
}

#if defined(_WIN32)
static void lv_win32_cap_duplex_suggested_latency(PaStreamParameters& inp, PaStreamParameters& outp, double sample_rate,
                                                  unsigned long hop) {
    if (env_truthy(std::getenv("LIVE_VOCODER_PA_DISABLE_SUGGESTED_CAP"))) {
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
    const double in0 = inp.suggestedLatency;
    const double out0 = outp.suggestedLatency;
    inp.suggestedLatency = std::clamp(inp.suggestedLatency, hop_sec, cap_sec);
    outp.suggestedLatency = std::clamp(outp.suggestedLatency, hop_sec, cap_sec);
    if (in0 > cap_sec + 1e-4 || out0 > cap_sec + 1e-4) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            std::fprintf(stderr,
                         "[LiveVocoder] Capping duplex PortAudio suggested latency to ~%.0f ms (device reported up to "
                         "%.3f s in / %.3f s out). Tune: LIVE_VOCODER_PA_MAX_SUGGESTED_LATENCY_SEC, or "
                         "LIVE_VOCODER_PA_DISABLE_SUGGESTED_CAP=1.\n",
                         cap_sec * 1000.0, in0, out0);
        }
    }
}

static bool lv_win32_device_api_is_wasapi(PaDeviceIndex dev) {
    const PaDeviceInfo* inf = Pa_GetDeviceInfo(dev);
    if (inf == nullptr) {
        return false;
    }
    const PaHostApiInfo* api = Pa_GetHostApiInfo(inf->hostApi);
    return api != nullptr && api->type == paWASAPI;
}

static void lv_win32_wasapi_duplex_info_init(PaWasapiStreamInfo* w, PaWasapiStreamOption raw_opt) {
    std::memset(w, 0, sizeof(*w));
    w->size = sizeof(PaWasapiStreamInfo);
    w->hostApiType = paWASAPI;
    w->version = 1;
    w->flags = paWinWasapiThreadPriority;
    w->threadPriority = eThreadPriorityProAudio;
    w->streamCategory = eAudioCategoryCommunications;
    w->streamOption = raw_opt;
}

static PaError lv_win32_open_duplex_wasapi(PaStream** stream, PaStreamParameters* inp, PaStreamParameters* outp,
                                           double sample_rate, unsigned long hop, PaStreamCallback callback,
                                           void* user_data) {
    if (lv_windows_is_wine_host() || env_truthy(std::getenv("LIVE_VOCODER_PA_DISABLE_WASAPI_STREAMINFO"))) {
        return Pa_OpenStream(stream, inp, outp, sample_rate, hop, paNoFlag, callback, user_data);
    }
    if (!lv_win32_device_api_is_wasapi(inp->device) || !lv_win32_device_api_is_wasapi(outp->device)) {
        return Pa_OpenStream(stream, inp, outp, sample_rate, hop, paNoFlag, callback, user_data);
    }
    PaWasapiStreamInfo wi{};
    PaWasapiStreamInfo wo{};
    const bool want_raw = env_truthy(std::getenv("LIVE_VOCODER_PA_WASAPI_RAW"));
    auto attempt = [&](PaWasapiStreamOption raw) -> PaError {
        lv_win32_wasapi_duplex_info_init(&wi, raw);
        lv_win32_wasapi_duplex_info_init(&wo, raw);
        inp->hostApiSpecificStreamInfo = &wi;
        outp->hostApiSpecificStreamInfo = &wo;
        return Pa_OpenStream(stream, inp, outp, sample_rate, hop, paNoFlag, callback, user_data);
    };
    PaError e = attempt(want_raw ? eStreamOptionRaw : eStreamOptionNone);
    if (e != paNoError && want_raw) {
        e = attempt(eStreamOptionNone);
    }
    if (e != paNoError) {
        inp->hostApiSpecificStreamInfo = nullptr;
        outp->hostApiSpecificStreamInfo = nullptr;
        e = Pa_OpenStream(stream, inp, outp, sample_rate, hop, paNoFlag, callback, user_data);
        if (e == paNoError) {
            static bool logged = false;
            if (!logged) {
                logged = true;
                std::fprintf(stderr,
                             "[LiveVocoder] WASAPI duplex: driver rejected host stream info; using PortAudio defaults.\n");
            }
        }
    } else {
        static bool logged_ok = false;
        if (!logged_ok) {
            logged_ok = true;
            std::fprintf(stderr,
                         "[LiveVocoder] WASAPI duplex: communications category + pro-audio thread priority. Optional: "
                         "LIVE_VOCODER_PA_WASAPI_RAW=1 (bypass engine DSP), LIVE_VOCODER_PA_DISABLE_WASAPI_STREAMINFO=1.\n");
        }
    }
    return e;
}
#endif

PaError pa_open_livevocoder_duplex(PaStream** stream, double sample_rate, unsigned long hop,
                                   PaStreamCallback callback, void* user_data) {
    PaDeviceIndex in_dev = pick_input_device();
    PaDeviceIndex out_dev = pick_output_device();
#if defined(_WIN32)
    lv_win32_align_duplex_devices_native(in_dev, out_dev);
#endif
    if (in_dev < 0) {
        return paInvalidDevice;
    }
    if (out_dev < 0) {
        return paInvalidDevice;
    }

    const auto open_pair = [&](PaDeviceIndex id, PaDeviceIndex od) -> PaError {
        const PaDeviceInfo* ii = Pa_GetDeviceInfo(id);
        const PaDeviceInfo* oi = Pa_GetDeviceInfo(od);
        if (ii == nullptr || oi == nullptr || ii->maxInputChannels < 1 || oi->maxOutputChannels < 2) {
            return paInvalidDevice;
        }
        PaStreamParameters inp{};
        inp.device = id;
        inp.channelCount = 1;
        inp.sampleFormat = paFloat32;
        inp.hostApiSpecificStreamInfo = nullptr;
        PaStreamParameters outp{};
        outp.device = od;
        outp.channelCount = 2;
        outp.sampleFormat = paFloat32;
        outp.hostApiSpecificStreamInfo = nullptr;
#if defined(_WIN32)
        /* VB-Cable: we default to low PortAudio latency (tighter monitoring). Opt into high buffers if
           VB control panel shows Pull loss: LIVE_VOCODER_PA_HIGH_LATENCY=1. */
        const bool vb_duplex = !lv_windows_is_wine_host() &&
                               (lv_win32_pa_name_hints_vb_virtual_cable(ii->name) ||
                                lv_win32_pa_name_hints_vb_virtual_cable(oi->name));
        const bool force_low = env_truthy(std::getenv("LIVE_VOCODER_PA_LOW_LATENCY")) ||
                               env_truthy(std::getenv("LIVE_VOCODER_LIVE_MONITORING"));
        const bool prefer_high =
            vb_duplex && (env_truthy(std::getenv("LIVE_VOCODER_PA_HIGH_LATENCY")) ||
                          env_truthy(std::getenv("LIVE_VOCODER_VB_HIGH_LATENCY")));
        const bool use_high = prefer_high && !force_low;
        if (use_high) {
            static bool logged = false;
            if (!logged) {
                logged = true;
                std::fprintf(stderr,
                             "[LiveVocoder] VB-Cable duplex: PortAudio high suggested latency "
                             "(LIVE_VOCODER_PA_HIGH_LATENCY / LIVE_VOCODER_VB_HIGH_LATENCY; fewer pull underruns, more delay).\n");
            }
            inp.suggestedLatency = ii->defaultHighInputLatency;
            outp.suggestedLatency = oi->defaultHighOutputLatency;
        } else {
            if (vb_duplex) {
                static bool logged_low = false;
                if (!logged_low) {
                    logged_low = true;
                    std::fprintf(stderr,
                                 "[LiveVocoder] VB-Cable duplex: low PortAudio latency. If Pull loss climbs, set "
                                 "LIVE_VOCODER_PA_HIGH_LATENCY=1.\n");
                }
            }
            inp.suggestedLatency = ii->defaultLowInputLatency;
            outp.suggestedLatency = oi->defaultLowOutputLatency;
            lv_win32_cap_duplex_suggested_latency(inp, outp, sample_rate, hop);
        }
#else
        inp.suggestedLatency = ii->defaultLowInputLatency;
        outp.suggestedLatency = oi->defaultLowOutputLatency;
#endif
#if defined(_WIN32)
        return lv_win32_open_duplex_wasapi(stream, &inp, &outp, sample_rate, hop, callback, user_data);
#else
        return Pa_OpenStream(stream, &inp, &outp, sample_rate, hop, paNoFlag, callback, user_data);
#endif
    };

    PaError e = open_pair(in_dev, out_dev);
#if defined(_WIN32)
    if (e == paBadIODeviceCombination && !lv_windows_is_wine_host()) {
        const PaHostApiIndex dh = Pa_GetDefaultHostApi();
        if (dh >= 0) {
            const PaHostApiInfo* hai = Pa_GetHostApiInfo(dh);
            if (hai != nullptr && hai->defaultInputDevice >= 0 && hai->defaultOutputDevice >= 0) {
                const PaDeviceInfo* di = Pa_GetDeviceInfo(hai->defaultInputDevice);
                const PaDeviceInfo* dout = Pa_GetDeviceInfo(hai->defaultOutputDevice);
                if (di != nullptr && dout != nullptr && di->maxInputChannels >= 1 && dout->maxOutputChannels >= 2 &&
                    di->hostApi == dout->hostApi) {
                    static bool logged_fb = false;
                    if (!logged_fb) {
                        logged_fb = true;
                        std::fprintf(stderr,
                                     "[LiveVocoder] Native Windows: retrying duplex on default host API [%s] after "
                                     "illegal I/O device combination.\n",
                                     hai->name != nullptr ? hai->name : "?");
                    }
                    const PaError e2 = open_pair(hai->defaultInputDevice, hai->defaultOutputDevice);
                    if (e2 == paNoError) {
                        g_duplex_out_dev = hai->defaultOutputDevice;
                        return paNoError;
                    }
                }
            }
        }
    }
#endif
    if (e == paNoError) {
        g_duplex_out_dev = out_dev;
    } else {
        g_duplex_out_dev = paNoDevice;
    }
    return e;
}

void pa_duplex_note_stream_closed() {
    g_duplex_out_dev = paNoDevice;
}

PaDeviceIndex pa_duplex_last_opened_output_device() {
    return g_duplex_out_dev;
}

bool pa_duplex_output_targets_virt_sink_route() {
    if (g_duplex_out_dev < 0) {
        return false;
    }
    const PaDeviceInfo* inf = Pa_GetDeviceInfo(g_duplex_out_dev);
    if (inf == nullptr || inf->name == nullptr) {
        return false;
    }
    if (pa_output_name_is_virt_sink_discord_route(inf->name)) {
        return true;
    }
#if defined(__linux__)
    // PULSE_SINK is honored for any stream going through the PulseAudio PortAudio host API, but the device
    // name is often "Built-in Audio …" not "pulse". Without this, Monitor off zeros samples and kills *_mic.
    if (lv_env_pulse_sink_targets_virt_route()) {
        const PaHostApiIndex pulse = lv_find_pulse_host_api();
        if (pulse >= 0 && inf->hostApi == pulse) {
            return true;
        }
    }
#endif
#if defined(_WIN32)
    if (pa_output_name_is_windows_virtual_audio_route(inf->name)) {
        return true;
    }
    // Wine: output is usually "… (PulseAudio Output)" — not "live_vocoder" in the device string, but PULSE_SINK
    // still routes the stream to the null sink after move-sink-input; treat like a virt route for Monitor loopback.
    if (lv_windows_is_wine_host() && lv_env_pulse_sink_targets_virt_route() &&
        lv_ci_hay_contains(inf->name, "pulseaudio")) {
        return true;
    }
#endif
    return pa_output_device_is_pulse_pcm_virt_route(inf->name);
}

void pa_log_stream_devices(PaStream* stream) {
    (void)stream;
    const PaDeviceIndex in_dev = pick_input_device();
    const PaDeviceIndex out_dev = pick_output_device();
    const PaDeviceInfo* idi = Pa_GetDeviceInfo(in_dev);
    const PaDeviceInfo* odi = Pa_GetDeviceInfo(out_dev);
    std::fprintf(stderr, "[LiveVocoder] PortAudio stream: input device %d \"%s\" → output device %d \"%s\"\n",
                 static_cast<int>(in_dev), idi != nullptr ? idi->name : "?",
                 static_cast<int>(out_dev), odi != nullptr ? odi->name : "?");
    std::fprintf(stderr,
                 "[LiveVocoder] Override: LIVE_VOCODER_PA_INPUT / LIVE_VOCODER_PA_OUTPUT (name substring), "
                 "or LIVE_VOCODER_PA_INPUT_INDEX / LIVE_VOCODER_PA_OUTPUT_INDEX; "
                 "LIVE_VOCODER_PA_LIST_DEVICES=1 lists devices.\n");
}

std::string pa_portaudio_virt_capture_hint() {
    const int n_raw = Pa_GetDeviceCount();
    if (n_raw <= 0) {
        return {};
    }
    const PaDeviceIndex n = static_cast<PaDeviceIndex>(n_raw);
    std::vector<std::string> hits;
    for (PaDeviceIndex i = 0; i < n; ++i) {
        const PaDeviceInfo* inf = Pa_GetDeviceInfo(i);
        if (inf == nullptr || inf->maxInputChannels < 1) {
            continue;
        }
        const char* name = inf->name;
        if (name == nullptr || name[0] == '\0') {
            continue;
        }
        if (lv_ci_hay_contains(name, "monitor") || lv_ci_hay_contains(name, "_mic") ||
            lv_ci_hay_contains(name, "virtual") || lv_ci_hay_contains(name, "live_vocoder") ||
            lv_ci_hay_contains(name, "remap")
#if defined(_WIN32)
            || lv_win32_pa_name_hints_vb_virtual_cable(name) || lv_ci_hay_contains(name, "voicemeeter")
#endif
        ) {
            hits.emplace_back(name);
            if (hits.size() >= 2) {
                break;
            }
        }
    }
    if (hits.empty()) {
        return {};
    }
    std::string out = "PortAudio capture looks like virtual route: ";
    out += hits[0];
    if (hits.size() > 1) {
        out += "; ";
        out += hits[1];
    }
    return out;
}

std::string pa_windows_virt_mic_route_hint() {
#if !defined(_WIN32)
    return {};
#else
    if (lv_windows_is_wine_host()) {
        return "Wine (Linux host): use PULSE_SINK / LIVE_VOCODER_PULSE_SINK (e.g. live_vocoder) when launching Wine, "
               "or move playback to that null sink in pavucontrol / WirePlumber. VB-Audio Virtual Cable "
               "auto-routing (CABLE Input/Output) applies on native Windows only, not under Wine. "
               "LIVE_VOCODER_PA_LIST_DEVICES=1 lists devices; or run native Linux LiveVocoder.exe.";
    }
    const char* dis_vb = std::getenv("LIVE_VOCODER_DISABLE_VB_CABLE");
    if (dis_vb != nullptr && dis_vb[0] != '\0' &&
        (dis_vb[0] == '1' || dis_vb[0] == 't' || dis_vb[0] == 'T' || dis_vb[0] == 'y' || dis_vb[0] == 'Y')) {
        return {};
    }
    const int n_raw = Pa_GetDeviceCount();
    if (n_raw <= 0) {
        return {};
    }
    const PaDeviceIndex cab =
        lv_native_windows_pick_vb_cable_output_device(static_cast<PaDeviceIndex>(n_raw));
    if (cab >= 0) {
        return "PortAudio → CABLE Input; Monitor on → speakers too. OBS: mic CABLE Output. "
               "LIVE_VOCODER_WIN_DEFAULT_VIRT_MIC=1 sets Windows default recording to CABLE. "
               "PA_OUTPUT / DISABLE_VB_CABLE / LIST_DEVICES=1.";
    }
    return "VB-Cable not in PortAudio — run VBCABLE_Setup_x64.exe from Program Files\\Live Vocoder\\extras "
           "(full driver pack) or https://vb-audio.com/Cable/ · reboot if asked · LIST_DEVICES=1.";
#endif
}

std::string pa_windows_native_vb_cable_portaudio_hint() {
#if !defined(_WIN32)
    return {};
#else
    // Merged into pa_windows_virt_mic_route_hint() so the status bar does not imply VB-Cable is present
    // when PortAudio does not list CABLE Input.
    return {};
#endif
}
