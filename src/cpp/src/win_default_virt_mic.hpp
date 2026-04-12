#pragma once

#include <string>

#if defined(_WIN32)
/** Native Windows only (skipped under Wine). If LIVE_VOCODER_WIN_DEFAULT_VIRT_MIC=1, set default recording to
 *  VB-Audio CABLE Output; otherwise leaves system default (real mic) unchanged. */
std::string lv_win32_try_set_default_capture_to_vb_cable();
#else
inline std::string lv_win32_try_set_default_capture_to_vb_cable() {
    return {};
}
#endif
