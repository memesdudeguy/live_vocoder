#pragma once

#include <string>

/** Linux: if PULSE_SINK is unset, copy LIVE_VOCODER_PULSE_SINK → PULSE_SINK (PipeWire/Pulse routing). */
void lv_apply_pulse_sink_env_before_portaudio();

/**
 * Linux SDL / minimal: create null sink + remapped ``*_mic`` with ``pactl`` when missing (like Python GTK).
 * Disabled when ``LIVE_VOCODER_AUTO_VIRT_MIC=0``. Uses ``LIVE_VOCODER_PULSE_SINK`` / ``LIVE_VOCODER_PULSE_DESCRIPTION``.
 */
void lv_linux_ensure_pipewire_virt_mic_stack();

/**
 * Linux PipeWire/Pulse: when output goes only to PULSE_SINK (null sink), route that sink's monitor to the default
 * playback device via module-loopback so “Monitor on” is audible on speakers/headphones. No-op on other platforms.
 * Unload by passing want_monitor=false (e.g. on Stop or Monitor off).
 */
void lv_linux_sync_speaker_monitor_loopback(bool want_monitor, const char* pulse_sink_base_name);

/**
 * Linux: null-sink base name for monitor loopback when env sink vars are unset (PortAudio can still use ``live_vocoder``).
 * Empty on non-Linux or if ``pactl`` / the sink is unavailable.
 */
std::string lv_linux_monitor_pulse_sink_base_for_loopback();

/**
 * Wine on Linux: same idea as ``lv_linux_monitor_pulse_sink_base_for_loopback`` — resolve ``live_vocoder*`` sink via host
 * ``pactl`` when env is empty. Empty on native Windows or if the sink is missing.
 */
std::string lv_wine_monitor_pulse_sink_base_for_loopback();

/**
 * Linux: mute or unmute this process's Pulse/PipeWire playback stream (sink-input). No-op on other OSes.
 */
void lv_linux_pulse_set_own_playback_muted(bool muted);

/**
 * Wine on Linux: after PortAudio starts, move this app's PipeWire sink-input to ``pulse_sink_name`` (e.g. live_vocoder2).
 * No-op on other platforms. PULSE_SINK is often only honored when set before ``wine`` starts; this patches routing at runtime.
 */
void lv_linux_wine_move_livevocoder_sink_input_to_pulse_sink(const char* pulse_sink_name);

/**
 * Wine on Linux: after ``Pa_StartStream``, resolve the null-sink target (``getenv`` / ``GetEnvironmentVariable`` /
 * ``LIVE_VOCODER_PULSE_SINK`` / host ``pactl``) and move this app's sink-input there. Prefer this over calling
 * ``lv_linux_wine_move_livevocoder_sink_input_to_pulse_sink`` with only ``getenv("PULSE_SINK")``.
 */
void lv_linux_wine_move_livevocoder_sink_input_after_pa_start();

/**
 * Native Linux: after ``Pa_StartStream``, move this app's sink-input to ``PULSE_SINK`` / ``live_vocoder*`` when needed.
 * Some PortAudio + Pulse backends ignore the env for routing; this mirrors the Wine post-start ``pactl`` fix.
 */
void lv_linux_move_livevocoder_sink_input_after_pa_start();

/** Wine on Linux: sync host PipeWire mute vs Monitor (full impl in LiveVocoder tree; stub here). */
void lv_win32_wine_pulse_sync_monitor_mute(bool streaming, bool monitor_on, bool virt_output);

/**
 * Linux: one-line status from pactl (sink live_vocoder*, *_mic, *.monitor).
 * Empty on non-Linux or if pactl is missing / fails.
 */
std::string lv_linux_pulse_virt_mic_status_line();
