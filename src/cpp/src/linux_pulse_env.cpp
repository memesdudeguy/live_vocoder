#include "linux_pulse_env.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#if defined(__linux__)
#include <cstdio>
#include <unistd.h>
#endif
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdio>
#include <stdlib.h>  // _putenv_s: sync CRT getenv with process env (SetEnvironmentVariable alone is not enough for MinGW)
#endif

namespace {

std::string trim_ws(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    return s;
}

#if defined(__linux__)

std::string slurp_popen_cmd(const std::string& cmd) {
    std::unique_ptr<FILE, int (*)(FILE*)> fp(popen(cmd.c_str(), "r"), pclose);
    if (!fp) {
        return {};
    }
    std::string out;
    char buf[4096];
    while (std::fgets(buf, sizeof buf, fp.get()) != nullptr) {
        out += buf;
    }
    return out;
}

std::vector<std::string> pactl_short_names(const char* short_kind) {
    std::string text = slurp_popen_cmd(std::string("pactl list short ") + short_kind);
    std::vector<std::string> names;
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        line = trim_ws(line);
        if (line.empty()) {
            continue;
        }
        const std::size_t t1 = line.find('\t');
        if (t1 == std::string::npos) {
            continue;
        }
        const std::size_t t2 = line.find('\t', t1 + 1);
        std::string name =
            (t2 == std::string::npos) ? line.substr(t1 + 1) : line.substr(t1 + 1, t2 - t1 - 1);
        name = trim_ws(name);
        if (!name.empty()) {
            names.push_back(std::move(name));
        }
    }
    return names;
}

#endif  // __linux__

bool vector_contains(const std::vector<std::string>& v, const std::string& x) {
    for (const auto& s : v) {
        if (s == x) {
            return true;
        }
    }
    return false;
}

bool ci_substr(const std::string& hay, const char* needle) {
    if (needle == nullptr || needle[0] == '\0') {
        return true;
    }
    const std::size_t nlen = std::strlen(needle);
    for (std::size_t i = 0; i + nlen <= hay.size(); ++i) {
        std::size_t j = 0;
        for (; j < nlen; ++j) {
            if (std::tolower(static_cast<unsigned char>(hay[i + j])) !=
                std::tolower(static_cast<unsigned char>(needle[j]))) {
                break;
            }
        }
        if (j == nlen) {
            return true;
        }
    }
    return false;
}

bool sink_name_matches_live_vocoder_brand(const std::string& s) {
    // PipeWire often names nodes "LiveVocoder-3" (no underscore); pactl may use "live_vocoder".
    return ci_substr(s, "live_vocoder") || ci_substr(s, "livevocoder");
}

std::vector<std::string> sinks_matching_live_vocoder(const std::vector<std::string>& sinks) {
    std::vector<std::string> out;
    for (const auto& s : sinks) {
        if (sink_name_matches_live_vocoder_brand(s)) {
            out.push_back(s);
        }
    }
    return out;
}

std::string sanitize_pulse_sink_token(const char* raw) {
    if (raw == nullptr || raw[0] == '\0') {
        return {};
    }
    std::string s;
    for (const char* p = raw; *p != '\0'; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (std::isalnum(c) || *p == '_' || *p == '-') {
            s += static_cast<char>(c);
        } else if (!s.empty() && s.back() != '_') {
            s += '_';
        }
    }
    while (!s.empty() && s.back() == '_') {
        s.pop_back();
    }
    return s;
}

std::string resolve_sink_name(const std::vector<std::string>& sink_names) {
    const char* env_raw = std::getenv("LIVE_VOCODER_PULSE_SINK");
    if (env_raw != nullptr && env_raw[0] != '\0') {
        std::string t = sanitize_pulse_sink_token(env_raw);
        if (!t.empty()) {
            return t;
        }
    }
    std::vector<std::string> cands = sinks_matching_live_vocoder(sink_names);
    std::sort(cands.begin(), cands.end());
    cands.erase(std::unique(cands.begin(), cands.end()), cands.end());
    if (!cands.empty()) {
        // Prefer the most specific name (e.g. live_vocoder2 over live_vocoder) when both exist.
        return cands.back();
    }
    return "live_vocoder";
}

std::string pulse_description_compact() {
    const char* d = std::getenv("LIVE_VOCODER_PULSE_DESCRIPTION");
    std::string s = (d != nullptr && d[0] != '\0') ? std::string(d) : std::string("LiveVocoder");
    std::string out;
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            out += c;
        }
    }
    return out.empty() ? std::string("LiveVocoder") : out;
}

std::string pw_safe_suffix_str(const std::string& name) {
    std::string o;
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            o += c;
        } else {
            o += '_';
        }
    }
    while (!o.empty() && o.back() == '_') {
        o.pop_back();
    }
    return o.empty() ? std::string("sink") : o;
}

#if defined(__linux__)

bool pactl_cli_available() {
    return std::system("command -v pactl >/dev/null 2>&1") == 0;
}

void lv_ensure_pipewire_virt_mic_stack_impl() {
    const char* dis = std::getenv("LIVE_VOCODER_AUTO_VIRT_MIC");
    if (dis != nullptr && dis[0] != '\0') {
        if (dis[0] == '0' || dis[0] == 'f' || dis[0] == 'F') {
            return;
        }
    }
    if (!pactl_cli_available()) {
        return;
    }
    std::vector<std::string> sinks = pactl_short_names("sinks");
    const std::string sn = resolve_sink_name(sinks);
    const std::string dc = pulse_description_compact();
    const std::string slug = pw_safe_suffix_str(sn);
    const std::string src_slug = pw_safe_suffix_str(sn + "_mic");

    if (!vector_contains(sinks, sn)) {
        const std::string props_sink = "device.description=" + dc + " node.description=" + dc + " node.name=audio_sink_" +
                                       slug + " media.class=Audio/Sink";
        // Default null-sink is often 44100 Hz; LiveVocoder uses 48 kHz stereo — mismatch sounds muffled / metallic.
        const std::string cmd = "pactl load-module module-null-sink sink_name=" + sn +
                                " rate=48000 channels=2 sink_properties='" + props_sink + "'";
        (void)std::system(cmd.c_str());
        usleep(150000);
        sinks = pactl_short_names("sinks");
    }

    const std::string mic = sn + "_mic";
    std::vector<std::string> sources = pactl_short_names("sources");
    if (vector_contains(sources, mic)) {
        return;
    }

    const std::string mon = sn + ".monitor";
    for (int poll = 0; poll < 25 && !vector_contains(sources, mon); ++poll) {
        if (poll > 0) {
            usleep(100000);
        }
        sources = pactl_short_names("sources");
    }
    if (!vector_contains(sources, mon)) {
        return;
    }

    const std::string label = dc + "VirtualMic";
    const std::string props_full = "device.description=" + label + " node.description=" + label + " node.nick=" + label +
                                   " node.name=audio_source_" + src_slug +
                                   " media.class=Audio/Source/Virtual device.form-factor=microphone "
                                   "device.icon-name=audio-input-microphone";
    const std::string props_min = "device.description=" + label + " node.description=" + label;

    auto try_load_mic = [&](const std::string& cmd) {
        (void)std::system(cmd.c_str());
        for (int k = 0; k < 10; ++k) {
            if (k > 0) {
                usleep(80000);
            }
            sources = pactl_short_names("sources");
            if (vector_contains(sources, mic)) {
                return true;
            }
        }
        return false;
    };

    const std::string cmd1 = "pactl load-module module-remap-source master=" + mon + " source_name=" + mic + " source_properties='" +
                             props_full + "'";
    if (try_load_mic(cmd1)) {
        return;
    }
    const std::string cmd2 = "pactl load-module module-remap-source master=" + mon + " source_name=" + mic + " source_properties='" +
                             props_min + "'";
    if (try_load_mic(cmd2)) {
        return;
    }
    const std::string cmd3 = "pactl load-module module-virtual-source master=" + mon + " source_name=" + mic + " source_properties='" +
                             props_full + "'";
    (void)try_load_mic(cmd3);
}

static int g_speaker_monitor_loopback_module = -1;
static std::string g_speaker_monitor_loopback_sink_base;

static bool pulse_loopback_name_token_ok(const std::string& s) {
    for (char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) == 0 && c != '_' && c != '-' && c != '.') {
            return false;
        }
    }
    return !s.empty();
}

void lv_linux_sync_speaker_monitor_loopback_impl(bool want_monitor, const char* pulse_sink_base) {
    if (!pactl_cli_available()) {
        return;
    }
    if (!want_monitor || pulse_sink_base == nullptr || pulse_sink_base[0] == '\0') {
        if (g_speaker_monitor_loopback_module >= 0) {
            (void)std::system(("pactl unload-module " + std::to_string(g_speaker_monitor_loopback_module)).c_str());
            g_speaker_monitor_loopback_module = -1;
            g_speaker_monitor_loopback_sink_base.clear();
        }
        return;
    }
    const std::string want_base(pulse_sink_base);
    if (g_speaker_monitor_loopback_module >= 0 && g_speaker_monitor_loopback_sink_base == want_base) {
        return;
    }
    if (g_speaker_monitor_loopback_module >= 0) {
        (void)std::system(("pactl unload-module " + std::to_string(g_speaker_monitor_loopback_module)).c_str());
        g_speaker_monitor_loopback_module = -1;
        g_speaker_monitor_loopback_sink_base.clear();
    }
    const std::string src = want_base + ".monitor";
    if (!pulse_loopback_name_token_ok(src)) {
        return;
    }
    std::vector<std::string> sources = pactl_short_names("sources");
    if (!vector_contains(sources, src)) {
        return;
    }
    const std::string cmd =
        "pactl load-module module-loopback source=" + src + " sink=@DEFAULT_SINK@ latency_msec=40 2>&1";
    std::string out = trim_ws(slurp_popen_cmd(cmd));
    if (out.empty()) {
        return;
    }
    std::size_t nl = out.find('\n');
    if (nl != std::string::npos) {
        out = trim_ws(out.substr(0, nl));
    }
    if (out.empty()) {
        return;
    }
    bool all_digit = true;
    for (char c : out) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            all_digit = false;
            break;
        }
    }
    if (!all_digit) {
        return;
    }
    const long mod = std::strtol(out.c_str(), nullptr, 10);
    if (mod > 0 && mod < 2147483647L) {
        g_speaker_monitor_loopback_module = static_cast<int>(mod);
        g_speaker_monitor_loopback_sink_base = want_base;
    }
}

#elif defined(_WIN32)

/** Last PULSE_SINK applied in this process (Wine: CRT/OS env reads can lag after assign). */
static std::string g_lv_win32_applied_pulse_sink_for_move;

/** MinGW/PE: std::getenv reads the CRT block; SetEnvironmentVariableA alone does not update it. */
static void lv_win32_assign_env_for_crt_and_os(const char* key, const char* val) {
    if (key == nullptr || val == nullptr || val[0] == '\0') {
        return;
    }
    (void)SetEnvironmentVariableA(key, val);
#if defined(_MSC_VER) || defined(__MINGW32__)
    (void)_putenv_s(key, val);
#else
    std::string kv = std::string(key) + "=" + val;
    (void)_putenv(kv.c_str());
#endif
    if (std::strcmp(key, "PULSE_SINK") == 0) {
        g_lv_win32_applied_pulse_sink_for_move.assign(val);
    }
}

static bool lv_win32_is_wine_host() {
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (ntdll == nullptr) {
        return false;
    }
    return GetProcAddress(ntdll, "wine_get_version") != nullptr;
}

/** Single-quoted bash string; internal ' → '\'' */
static std::string sh_single_quote_bash(const std::string& s) {
    std::string o = "'";
    for (char c : s) {
        if (c == '\'') {
            o += "'\\''";
        } else {
            o += c;
        }
    }
    o += '\'';
    return o;
}

/**
 * Wine exposes WINE_HOST_XDG_RUNTIME_DIR; host pactl needs XDG_RUNTIME_DIR for the Pulse/PipeWire socket.
 * pactl --version does not connect; list/load-module do — without this, sink list stays empty and PULSE_SINK is never set.
 */
static std::string win32_wine_wrap_inner_for_host_pulse(const std::string& inner) {
    // Wine's Win32 environment often omits a Unix PATH; host pactl/winepath live under /usr/bin.
    std::string prefix = "export PATH=/usr/bin:/bin:$PATH;";
    char rt[1024];
    DWORD n = GetEnvironmentVariableA("WINE_HOST_XDG_RUNTIME_DIR", rt, sizeof(rt));
    if (n == 0U) {
        return prefix + inner;
    }
    if (n >= sizeof(rt)) {
        // n is required buffer size in TCHARs including null (buffer was too small).
        std::vector<char> d(static_cast<std::size_t>(n));
        const DWORD m = GetEnvironmentVariableA("WINE_HOST_XDG_RUNTIME_DIR", d.data(), static_cast<DWORD>(d.size()));
        if (m == 0U || m >= d.size()) {
            return prefix + inner;
        }
        d[m] = '\0';
        return prefix + "export XDG_RUNTIME_DIR=" + sh_single_quote_bash(std::string(d.data())) + ";" + inner;
    }
    rt[n] = '\0';
    return prefix + "export XDG_RUNTIME_DIR=" + sh_single_quote_bash(std::string(rt)) + ";" + inner;
}

static std::string win32_win32_path_to_fwd_slashes(std::string p) {
    for (char& c : p) {
        if (c == '\\') {
            c = '/';
        }
    }
    return p;
}

static std::string win32_escape_for_bash_lc_double_quotes(const std::string& inner) {
    std::string o;
    o.reserve(inner.size() + 8);
    for (char c : inner) {
        // Do not escape '$': host PATH/XDG and $(winepath …) must expand inside -lc "…".
        if (c == '\\' || c == '"' || c == '`') {
            o += '\\';
        }
        o += c;
    }
    return o;
}

/**
 * Run host bash -lc under Wine without cmd.exe (system/_popen mangle Z:\\ paths).
 * lpCommandLine must be a writable buffer per CreateProcessA rules.
 *
 * Wine often fails to wire inherited pipes from Win32 CreateProcess to Unix bash stdout;
 * when capture_stdout is set, redirect ( subshell ) to %TEMP% via winepath -u and read the file.
 */
static bool win32_wine_spawn_bash_lc(const std::string& bash_exe, const std::string& inner_script,
                                     std::string* capture_stdout, DWORD* exit_code_out) {
    const std::string wrapped_base = win32_wine_wrap_inner_for_host_pulse(inner_script);
    std::string run_inner = wrapped_base;
    std::string win_cap_file;
    if (capture_stdout != nullptr) {
        capture_stdout->clear();
        char tdir[MAX_PATH];
        const DWORD tl = GetTempPathA(static_cast<DWORD>(sizeof(tdir)), tdir);
        if (tl == 0U || tl >= sizeof(tdir)) {
            return false;
        }
        const std::string tag =
            std::to_string(GetCurrentProcessId()) + "_" + std::to_string(GetTickCount());
        win_cap_file = std::string(tdir) + "lv_bw_" + tag + ".out";
        // Bash writes via Unix path; PE reads via Win32 path (Z:/tmp is not openable from all MinGW builds).
        run_inner = "u=$(winepath -u " + sh_single_quote_bash(win_cap_file) + ");(" + wrapped_base + ") >\"$u\" 2>&1";
        (void)DeleteFileA(win32_win32_path_to_fwd_slashes(win_cap_file).c_str());
    }
    const std::string esc = win32_escape_for_bash_lc_double_quotes(run_inner);
    std::string cmdline = "\"" + bash_exe + "\" -lc \"" + esc + "\"";
    std::vector<char> cmd_buf(cmdline.begin(), cmdline.end());
    cmd_buf.push_back('\0');

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE nul_in = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    if (nul_in == INVALID_HANDLE_VALUE) {
        return false;
    }
    HANDLE nul_out = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    if (nul_out == INVALID_HANDLE_VALUE) {
        CloseHandle(nul_in);
        return false;
    }

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul_in;
    si.hStdOutput = nul_out;
    si.hStdError = nul_out;

    PROCESS_INFORMATION pi{};
    const BOOL created =
        CreateProcessA(nullptr, cmd_buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    CloseHandle(nul_out);
    CloseHandle(nul_in);

    if (created == 0) {
        return false;
    }

    CloseHandle(pi.hThread);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = static_cast<DWORD>(-1);
    if (GetExitCodeProcess(pi.hProcess, &code) == 0) {
        code = static_cast<DWORD>(-1);
    }
    CloseHandle(pi.hProcess);

    if (capture_stdout != nullptr) {
        Sleep(130);
        const std::string cap_path = win32_win32_path_to_fwd_slashes(win_cap_file);
        HANDLE hf = INVALID_HANDLE_VALUE;
        char full_buf[MAX_PATH * 2];
        std::string delete_path = cap_path;
        for (int attempt = 0; attempt < 12 && hf == INVALID_HANDLE_VALUE; ++attempt) {
            if (attempt > 0) {
                Sleep(20);
            }
            const DWORD fn = GetFullPathNameA(cap_path.c_str(), static_cast<DWORD>(sizeof(full_buf)), full_buf, nullptr);
            const char* open_path = (fn > 0U && fn < sizeof(full_buf)) ? full_buf : cap_path.c_str();
            hf = CreateFileA(open_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hf != INVALID_HANDLE_VALUE) {
                delete_path.assign(open_path);
            }
        }
        if (hf != INVALID_HANDLE_VALUE) {
            char io[16384];
            DWORD nread = 0;
            while (ReadFile(hf, io, sizeof io, &nread, nullptr) != 0 && nread > 0) {
                capture_stdout->append(io, static_cast<std::size_t>(nread));
            }
            CloseHandle(hf);
        }
        (void)DeleteFileA(delete_path.c_str());
    }
    if (exit_code_out != nullptr) {
        *exit_code_out = code;
    }
    return true;
}

static void win32_wine_bash_candidates(std::vector<std::string>* out) {
    out->clear();
    const char* from_env = std::getenv("LIVE_VOCODER_WINE_BASH");
    if (from_env != nullptr && from_env[0] != '\0') {
        out->emplace_back(from_env);
    }
    // Linux hosts only have "bash", not "bash.exe"; Wine resolves Z: → / so try extensionless first.
    static const char* kCand[] = {"Z:\\usr\\bin\\bash", "Z:\\bin\\bash", "Z:\\usr\\bin\\bash.exe",
                                  "Z:\\bin\\bash.exe"};
    for (const char* c : kCand) {
        out->emplace_back(c);
    }
}

static std::string win32_wine_bash_lc_slurp(const std::string& bash_inner) {
    if (!lv_win32_is_wine_host()) {
        return {};
    }
    std::vector<std::string> cands;
    win32_wine_bash_candidates(&cands);
    for (const auto& bash_exe : cands) {
        std::string cap;
        DWORD code = static_cast<DWORD>(-1);
        if (!win32_wine_spawn_bash_lc(bash_exe, bash_inner, &cap, &code)) {
            continue;
        }
        // Wine may not report Unix exit codes via GetExitCodeProcess; trust captured output when present.
        if (!cap.empty()) {
            return cap;
        }
        if (code == 0U) {
            return cap;
        }
    }
    return {};
}

static int win32_wine_bash_lc_exec(const std::string& bash_inner) {
    if (!lv_win32_is_wine_host()) {
        return -1;
    }
    std::vector<std::string> cands;
    win32_wine_bash_candidates(&cands);
    for (const auto& bash_exe : cands) {
        DWORD code = 1;
        if (!win32_wine_spawn_bash_lc(bash_exe, bash_inner, nullptr, &code)) {
            continue;
        }
        return static_cast<int>(code);
    }
    return -1;
}

static bool win32_wine_pactl_cli_available() {
    // Require a real server connection; --version alone succeeds without XDG_RUNTIME_DIR.
    std::string o = win32_wine_bash_lc_slurp(
        "command -v pactl >/dev/null 2>&1 && pactl info >/dev/null 2>&1 && printf OK");
    return trim_ws(o) == "OK";
}

static std::vector<std::string> win32_wine_pactl_short_names(const char* short_kind) {
    std::string text = win32_wine_bash_lc_slurp(std::string("pactl list short ") + short_kind);
    std::vector<std::string> names;
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        line = trim_ws(line);
        if (line.empty()) {
            continue;
        }
        const std::size_t t1 = line.find('\t');
        if (t1 == std::string::npos) {
            continue;
        }
        const std::size_t t2 = line.find('\t', t1 + 1);
        std::string name =
            (t2 == std::string::npos) ? line.substr(t1 + 1) : line.substr(t1 + 1, t2 - t1 - 1);
        name = trim_ws(name);
        if (!name.empty()) {
            names.push_back(std::move(name));
        }
    }
    return names;
}

void lv_win32_wine_ensure_pipewire_virt_mic_stack_impl() {
    if (!lv_win32_is_wine_host()) {
        return;
    }
    const char* dis = std::getenv("LIVE_VOCODER_AUTO_VIRT_MIC");
    if (dis != nullptr && dis[0] != '\0') {
        if (dis[0] == '0' || dis[0] == 'f' || dis[0] == 'F') {
            return;
        }
    }
    if (!win32_wine_pactl_cli_available()) {
        return;
    }
    std::vector<std::string> sinks = win32_wine_pactl_short_names("sinks");
    const std::string sn = resolve_sink_name(sinks);
    const std::string dc = pulse_description_compact();
    const std::string slug = pw_safe_suffix_str(sn);
    const std::string src_slug = pw_safe_suffix_str(sn + "_mic");

    if (!vector_contains(sinks, sn)) {
        const std::string props_sink = "device.description=" + dc + " node.description=" + dc + " node.name=audio_sink_" +
                                       slug + " media.class=Audio/Sink";
        const std::string cmd = "pactl load-module module-null-sink sink_name=" + sn +
                                " rate=48000 channels=2 sink_properties='" + props_sink + "'";
        (void)win32_wine_bash_lc_exec(cmd);
        (void)win32_wine_bash_lc_exec("sleep 0.2");
        sinks = win32_wine_pactl_short_names("sinks");
    }

    const std::string mic = sn + "_mic";
    std::vector<std::string> sources = win32_wine_pactl_short_names("sources");
    if (vector_contains(sources, mic)) {
        return;
    }

    const std::string mon = sn + ".monitor";
    for (int poll = 0; poll < 25 && !vector_contains(sources, mon); ++poll) {
        if (poll > 0) {
            (void)win32_wine_bash_lc_exec("sleep 0.1");
        }
        sources = win32_wine_pactl_short_names("sources");
    }
    if (!vector_contains(sources, mon)) {
        return;
    }

    const std::string label = dc + "VirtualMic";
    const std::string props_full = "device.description=" + label + " node.description=" + label + " node.nick=" + label +
                                   " node.name=audio_source_" + src_slug +
                                   " media.class=Audio/Source/Virtual device.form-factor=microphone "
                                   "device.icon-name=audio-input-microphone";
    const std::string props_min = "device.description=" + label + " node.description=" + label;

    auto wine_try_load_mic = [&](const std::string& pactl_cmd) -> bool {
        (void)win32_wine_bash_lc_exec(pactl_cmd);
        for (int k = 0; k < 14; ++k) {
            if (k > 0) {
                (void)win32_wine_bash_lc_exec("sleep 0.08");
            }
            sources = win32_wine_pactl_short_names("sources");
            if (vector_contains(sources, mic)) {
                return true;
            }
        }
        return false;
    };

    std::string cmd1 = "pactl load-module module-remap-source master=" + mon + " source_name=" + mic + " source_properties='" +
                       props_full + "'";
    if (wine_try_load_mic(cmd1)) {
        return;
    }
    std::string cmd2 = "pactl load-module module-remap-source master=" + mon + " source_name=" + mic + " source_properties='" +
                       props_min + "'";
    if (wine_try_load_mic(cmd2)) {
        return;
    }
    std::string cmd3 = "pactl load-module module-virtual-source master=" + mon + " source_name=" + mic + " source_properties='" +
                       props_full + "'";
    (void)wine_try_load_mic(cmd3);
}

static int g_wine_speaker_monitor_loopback_module = -1;
static std::string g_wine_speaker_monitor_loopback_sink_base;

static bool pulse_loopback_name_token_ok_wine(const std::string& s) {
    for (char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) == 0 && c != '_' && c != '-' && c != '.') {
            return false;
        }
    }
    return !s.empty();
}

void lv_win32_wine_sync_speaker_monitor_loopback_impl(bool want_monitor, const char* pulse_sink_base) {
    if (!lv_win32_is_wine_host() || !win32_wine_pactl_cli_available()) {
        return;
    }
    if (!want_monitor || pulse_sink_base == nullptr || pulse_sink_base[0] == '\0') {
        if (g_wine_speaker_monitor_loopback_module >= 0) {
            (void)win32_wine_bash_lc_exec("pactl unload-module " + std::to_string(g_wine_speaker_monitor_loopback_module));
            g_wine_speaker_monitor_loopback_module = -1;
            g_wine_speaker_monitor_loopback_sink_base.clear();
        }
        return;
    }
    const std::string want_base(pulse_sink_base);
    if (g_wine_speaker_monitor_loopback_module >= 0 && g_wine_speaker_monitor_loopback_sink_base == want_base) {
        return;
    }
    if (g_wine_speaker_monitor_loopback_module >= 0) {
        (void)win32_wine_bash_lc_exec("pactl unload-module " + std::to_string(g_wine_speaker_monitor_loopback_module));
        g_wine_speaker_monitor_loopback_module = -1;
        g_wine_speaker_monitor_loopback_sink_base.clear();
    }
    const std::string src = want_base + ".monitor";
    if (!pulse_loopback_name_token_ok_wine(src)) {
        return;
    }
    std::vector<std::string> sources = win32_wine_pactl_short_names("sources");
    if (!vector_contains(sources, src)) {
        return;
    }
    const std::string cap = win32_wine_bash_lc_slurp("pactl load-module module-loopback source=" + src +
                                                     " sink=@DEFAULT_SINK@ latency_msec=40");
    long mod = 0;
    {
        std::istringstream iss(cap);
        std::string line;
        while (std::getline(iss, line)) {
            line = trim_ws(line);
            if (line.empty()) {
                continue;
            }
            bool all_digit = true;
            for (char c : line) {
                if (!std::isdigit(static_cast<unsigned char>(c))) {
                    all_digit = false;
                    break;
                }
            }
            if (all_digit) {
                mod = std::strtol(line.c_str(), nullptr, 10);
                break;
            }
        }
    }
    if (mod > 0 && mod < 2147483647L) {
        g_wine_speaker_monitor_loopback_module = static_cast<int>(mod);
        g_wine_speaker_monitor_loopback_sink_base = want_base;
    }
}

/**
 * Wine: mute/unmute all host sink-inputs that look like this app's playback (PipeWire labels often
 * ``LiveVocoder.exe [audio stream…]``). Used when not on the null-sink virt route (Monitor off).
 */
static void win32_wine_pulse_set_livevocoder_sink_inputs_muted_impl(bool muted) {
    if (!lv_win32_is_wine_host() || !win32_wine_pactl_cli_available()) {
        return;
    }
    const std::string flag = muted ? "1" : "0";
    const std::string inner =
        "pactl list sink-inputs | awk "
        "'/^Sink Input #[0-9]+/{if(s!=\"\"){if(hit)print s}"
        "sub(/^Sink Input #/,\"\",$0);sub(/[^0-9].*/,\"\",$0);s=$0;hit=0;next}"
        "/application\\.name = /&&(/[Ll]ive[Vv]ocoder|[Ll]ive [Vv]ocoder|[Pp]ort[Aa]udio|\\[audio stream/"
        "){hit=1;next}"
        "/media\\.name = /&&(/[Ll]ive[Vv]ocoder|[Ll]ive [Vv]ocoder|[Vv]ocoder/){hit=1;next}"
        "END{if(s!=\"\"){if(hit)print s}}' | while read -r sid; do pactl set-sink-input-mute \"$sid\" " +
        flag + " || true; done";
    (void)win32_wine_bash_lc_exec(inner);
}

/**
 * Wine's pulse driver often ignores PULSE_SINK set from the PE after startup; host launcher env works.
 * After PortAudio opens "Speakers (PulseAudio Output)", move **every** matching playback sink-input to the null
 * sink (PipeWire can expose multiple nodes; omitting one leaves audio on the default headphones sink).
 */
static void lv_win32_wine_move_livevocoder_sink_input_to_pulse_sink_impl(const char* pulse_sink_name) {
    if (!lv_win32_is_wine_host() || pulse_sink_name == nullptr || pulse_sink_name[0] == '\0') {
        return;
    }
    if (!win32_wine_pactl_cli_available()) {
        return;
    }
    const std::string sk = sanitize_pulse_sink_token(pulse_sink_name);
    if (sk.empty()) {
        return;
    }
    const std::string qsk = sh_single_quote_bash(sk);
    for (int attempt = 0; attempt < 48; ++attempt) {
        Sleep(150);
        const std::string inner =
            std::string("sn=") + qsk +
            ";ts=$(pactl list short sinks | awk -v w=\\\"$sn\\\" 'BEGIN{FS=\"\t\"} "
            "{gsub(/^[ \\t]+|[ \\t]+$/,\"\",$2);if($2==w){print $1;exit}}');"
            "test -n \"$ts\"||exit 0;"
            "pactl list sink-inputs | awk -v tsid=\"$ts\" "
            "'/^Sink Input #[0-9]+/{if(s!=\"\"){if(hit&&sk!=\"\"&&sk!=tsid)print s}"
            "sub(/^Sink Input #/,\"\",$0);sub(/[^0-9].*/,\"\",$0);s=$0;sk=\"\";hit=0;next}"
            "/^[ \\t]*Sink:/{line=$0;sub(/^[ \\t]*Sink:[ \\t]*/,\"\",line);sub(/[^0-9].*/,\"\",line);sk=line;next}"
            "/application\\.name = /&&(/[Ll]ive[Vv]ocoder|[Ll]ive [Vv]ocoder|[Pp]ort[Aa]udio|\\[audio stream|"
            "MMDev|[Ww]asapi|[Pp]ulseAudio|alsa\\.plug/){hit=1;next}"
            "/media\\.name = /&&(/[Ll]ive[Vv]ocoder|[Ll]ive [Vv]ocoder|[Vv]ocoder/){hit=1;next}"
            "END{if(s!=\"\"){if(hit&&sk!=\"\"&&sk!=tsid)print s}}' | while read -r mov; do pactl move-sink-input "
            "\"$mov\" \"$ts\" || true; done";
        (void)win32_wine_bash_lc_exec(inner);
    }
}

/**
 * MinGW CRT getenv("PULSE_SINK") can stay empty after lv_win32_assign_env_for_crt_and_os; the Win32 process env
 * still has the value. Re-read PULSE_SINK from the OS and fall back to pactl like lv_apply_pulse_sink_env.
 */
static std::string lv_win32_wine_resolve_pulse_sink_base_name() {
    std::string raw;
    if (!g_lv_win32_applied_pulse_sink_for_move.empty()) {
        raw = g_lv_win32_applied_pulse_sink_for_move;
    } else if (const char* ps = std::getenv("PULSE_SINK"); ps != nullptr && ps[0] != '\0') {
        raw = ps;
    } else if (const char* lv = std::getenv("LIVE_VOCODER_PULSE_SINK"); lv != nullptr && lv[0] != '\0') {
        raw = lv;
    } else {
        char buf[512];
        const DWORD n = GetEnvironmentVariableA("PULSE_SINK", buf, static_cast<DWORD>(sizeof(buf)));
        if (n > 0U && n < static_cast<DWORD>(sizeof(buf))) {
            raw.assign(buf, static_cast<std::size_t>(n));
        }
    }
    if (raw.empty()) {
        char buf[512];
        const DWORD n = GetEnvironmentVariableA("LIVE_VOCODER_PULSE_SINK", buf, static_cast<DWORD>(sizeof(buf)));
        if (n > 0U && n < static_cast<DWORD>(sizeof(buf))) {
            raw.assign(buf, static_cast<std::size_t>(n));
        }
    }
    std::string sn = sanitize_pulse_sink_token(raw.c_str());
    if (sn.empty() && win32_wine_pactl_cli_available()) {
        std::vector<std::string> sinks = win32_wine_pactl_short_names("sinks");
        sn = resolve_sink_name(sinks);
    }
    return sn;
}

static void lv_win32_wine_move_livevocoder_sink_input_after_pa_start_impl() {
    if (!lv_win32_is_wine_host()) {
        return;
    }
    const std::string sn = lv_win32_wine_resolve_pulse_sink_base_name();
    if (!sn.empty()) {
        lv_win32_wine_move_livevocoder_sink_input_to_pulse_sink_impl(sn.c_str());
    }
}

#endif  // __linux__ / _WIN32 wine section

#if defined(__linux__)
std::string lv_monitor_pulse_sink_base_resolve_for_export() {
    if (!pactl_cli_available()) {
        return {};
    }
    std::vector<std::string> sinks = pactl_short_names("sinks");
    const std::string sn = resolve_sink_name(sinks);
    if (!vector_contains(sinks, sn)) {
        return {};
    }
    return sn;
}
#endif

}  // namespace

#if defined(__linux__)
namespace {

std::string lv_export_sh_single_quote_bash(const std::string& s) {
    std::string o = "'";
    for (char c : s) {
        if (c == '\'') {
            o += "'\\''";
        } else {
            o += c;
        }
    }
    o += '\'';
    return o;
}

}  // namespace
#endif

std::string lv_linux_monitor_pulse_sink_base_for_loopback() {
#if !defined(__linux__)
    return {};
#else
    return lv_monitor_pulse_sink_base_resolve_for_export();
#endif
}

std::string lv_wine_monitor_pulse_sink_base_for_loopback() {
#if defined(_WIN32)
    if (!lv_win32_is_wine_host() || !win32_wine_pactl_cli_available()) {
        return {};
    }
    std::string sn = lv_win32_wine_resolve_pulse_sink_base_name();
    if (sn.empty()) {
        return {};
    }
    std::vector<std::string> sinks = win32_wine_pactl_short_names("sinks");
    if (!vector_contains(sinks, sn)) {
        return {};
    }
    return sn;
#else
    return {};
#endif
}

void lv_linux_pulse_set_own_playback_muted(bool muted) {
#if !defined(__linux__)
    (void)muted;
    return;
#else
    if (std::system("command -v pactl >/dev/null 2>&1") != 0) {
        return;
    }
    const long pid = static_cast<long>(getpid());
    const std::string flag = muted ? "1" : "0";
    const std::string inner =
        std::string("export PATH=/usr/bin:/bin:$PATH\n") + "LP=" + std::to_string(pid) + "\n" +
        "in=$(pactl list sink-inputs 2>/dev/null | awk -v p=\"$LP\" '\n"
        "/^Sink Input #/ {\n"
        "  if (sid != \"\" && (gotpid || gotname)) { print sid; exit }\n"
        "  line=$0; sub(/^Sink Input #/, \"\", line); sub(/[^0-9].*/, \"\", line); sid=line\n"
        "  gotpid=0; gotname=0\n"
        "  next\n"
        "}\n"
        "index($0, \"application.process.id\") > 0 {\n"
        "  line=$0; sub(/^[^=]*=[[:space:]]*/, \"\", line); gsub(/\"/, \"\", line)\n"
        "  if (line == p) gotpid=1\n"
        "  next\n"
        "}\n"
        "index($0, \"application.name\") > 0 && "
        "$0 ~ /LiveVocoder|livevocoder|Live Vocoder|PortAudio|portaudio|\\[audio stream/ {\n"
        "  gotname=1\n"
        "  next\n"
        "}\n"
        "index($0, \"media.name\") > 0 && "
        "$0 ~ /LiveVocoder|livevocoder|Live Vocoder|[Vv]ocoder/ {\n"
        "  gotname=1\n"
        "  next\n"
        "}\n"
        "END {\n"
        "  if (sid != \"\" && (gotpid || gotname)) print sid\n"
        "}\n"
        "' )\n"
        "[ -n \"$in\" ] && pactl set-sink-input-mute \"$in\" " +
        flag + " 2>/dev/null || true\n";
    (void)std::system(("bash -lc " + lv_export_sh_single_quote_bash(inner)).c_str());
#endif
}

void lv_linux_ensure_pipewire_virt_mic_stack() {
#if defined(__linux__)
    lv_ensure_pipewire_virt_mic_stack_impl();
#elif defined(_WIN32)
    lv_win32_wine_ensure_pipewire_virt_mic_stack_impl();
#endif
}

void lv_apply_pulse_sink_env_before_portaudio() {
#if defined(__linux__)
    if (std::getenv("PULSE_SINK") != nullptr && std::getenv("PULSE_SINK")[0] != '\0') {
        return;
    }
    const char* lv = std::getenv("LIVE_VOCODER_PULSE_SINK");
    if (lv != nullptr && lv[0] != '\0') {
        std::string t = sanitize_pulse_sink_token(lv);
        if (!t.empty()) {
            (void)setenv("PULSE_SINK", t.c_str(), 1);
        }
        return;
    }
    if (!pactl_cli_available()) {
        return;
    }
    std::vector<std::string> sinks = pactl_short_names("sinks");
    const std::string sn = resolve_sink_name(sinks);
    if (vector_contains(sinks, sn)) {
        (void)setenv("PULSE_SINK", sn.c_str(), 1);
    }
#elif defined(_WIN32)
    char psbuf[256];
    const DWORD ps_len = GetEnvironmentVariableA("PULSE_SINK", psbuf, static_cast<DWORD>(sizeof(psbuf)));
    if (ps_len > 0U && ps_len < static_cast<DWORD>(sizeof(psbuf))) {
        psbuf[ps_len] = '\0';
        // Wine: MinGW getenv can miss values only in the Win32 block unless CRT is synced (same as native).
        lv_win32_assign_env_for_crt_and_os("PULSE_SINK", psbuf);
        return;
    }
    const char* lv = std::getenv("LIVE_VOCODER_PULSE_SINK");
    if (lv != nullptr && lv[0] != '\0') {
        std::string t = sanitize_pulse_sink_token(lv);
        if (!t.empty()) {
            lv_win32_assign_env_for_crt_and_os("PULSE_SINK", t.c_str());
        }
        return;
    }
    if (!win32_wine_pactl_cli_available()) {
        return;
    }
    std::vector<std::string> sinks = win32_wine_pactl_short_names("sinks");
    const std::string sn = resolve_sink_name(sinks);
    if (vector_contains(sinks, sn)) {
        lv_win32_assign_env_for_crt_and_os("PULSE_SINK", sn.c_str());
    }
#endif
}

void lv_linux_sync_speaker_monitor_loopback(bool want_monitor, const char* pulse_sink_base) {
#if defined(__linux__)
    lv_linux_sync_speaker_monitor_loopback_impl(want_monitor, pulse_sink_base);
#elif defined(_WIN32)
    lv_win32_wine_sync_speaker_monitor_loopback_impl(want_monitor, pulse_sink_base);
#else
    (void)want_monitor;
    (void)pulse_sink_base;
#endif
}

void lv_linux_wine_move_livevocoder_sink_input_to_pulse_sink(const char* pulse_sink_name) {
#if defined(_WIN32)
    lv_win32_wine_move_livevocoder_sink_input_to_pulse_sink_impl(pulse_sink_name);
#else
    (void)pulse_sink_name;
#endif
}

void lv_linux_wine_move_livevocoder_sink_input_after_pa_start() {
#if defined(_WIN32)
    lv_win32_wine_move_livevocoder_sink_input_after_pa_start_impl();
#endif
}

void lv_linux_move_livevocoder_sink_input_after_pa_start() {
#if !defined(__linux__)
    return;
#else
    if (!pactl_cli_available()) {
        return;
    }
    std::string sn;
    if (const char* ps = std::getenv("PULSE_SINK"); ps != nullptr && ps[0] != '\0') {
        sn = sanitize_pulse_sink_token(ps);
    }
    if (sn.empty()) {
        if (const char* lv = std::getenv("LIVE_VOCODER_PULSE_SINK"); lv != nullptr && lv[0] != '\0') {
            sn = sanitize_pulse_sink_token(lv);
        }
    }
    if (sn.empty()) {
        std::vector<std::string> sinks = pactl_short_names("sinks");
        sn = resolve_sink_name(sinks);
    }
    if (sn.empty()) {
        return;
    }
    {
        std::vector<std::string> sinks = pactl_short_names("sinks");
        if (!vector_contains(sinks, sn)) {
            return;
        }
    }
    const std::string qsk = lv_export_sh_single_quote_bash(sn);
    for (int attempt = 0; attempt < 32; ++attempt) {
        usleep(110000);
        const std::string inner =
            std::string("export PATH=/usr/bin:/bin:$PATH\nsn=") + qsk +
            ";ts=$(pactl list short sinks | awk -v w=\"$sn\" 'BEGIN{FS=\"\t\"} "
            "{gsub(/^[ \\t]+|[ \\t]+$/,\"\",$2);if($2==w){print $1;exit}}');"
            "test -n \"$ts\"||exit 0;"
            "pactl list sink-inputs | awk -v tsid=\"$ts\" "
            "'/^Sink Input #[0-9]+/{if(s!=\"\"){if(hit&&sk!=\"\"&&sk!=tsid)print s}"
            "sub(/^Sink Input #/,\"\",$0);sub(/[^0-9].*/,\"\",$0);s=$0;sk=\"\";hit=0;next}"
            "/^[ \\t]*Sink:/{line=$0;sub(/^[ \\t]*Sink:[ \\t]*/,\"\",line);sub(/[^0-9].*/,\"\",line);sk=line;next}"
            "/application\\.name = /&&(/[Ll]ive[Vv]ocoder|[Ll]ive [Vv]ocoder|[Pp]ort[Aa]udio|\\[audio stream|"
            "MMDev|[Ww]asapi|[Pp]ulseAudio|alsa\\.plug/){hit=1;next}"
            "/media\\.name = /&&(/[Ll]ive[Vv]ocoder|[Ll]ive [Vv]ocoder|[Vv]ocoder/){hit=1;next}"
            "END{if(s!=\"\"){if(hit&&sk!=\"\"&&sk!=tsid)print s}}' "
            "| while read -r mov; do pactl move-sink-input \"$mov\" \"$ts\" || true; done";
        (void)std::system(("bash -lc " + lv_export_sh_single_quote_bash(inner)).c_str());
    }
#endif
}

void lv_win32_wine_pulse_sync_monitor_mute(bool streaming, bool monitor_on, bool virt_output) {
#if defined(_WIN32)
    if (!lv_win32_is_wine_host() || !win32_wine_pactl_cli_available()) {
        return;
    }
    if (virt_output || !streaming || monitor_on) {
        win32_wine_pulse_set_livevocoder_sink_inputs_muted_impl(false);
    } else {
        win32_wine_pulse_set_livevocoder_sink_inputs_muted_impl(true);
    }
#else
    (void)streaming;
    (void)monitor_on;
    (void)virt_output;
#endif
}

std::string lv_linux_pulse_virt_mic_status_line() {
#if defined(__linux__)
    std::vector<std::string> sinks = pactl_short_names("sinks");
    if (sinks.empty() && slurp_popen_cmd("pactl list short sinks").empty()) {
        return "PipeWire: pactl unavailable — install pipewire-pulse; optional LIVE_VOCODER_PULSE_SINK for null sink.";
    }
    const std::string sn = resolve_sink_name(sinks);
    const std::string mic = sn + "_mic";
    const std::string mon = sn + ".monitor";

    std::vector<std::string> sources = pactl_short_names("sources");
    const bool has_sink = vector_contains(sinks, sn);
    const bool has_mic = vector_contains(sources, mic);
    const bool has_mon = vector_contains(sources, mon);

    if (!has_sink && sinks_matching_live_vocoder(sinks).empty()) {
        return "PipeWire: no live_vocoder* sink — create null sink or set LIVE_VOCODER_PULSE_SINK / "
               "LIVE_VOCODER_PA_OUTPUT=Null.";
    }
    if (has_mic) {
        return "PipeWire: virtual input `" + mic + "` present — pick it explicitly for recording apps if needed. "
               "Playback uses PULSE_SINK when set.";
    }
    if (has_mon) {
        return "PipeWire: monitor `" + mon + "` OK (no *_mic). "
               "Output: LIVE_VOCODER_PULSE_SINK=" + sn + " or LIVE_VOCODER_PA_OUTPUT.";
    }
    return "PipeWire: sink `" + sn + "` present but no `" + mic + "` / `" + mon +
           "` yet — open Python GTK once or: pactl load-module module-remap-source …";
#elif defined(_WIN32)
    if (!lv_win32_is_wine_host() || !win32_wine_pactl_cli_available()) {
        return {};
    }
    std::vector<std::string> sinks = win32_wine_pactl_short_names("sinks");
    if (sinks.empty()) {
        return "PipeWire (Wine): could not read sink list from host pactl (transient Wine I/O); "
               "set LIVE_VOCODER_PULSE_SINK or retry. LIVE_VOCODER_WINE_BASH if bash is nonstandard.";
    }
    const std::string sn = resolve_sink_name(sinks);
    const std::string mic = sn + "_mic";
    const std::string mon = sn + ".monitor";
    std::vector<std::string> sources = win32_wine_pactl_short_names("sources");
    const bool has_sink = vector_contains(sinks, sn);
    const bool has_mic = vector_contains(sources, mic);
    const bool has_mon = vector_contains(sources, mon);
    if (!has_sink && sinks_matching_live_vocoder(sinks).empty()) {
        return "PipeWire (Wine): no live_vocoder* sink — auto-setup may have failed; set LIVE_VOCODER_PULSE_SINK.";
    }
    if (has_mic) {
        return "PipeWire (Wine): virtual input `" + mic + "` — select explicitly in recording software if needed. "
               "PULSE_SINK set for Wine.";
    }
    if (has_mon) {
        return "PipeWire (Wine): monitor `" + mon + "` OK — use Monitor of sink or remapped *_mic.";
    }
    return "PipeWire (Wine): sink `" + sn + "` — waiting for *_mic; check pactl / PipeWire.";
#else
    return {};
#endif
}
