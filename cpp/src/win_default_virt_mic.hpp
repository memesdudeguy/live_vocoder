#pragma once

#include <string>

#if defined(_WIN32)
/** Native Windows only (skipped under Wine). Best-effort: set default recording device to VB-Audio CABLE Output. */
std::string lv_win32_try_set_default_capture_to_vb_cable();
#else
inline std::string lv_win32_try_set_default_capture_to_vb_cable() {
    return {};
}
#endif
