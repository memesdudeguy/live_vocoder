#pragma once

#include <filesystem>
#include <string>
#include <vector>

/** True if extension is .f32 (case-insensitive), or filename ends with ``.f32`` (Wine path quirks). */
bool carrier_path_is_raw_f32(const std::filesystem::path& path);

/**
 * Skip ffmpeg when this path is raw float32: ``.f32`` by name, or (no known audio extension) size/header sniff.
 * Important under Wine so ``.f32`` drops are not misclassified.
 */
bool carrier_file_is_raw_f32_carrier(const std::filesystem::path& path);

/**
 * True if ``p`` exists and is not a directory. On Windows, OneDrive “online-only” files may fail
 * ``is_regular_file`` until hydrated; this still returns true so conversion can open and recall them.
 */
bool carrier_source_path_usable(const std::filesystem::path& p, std::error_code& ec);

#if defined(_WIN32)
std::filesystem::path carrier_win32_localize_path_for_filesystem(const std::filesystem::path& raw);
/** Shell "Documents" / My Documents (current path; honors folder relocation). Empty if unavailable. */
std::filesystem::path carrier_win32_documents_folder();
#endif

/**
 * Decode any audio format ffmpeg understands → mono f32le at sample_rate Hz.
 * Overwrites dst_f32. On Unix: ``ffmpeg`` on PATH. On Windows / Wine: ``ffmpeg.exe`` on PATH,
 * or ``ffmpeg.exe`` next to ``LiveVocoder.exe``, or env ``LIVE_VOCODER_FFMPEG`` = full path
 * to ``ffmpeg.exe`` (under Wine: only if that file is a Windows PE). Raw ``.f32`` carriers skip ffmpeg.
 */
bool carrier_ffmpeg_to_f32(int sample_rate, const std::filesystem::path& src,
                           const std::filesystem::path& dst_f32, std::string& err_out);

/**
 * Non-recursive scan of dir: for each file with a known audio extension (not .f32), writes
 * <stem>.f32 in the same folder when missing or older than the source. Logs failures to stderr.
 */
void carrier_convert_audio_in_folder(int sample_rate, const std::filesystem::path& dir);

/** Non-recursive scan of ``dir``: ``.f32`` and ffmpeg-convertible audio; sorted by filename. */
void carrier_collect_library_entries(const std::filesystem::path& dir, std::vector<std::filesystem::path>& out);
