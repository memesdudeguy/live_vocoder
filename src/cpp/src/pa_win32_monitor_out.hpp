#pragma once

#include <portaudio.h>

#if defined(_WIN32)
/**
 * When duplex playback is VB-Cable (or similar virtual route), opens a second PortAudio output-only stream
 * to the first non-virtual stereo device so Monitor sends the same audio to real speakers/headphones.
 * No-op if no suitable speaker device exists.
 */
PaError pa_win32_monitor_output_start(PaStream** out_stream, double sample_rate, unsigned long hop,
                                      PaDeviceIndex duplex_output_device);

void pa_win32_monitor_output_stop(PaStream* stream);

/** Call from PortAudio duplex callback: copies interleaved stereo to the monitor stream (non-blocking). */
void pa_win32_monitor_output_feed(bool duplex_is_win_virt_route, bool monitor_on, const float* interleaved_stereo_lr,
                                  unsigned long frames);
#else
inline PaError pa_win32_monitor_output_start(PaStream**, double, unsigned long, PaDeviceIndex) {
    return paNoError;
}
inline void pa_win32_monitor_output_stop(PaStream*) {}
inline void pa_win32_monitor_output_feed(bool, bool, const float*, unsigned long) {}
#endif
