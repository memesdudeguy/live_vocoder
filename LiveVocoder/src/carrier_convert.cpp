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

/** ``LiveVocoder.exe`` directory + ``ffmpeg.exe`` (for Wine / portable bundles without PATH). */
static std::string lv_sibling_ffmpeg_exe() {
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return {};
    }
    std::error_code ec;
    std::filesystem::path exe(buf);
    std::filesystem::path cand = exe.parent_path() / "ffmpeg.exe";
    if (std::filesystem::is_regular_file(cand, ec)) {
        return cand.string();
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
    std::filesystem::path loc = carrier_win32_localize_path_for_filesystem(p);
    return loc.generic_string();
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
    const std::string src_s = ffmpeg_path_arg_windows(src);
    const std::string dst_s = ffmpeg_path_arg_windows(dst_f32);
    const std::string ar_s = std::to_string(sample_rate);

    // Windows / Wine: host Linux ffmpeg is not runnable — need ffmpeg.exe on PATH, next to the exe,
    // or LIVE_VOCODER_FFMPEG=full\path\to\ffmpeg.exe
    std::vector<std::string> candidates;
    if (const char* ev = std::getenv("LIVE_VOCODER_FFMPEG")) {
        lv_push_unique_ffmpeg_candidate(candidates, std::string(ev));
    }
    lv_push_unique_ffmpeg_candidate(candidates, lv_sibling_ffmpeg_exe());
    lv_push_unique_ffmpeg_candidate(candidates, std::string("ffmpeg.exe"));
    lv_push_unique_ffmpeg_candidate(candidates, std::string("ffmpeg"));

    std::error_code ec_tmp;
    std::filesystem::path err_log = std::filesystem::temp_directory_path(ec_tmp) / "live_vocoder_ffmpeg_err.txt";
    if (err_log.parent_path().empty()) {
        err_log = std::filesystem::path("live_vocoder_ffmpeg_err.txt");
    }
    const std::string err_redir_target = ffmpeg_path_arg_windows(err_log);

    int exit_code = -1;
    for (const std::string& ffmpeg_exe : candidates) {
        std::filesystem::remove(err_log, ec_tmp);
        const std::string cmd = quote_cmd_exe_arg(ffmpeg_exe) + " -y -nostdin -hide_banner -loglevel error -i " +
                                quote_cmd_exe_arg(src_s) + " -f f32le -ac 1 -ar " + ar_s + " " +
                                quote_cmd_exe_arg(dst_s) + " 2>" + quote_cmd_exe_arg(err_redir_target);
        const int r = std::system(cmd.c_str());
        if (r == -1) {
            continue;
        }
        exit_code = system_normalized_exit(r);
        break;
    }
    if (exit_code == -1) {
        err_out =
            "could not run ffmpeg.exe (Wine cannot use Linux /usr/bin/ffmpeg). "
            "Put ffmpeg.exe next to LiveVocoder.exe, add it to PATH, set LIVE_VOCODER_FFMPEG, "
            "or use a raw .f32 carrier.";
        return false;
    }
    if (exit_code != 0) {
        std::string detail = slurp_text_file_trunc(err_log, 900);
        err_out = "ffmpeg failed (exit " + std::to_string(exit_code) + ")";
        if (!detail.empty()) {
            err_out += ":\n";
            err_out += detail;
        } else {
            err_out += " — unsupported format, bad path under Wine, or corrupt file. "
                       "Try a .f32 carrier or drag from Wine’s file dialog.";
        }
        std::filesystem::remove(err_log, ec_tmp);
        return false;
    }
    std::filesystem::remove(err_log, ec_tmp);
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
