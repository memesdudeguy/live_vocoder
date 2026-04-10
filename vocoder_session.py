"""
Shared live-vocoder session (load carrier, start/stop duplex stream) for Tk or web UI.
"""
from __future__ import annotations

import os
import sys
import threading
import time
from pathlib import Path

import numpy as np
import sounddevice as sd

from live_vocoder import MonoConvolutionReverb, StreamingVocoder, load_audio_ffmpeg
from pulse_portaudio import PULSE_VIRT_MIC_TROUBLESHOOT
from pulse_virtmic import (
    VirtMicSetup,
    apply_pulse_sink_env,
    discover_loopback_sink_input,
    get_default_sink_name,
    load_loopback_monitor_to_default_with_fallback,
    iter_move_to_virt_sink,
    monitor_source_name,
    move_sink_inputs_for_pid_to_sink,
    mute_monitor_loopback_sink_inputs,
    mute_playback_sink_inputs_not_on_virt_sink,
    resolved_pulse_sink_identity,
    source_exists,
    status_hint_missing_virt_mic_source,
    unload_loopbacks_for_pulse_source,
    unload_module,
    virtual_mic_source_name,
    wait_for_live_vocoder_playback_sink_input,
)
from virt_mic_unified import setup_virt_mic_unified, teardown_unified_virt_mic

from carrier_library import ensure_carrier_library_dir, list_carrier_files


class VocoderSession:
    def __init__(
        self,
        sr: int = 48000,
        input_device: int | None = None,
        output_device: int | None = None,
        use_pulse_virt_mic: bool = False,
        pulse_sink_name: str | None = None,
        pulse_sink_description: str | None = None,
        virt_mic_hear_loopback: bool = False,
        mute_speakers: bool = True,
        mute_vocoded: bool = True,
        virt_mic_device_hint: str | None = None,
        reverb_mix: float = 0.0,
        reverb_room: float = 0.45,
    ):
        self.sr = sr
        self.lock = threading.Lock()
        self.stop_event = threading.Event()
        self.hear_self = False
        self.hear_vocoded = True
        self.gain = 6.0
        self.wet_level = 1.15
        self.presence_db = 4.0
        self.reverb_mix = float(np.clip(reverb_mix, 0.0, 1.0))
        self.reverb_room = float(np.clip(reverb_room, 0.0, 1.0))
        self._reverb_ref: MonoConvolutionReverb | None = None
        self.voc: StreamingVocoder | None = None
        self.carrier_arr: np.ndarray | None = None
        self.last_status = "Pick a carrier, then Start."
        self.path_hint = ""

        self.n_fft = 2048
        self.hop = self.n_fft // 4
        self.block = self.hop * max(1, 512 // self.hop)

        self._input_device_explicit = input_device is not None
        self._output_device_explicit = output_device is not None
        self.in_dev = input_device if input_device is not None else sd.default.device[0]
        self.out_dev = output_device if output_device is not None else sd.default.device[1]
        try:
            out_info = sd.query_devices(self.out_dev, "output")
        except Exception:
            out_info = sd.query_devices(self.out_dev)
        self.n_out = max(1, min(2, int(out_info.get("max_output_channels") or 1)))

        self._audio_thread: threading.Thread | None = None
        self.use_pulse_virt_mic = bool(use_pulse_virt_mic)
        rs, rd = resolved_pulse_sink_identity()
        self.pulse_sink_name = (
            pulse_sink_name if pulse_sink_name is not None else rs
        )
        self.pulse_sink_description = (
            pulse_sink_description if pulse_sink_description is not None else rd
        )
        self.virt_mic_hear_loopback = bool(virt_mic_hear_loopback)
        self._virt_setup: VirtMicSetup | None = None
        self.mute_speakers = bool(mute_speakers)
        self.mute_vocoded = bool(mute_vocoded)
        env_hint = os.environ.get("LIVE_VOCODER_VIRT_DEVICE", "").strip()
        self.virt_mic_device_hint = (virt_mic_device_hint or env_hint or None)
        self._stream_virt_cable: bool = False
        # module-remap-source id so we can remove **… Virtual Mic** on app quit (kept after Stop).
        self._pulse_remap_module_id: int | None = None
        # One-line Pulse monitor/loopback issue; merged into :meth:`display_status` (not appended to ``last_status``).
        self._pulse_monitor_warn: str | None = None
        # Virtual mic mode active when the current stream's ``_run_stream`` began (for GTK toggle guard).
        self._stream_started_with_virt_mic: bool = False
        # Callback increments while RMS stays ~silent (muted / wrong default input in KDE).
        self._mic_silent_streak: int = 0
        # Bypass vocoder: dry mic to outputs / virtual mic (still need a carrier loaded to Start).
        self.clean_mic: bool = False

    def display_status(self) -> str:
        """``last_status`` plus optional Pulse monitor warning (loopback / default sink)."""
        parts: list[str] = [self.last_status]
        w = self._pulse_monitor_warn
        if w:
            parts.append(w)
        if self.is_streaming():
            with self.lock:
                streak = self._mic_silent_streak
            if streak > 80:
                parts.append(
                    "**No mic signal** — In system **Sound → Input**, unmute your microphone and raise "
                    "its volume (PipeWire default input is often muted at 0%). Unmute **main output** "
                    "if the tray speaker icon is muted."
                )
        return " ".join(parts)

    def is_streaming(self) -> bool:
        return (
            self._audio_thread is not None
            and self._audio_thread.is_alive()
        )

    def set_virt_mic_device_hint(self, s: str) -> None:
        """Substring to match a virtual cable output (Windows/macOS fallback)."""
        t = (s or "").strip()
        self.virt_mic_device_hint = t or None

    def _mute_local_off_virt_sink(self) -> bool:
        """Duplicate off-virt playback: mute when speakers off or vocoded local preview off."""
        return self.mute_speakers or (
            self.mute_vocoded and not self.hear_self and not self.clean_mic
        )

    def _want_monitor_loopback_playing(self) -> bool:
        """
        Load monitor → headphones loopback only when it would carry something you asked to hear.

        If “Hear vocoded in that monitor” is off and you are not sending dry sidetone, **unload**
        the loopback entirely — PipeWire often ignores ``set-sink-input-mute`` on loopback, so mute
        was unreliable; teardown guarantees silence while the virtual mic stream stays on the null sink.
        """
        return (
            self.virt_mic_hear_loopback
            and not self.mute_speakers
            and (not self.mute_vocoded or self.hear_self or self.clean_mic)
        )

    def _sync_pulse_speaker_loopback(self) -> None:
        """
        Pulse/PipeWire: null-sink monitor → default sink via ``module-loopback``.

        - **mute_speakers**: unload loopback; null sink / virtual mic unchanged.
        - **mute_vocoded** with no dry sidetone: unload loopback (no vocoded in headphones); VM unchanged.
        - **hear_self** with **mute_vocoded**: keep loopback for dry sidetone only.
        """
        if not self.use_pulse_virt_mic:
            self._pulse_monitor_warn = None
            mon = monitor_source_name(self.pulse_sink_name)
            unload_loopbacks_for_pulse_source(mon)
            mute_monitor_loopback_sink_inputs(self.pulse_sink_name, True)
            return
        mon = monitor_source_name(self.pulse_sink_name)
        with self.lock:
            vs = self._virt_setup
        want_loopback = self._want_monitor_loopback_playing()

        if vs is None:
            if not want_loopback:
                self._pulse_monitor_warn = None
                unload_loopbacks_for_pulse_source(mon)
                mute_monitor_loopback_sink_inputs(self.pulse_sink_name, True)
            return

        if not want_loopback:
            self._pulse_monitor_warn = None
            if vs.loopback_module_id is not None:
                unload_module(vs.loopback_module_id)
            unload_loopbacks_for_pulse_source(mon)
            vs.loopback_module_id = None
            vs.loopback_sink_input_id = None
            mute_monitor_loopback_sink_inputs(self.pulse_sink_name, True)
            return

        if vs.loopback_module_id is None:
            mid, err = load_loopback_monitor_to_default_with_fallback(mon)
            if mid is not None:
                self._pulse_monitor_warn = None
                vs.loopback_module_id = mid
                vs.loopback_sink_input_id = discover_loopback_sink_input(mid)
            else:
                had_default_name = get_default_sink_name() is not None
                if not had_default_name:
                    self._pulse_monitor_warn = (
                        "**Pulse:** no default sink (pactl get-default-sink); "
                        f"cannot route monitor to headphones. {err}"
                    )[:500]
                else:
                    self._pulse_monitor_warn = (
                        f"**Pulse:** monitor loopback failed: {err}"
                    )[:500]
        elif vs.loopback_sink_input_id is None:
            self._pulse_monitor_warn = None
            vs.loopback_sink_input_id = discover_loopback_sink_input(vs.loopback_module_id)
        else:
            self._pulse_monitor_warn = None

    def set_mute_speakers(self, v: bool) -> None:
        """Silence local headphones only; vocoded output still reaches the Pulse null sink / virtual mic.

        Re-applies ``move-sink-input`` onto the null sink so leakage to the default USB sink stops;
        then syncs loopback (unload / mute paths) without zeroing the audio buffer sent to the VM.
        """
        self.mute_speakers = bool(v)
        with self.lock:
            vs = self._virt_setup
        if self.use_pulse_virt_mic and vs is not None:
            iter_move_to_virt_sink(vs.sink_name)
        self._sync_pulse_speaker_loopback()
        if self.use_pulse_virt_mic and vs is not None:
            mute_playback_sink_inputs_not_on_virt_sink(
                vs.sink_name, self._mute_local_off_virt_sink()
            )
        if self.mute_speakers and self.use_pulse_virt_mic:
            mon = monitor_source_name(self.pulse_sink_name)
            unload_loopbacks_for_pulse_source(mon)
            with self.lock:
                vs2 = self._virt_setup
            if vs2 is not None:
                iter_move_to_virt_sink(vs2.sink_name, max_rounds=25)
                mute_playback_sink_inputs_not_on_virt_sink(
                    vs2.sink_name, True
                )

    def set_virt_mic_hear_loopback(self, v: bool) -> None:
        """Pulse-only: route null-sink monitor to default speakers (or remove) while streaming."""
        self.virt_mic_hear_loopback = bool(v)
        self._sync_pulse_speaker_loopback()
        if self.use_pulse_virt_mic and not self.virt_mic_hear_loopback:
            mon = monitor_source_name(self.pulse_sink_name)
            unload_loopbacks_for_pulse_source(mon)
            with self.lock:
                vs = self._virt_setup
            if vs is not None:
                iter_move_to_virt_sink(vs.sink_name, max_rounds=25)
                mute_playback_sink_inputs_not_on_virt_sink(
                    vs.sink_name, self._mute_local_off_virt_sink()
                )

    def set_hear_self(self, v: bool) -> None:
        self.hear_self = bool(v)
        with self.lock:
            vs = self._virt_setup
        if self.use_pulse_virt_mic and vs is not None:
            iter_move_to_virt_sink(vs.sink_name)
        self._sync_pulse_speaker_loopback()
        if self.use_pulse_virt_mic and vs is not None:
            mute_playback_sink_inputs_not_on_virt_sink(
                vs.sink_name, self._mute_local_off_virt_sink()
            )

    def set_hear_vocoded(self, v: bool) -> None:
        self.hear_vocoded = bool(v)

    def set_clean_mic(self, v: bool) -> None:
        """Dry mic pass-through (no vocoder, no reverb). Syncs Pulse loopback / duplicate muting."""
        self.clean_mic = bool(v)
        with self.lock:
            vs = self._virt_setup
        if self.use_pulse_virt_mic and vs is not None:
            iter_move_to_virt_sink(vs.sink_name)
        self._sync_pulse_speaker_loopback()
        if self.use_pulse_virt_mic and vs is not None:
            mute_playback_sink_inputs_not_on_virt_sink(
                vs.sink_name, self._mute_local_off_virt_sink()
            )

    def set_mute_vocoded(self, v: bool) -> None:
        """With Pulse VM: unload monitor loopback when vocoded preview off (unless dry sidetone); null sink unchanged. Without VM, zeros vocoded output. Vocoder keeps running."""
        self.mute_vocoded = bool(v)
        with self.lock:
            vs = self._virt_setup
        if self.use_pulse_virt_mic and vs is not None:
            iter_move_to_virt_sink(vs.sink_name)
        self._sync_pulse_speaker_loopback()
        if self.use_pulse_virt_mic and vs is not None:
            mute_playback_sink_inputs_not_on_virt_sink(
                vs.sink_name, self._mute_local_off_virt_sink()
            )

    def set_gain(self, v: float) -> None:
        self.gain = float(v)

    def set_wet(self, v: float) -> None:
        self.wet_level = float(v)
        with self.lock:
            if self.voc is not None:
                self.voc.wet_level = self.wet_level

    def set_presence(self, v: float) -> None:
        self.presence_db = max(0.0, float(v))
        with self.lock:
            if self.voc is not None:
                self.voc.mod_presence_db = self.presence_db

    def set_reverb_mix(self, v: float) -> None:
        self.reverb_mix = float(np.clip(v, 0.0, 1.0))
        with self.lock:
            r = self._reverb_ref
        if r is not None:
            r.set_mix(self.reverb_mix)

    def set_reverb_room(self, v: float) -> None:
        self.reverb_room = float(np.clip(v, 0.0, 1.0))
        with self.lock:
            r = self._reverb_ref
        if r is not None:
            r.set_room(self.reverb_room)

    def load_from_path(self, path_str: str, err_to_status: bool = True) -> str:
        p = Path(path_str.strip()).expanduser()
        self.path_hint = str(p)
        if not p.is_file():
            msg = f"Not a file: {p}"
            if err_to_status:
                self.last_status = msg
            return msg
        try:
            data = load_audio_ffmpeg(p, self.sr)
        except Exception as e:
            msg = str(e)
            if err_to_status:
                self.last_status = msg
            return msg
        if data.size < self.n_fft:
            msg = "Carrier too short."
            if err_to_status:
                self.last_status = msg
            return msg
        self.carrier_arr = np.asarray(data, dtype=np.float32).copy()
        self.last_status = f"Loaded: {p.name} ({len(data) / self.sr:.2f}s)"
        return self.last_status

    def apply_carrier_live(self) -> str:
        if self.carrier_arr is None:
            return "No carrier in memory."
        if self.voc is None:
            return self.last_status
        with self.lock:
            self.voc.replace_carrier(np.asarray(self.carrier_arr, dtype=np.float64))
        self.last_status = self.last_status + " — applied live"
        return self.last_status

    def try_default_carrier(self, initial: Path | None) -> None:
        ensure_carrier_library_dir()
        if initial and initial.is_file():
            self.load_from_path(str(initial), err_to_status=False)
            return
        lib = list_carrier_files()
        if lib:
            self.path_hint = str(lib[0])
            self.load_from_path(str(lib[0]), err_to_status=False)

    def _run_stream(self) -> None:
        car = self.carrier_arr
        if car is None:
            self.last_status = "No carrier loaded."
            return
        self._stream_started_with_virt_mic = self.use_pulse_virt_mic
        self._pulse_monitor_warn = None
        with self.lock:
            self._mic_silent_streak = 0
        carrier_64 = np.asarray(car, dtype=np.float64).copy()
        voc = StreamingVocoder(
            carrier_64,
            self.sr,
            n_fft=self.n_fft,
            hop_length=self.hop,
            wet_level=self.wet_level,
            mode="bands",
            n_bands=36,
            band_smooth=0.62,
            mod_presence_db=self.presence_db,
        )
        with self.lock:
            self.voc = voc

        cable_out: int | None = None
        self._stream_virt_cable = False
        if self.use_pulse_virt_mic:
            # Read fresh so mute/hear toggles right before Start match setup.
            hear_lb = self._want_monitor_loopback_playing()
            ok, vsetup, cable_out, vmsg = setup_virt_mic_unified(
                sink_name=self.pulse_sink_name,
                description=self.pulse_sink_description,
                hear_on_default_sink=hear_lb,
                cable_name_hint=self.virt_mic_device_hint,
            )
            with self.lock:
                self._virt_setup = vsetup
            self.last_status = vmsg
            if not ok:
                with self.lock:
                    self.voc = None
                return
            self._stream_virt_cable = cable_out is not None and vsetup is None
            # Align loopback with current mute / “hear on speakers” (may differ from hear_lb at setup time).
            self._sync_pulse_speaker_loopback()
            if vsetup is not None and not source_exists(
                virtual_mic_source_name(self.pulse_sink_name)
            ):
                self.last_status = (
                    f"{self.last_status} "
                    f"{status_hint_missing_virt_mic_source(self.pulse_sink_description, self.pulse_sink_name)}"
                )

        block = self.block
        if self.use_pulse_virt_mic:
            if self._virt_setup is not None:
                from pulse_portaudio import resolve_stream_device_for_pulse_virt_mic

                pa_idx = resolve_stream_device_for_pulse_virt_mic()
                out_dev = pa_idx
                if pa_idx is not None:
                    try:
                        di = sd.query_devices(pa_idx)
                        dn = di.get("name", "?")
                        inch = int(di.get("max_input_channels") or 0)
                        self.last_status = (
                            f"{self.last_status} Pulse stream → [{pa_idx}] {dn!r} "
                            f"(in_ch={inch}, PULSE_SINK=`{self.pulse_sink_name}`)."
                        )
                    except Exception:
                        pass
                else:
                    out_dev = None
                    self.last_status = (
                        f"{self.last_status} "
                        "No ALSA **pulse**/**pipewire** PortAudio device found — "
                        "audio may not reach the virtual sink; install **pipewire-alsa** "
                        "or use a Pulse-backed default."
                    )
            else:
                out_dev = cable_out
        else:
            out_dev = self.out_dev

        stream_in = self.in_dev
        stream_out = out_dev
        duplex_off = os.environ.get("LIVE_VOCODER_PULSE_DUPLEX", "1").lower() in (
            "0",
            "false",
            "no",
            "off",
        )
        if sys.platform == "linux" and not duplex_off and not self._input_device_explicit:
            from pulse_portaudio import (
                resolve_duplex_pulse_device,
                resolve_stream_device_for_pulse_virt_mic,
            )

            pd = resolve_duplex_pulse_device()
            if self.use_pulse_virt_mic and self._virt_setup is not None:
                # Same duplex PCM for mic + null-sink playback (matches out_dev above).
                pvm = resolve_stream_device_for_pulse_virt_mic()
                if pvm is not None:
                    stream_in = pvm
                    stream_out = pvm
                elif pd is not None and stream_out is None:
                    stream_in = pd
                    stream_out = pd
            elif self.use_pulse_virt_mic and self._stream_virt_cable and pd is not None:
                stream_in = pd
            elif not self.use_pulse_virt_mic and not self._output_device_explicit and pd is not None:
                stream_in = pd
                stream_out = pd

        if self.use_pulse_virt_mic and self._virt_setup is not None:
            from pulse_portaudio import resolve_stream_device_for_pulse_virt_mic

            if stream_out is None:
                fix = resolve_stream_device_for_pulse_virt_mic()
                if fix is not None:
                    stream_out = fix
            if stream_out is None:
                self.last_status = (
                    f"{self.last_status} **Error:** no Pulse-compatible PortAudio output device — "
                    "audio cannot reach the virtual sink. Install **pipewire-alsa** "
                    "(Arch: `sudo pacman -S pipewire-alsa`) and ensure `python -m sounddevice` "
                    "lists **pulse** or **pipewire**. "
                    + PULSE_VIRT_MIC_TROUBLESHOOT
                )
                with self.lock:
                    vs = self._virt_setup
                    self._virt_setup = None
                teardown_unified_virt_mic(vs)
                with self.lock:
                    self.voc = None
                return
            with self.lock:
                h_self = self.hear_self
                h_voc = self.hear_vocoded
            if not h_self and not h_voc:
                self.last_status = (
                    f"{self.last_status} **Note:** “Hear vocoded” and “Hear myself” are both off — "
                    "local headphones / monitor loopback are silent; virtual mic still receives vocoded audio."
                )

        lat = os.environ.get("LIVE_VOCODER_STREAM_LATENCY", "high").lower()
        if lat not in ("low", "high"):
            lat = "high"

        reverb = MonoConvolutionReverb(
            self.sr,
            mix=self.reverb_mix,
            room=self.reverb_room,
        )
        with self.lock:
            self._reverb_ref = reverb

        def callback(indata, outdata, frames, time_info, status) -> None:
            mono = indata[:, 0].astype(np.float32)
            if mono.shape[0] != block:
                return
            rms = float(np.sqrt(np.mean(np.float64(mono) * mono)))
            with self.lock:
                if rms < 4e-5:
                    self._mic_silent_streak = min(self._mic_silent_streak + 1, 100000)
                else:
                    self._mic_silent_streak = 0
                g = self.gain
                hear = self.hear_self
                hear_voc = self.hear_vocoded
                mute_sp = self.mute_speakers
                mute_voc = self.mute_vocoded
                uvm = self.use_pulse_virt_mic
                virt_mic_routing = (
                    self._virt_setup is not None or self._stream_virt_cable
                )
                voc.wet_level = self.wet_level
                voc.mod_presence_db = self.presence_db
                clean = self.clean_mic
            if clean:
                y = np.asarray(mono, dtype=np.float32).copy()
            else:
                y = voc.process_block(mono)
                if y.size != block:
                    outdata[:] = 0
                    return
                hear_out = hear and not (
                    (mute_sp or (mute_voc and not hear)) and uvm and virt_mic_routing
                )
                if hear_out:
                    y = np.asarray(mono, dtype=np.float32).copy()
                elif not hear_voc and not virt_mic_routing:
                    # Local preview off: silence speakers only; VM / cable still needs vocoded samples.
                    y = np.zeros(block, dtype=np.float32)
                if mute_voc and not hear_out and not virt_mic_routing:
                    y.fill(0.0)
            with self.lock:
                r = self._reverb_ref
            if r is not None and not clean:
                y = r.process(y)
            np.clip(y * g, -1.0, 1.0, out=y)
            if mute_sp:
                if not uvm:
                    y.fill(0.0)
                elif not virt_mic_routing:
                    y.fill(0.0)
            for ch in range(outdata.shape[1]):
                outdata[:, ch] = y

        try:
            if self.use_pulse_virt_mic and self._virt_setup is not None:
                apply_pulse_sink_env(self.pulse_sink_name)
            with sd.Stream(
                samplerate=self.sr,
                blocksize=block,
                dtype="float32",
                channels=(1, self.n_out),
                callback=callback,
                latency=lat,
                device=(stream_in, stream_out),
            ):
                if self.use_pulse_virt_mic and self._virt_setup is not None:
                    # Brief settle: PULSE_SINK often applies on first buffers; immediate move-sink-input
                    # reconnects the stream and can cause audible cutouts on PipeWire.
                    _raw_settle = os.environ.get(
                        "LIVE_VOCODER_PULSE_MOVE_SETTLE_SEC", "0.05"
                    ).strip()
                    try:
                        _settle = float(_raw_settle)
                    except ValueError:
                        _settle = 0.05
                    time.sleep(max(0.0, min(_settle, 0.5)))
                    iter_move_to_virt_sink(self.pulse_sink_name)
                    self._sync_pulse_speaker_loopback()
                    if self._mute_local_off_virt_sink():
                        mute_playback_sink_inputs_not_on_virt_sink(
                            self.pulse_sink_name, True
                        )
                    skip_chk = os.environ.get(
                        "LIVE_VOCODER_SKIP_PULSE_PLAYBACK_CHECK", ""
                    ).strip().lower() in ("1", "true", "yes", "on")
                    if skip_chk:
                        self.last_status = "Streaming…"
                    elif not wait_for_live_vocoder_playback_sink_input(self.pulse_sink_name):
                        self.last_status = (
                            "Streaming… **Pulse:** no playback stream for this process in "
                            "`pactl list sink-inputs` after retries — PortAudio is probably not "
                            "using PipeWire (raw ALSA ignores `PULSE_SINK`). "
                            "Install **pipewire-alsa** (Arch: `sudo pacman -S pipewire-alsa`), then "
                            "run `python -m sounddevice` and set "
                            "`LIVE_VOCODER_PORTAUDIO_OUTPUT=<index>` to a **pulse** or **pipewire** "
                            "output. Use the same login session as your desktop (or match "
                            "`XDG_RUNTIME_DIR` / `PULSE_SERVER` to the graphical session). "
                            "Expert: `LIVE_VOCODER_SKIP_PULSE_PLAYBACK_CHECK=1` skips this check. "
                            + PULSE_VIRT_MIC_TROUBLESHOOT
                        )
                    else:
                        self.last_status = "Streaming…"
                while not self.stop_event.is_set():
                    time.sleep(0.2)
        except Exception as e:
            self.last_status = f"Audio error: {e}"
        finally:
            with self.lock:
                vs = self._virt_setup
                self._virt_setup = None
            if vs is not None and vs.remap_module_id is not None:
                self._pulse_remap_module_id = vs.remap_module_id
            teardown_unified_virt_mic(vs, unload_remapped_mic=False)
            self._stream_virt_cable = False
            with self.lock:
                self.voc = None
                self._reverb_ref = None
            self.last_status = "Stopped."
            self._pulse_monitor_warn = None
            with self.lock:
                self._mic_silent_streak = 0

    def start(self) -> str:
        if self._audio_thread is not None and self._audio_thread.is_alive():
            return self.last_status
        if self.carrier_arr is None and self.path_hint:
            self.load_from_path(self.path_hint)
        if self.carrier_arr is None:
            self.last_status = "Load a carrier file first."
            return self.last_status
        self.stop_event.clear()
        self._audio_thread = threading.Thread(target=self._run_stream, daemon=True)
        self._audio_thread.start()
        return self.last_status

    def stop(self) -> str:
        self.stop_event.set()
        if self._audio_thread is not None:
            self._audio_thread.join(timeout=3.0)
        self._audio_thread = None
        return self.last_status

    def shutdown(self) -> None:
        self.stop()
        self._pulse_remap_module_id = None
        if not self.use_pulse_virt_mic:
            return
        from pulse_virtmic import pactl_available, teardown_pulse_virt_mic_full

        if pactl_available():
            teardown_pulse_virt_mic_full(self.pulse_sink_name)
        if os.environ.get("PULSE_SINK") == self.pulse_sink_name:
            os.environ.pop("PULSE_SINK", None)
