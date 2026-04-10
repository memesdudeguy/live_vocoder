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
#include <sys/stat.h>
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

std::vector<std::string> sinks_matching_live_vocoder(const std::vector<std::string>& sinks) {
    std::vector<std::string> out;
    for (const auto& s : sinks) {
        if (ci_substr(s, "live_vocoder")) {
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
        // One virtual mic stack: prefer the canonical sink name. Sorted order puts `live_vocoder` before
        // `live_vocoder2` / other suffixes — do not pick `back()` (that favored duplicates like live_vocoder2).
        return cands.front();
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
 * PipeWire/Pulse: drop stacked null sinks (live_vocoder2, …) and all remap/virtual mics for live_vocoder_mic so
 * ensure can recreate a single stack. Safe at startup; avoids duplicate KDE "LiveVocoder" / VirtualMic rows when
 * pactl list short was intermittently empty under Wine (each run loaded another null-sink).
 */
static std::string lv_live_vocoder_pipewire_purge_script_body() {
    // Tear down in dependency order: loopbacks and remaps reference live_vocoder.monitor; then drop all
    // matching null sinks (including canonical live_vocoder) so ensure recreates a single stack. Module
    // lines vary across PipeWire versions; match substrings on the full pactl row, not only sink_name=.
    return std::string("export PATH=/usr/bin:/bin:$PATH\n") +
           "pactl list modules short 2>/dev/null | awk '$2 ~ /loopback/ && index($0,\"live_vocoder.monitor\"){print $1}' | "
           "while read -r id; do [ -n \"$id\" ] && pactl unload-module \"$id\" 2>/dev/null || true; done\n"
           "pactl list modules short 2>/dev/null | awk "
           "'($2==\"module-remap-source\"||$2==\"module-virtual-source\")&& "
           "(/live_vocoder_mic/||/LiveVocoderVirtualMic/){print $1}' | "
           "while read -r id; do [ -n \"$id\" ] && pactl unload-module \"$id\" 2>/dev/null || true; done\n"
           "pactl list modules short 2>/dev/null | awk "
           "'$2==\"module-null-sink\" && (/live_vocoder/||/LiveVocoder/){print $1}' | "
           "while read -r id; do [ -n \"$id\" ] && pactl unload-module \"$id\" 2>/dev/null || true; done\n"
           "sleep 0.12\n";
}

#if defined(__linux__)

bool pactl_cli_available() {
    return std::system("command -v pactl >/dev/null 2>&1") == 0;
}

static void lv_linux_run_purge_duplicate_live_vocoder_devices() {
    if (!pactl_cli_available()) {
        return;
    }
    const std::string path =
        std::string("/tmp/lv_livevocoder_purge_") + std::to_string(static_cast<long long>(getpid())) + ".sh";
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) {
        return;
    }
    const std::string full = std::string("#!/usr/bin/env bash\n") + lv_live_vocoder_pipewire_purge_script_body() + "\n";
    (void)std::fwrite(full.data(), 1, full.size(), f);
    std::fclose(f);
    (void)chmod(path.c_str(), 0700);
    (void)std::system(("bash " + path + " 2>/dev/null").c_str());
    (void)std::remove(path.c_str());
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
    lv_linux_run_purge_duplicate_live_vocoder_devices();
    std::vector<std::string> sinks = pactl_short_names("sinks");
    for (int retry = 0; retry < 5 && sinks.empty(); ++retry) {
        if (retry > 0) {
            usleep(150000);
        }
        sinks = pactl_short_names("sinks");
    }
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
        // Nuclear option: when turning Monitor OFF, kill EVERY loopback module on the system.
        // This guarantees speakers are muted. The virtual mic (remap-source) is left alone.
        std::system("pactl list modules short 2>/dev/null | awk '$2 ~ /loopback/ {print $1}' | xargs -r pactl unload-module 2>/dev/null || true");
        g_speaker_monitor_loopback_module = -1;
        g_speaker_monitor_loopback_sink_base.clear();
        return;
    }
    const std::string want_base(pulse_sink_base);
    const std::string src = want_base + ".monitor";
    if (!pulse_loopback_name_token_ok(src)) {
        return;
    }
    // Always drop every loopback tied to this monitor source, then load one. Stale g_speaker_* or failed
    // unloads used to leave 2–3 module-loopback rows and could wedge PipeWire / KDE audio.
    {
        const std::string esc = sh_single_quote_bash("source=" + src);
        const std::string unload =
            "pactl list modules short 2>/dev/null | awk -v s=" + esc + " '$2 ~ /loopback/ && index($0,s){print $1}' | "
            "xargs -r pactl unload-module 2>/dev/null || true";
        (void)std::system(("bash -lc " + sh_single_quote_bash(unload)).c_str());
    }
    g_speaker_monitor_loopback_module = -1;
    g_speaker_monitor_loopback_sink_base.clear();
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
        Sleep(80);
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

/**
 * Run a multi-line bash script on the Linux host. Uses a temp file so awk $1 / while read $id are not broken by
 * bash -lc "…" outer double-quote expansion (which was preventing pactl unload-module from ever running).
 */
static void win32_wine_run_host_script(const std::string& body) {
    if (!lv_win32_is_wine_host()) {
        return;
    }
    char tdir[MAX_PATH];
    if (GetTempPathA(static_cast<DWORD>(sizeof(tdir)), tdir) == 0U) {
        return;
    }
    const std::string win_path = std::string(tdir) + "lv_lv_host_" + std::to_string(GetCurrentProcessId()) + "_" +
                                 std::to_string(GetTickCount()) + ".sh";
    FILE* f = std::fopen(win_path.c_str(), "wb");
    if (f == nullptr) {
        return;
    }
    const std::string full = std::string("#!/usr/bin/env bash\n") + body + "\n";
    (void)std::fwrite(full.data(), 1, full.size(), f);
    std::fclose(f);
    const std::string inner =
        "f=$(winepath -u " + sh_single_quote_bash(win_path) + "); chmod +x \"$f\" 2>/dev/null; exec bash \"$f\"";
    (void)win32_wine_bash_lc_exec(inner);
    (void)DeleteFileA(win32_win32_path_to_fwd_slashes(win_path).c_str());
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
    win32_wine_run_host_script(lv_live_vocoder_pipewire_purge_script_body());
    (void)win32_wine_bash_lc_exec("sleep 0.08");
    std::vector<std::string> sinks = win32_wine_pactl_short_names("sinks");
    for (int retry = 0; retry < 6 && sinks.empty(); ++retry) {
        if (retry > 0) {
            (void)win32_wine_bash_lc_exec("sleep 0.15");
        }
        sinks = win32_wine_pactl_short_names("sinks");
    }
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

static void wine_unload_loopback_modules_by_source(const std::string& source_name) {
    if (source_name.empty()) {
        return;
    }
    if (!pulse_loopback_name_token_ok_wine(source_name)) {
        return;
    }
    const std::string sq = sh_single_quote_bash(source_name);
    const std::string body =
        std::string("export PATH=/usr/bin:/bin:$PATH\n") + "SN=" + sq + "\n" +
        "pactl list modules short 2>/dev/null | awk -v n=\"source=${SN}\" "
        "'$2 ~ /loopback/ && index($0, n) {print $1}' | "
        "while read -r id; do [ -n \"$id\" ] && pactl unload-module \"$id\" 2>/dev/null || true; done\n";
    win32_wine_run_host_script(body);
}

/** Resolve null-sink name for host pactl (Wine: CRT getenv can lag; match move-sink-input logic). */
static std::string win32_wine_resolve_null_sink_name_for_host() {
    if (!g_lv_win32_applied_pulse_sink_for_move.empty()) {
        std::string t = sanitize_pulse_sink_token(g_lv_win32_applied_pulse_sink_for_move.c_str());
        if (!t.empty()) {
            return t;
        }
    }
    const char* p = std::getenv("PULSE_SINK");
    if (p != nullptr && p[0] != '\0') {
        std::string t = sanitize_pulse_sink_token(p);
        if (!t.empty()) {
            return t;
        }
    }
    const char* lv = std::getenv("LIVE_VOCODER_PULSE_SINK");
    if (lv != nullptr && lv[0] != '\0') {
        std::string t = sanitize_pulse_sink_token(lv);
        if (!t.empty()) {
            return t;
        }
    }
    char buf[512];
    DWORD n = GetEnvironmentVariableA("PULSE_SINK", buf, static_cast<DWORD>(sizeof(buf)));
    if (n > 0U && n < sizeof(buf)) {
        std::string t = sanitize_pulse_sink_token(std::string(buf, static_cast<std::size_t>(n)).c_str());
        if (!t.empty()) {
            return t;
        }
    }
    n = GetEnvironmentVariableA("LIVE_VOCODER_PULSE_SINK", buf, static_cast<DWORD>(sizeof(buf)));
    if (n > 0U && n < sizeof(buf)) {
        std::string t = sanitize_pulse_sink_token(std::string(buf, static_cast<std::size_t>(n)).c_str());
        if (!t.empty()) {
            return t;
        }
    }
    return "live_vocoder";
}

/**
 * One host bash script (temp file): unload every PipeWire/Pulse loopback module, then retry moving
 * LiveVocoder.exe's sink-input onto the null sink. Monitor-off must do both — loopback alone leaves wet audio
 * on @DEFAULT_SINK@ when Wine never moved the stream off the physical device.
 */
static void wine_host_unload_all_loopbacks_script_only() {
    if (!lv_win32_is_wine_host() || !win32_wine_pactl_cli_available()) {
        return;
    }
    const std::string body =
        std::string("export PATH=/usr/bin:/bin:$PATH\n") +
        "pactl list modules short 2>/dev/null | awk '$2 ~ /loopback/ {print $1}' | "
        "while read -r id; do [ -n \"$id\" ] && pactl unload-module \"$id\" 2>/dev/null || true; done\n";
    win32_wine_run_host_script(body);
}

static void wine_host_mute_monitor_on_default_speakers(const std::string& sink_name) {
    if (!lv_win32_is_wine_host() || !win32_wine_pactl_cli_available()) {
        return;
    }
    std::string sk = sanitize_pulse_sink_token(sink_name.c_str());
    if (sk.empty()) {
        sk = "live_vocoder";
    }
    const std::string q = sh_single_quote_bash(sk);
    const std::string body =
        std::string("export PATH=/usr/bin:/bin:$PATH\n") + "SN=" + q + "\n" +
        "pactl list modules short 2>/dev/null | awk '$2 ~ /loopback/ {print $1}' | "
        "while read -r id; do [ -n \"$id\" ] && pactl unload-module \"$id\" 2>/dev/null || true; done\n"
        "for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25; do\n"
        "  sid=$(pactl list short sinks 2>/dev/null | awk -v w=\"$SN\" 'BEGIN{FS=\"\t\"} "
        "{gsub(/^[ \\t]+|[ \\t]+$/,\"\",$2);if($2==w){print $1;exit}}')\n"
        "  id=$(pactl list sink-inputs 2>/dev/null | awk '/^Sink Input #[0-9]+/{sub(/^Sink Input #/,\"\",$0);"
        "gsub(/[^0-9].*/,\"\",$0);sid=$0}"
        "(index(tolower($0),\"application.name\")>0 || index($0,\"application.process.binary\")>0) && "
        "(index(tolower($0),\"livevocoder\")>0 || index(tolower($0),\"live vocoder\")>0 || "
        "index(tolower($0),\"portaudio\")>0){print sid;exit}')\n"
        "  if test -n \"$id\" && test -n \"$sid\"; then pactl move-sink-input \"$id\" \"$SN\" 2>/dev/null && break; fi\n"
        "  sleep 0.2\n"
        "done\n";
    win32_wine_run_host_script(body);
}

/**
 * Wine on Linux: align PipeWire sink-input mute flags with Monitor + null-sink routing.
 * When monitor is off but output targets the null sink, keep the null-sink stream unmuted (virtual mic) and mute
 * any duplicate/misrouted stream still on the default device — a case native Linux handles via PID match; Wine
 * streams need name/binary heuristics and per-sink logic.
 */
static void lv_win32_wine_pulse_sync_monitor_mute_impl(bool streaming, bool monitor_on, bool virt_output) {
    if (!lv_win32_is_wine_host() || !win32_wine_pactl_cli_available()) {
        return;
    }
    std::string sk = win32_wine_resolve_null_sink_name_for_host();
    if (sk.empty()) {
        sk = "live_vocoder";
    }
    if (!pulse_loopback_name_token_ok_wine(sk)) {
        return;
    }
    const std::string st = streaming ? "1" : "0";
    const std::string mo = monitor_on ? "1" : "0";
    const std::string vi = virt_output ? "1" : "0";
    const std::string body =
        std::string("export PATH=/usr/bin:/bin:$PATH\n") +
        "if [ -n \"$WINE_HOST_XDG_RUNTIME_DIR\" ]; then export XDG_RUNTIME_DIR=\"$WINE_HOST_XDG_RUNTIME_DIR\"; fi\n"
        "pactl list sink-inputs 2>/dev/null | awk -v STREAMING=" +
        st + " -v MON=" + mo + " -v VIRT=" + vi + " -v SN=" + sk + " '\n"
        "function domute(i, v) {\n"
        "  system(\"pactl set-sink-input-mute \" i \" \" v \" 2>/dev/null\")\n"
        "}\n"
        "/^Sink Input #/ {\n"
        "  if (id != \"\" && hit) {\n"
        "    if ((STREAMING+0)==0 || (MON+0)==1) domute(id, 0)\n"
        "    else if ((VIRT+0)==0) domute(id, 1)\n"
        "    else { if (on_null) domute(id, 0); else domute(id, 1) }\n"
        "  }\n"
        "  line=$0; sub(/^Sink Input #/, \"\", line); sub(/[^0-9].*/, \"\", line); id=line\n"
        "  hit=0; on_null=0\n"
        "  next\n"
        "}\n"
        "index($0, \"Sink:\") > 0 && index($0, \"Sink Input\") == 0 {\n"
        "  on_null = (index($0, SN) > 0)\n"
        "  next\n"
        "}\n"
        "{\n"
        "  l=tolower($0)\n"
        "  if (index($0, \"application.name\") || index($0, \"application.process.binary\") || "
        "index($0, \"media.name\")) {\n"
        "    if (index(l, \"livevocoder\") || index(l, \"live vocoder\") || index(l, \"portaudio\") || "
        "index(l, \"wineaudio\"))\n"
        "      hit=1\n"
        "  }\n"
        "}\n"
        "END {\n"
        "  if (id != \"\" && hit) {\n"
        "    if ((STREAMING+0)==0 || (MON+0)==1) domute(id, 0)\n"
        "    else if ((VIRT+0)==0) domute(id, 1)\n"
        "    else { if (on_null) domute(id, 0); else domute(id, 1) }\n"
        "  }\n"
        "}\n"
        "'\n";
    win32_wine_run_host_script(body);
}

static void lv_win32_wine_sync_speaker_monitor_loopback_impl(bool want_monitor, const char* pulse_sink_base,
                                                               bool wine_host_mute_move_retries) {
    if (!lv_win32_is_wine_host() || !win32_wine_pactl_cli_available()) {
        return;
    }
    if (!want_monitor || pulse_sink_base == nullptr || pulse_sink_base[0] == '\0') {
        if (wine_host_mute_move_retries) {
            wine_host_mute_monitor_on_default_speakers(win32_wine_resolve_null_sink_name_for_host());
        } else {
            wine_host_unload_all_loopbacks_script_only();
        }
        g_wine_speaker_monitor_loopback_module = -1;
        g_wine_speaker_monitor_loopback_sink_base.clear();
        return;
    }
    const std::string want_base(pulse_sink_base);
    if (!pulse_loopback_name_token_ok_wine(want_base)) {
        return;
    }
    const std::string src = want_base + ".monitor";
    if (!pulse_loopback_name_token_ok_wine(src)) {
        return;
    }
    // Idempotent: always clear loopbacks for this monitor source, then load one. The old
    // g_wine_speaker_monitor_loopback_sink_base==want_base early-return left stale duplicates on the host
    // (sync called before unload finished, or pactl list differed), which matches stacked 536870921/922/923.
    if (!g_wine_speaker_monitor_loopback_sink_base.empty() &&
        g_wine_speaker_monitor_loopback_sink_base != want_base) {
        wine_unload_loopback_modules_by_source(g_wine_speaker_monitor_loopback_sink_base + ".monitor");
    }
    wine_unload_loopback_modules_by_source(src);
    (void)win32_wine_bash_lc_exec("sleep 0.1");
    // Skip source-existence pre-check: win32_wine_pactl_short_names uses unreliable Wine stdout
    // capture and intermittently returns empty. Just attempt the load; pactl fails gracefully if
    // the source doesn't exist.
    (void)win32_wine_bash_lc_exec("pactl load-module module-loopback source=" + src +
                                  " sink=@DEFAULT_SINK@ latency_msec=40 2>/dev/null");
    g_wine_speaker_monitor_loopback_module = -1;
    g_wine_speaker_monitor_loopback_sink_base = want_base;
}

/**
 * Wine's pulse driver often ignores PULSE_SINK set from the PE after startup; host launcher env works.
 * After PortAudio opens "Speakers (PulseAudio Output)", move our sink-input to the null sink with pactl.
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
    // PipeWire: duplicate sink names break `pactl move-sink-input … <name>`; resolve numeric sink id in the same
    // bash snapshot as the sink-input id. Sink-input often appears ~1–2s after Pa_StartStream under Wine.
    for (int attempt = 0; attempt < 32; ++attempt) {
        Sleep(110);
        // bash -lc wraps this in double quotes — do not use $'\t' (ANSI-C quoting fails there). Use a literal tab in awk FS.
        const std::string inner =
            std::string("sn=") + qsk +
            ";sid=$(pactl list short sinks | awk -v w=\\\"$sn\\\" 'BEGIN{FS=\"\t\"} "
            "{gsub(/^[ \\t]+|[ \\t]+$/,\"\",$2);if($2==w)i=$1}END{print i}');"
            "id=$(pactl list sink-inputs | awk '/^Sink Input #[0-9]+/{sub(/^Sink Input #/,\"\",$0);"
            "gsub(/[^0-9].*/,\"\",$0);sid=$0}"
            "(index(tolower($0),\"application.name\")>0 || index($0,\"application.process.binary\")>0) && "
            "(index(tolower($0),\"livevocoder\")>0 || index(tolower($0),\"live vocoder\")>0 || "
            "index(tolower($0),\"portaudio\")>0){print sid;exit}');"
            "if test -n \"$id\" && test -n \"$sid\"; then pactl move-sink-input \"$id\" \"$sid\"; fi";
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
        "$0 ~ /LiveVocoder|livevocoder|Live Vocoder|PortAudio|portaudio/ {\n"
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
        g_lv_win32_applied_pulse_sink_for_move.assign(psbuf, static_cast<std::size_t>(ps_len));
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

void lv_linux_sync_speaker_monitor_loopback(bool want_monitor, const char* pulse_sink_base,
                                            bool wine_host_mute_move_retries) {
#if defined(__linux__)
    (void)wine_host_mute_move_retries;
    lv_linux_sync_speaker_monitor_loopback_impl(want_monitor, pulse_sink_base);
#elif defined(_WIN32)
    lv_win32_wine_sync_speaker_monitor_loopback_impl(want_monitor, pulse_sink_base, wine_host_mute_move_retries);
#else
    (void)want_monitor;
    (void)pulse_sink_base;
    (void)wine_host_mute_move_retries;
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

void lv_win32_wine_pulse_sync_monitor_mute(bool streaming, bool monitor_on, bool virt_output) {
#if defined(_WIN32)
    lv_win32_wine_pulse_sync_monitor_mute_impl(streaming, monitor_on, virt_output);
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
