#pragma once

#include <portaudio.h>

#include <cstdio>
#include <string>

/** If LIVE_VOCODER_PA_LIST_DEVICES=1, prints every PortAudio device to `out`. */
void pa_log_all_devices_if_requested(FILE* out);

/**
 * Duplex stream: 1 float input channel, 2 float output channels, sample_rate, hop frames.
 * Device selection (first match wins):
 *   LIVE_VOCODER_PA_INPUT_INDEX / LIVE_VOCODER_PA_OUTPUT_INDEX — non-negative integer
 *   else LIVE_VOCODER_PA_INPUT / LIVE_VOCODER_PA_OUTPUT — case-insensitive substring of device name
 *   else LIVE_VOCODER_PULSE_SINK — substring match on output device name (Linux; after PULSE_SINK env)
 *   else on native Windows (not Wine): VB-Audio Virtual Cable playback (CABLE Input) if installed and
 *        LIVE_VOCODER_PA_OUTPUT / _INDEX unset — disable with LIVE_VOCODER_DISABLE_VB_CABLE=1; if the default
 *        input is CABLE Output, prefers another capture device for the microphone when available; on native Windows,
 *        optional: LIVE_VOCODER_WIN_DEFAULT_VIRT_MIC=1 sets system default recording to CABLE Output for OBS/Discord
 *        "Default" mic (otherwise real mic stays Windows default).
 *   else system default input/output
 * Output device must report at least 2 output channels (stereo).
 * Native Windows: if the chosen mic and output use different PortAudio host APIs (e.g. MME vs WASAPI),
 *   the duplex open is retargeted to one API (VB-Cable on the mic's API, else mic on the cable's API) so
 *   Pa_OpenStream does not fail with paBadIODeviceCombination ("Illegal combination of I/O devices").
 * Native Windows: VB-Cable duplex defaults to low suggested latency with a cap (~48 ms); optional
 * LIVE_VOCODER_PA_HIGH_LATENCY / LIVE_VOCODER_VB_HIGH_LATENCY for larger buffers. WASAPI streams use
 * communications category + pro-audio thread priority when supported (see stderr on startup).
 */
PaError pa_open_livevocoder_duplex(PaStream** stream, double sample_rate, unsigned long hop,
                                   PaStreamCallback callback, void* user_data);

/** Writes chosen device names to stderr (after successful open). */
void pa_log_stream_devices(PaStream* stream);

/** After Pa_CloseStream on the duplex stream, call so monitor/sink heuristics reset. */
void pa_duplex_note_stream_closed();

/** Output device index from the last successful ``pa_open_livevocoder_duplex`` (or paNoDevice). */
PaDeviceIndex pa_duplex_last_opened_output_device();

/**
 * True if the output device used for the last successful ``pa_open_livevocoder_duplex`` looks like a PipeWire/Pulse
 * null sink (e.g. ``live_vocoder``) — not normal speakers. Used so Monitor off still feeds that sink.
 */
bool pa_duplex_output_targets_virt_sink_route();

/**
 * After Pa_Initialize: short hint if any capture device name looks like a PipeWire monitor / virtual mic.
 * Empty if nothing matches (call Pa_Initialize first).
 */
std::string pa_portaudio_virt_capture_hint();

/** Windows: short footer hint for device routing (Wine vs native). Empty on other platforms. */
std::string pa_windows_virt_mic_route_hint();

/** Native Windows: reserved; VB-Cable install hints are folded into pa_windows_virt_mic_route_hint(). */
std::string pa_windows_native_vb_cable_portaudio_hint();
