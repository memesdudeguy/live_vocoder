#if defined(_WIN32)

#include "update_check_win.hpp"
#include "app_version.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>

namespace {

bool wine_get_version(char* buf, size_t cap) {
    if (!buf || cap == 0) return false;
    buf[0] = '\0';
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) return false;
    using wine_get_version_t = const char* (*)();
    auto p = reinterpret_cast<wine_get_version_t>(GetProcAddress(ntdll, "wine_get_version"));
    if (!p) return false;
    const char* v = p();
    if (!v) return false;
    strncpy(buf, v, cap - 1);
    buf[cap - 1] = '\0';
    return true;
}

bool env_truthy(const char* v) {
    if (!v || !*v) return false;
    return (v[0] == '1' || v[0] == 't' || v[0] == 'T' || v[0] == 'y' || v[0] == 'Y');
}

std::wstring utf8_to_utf16(const char* s) {
    if (!s || !*s) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
}

bool get_exe_path(wchar_t* buf, DWORD cap) {
    DWORD n = GetModuleFileNameW(nullptr, buf, cap);
    return n > 0 && n < cap;
}

long long file_time_to_unix_utc(const FILETIME& ft) {
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    if (u.QuadPart == 0) return 0;
    const long long epoch = 116444736000000000LL;
    long long t100ns = static_cast<long long>(u.QuadPart);
    return (t100ns - epoch) / 10000000LL;
}

long long get_this_exe_mtime_unix_utc() {
    wchar_t path[MAX_PATH];
    if (!get_exe_path(path, MAX_PATH)) return 0;
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return 0;
    FILETIME ft{};
    if (!GetFileTime(h, nullptr, nullptr, &ft)) {
        CloseHandle(h);
        return 0;
    }
    CloseHandle(h);
    return file_time_to_unix_utc(ft);
}

bool http_get_utf8(const wchar_t* host, INTERNET_PORT port, bool https,
                   const wchar_t* path_and_query, const wchar_t* user_agent,
                   std::string& body_out, std::string& err_out) {
    body_out.clear();
    err_out.clear();
    DWORD access = https ? WINHTTP_ACCESS_TYPE_DEFAULT_PROXY : WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;
    HINTERNET ses = WinHttpOpen(user_agent, access, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) {
        err_out = "WinHttpOpen failed";
        return false;
    }
    DWORD prot = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#if defined(WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3)
    prot |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    WinHttpSetOption(ses, WINHTTP_OPTION_SECURE_PROTOCOLS, &prot, sizeof(prot));

    HINTERNET con = WinHttpConnect(ses, host, port, 0);
    if (!con) {
        err_out = "WinHttpConnect failed";
        WinHttpCloseHandle(ses);
        return false;
    }
    DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(con, L"GET", path_and_query, nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req) {
        err_out = "WinHttpOpenRequest failed";
        WinHttpCloseHandle(con);
        WinHttpCloseHandle(ses);
        return false;
    }
    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req, nullptr)) {
        err_out = "WinHttp request failed";
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(con);
        WinHttpCloseHandle(ses);
        return false;
    }
    DWORD status = 0;
    DWORD sz = sizeof(status);
    if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX) ||
        status != 200) {
        err_out = "HTTP status not 200";
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(con);
        WinHttpCloseHandle(ses);
        return false;
    }
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail)) break;
        if (avail == 0) break;
        std::string chunk(static_cast<size_t>(avail), '\0');
        DWORD read = 0;
        if (!WinHttpReadData(req, chunk.data(), avail, &read) || read == 0) break;
        chunk.resize(read);
        body_out += chunk;
        if (body_out.size() > 8 * 1024 * 1024) {
            err_out = "response too large";
            WinHttpCloseHandle(req);
            WinHttpCloseHandle(con);
            WinHttpCloseHandle(ses);
            return false;
        }
    }
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return true;
}

// Find "key":"value" where value may contain escapes; returns empty if not found.
std::string json_string_value_after(const std::string& j, const char* key) {
    std::string needle = std::string("\"") + key + "\":\"";
    size_t p = j.find(needle);
    if (p == std::string::npos) return {};
    p += needle.size();
    std::string out;
    while (p < j.size()) {
        char c = j[p++];
        if (c == '"') break;
        if (c == '\\' && p < j.size()) {
            char e = j[p++];
            if (e == 'n') out += '\n';
            else if (e == 'r') out += '\r';
            else if (e == 't') out += '\t';
            else if (e == '\\' || e == '"' || e == '/') out += e;
            else if (e == 'u' && p + 4 <= j.size()) {
                unsigned v = 0;
                for (int i = 0; i < 4; ++i) {
                    char h = j[p++];
                    v <<= 4;
                    if (h >= '0' && h <= '9') v += static_cast<unsigned>(h - '0');
                    else if (h >= 'a' && h <= 'f') v += 10u + static_cast<unsigned>(h - 'a');
                    else if (h >= 'A' && h <= 'F') v += 10u + static_cast<unsigned>(h - 'A');
                    else {
                        out += "?";
                        v = 0;
                        break;
                    }
                }
                if (v < 128) out += static_cast<char>(v);
                else out += '?';
            } else out += e;
        } else out += c;
    }
    return out;
}

// Parse ISO8601 ...Z to Unix UTC (simplified: no subsecond).
long long parse_github_time_utc(const std::string& iso) {
    if (iso.size() < 20) return 0;
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    if (std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6) return 0;
    if (y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
    static const int mdays[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    auto is_leap = [](int yy) {
        return (yy % 4 == 0 && yy % 100 != 0) || (yy % 400 == 0);
    };
    long long days = 0;
    for (int yy = 1970; yy < y; ++yy) days += is_leap(yy) ? 366 : 365;
    for (int m = 1; m < mo; ++m) {
        int dim = mdays[m];
        if (m == 2 && is_leap(y)) dim = 29;
        days += dim;
    }
    days += (d - 1);
    return days * 86400LL + h * 3600LL + mi * 60LL + se;
}

// Parse tag_name like "v7.0" or "7.0.1" → compare to app version.
bool tag_newer_than_app(const std::string& tag) {
    std::string t = tag;
    while (!t.empty() && (t[0] == 'v' || t[0] == 'V')) t.erase(t.begin());
    int maj = 0, mino = 0, pat = 0;
    if (std::sscanf(t.c_str(), "%d.%d.%d", &maj, &mino, &pat) < 2) {
        if (std::sscanf(t.c_str(), "%d.%d", &maj, &mino) != 2) return false;
        pat = 0;
    }
    if (maj != kLiveVocoderVersionMajor) return maj > kLiveVocoderVersionMajor;
    if (mino != kLiveVocoderVersionMinor) return mino > kLiveVocoderVersionMinor;
    return pat > kLiveVocoderVersionPatch;
}

// In GitHub releases JSON, find asset with name LiveVocoder-Setup.exe and return browser_download_url + updated_at of that asset.
bool parse_latest_release_setup(const std::string& j, std::string& url_out, long long& asset_time_out,
                                bool& tag_newer_out) {
    url_out.clear();
    asset_time_out = 0;
    tag_newer_out = false;
    std::string tag = json_string_value_after(j, "tag_name");
    tag_newer_out = tag_newer_than_app(tag);

    const char* marker = "\"name\":\"LiveVocoder-Setup.exe\"";
    size_t pos = 0;
    for (;;) {
        size_t m = j.find(marker, pos);
        if (m == std::string::npos) return false;
        size_t block_start = j.rfind('{', m);
        if (block_start == std::string::npos) {
            pos = m + 1;
            continue;
        }
        size_t depth = 0;
        size_t block_end = std::string::npos;
        for (size_t i = block_start; i < j.size(); ++i) {
            if (j[i] == '{') ++depth;
            else if (j[i] == '}') {
                if (--depth == 0) {
                    block_end = i;
                    break;
                }
            }
        }
        if (block_end == std::string::npos) {
            pos = m + 1;
            continue;
        }
        std::string block = j.substr(block_start, block_end - block_start + 1);
        url_out = json_string_value_after(block, "browser_download_url");
        std::string upd = json_string_value_after(block, "updated_at");
        asset_time_out = parse_github_time_utc(upd);
        if (!url_out.empty() && asset_time_out > 0) return true;
        pos = block_end + 1;
    }
}

void default_repo(std::string& owner, std::string& name) {
    const char* e = std::getenv("LIVE_VOCODER_UPDATE_REPO");
    if (e && *e) {
        std::string s(e);
        size_t slash = s.find('/');
        if (slash != std::string::npos && slash > 0 && slash + 1 < s.size()) {
            owner = s.substr(0, slash);
            name = s.substr(slash + 1);
            return;
        }
    }
    owner = "memesdudeguy";
    name = "live_vocoder";
}

void run_check_worker(LiveVocoderUpdateCheck* s) {
    char winebuf[64];
    if (wine_get_version(winebuf, sizeof(winebuf))) {
        s->finished.store(true, std::memory_order_release);
        return;
    }
    if (env_truthy(std::getenv("LIVE_VOCODER_NO_AUTO_UPDATE"))) {
        s->finished.store(true, std::memory_order_release);
        return;
    }

    std::string owner, repo;
    default_repo(owner, repo);
    std::string path = "/repos/" + owner + "/" + repo + "/releases/latest";
    std::wstring wpath = utf8_to_utf16(path.c_str());

    const wchar_t* ua = L"LiveVocoder/6.0 (Windows; +https://github.com/memesdudeguy/live_vocoder)";
    std::string body, err;
    if (!http_get_utf8(L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, true, wpath.c_str(), ua, body, err)) {
        s->finished.store(true, std::memory_order_release);
        return;
    }

    std::string url;
    long long asset_t = 0;
    bool tag_newer = false;
    if (!parse_latest_release_setup(body, url, asset_t, tag_newer)) {
        s->finished.store(true, std::memory_order_release);
        return;
    }

    long long exe_t = get_this_exe_mtime_unix_utc();
    const long long slack_sec = 90;
    bool time_newer = (asset_t > 0 && exe_t > 0 && asset_t > exe_t + slack_sec);
    bool need = tag_newer || time_newer;

    {
        std::lock_guard<std::mutex> lock(s->mu);
        s->update_available = need;
        s->setup_download_url = need ? std::move(url) : std::string{};
    }
    s->finished.store(true, std::memory_order_release);
}

}  // namespace

void live_vocoder_update_check_begin(LiveVocoderUpdateCheck* s) {
    if (!s) return;
    char winebuf[64];
    if (wine_get_version(winebuf, sizeof(winebuf))) {
        s->finished.store(true, std::memory_order_release);
        return;
    }
    if (env_truthy(std::getenv("LIVE_VOCODER_NO_AUTO_UPDATE"))) {
        s->finished.store(true, std::memory_order_release);
        return;
    }
    try {
        s->worker = std::thread([s] { run_check_worker(s); });
        s->worker.detach();
    } catch (...) {
        s->finished.store(true, std::memory_order_release);
    }
}

bool live_vocoder_update_check_poll(LiveVocoderUpdateCheck* s, bool* has_update, std::string* url) {
    if (!s || !has_update || !url) return false;
    if (!s->finished.load(std::memory_order_acquire)) return false;
    if (s->consumed.exchange(true)) return false;
    std::lock_guard<std::mutex> lock(s->mu);
    *has_update = s->update_available;
    *url = s->setup_download_url;
    return true;
}

bool live_vocoder_download_and_run_installer(const char* https_url, std::string& err_out) {
    err_out.clear();
    if (!https_url || !*https_url) {
        err_out = "empty url";
        return false;
    }
    std::string u(https_url);
    if (u.rfind("https://", 0) != 0) {
        err_out = "not https";
        return false;
    }

    size_t host_start = 8;
    size_t path_slash = u.find('/', host_start);
    if (path_slash == std::string::npos) {
        err_out = "bad url";
        return false;
    }
    std::string host_utf8 = u.substr(host_start, path_slash - host_start);
    std::string path_utf8 = u.substr(path_slash);
    std::wstring whost = utf8_to_utf16(host_utf8.c_str());
    std::wstring wpath = utf8_to_utf16(path_utf8.c_str());
    if (whost.empty() || wpath.empty()) {
        err_out = "bad url encoding";
        return false;
    }

    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
    const wchar_t* ua = L"LiveVocoder/7.0 (download)";
    std::string body, err;
    if (!http_get_utf8(whost.c_str(), port, true, wpath.c_str(), ua, body, err)) {
        err_out = err.empty() ? "download failed" : err;
        return false;
    }

    wchar_t tmp[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, tmp);
    if (n == 0 || n >= MAX_PATH) {
        err_out = "GetTempPath failed";
        return false;
    }
    std::wstring out_path = std::wstring(tmp) + L"LiveVocoder-Setup-update.exe";
    HANDLE hf = CreateFileW(out_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
    if (hf == INVALID_HANDLE_VALUE) {
        err_out = "create temp file failed";
        return false;
    }
    const char* p = body.data();
    size_t left = body.size();
    while (left > 0) {
        DWORD chunk = static_cast<DWORD>(left > 0x7fffffffu ? 0x7fffffffu : left);
        DWORD written = 0;
        if (!WriteFile(hf, p, chunk, &written, nullptr) || written == 0) {
            CloseHandle(hf);
            err_out = "write failed";
            return false;
        }
        p += written;
        left -= written;
    }
    CloseHandle(hf);

    HINSTANCE hi = ShellExecuteW(nullptr, L"open", out_path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<intptr_t>(hi) <= 32) {
        err_out = "ShellExecute failed";
        return false;
    }
    return true;
}

#endif  // _WIN32
