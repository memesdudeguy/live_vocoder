#include "carrier_convert.hpp"

#include <cctype>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <filesystem>

bool carrier_path_is_raw_f32(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext == ".f32";
}

#if defined(_WIN32)
static int carrier_hex_nibble(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

static void carrier_percent_decode_inplace(std::string& s) {
    for (std::size_t i = 0; i + 2 < s.size();) {
        if (s[i] != '%') {
            ++i;
            continue;
        }
        const int hi = carrier_hex_nibble(s[i + 1]);
        const int lo = carrier_hex_nibble(s[i + 2]);
        if (hi < 0 || lo < 0) {
            ++i;
            continue;
        }
        const char c = static_cast<char>((hi << 4) | lo);
        s.replace(i, 3, 1, c);
    }
}

/**
 * True when running under Wine (Z: mapping, cmd.exe stderr quirks). Native Windows must not be misclassified:
 * some security/compat shims have been observed to export wine-like symbols without a real Wine host.
 * Require ntdll's wine_get_version plus a normal Wine marker (wine.dll or WINEPREFIX / WINELOADER).
 */
static bool carrier_win32_running_under_wine() {
    static int cached = 0;
    if (cached != 0) {
        return cached == 1;
    }
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) {
        cached = 2;
        return false;
    }
    if (GetProcAddress(ntdll, "wine_get_version") == nullptr) {
        cached = 2;
        return false;
    }
    if (GetModuleHandleW(L"wine.dll") != nullptr) {
        cached = 1;
        return true;
    }
    wchar_t envbuf[1024];
    if (GetEnvironmentVariableW(L"WINEPREFIX", envbuf, static_cast<DWORD>(sizeof(envbuf) / sizeof(envbuf[0]))) > 0) {
        cached = 1;
        return true;
    }
    if (GetEnvironmentVariableW(L"WINELOADER", envbuf, static_cast<DWORD>(sizeof(envbuf) / sizeof(envbuf[0]))) > 0) {
        cached = 1;
        return true;
    }
    cached = 2;
    return false;
}

/** Wine-specific error wording only when ``wine.dll`` is loaded (real Wine). Native can still export ``wine_get_version`` shims. */
static bool carrier_win32_wine_dll_loaded() {
    return GetModuleHandleW(L"wine.dll") != nullptr;
}

std::filesystem::path carrier_win32_localize_path_for_filesystem(const std::filesystem::path& raw) {
    std::string g = raw.generic_string();
    if (g.compare(0, 7, "file://") == 0) {
        g = g.substr(7);
        if (g.size() >= 3 && std::isalpha(static_cast<unsigned char>(g[0])) && g[1] == '|' && g[2] == '/') {
            g[1] = ':';
            g.erase(0, 2);
        }
    }
    carrier_percent_decode_inplace(g);
    if (g.size() >= 2 && std::isalpha(static_cast<unsigned char>(g[0])) && g[1] == ':') {
        return std::filesystem::path(g);
    }
    if (!carrier_win32_running_under_wine()) {
        return raw;
    }
    if (g.size() >= 2 && g[0] == 'Z' && g[1] == ':') {
        return std::filesystem::path(g);
    }
    if (g.size() >= 2 && g[0] == 'z' && g[1] == ':') {
        g[0] = 'Z';
        return std::filesystem::path(g);
    }
    if (!g.empty() && g[0] == '/' && !(g.size() > 1 && g[1] == '/')) {
        return std::filesystem::path(std::string("Z:") + g);
    }
    return std::filesystem::path(g);
}

std::filesystem::path carrier_win32_documents_folder() {
    PWSTR pw = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &pw);
    if (SUCCEEDED(hr) && pw != nullptr) {
        std::filesystem::path p(pw);
        CoTaskMemFree(pw);
        if (!p.empty()) {
            return p.lexically_normal();
        }
    }
    wchar_t buf[MAX_PATH];
    hr = SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, buf);
    if (SUCCEEDED(hr) && buf[0] != L'\0') {
        return std::filesystem::path(buf).lexically_normal();
    }
    return {};
}

/** ``LiveVocoder.exe`` directory + ``ffmpeg.exe`` (for Wine / portable bundles without PATH). */
static std::string lv_sibling_ffmpeg_exe() {
    std::wstring wbuf(512, L'\0');
    for (;;) {
        const DWORD cap = static_cast<DWORD>(wbuf.size());
        const DWORD n = GetModuleFileNameW(nullptr, wbuf.data(), cap);
        if (n == 0) {
            return {};
        }
        if (n < cap - 1) {
            wbuf.resize(n);
            break;
        }
        wbuf.resize(wbuf.size() * 2);
    }
    std::error_code ec;
    const std::filesystem::path exe(wbuf);
    const std::filesystem::path cand = exe.parent_path() / L"ffmpeg.exe";
    if (std::filesystem::is_regular_file(cand, ec)) {
        return cand.u8string();
    }
    return {};
}

static void lv_push_unique_ffmpeg_candidate(std::vector<std::string>& out, const std::string& s) {
    if (s.empty()) {
        return;
    }
    for (const auto& x : out) {
        if (x == s) {
            return;
        }
    }
    out.push_back(s);
}

/**
 * Wine maps drive Z: to the Linux root. Windows ffmpeg.exe often fails on paths like ``/home/...``;
 * ``Z:/home/...`` works. Leave ``C:\…``, relative paths, and UNC as-is.
 */
static std::string ffmpeg_path_arg_windows(const std::filesystem::path& p) {
    const std::filesystem::path loc = carrier_win32_localize_path_for_filesystem(p);
    return loc.u8string();
}

/** ``cmd.exe``-safe double quotes around an argument (internal ``"`` → ``\"`` for one cmd /c line). */
static std::string quote_cmd_exe_arg(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"') {
            o += "\\\"";
        } else {
            o += c;
        }
    }
    o += '"';
    return o;
}

static std::wstring carrier_win32_quote_cmd_arg_w(const std::wstring& s) {
    std::wstring o = L"\"";
    for (wchar_t c : s) {
        if (c == L'"') {
            o += L'\\';
        }
        o += c;
    }
    o += L'"';
    return o;
}

/** Resolve ffmpeg on PATH or verify an explicit path (native Windows; avoids cmd.exe ``2>`` / UTF-8 issues). */
static std::wstring carrier_win32_resolve_ffmpeg_exe_w(const std::string& cand_utf8) {
    std::error_code ec;
    const std::filesystem::path p = std::filesystem::u8path(cand_utf8).lexically_normal();
    if (std::filesystem::is_regular_file(p, ec)) {
        return p.wstring();
    }
    std::wstring stem = p.filename().wstring();
    if (stem.empty()) {
        stem = std::filesystem::u8path(cand_utf8).wstring();
    }
    wchar_t found[4096];
    wchar_t* fname_part = nullptr;
    const DWORD n = SearchPathW(nullptr, stem.c_str(), L".exe", static_cast<DWORD>(sizeof(found) / sizeof(found[0])),
                                found, &fname_part);
    if (n != 0 && n < sizeof(found) / sizeof(found[0])) {
        return std::wstring(found);
    }
    return {};
}

/**
 * Run ffmpeg with stderr redirected via an inherited handle (no cmd.exe redirection).
 * Returns exit status 0..255, or -998 spawn failure, or -999 I/O setup failure.
 */
static int carrier_win32_run_ffmpeg_createprocess(const std::wstring& ffmpeg_exe, const std::wstring& src,
                                                  const std::wstring& dst, int sample_rate,
                                                  const std::wstring& err_path_w) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE errf = CreateFileW(err_path_w.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (errf == INVALID_HANDLE_VALUE) {
        return -999;
    }
    HANDLE nul_in = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ, &sa, OPEN_EXISTING, 0, nullptr);
    HANDLE nul_out = CreateFileW(L"NUL", GENERIC_WRITE, 0, &sa, OPEN_EXISTING, 0, nullptr);
    if (nul_in == INVALID_HANDLE_VALUE || nul_out == INVALID_HANDLE_VALUE) {
        CloseHandle(errf);
        if (nul_in != INVALID_HANDLE_VALUE) {
            CloseHandle(nul_in);
        }
        if (nul_out != INVALID_HANDLE_VALUE) {
            CloseHandle(nul_out);
        }
        return -999;
    }
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul_in;
    si.hStdOutput = nul_out;
    si.hStdError = errf;
    const std::wstring ar = std::to_wstring(sample_rate);
    std::wstring cmd = carrier_win32_quote_cmd_arg_w(ffmpeg_exe) +
                       L" -y -nostdin -hide_banner -loglevel info -i " + carrier_win32_quote_cmd_arg_w(src) +
                       L" -f f32le -ac 1 -ar " + ar + L" " + carrier_win32_quote_cmd_arg_w(dst);
    std::vector<wchar_t> cmd_mut(cmd.begin(), cmd.end());
    cmd_mut.push_back(L'\0');
    std::wstring cwd_storage;
    const wchar_t* cwd_arg = nullptr;
    {
        const std::filesystem::path ff_parent = std::filesystem::path(ffmpeg_exe).parent_path();
        if (!ff_parent.empty()) {
            cwd_storage = ff_parent.wstring();
            cwd_arg = cwd_storage.c_str();
        }
    }
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(nullptr, cmd_mut.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                     cwd_arg, &si, &pi);
    if (!ok) {
        CloseHandle(errf);
        CloseHandle(nul_in);
        CloseHandle(nul_out);
        return -998;
    }
    // Wait before closing stdio handles: some Wine builds drop stderr if the parent closes ``errf`` immediately
    // after CreateProcess (child may not have duplicated the handle yet).
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    (void)GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(errf);
    CloseHandle(nul_in);
    CloseHandle(nul_out);
    if (code > 255u) {
        return 1;
    }
    return static_cast<int>(code);
}

/** MinGW / Wine often return child exit in the high byte (e.g. 256 for exit 1). */
static int system_normalized_exit(int r) {
    if (r == -1) {
        return -1;
    }
    if (r > 0 && (r & 0xff) == 0) {
        return (r >> 8) & 0xff;
    }
    return r & 0xff;
}

static std::string slurp_text_file_trunc(const std::filesystem::path& path, std::size_t max_chars) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return {};
    }
    std::ostringstream oss;
    char buf[512];
    std::size_t total = 0;
    while (f.read(buf, sizeof buf) || f.gcount() > 0) {
        const std::streamsize n = f.gcount();
        for (std::streamsize i = 0; i < n && total < max_chars; ++i) {
            const unsigned char c = static_cast<unsigned char>(buf[i]);
            if (c == '\r') {
                continue;
            }
            oss.put(static_cast<char>(c));
            ++total;
        }
        if (total >= max_chars) {
            break;
        }
    }
    std::string s = oss.str();
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) {
        s.pop_back();
    }
    return s;
}

/** CMD ``2>`` redirection is more reliable when the path is short/ASCII (8.3 if Windows provides it). */
static std::filesystem::path carrier_win32_short_path_for_cmd_redir(const std::filesystem::path& p) {
    std::wstring w = p.wstring();
    DWORD need = GetShortPathNameW(w.c_str(), nullptr, 0);
    if (need == 0) {
        return p;
    }
    std::vector<wchar_t> buf(need);
    DWORD n = GetShortPathNameW(w.c_str(), buf.data(), need);
    if (n == 0 || n >= need) {
        return p;
    }
    return std::filesystem::path(std::wstring(buf.data(), n));
}

/** Native Windows: pass shortened paths to ffmpeg inside cmd.exe (OneDrive / long paths). Wine: keep Z: layout. */
static std::string ffmpeg_path_for_win32_ffmpeg_cmd(const std::filesystem::path& p) {
    std::filesystem::path loc = carrier_win32_localize_path_for_filesystem(p);
    if (!carrier_win32_running_under_wine()) {
        loc = carrier_win32_short_path_for_cmd_redir(loc);
    }
    return loc.u8string();
}

static void carrier_win32_err_ffmpeg_not_run(std::string& err_out) {
    err_out =
        "could not run ffmpeg.exe. On Windows put ffmpeg.exe next to LiveVocoder.exe (installer does this), "
        "or add ffmpeg to PATH, or set LIVE_VOCODER_FFMPEG to the full path. ";
    if (carrier_win32_wine_dll_loaded()) {
        err_out += "Under Wine on Linux you cannot use the host /usr/bin/ffmpeg — use Windows ffmpeg.exe. ";
    }
    err_out += "You can also use a pre-made .f32 carrier.";
}

/** When stderr capture is empty: native Windows gets accurate hints; Wine keeps the old wording. */
static void carrier_win32_err_ffmpeg_failed_no_stderr(std::string& err_out) {
    if (carrier_win32_wine_dll_loaded()) {
        err_out +=
            " — unsupported format, unreadable path under Wine, or missing codec in ffmpeg.exe. "
            "Fix: put ffmpeg.exe next to LiveVocoder.exe (or set LIVE_VOCODER_FFMPEG), use Library… "
            "inside the app, or pre-convert on the host: "
            "ffmpeg -y -i track.wav -f f32le -ac 1 -ar 48000 carrier.f32 then drop the .f32.";
        return;
    }
    err_out +=
        " — unsupported format, DRM-protected media, or this ffmpeg.exe build is missing the decoder "
        "(try a full Windows build from gyan.dev or BtbN GitHub releases). "
        "If the file is in OneDrive, make sure it is downloaded (open once in Explorer, or Always keep on this device). "
        "The carriers folder existing only means ffmpeg can write the .f32 there; decoding still happens inside ffmpeg. "
        "Fix: set LIVE_VOCODER_FFMPEG to a fuller ffmpeg.exe, use WAV/FLAC, or pre-convert to .f32: "
        "ffmpeg -y -i track.wav -f f32le -ac 1 -ar 48000 carrier.f32.";
}

#endif

static bool carrier_extension_is_convertible_audio(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    static const char* const kAudio[] = {".mp3",  ".wav",  ".flac", ".ogg",  ".oga", ".opus", ".m4a", ".aac",
                                         ".wma",  ".aiff", ".aif",  ".caf", ".mpc", ".wv",  ".mp2", ".3gp"};
    for (const char* a : kAudio) {
        if (ext == a) {
            return true;
        }
    }
    return false;
}

void carrier_collect_library_entries(const std::filesystem::path& dir, std::vector<std::filesystem::path>& out) {
    out.clear();
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) {
        return;
    }
    for (const std::filesystem::directory_entry& ent : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) {
            break;
        }
        if (!ent.is_regular_file()) {
            continue;
        }
        const std::filesystem::path& p = ent.path();
        if (!carrier_path_is_raw_f32(p) && !carrier_extension_is_convertible_audio(p)) {
            continue;
        }
        out.push_back(p);
    }
    std::sort(out.begin(), out.end(), [](const std::filesystem::path& a, const std::filesystem::path& b) {
        return a.filename().string() < b.filename().string();
    });
}

void carrier_convert_audio_in_folder(int sample_rate, const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) {
        return;
    }
    for (const std::filesystem::directory_entry& ent : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) {
            break;
        }
        if (!ent.is_regular_file()) {
            continue;
        }
        const std::filesystem::path& p = ent.path();
        if (carrier_path_is_raw_f32(p) || !carrier_extension_is_convertible_audio(p)) {
            continue;
        }
        std::filesystem::path stem = p.stem();
        if (stem.empty()) {
            stem = "carrier";
        }
        const std::filesystem::path dest = p.parent_path() / (stem.string() + ".f32");
        bool need = true;
        ec.clear();
        if (std::filesystem::exists(dest, ec)) {
            ec.clear();
            const std::uintmax_t sz = std::filesystem::file_size(dest, ec);
            // Re-convert if .f32 is missing, empty, or truncated (failed run left 0-byte file).
            if (!ec && sz >= sizeof(float) * 64) {
                const auto st_src = std::filesystem::last_write_time(p, ec);
                std::error_code ec2;
                const auto st_dst = std::filesystem::last_write_time(dest, ec2);
                if (!ec && !ec2 && st_dst >= st_src) {
                    need = false;
                }
            }
        }
        if (!need) {
            continue;
        }
        std::string err;
        if (!carrier_ffmpeg_to_f32(sample_rate, p, dest, err)) {
            std::fprintf(stderr, "[LiveVocoder] carrier folder: skipped %s — %s\n", p.filename().string().c_str(),
                         err.c_str());
            continue;
        }
        std::fprintf(stderr, "[LiveVocoder] carrier folder: converted %s → %s\n", p.filename().string().c_str(),
                     dest.filename().string().c_str());
    }
}

bool carrier_ffmpeg_to_f32(int sample_rate, const std::filesystem::path& src,
                             const std::filesystem::path& dst_f32, std::string& err_out) {
    err_out.clear();
    if (sample_rate <= 0) {
        err_out = "invalid sample rate";
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(src, ec)) {
        err_out = "source is not a readable file";
        return false;
    }
    std::filesystem::create_directories(dst_f32.parent_path(), ec);

#if defined(_WIN32)
    // Stage in %TEMP% when CopyFile succeeds: OneDrive / long paths / Unicode segments often break ffmpeg or
    // stderr capture under Wine; temp names are short and ASCII-heavy.
    std::filesystem::path src_run = src;
    std::filesystem::path dst_run = dst_f32;
    std::filesystem::path tmp_in_path;
    std::filesystem::path tmp_out_path;
    bool ffmpeg_staged = false;
    {
        wchar_t tmp_root[MAX_PATH + 2];
        const DWORD nt_root = GetTempPathW(static_cast<DWORD>(MAX_PATH), tmp_root);
        if (nt_root > 0U && nt_root < static_cast<DWORD>(MAX_PATH)) {
            std::wstring uniq = std::to_wstring(GetTickCount64());
            uniq += L'_';
            uniq += std::to_wstring(GetCurrentProcessId());
            std::wstring ext = src.extension().wstring();
            if (ext.empty()) {
                ext = L".bin";
            }
            tmp_in_path = std::filesystem::path(std::wstring(tmp_root, static_cast<std::size_t>(nt_root))) /
                          (std::wstring(L"lvoc_in_") + uniq + ext);
            tmp_out_path = std::filesystem::path(std::wstring(tmp_root, static_cast<std::size_t>(nt_root))) /
                           (std::wstring(L"lvoc_out_") + uniq + L".f32");
            std::error_code abs_ec;
            const std::filesystem::path src_abs = std::filesystem::absolute(src, abs_ec);
            if (!abs_ec) {
                if (CopyFileW(src_abs.wstring().c_str(), tmp_in_path.wstring().c_str(), FALSE) != 0) {
                    src_run = tmp_in_path;
                    dst_run = tmp_out_path;
                    ffmpeg_staged = true;
                }
            }
        }
    }

    const std::string src_s = ffmpeg_path_for_win32_ffmpeg_cmd(src_run);
    const std::string dst_s = ffmpeg_path_for_win32_ffmpeg_cmd(dst_run);
    const std::string ar_s = std::to_string(sample_rate);

    // Windows / Wine: host Linux ffmpeg is not runnable — need ffmpeg.exe on PATH, next to the exe,
    // or LIVE_VOCODER_FFMPEG=full\path\to\ffmpeg.exe
    std::vector<std::string> candidates;
    if (const char* ev = std::getenv("LIVE_VOCODER_FFMPEG")) {
        if (ev[0] != '\0') {
            lv_push_unique_ffmpeg_candidate(candidates, std::string(ev));
        }
    }
    lv_push_unique_ffmpeg_candidate(candidates, lv_sibling_ffmpeg_exe());
    lv_push_unique_ffmpeg_candidate(candidates, std::string("ffmpeg.exe"));
    lv_push_unique_ffmpeg_candidate(candidates, std::string("ffmpeg"));

    std::error_code ec_tmp;
    wchar_t tmpw[MAX_PATH + 2];
    const DWORD nt = GetTempPathW(static_cast<DWORD>(MAX_PATH), tmpw);
    std::filesystem::path err_log;
    if (nt > 0U && nt < static_cast<DWORD>(MAX_PATH)) {
        err_log = std::filesystem::path(std::wstring(tmpw, static_cast<std::size_t>(nt))) / L"lvoc_ff.txt";
    } else {
        err_log = std::filesystem::temp_directory_path(ec_tmp) / "lvoc_ff.txt";
    }
    // CreateProcess opens ``err_log`` by wide path. Short paths are only for cmd.exe ``2>`` fallback (Wine / edge cases).
    const std::filesystem::path err_log_cmd = carrier_win32_short_path_for_cmd_redir(err_log);
    const std::string err_redir_target = ffmpeg_path_arg_windows(err_log_cmd);

    const bool run_on_wine = carrier_win32_running_under_wine();
    int exit_code = -999;
    for (const std::string& ffmpeg_exe : candidates) {
        if (ffmpeg_exe.empty()) {
            continue;
        }
        std::filesystem::remove(err_log, ec_tmp);
        int ex = -1;
        const std::wstring ff = carrier_win32_resolve_ffmpeg_exe_w(ffmpeg_exe);
        if (!ff.empty()) {
            const std::wstring src_w = std::filesystem::u8path(src_s).wstring();
            const std::wstring dst_w = std::filesystem::u8path(dst_s).wstring();
            const int cr =
                carrier_win32_run_ffmpeg_createprocess(ff, src_w, dst_w, sample_rate, err_log.wstring());
            if (cr != -999 && cr != -998) {
                ex = cr;
            }
        }
        // Wine used to rely on ``system()`` + ``2>``; stderr often stayed empty (exit 1, useless UI). Native always used
        // CreateProcess above. If spawn failed or only a bare ``ffmpeg`` name resolved via PATH, fall back to cmd.
        if (ex == -1 && run_on_wine) {
            const std::string cmd = quote_cmd_exe_arg(ffmpeg_exe) + " -y -nostdin -hide_banner -loglevel info -i " +
                                    quote_cmd_exe_arg(src_s) + " -f f32le -ac 1 -ar " + ar_s + " " +
                                    quote_cmd_exe_arg(dst_s) + " 2>" + quote_cmd_exe_arg(err_redir_target);
            const int r = std::system(cmd.c_str());
            if (r == -1) {
                continue;
            }
            ex = system_normalized_exit(r);
        } else if (ex == -1) {
            continue;
        }
        if (ex == 0) {
            exit_code = 0;
            break;
        }
        exit_code = ex;
    }
    if (exit_code == -999) {
        if (ffmpeg_staged) {
            std::filesystem::remove(tmp_in_path, ec_tmp);
            std::filesystem::remove(tmp_out_path, ec_tmp);
        }
        carrier_win32_err_ffmpeg_not_run(err_out);
        return false;
    }
    if (exit_code != 0) {
        if (ffmpeg_staged) {
            std::filesystem::remove(tmp_in_path, ec_tmp);
            std::filesystem::remove(tmp_out_path, ec_tmp);
        }
        std::string detail = slurp_text_file_trunc(err_log, 900);
        err_out = "ffmpeg failed (exit " + std::to_string(exit_code) + ")";
        if (!detail.empty()) {
            err_out += ":\n";
            err_out += detail;
        } else {
            carrier_win32_err_ffmpeg_failed_no_stderr(err_out);
        }
        std::filesystem::remove(err_log, ec_tmp);
        return false;
    }
    std::filesystem::remove(err_log, ec_tmp);
    if (ffmpeg_staged) {
        std::filesystem::remove(tmp_in_path, ec_tmp);
        std::filesystem::remove(dst_f32, ec_tmp);
        std::error_code eren;
        std::filesystem::rename(tmp_out_path, dst_f32, eren);
        if (eren) {
            std::filesystem::copy_file(tmp_out_path, dst_f32, std::filesystem::copy_options::overwrite_existing, eren);
            std::filesystem::remove(tmp_out_path, ec_tmp);
            if (eren) {
                err_out =
                    "could not place converted .f32 in the carriers folder (permissions, cloud sync locked the file, "
                    "or disk full)";
                return false;
            }
        }
    }
#else
    const std::string src_s = src.string();
    const std::string dst_s = dst_f32.string();
    const std::string ar_s = std::to_string(sample_rate);

#endif
#if !defined(_WIN32)
    const pid_t pid = fork();
    if (pid < 0) {
        err_out = "fork failed";
        return false;
    }
    if (pid == 0) {
        char ar_buf[32];
        std::snprintf(ar_buf, sizeof ar_buf, "%d", sample_rate);
        execlp("ffmpeg", "ffmpeg", "-y", "-nostdin", "-hide_banner", "-loglevel", "error", "-i", src_s.c_str(), "-f",
               "f32le", "-ac", "1", "-ar", ar_buf, dst_s.c_str(), reinterpret_cast<char*>(0));
        _exit(127);
    }
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) {
        err_out = "waitpid failed";
        return false;
    }
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        err_out = "ffmpeg failed (is ffmpeg installed? unsupported format?)";
        return false;
    }
#endif

    ec.clear();
    if (!std::filesystem::is_regular_file(dst_f32, ec)) {
        err_out = "ffmpeg produced no output file";
        return false;
    }
    if (std::filesystem::file_size(dst_f32, ec) < sizeof(float) * 64) {
        err_out = "converted carrier is too short";
        std::filesystem::remove(dst_f32, ec);
        return false;
    }
    return true;
}
