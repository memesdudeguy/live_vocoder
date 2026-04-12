#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#if defined(_WIN32)

struct LiveVocoderUpdateCheck {
    std::atomic<bool> finished{false};
    std::atomic<bool> consumed{false};
    bool update_available = false;
    std::string setup_download_url;
    std::mutex mu;
    std::thread worker;
};

/** Background: GitHub API + compare to this exe mtime / semver. Skips Wine and LIVE_VOCODER_NO_AUTO_UPDATE. */
void live_vocoder_update_check_begin(LiveVocoderUpdateCheck* s);

/** Once when finished: returns true exactly once with has_update + url (if any). */
bool live_vocoder_update_check_poll(LiveVocoderUpdateCheck* s, bool* has_update, std::string* url);

/** Download HTTPS URL to %TEMP% and ShellExecute the installer. */
bool live_vocoder_download_and_run_installer(const char* https_url, std::string& err_out);

#else

struct LiveVocoderUpdateCheck {};

inline void live_vocoder_update_check_begin(LiveVocoderUpdateCheck*) {}
inline bool live_vocoder_update_check_poll(LiveVocoderUpdateCheck*, bool*, std::string*) {
    return false;
}
inline bool live_vocoder_download_and_run_installer(const char*, std::string&) {
    return false;
}

#endif
