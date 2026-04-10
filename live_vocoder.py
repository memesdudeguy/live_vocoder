#!/usr/bin/env python3
"""
Live microphone vocoder: spectral envelope from your voice, phase from a carrier (MP3 loop).
Requires: ffmpeg (for MP3), PortAudio (sounddevice), NumPy/SciPy.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

import numpy as np
import sounddevice as sd
from pulse_virtmic import resolved_pulse_sink_identity
from scipy import signal
from scipy.io import wavfile

_DEFAULT_VM_SINK, _DEFAULT_VM_DESC = resolved_pulse_sink_identity()


def load_audio_ffmpeg(path: Path, sample_rate: int) -> np.ndarray:
    """Decode any format ffmpeg understands to mono float32 at sample_rate."""
    cmd = [
        "ffmpeg",
        "-nostdin",
        "-hide_banner",
        "-loglevel",
        "error",
        "-i",
        str(path),
        "-f",
        "f32le",
        "-acodec",
        "pcm_f32le",
        "-ac",
        "1",
        "-ar",
        str(sample_rate),
        "-",
    ]
    proc = subprocess.run(cmd, capture_output=True)
    if proc.returncode != 0:
        err = proc.stderr.decode(errors="replace") or "ffmpeg failed"
        raise RuntimeError(err)
    if not proc.stdout:
        raise RuntimeError("ffmpeg produced no audio")
    return np.frombuffer(proc.stdout, dtype=np.float32).copy()


def _log_band_slices(n_fft: int, n_bands: int) -> list[tuple[int, int]]:
    """Perceptual-ish log-spaced bands over rFFT bins 1..n_fft//2 (skip DC)."""
    hi = n_fft // 2
    if hi < 2:
        return [(1, hi + 1)]
    edges = np.unique(np.round(np.geomspace(1, hi, n_bands + 1)).astype(int))
    edges = np.clip(edges, 1, hi)
    out: list[tuple[int, int]] = []
    for a, b in zip(edges[:-1], edges[1:]):
        lo_b, hi_b = int(a), int(b)
        if hi_b <= lo_b:
            continue
        out.append((lo_b, hi_b + 1))
    return out if out else [(1, hi + 1)]


def _noise_reverb_ir(sample_rate: int, tail_ms: float, room: float, seed: int = 42) -> np.ndarray:
    """Short colored-noise impulse for cheap convolution reverb (mono)."""
    tail_ms = float(np.clip(tail_ms, 40.0, 200.0))
    room = float(np.clip(room, 0.0, 1.0))
    L = max(32, int(sample_rate * tail_ms / 1000.0))
    L = min(L, int(0.22 * sample_rate))
    rng = np.random.default_rng(seed)
    t = np.linspace(0.0, 1.0, L, dtype=np.float64)
    decay = 3.0 + room * 14.0
    env = np.exp(-decay * t)
    ir = rng.standard_normal(L).astype(np.float64) * env
    ir -= float(np.mean(ir))
    peak = float(np.max(np.abs(ir))) + 1e-9
    ir = (ir / peak * 0.5).astype(np.float32)
    return ir


class MonoConvolutionReverb:
    """
    Streaming mono wet/dry reverb via short noise IR and overlap-save convolution.
    ``mix`` is adjustable in real time; ``set_room`` rebuilds the tail (safe under PortAudio callback).
    """

    def __init__(
        self,
        sample_rate: int,
        mix: float = 0.0,
        room: float = 0.45,
        tail_ms: float = 110.0,
    ) -> None:
        self.sr = int(sample_rate)
        self.mix = float(np.clip(mix, 0.0, 1.0))
        self.room = float(np.clip(room, 0.0, 1.0))
        self._tail_ms = float(np.clip(tail_ms, 50.0, 200.0))
        self._lock = threading.Lock()
        self.h = np.ones(1, dtype=np.float32)
        self.L = 1
        self._prev = np.zeros(0, dtype=np.float32)
        self._rebuild_ir()

    def _rebuild_ir(self) -> None:
        self.h = _noise_reverb_ir(self.sr, self._tail_ms, self.room)
        self.L = int(len(self.h))
        self._prev = np.zeros(max(0, self.L - 1), dtype=np.float32)

    def set_mix(self, m: float) -> None:
        self.mix = float(np.clip(m, 0.0, 1.0))

    def set_room(self, r: float) -> None:
        r = float(np.clip(r, 0.0, 1.0))
        with self._lock:
            if abs(r - self.room) < 1e-5:
                return
            self.room = r
            self._rebuild_ir()

    def reset(self) -> None:
        with self._lock:
            self._prev.fill(0.0)

    def process(self, x: np.ndarray) -> np.ndarray:
        if self.mix < 1e-6:
            # NumPy 2.x: copy=False raises if a cast requires a copy (e.g. from float64 buffer).
            return np.asarray(x, dtype=np.float32)
        x = np.asarray(x, dtype=np.float32).ravel()
        if x.size == 0:
            return x
        m = float(self.mix)
        with self._lock:
            h = self.h
            L = self.L
            sig = np.concatenate([self._prev, x])
            wet = np.convolve(sig.astype(np.float64), h.astype(np.float64), mode="valid").astype(
                np.float32
            )
            if wet.shape[0] == x.shape[0] and L > 0:
                self._prev = sig[-(L - 1) :].astype(np.float32).copy()
            dry = x
            y = dry * (1.0 - m) + wet * m
        return np.clip(y, -1.0, 1.0)


class StreamingVocoder:
    def __init__(
        self,
        carrier: np.ndarray,
        sample_rate: int,
        n_fft: int = 2048,
        hop_length: int | None = None,
        carrier_mix: float = 1.0,
        dry_mix: float = 0.0,
        wet_level: float = 1.0,
        mode: str = "bands",
        n_bands: int = 36,
        band_smooth: float = 0.62,
        mic_hp_hz: float = 100.0,
        mic_preemph: float = 0.88,
        mod_presence_db: float = 4.0,
        mod_presence_hz: float = 1800.0,
    ):
        self.sr = sample_rate
        self.n_fft = n_fft
        self.hop_length = hop_length if hop_length is not None else n_fft // 4
        if self.n_fft % self.hop_length != 0:
            raise ValueError("n_fft should be divisible by hop_length for clean OLA")
        self.win = np.hanning(n_fft).astype(np.float64)
        self.carrier = np.asarray(carrier, dtype=np.float64)
        if self.carrier.size < n_fft:
            raise ValueError("Carrier must be at least n_fft samples long")
        self.carrier_pos = 0
        self.mic_buf = np.zeros(n_fft, dtype=np.float64)
        self.synth_buf = np.zeros(n_fft, dtype=np.float64)
        self.carrier_mix = float(carrier_mix)
        self.dry_mix = float(dry_mix)
        self.wet_level = float(wet_level)
        self._eps = 1e-7
        self.mode = mode
        self.n_bands_cfg = n_bands
        self.band_slices = _log_band_slices(n_fft, n_bands)
        self._band_smooth = float(np.clip(band_smooth, 0.0, 0.999))
        self._band_env = np.zeros(len(self.band_slices), dtype=np.float64)
        self.mic_hp_hz = float(max(0.0, mic_hp_hz))
        self.mic_preemph = float(np.clip(mic_preemph, 0.0, 0.99))
        self.mod_presence_db = float(max(0.0, mod_presence_db))
        self.mod_presence_hz = float(max(200.0, mod_presence_hz))
        self._rfft_freqs = np.fft.rfftfreq(n_fft, 1.0 / sample_rate)
        ny = 0.5 * sample_rate
        self._hp_sos: np.ndarray | None = None
        self._hp_zi: np.ndarray | None = None
        if self.mic_hp_hz > 1.0 and self.mic_hp_hz < ny * 0.45:
            wn = self.mic_hp_hz / ny
            self._hp_sos = signal.butter(2, wn, btype="high", output="sos")
            self._hp_zi = signal.sosfilt_zi(self._hp_sos)
        self._pre_x = 0.0

    def replace_carrier(self, carrier: np.ndarray) -> None:
        """Swap looped carrier audio; resets envelopes and buffers (for GUI / hot swap)."""
        c = np.asarray(carrier, dtype=np.float64).copy()
        if c.size < self.n_fft:
            raise ValueError("Carrier must be at least n_fft samples long")
        self.carrier = c
        self.carrier_pos = 0
        self._band_env.fill(0.0)
        self.synth_buf.fill(0.0)
        self.mic_buf.fill(0.0)
        self._pre_x = 0.0
        if self._hp_sos is not None:
            self._hp_zi = signal.sosfilt_zi(self._hp_sos)

    def _condition_mic(self, x: np.ndarray) -> np.ndarray:
        """High-pass rumble + pre-emphasis so consonants/formants read clearer."""
        y = np.asarray(x, dtype=np.float64).ravel().copy()
        if self._hp_sos is not None and self._hp_zi is not None:
            y, self._hp_zi = signal.sosfilt(self._hp_sos, y, zi=self._hp_zi)
        if self.mic_preemph > 1e-6:
            out = np.empty_like(y)
            out[0] = y[0] - self.mic_preemph * self._pre_x
            out[1:] = y[1:] - self.mic_preemph * y[:-1]
            self._pre_x = float(y[-1])
            y = out
        return y

    def _modulator_presence(self, M: np.ndarray) -> np.ndarray:
        """Gentle high-frequency lift on |modulator| (speech intelligibility)."""
        if self.mod_presence_db < 0.05:
            return M
        f0 = self.mod_presence_hz
        nyq = 0.5 * self.sr
        t = np.clip((self._rfft_freqs - f0) / max(nyq - f0, 1.0), 0.0, 1.0)
        g = 1.0 + t * (10.0 ** (self.mod_presence_db / 20.0) - 1.0)
        mag = np.abs(M) * g
        return mag * np.exp(1j * np.angle(M))

    def _next_carrier_frame(self) -> np.ndarray:
        n = self.n_fft
        c = self.carrier
        L = len(c)
        end = self.carrier_pos + n
        if end <= L:
            frame = c[self.carrier_pos : end]
        else:
            frame = np.concatenate((c[self.carrier_pos :], c[: end - L]))
        self.carrier_pos = (self.carrier_pos + self.hop_length) % L
        return frame

    def _peek_carrier_frame(self) -> np.ndarray:
        """Same samples as the next _next_carrier_frame() would return, without advancing."""
        n = self.n_fft
        c = self.carrier
        L = len(c)
        end = self.carrier_pos + n
        if end <= L:
            return np.asarray(c[self.carrier_pos : end], dtype=np.float64)
        return np.concatenate((c[self.carrier_pos :], c[: end - L]))

    def _vocode_spectrum(self, M: np.ndarray, C: np.ndarray) -> np.ndarray:
        M = self._modulator_presence(M)
        if self.mode == "phase":
            phase = np.angle(C + self._eps)
            mag = np.abs(M)
            return mag * np.exp(1j * phase) * self.carrier_mix

        V = np.zeros_like(C, dtype=np.complex128)
        V[0] = 0.0
        for i, (a, b) in enumerate(self.band_slices):
            sl = slice(a, b)
            m_b = M[sl]
            c_b = C[sl]
            env = float(np.sqrt(np.mean(np.abs(m_b) ** 2)))
            car_rms = float(np.sqrt(np.mean(np.abs(c_b) ** 2)))
            car_rms = max(car_rms, self._eps)
            env = max(env, self._eps)
            self._band_env[i] = self._band_smooth * self._band_env[i] + (
                1.0 - self._band_smooth
            ) * env
            scale = (self._band_env[i] / car_rms) * self.carrier_mix
            scale = min(scale, 80.0)
            V[sl] = scale * c_b
        return V

    def process_block(self, mic_mono: np.ndarray) -> np.ndarray:
        """mic_mono: shape (frames,) float; returns same length vocoded."""
        mic_mono = self._condition_mic(np.asarray(mic_mono, dtype=np.float64))
        h = self.hop_length
        out = np.zeros(len(mic_mono), dtype=np.float64)
        o = 0
        i = 0
        while i + h <= len(mic_mono):
            hop = mic_mono[i : i + h]
            self.mic_buf[:-h] = self.mic_buf[h:]
            self.mic_buf[-h:] = hop
            mod_w = self.mic_buf * self.win
            car = self._next_carrier_frame() * self.win
            M = np.fft.rfft(mod_w)
            C = np.fft.rfft(car)
            V = self._vocode_spectrum(M, C)
            y = np.fft.irfft(V, n=self.n_fft) * self.win
            self.synth_buf += y
            chunk = (self.synth_buf[:h] * self.wet_level).astype(np.float32)
            if self.dry_mix != 0:
                chunk = chunk + self.dry_mix * hop.astype(np.float32)
            out[o : o + h] = chunk
            self.synth_buf[:-h] = self.synth_buf[h:]
            self.synth_buf[-h:] = 0.0
            o += h
            i += h
        return out[:o]


def main() -> None:
    p = argparse.ArgumentParser(description="Live mic vocoder with MP3 carrier")
    p.add_argument(
        "carrier",
        nargs="?",
        default=None,
        type=Path,
        help="Carrier audio file (MP3/WAV/etc.); optional when using --gtk-gui / --web-gui / --gui",
    )
    p.add_argument("--sr", type=int, default=48000, help="Sample rate (default 48000)")
    p.add_argument("--block", type=int, default=512, help="Audio block size (lower = less latency, more CPU)")
    p.add_argument("--n-fft", type=int, default=2048, help="FFT size (must be multiple of hop)")
    p.add_argument("--hop", type=int, default=None, help="STFT hop (default n_fft/4)")
    p.add_argument("--gain", type=float, default=6.0, help="Output gain after mix (default loud; output is clipped to ±1)")
    p.add_argument(
        "--wet",
        type=float,
        default=1.15,
        metavar="X",
        help="Vocoded voice level before dry mix (0..4; default 1.15, louder)",
    )
    p.add_argument("--dry", type=float, default=0.0, help="Mix raw mic 0..1 (monitor)")
    p.add_argument(
        "--mute-vocoded",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="With --virt-mic + Pulse: mute local vocoded preview; null sink/virtual mic stay hot. "
        "Without Pulse VM: silence vocoded output. Default: on (start muted locally). "
        "Use --no-mute-vocoded to hear vocoded in your speakers.",
    )
    p.add_argument(
        "--virt-mic",
        action="store_true",
        help="Route vocoded audio for other apps: Pulse/PipeWire when available, else virtual cable "
        "(VB-Cable / BlackHole; see --virt-mic-device)",
    )
    p.add_argument(
        "--no-virt-mic",
        action="store_true",
        help="Disable virtual mic routing. GTK on Linux normally enables it automatically when pactl is "
        "available (PipeWire-Pulse).",
    )
    p.add_argument(
        "--virt-mic-sink",
        default=_DEFAULT_VM_SINK,
        help="Null sink name for --virt-mic (env LIVE_VOCODER_PULSE_SINK overrides default; use a "
        "unique name if another virtual mic already uses live_vocoder).",
    )
    p.add_argument(
        "--virt-mic-desc",
        default=_DEFAULT_VM_DESC,
        help="Device description in Pulse/PipeWire UI (env LIVE_VOCODER_PULSE_DESCRIPTION).",
    )
    p.add_argument(
        "--virt-mic-device",
        default=None,
        metavar="SUBSTRING",
        help="If Pulse is unavailable, match this substring in an output device name "
        "(e.g. CABLE, BlackHole). Env LIVE_VOCODER_VIRT_DEVICE overrides when set.",
    )
    p.add_argument(
        "--virt-mic-hear",
        action=argparse.BooleanOptionalAction,
        default=False,
        help="With --virt-mic, loopback monitor of the null sink to your default output (hear vocoder locally). "
        "Default: off (silent locally).",
    )
    p.add_argument(
        "--mute-speakers",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Mute local headphone/speaker path; full vocoded audio still goes to the virtual mic (Pulse null sink). "
        "With --virt-mic, local silence is via loopback mute/unload. Default: on. Use --no-mute-speakers to hear locally.",
    )
    p.add_argument(
        "--hear-local",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="GTK + Linux + virtual mic: **Monitor on my speakers** + **Hear vocoded** start ON so you hear "
        "yourself (PipeWire loopback). Default: on. Use **--no-hear-local** for Discord-only (silent headphones, "
        "VM still works).",
    )
    p.add_argument("--carrier-gain", type=float, default=1.0, help="Carrier path strength")
    p.add_argument("--input-device", type=int, default=None, help="PortAudio input device index")
    p.add_argument("--output-device", type=int, default=None, help="PortAudio output device index")
    p.add_argument(
        "--mode",
        choices=("bands", "phase"),
        default="bands",
        help="bands=channel-style envelopes (default, obvious vocoder); "
        "phase=bin-wise |mic|·phase(carrier) (often sounds like filtered speech)",
    )
    p.add_argument("--n-bands", type=int, default=36, help="Log bands for --mode bands (more = finer / clearer)")
    p.add_argument(
        "--band-smooth",
        type=float,
        default=0.62,
        help="0..0.99 band envelope smoothing (lower = snappier / clearer consonants)",
    )
    p.add_argument(
        "--mic-hpf",
        type=float,
        default=100.0,
        metavar="HZ",
        help="High-pass mic at HZ Hz (0 = off) to cut rumble",
    )
    p.add_argument(
        "--mic-preemph",
        type=float,
        default=0.88,
        help="Mic pre-emphasis 0..0.99 (0=off, ~0.88 brightens speech for the vocoder)",
    )
    p.add_argument(
        "--presence-db",
        type=float,
        default=4.0,
        help="High-frequency lift on modulator spectrum (dB at Nyquist, 0=off)",
    )
    p.add_argument(
        "--presence-hz",
        type=float,
        default=1800.0,
        help="Presence ramp starts at this Hz",
    )
    p.add_argument(
        "--reverb-mix",
        type=float,
        default=0.0,
        metavar="X",
        help="Algorithmic reverb wet/dry on output (0=off .. 1; default 0)",
    )
    p.add_argument(
        "--reverb-room",
        type=float,
        default=0.45,
        metavar="X",
        help="Reverb decay / room size (0..1); rebuilds convolution tail",
    )
    p.add_argument(
        "-q",
        "--quiet",
        action="store_true",
        help="Disable stderr level diagnostics (they are on by default)",
    )
    p.add_argument(
        "--record",
        type=float,
        default=0.0,
        metavar="SEC",
        help="Record SEC seconds of mic + vocoded output to --record-dir, then exit",
    )
    p.add_argument(
        "--record-dir",
        type=Path,
        default=Path("vocoder_debug"),
        help="Output folder for --record",
    )
    p.add_argument(
        "--audio-info",
        action="store_true",
        help="Print OS/kernel hint, PortAudio version, host APIs (WASAPI/ALSA/…), and default devices; exit.",
    )
    p.add_argument("--list-devices", action="store_true", help="Print devices and exit")
    p.add_argument(
        "--diagnose-routing",
        action="store_true",
        help="Print Pulse/PipeWire + PortAudio monitor/routing checklist and exit (no carrier file).",
    )
    p.add_argument(
        "--gui",
        action="store_true",
        help="Open tkinter GUI (needs system Tk; Arch: sudo pacman -S tk). "
        "On Linux with pactl, virtual mic is enabled by default; use --no-virt-mic to disable.",
    )
    p.add_argument(
        "--gtk-gui",
        action="store_true",
        help="GTK 4 window (Arch: sudo pacman -S python-gobject gtk4). "
        "On Linux with pactl, virtual mic is enabled by default; use --no-virt-mic to disable.",
    )
    p.add_argument(
        "--web-gui",
        action="store_true",
        help="Open browser UI (Gradio, no Tk — pip install -r requirements.txt). "
        "On Linux with pactl, virtual mic (PipeWire) is enabled by default; use --no-virt-mic to disable.",
    )
    p.add_argument(
        "--web-host",
        default="127.0.0.1",
        help="Bind address for --web-gui (default 127.0.0.1)",
    )
    p.add_argument(
        "--web-port",
        type=int,
        default=7860,
        help="Port for --web-gui (default 7860)",
    )
    args = p.parse_args()
    no_virt_mic_hear = not args.virt_mic_hear

    if args.audio_info:
        from audio_backend import format_audio_backend_report

        print(format_audio_backend_report())
        raise SystemExit(0)

    _had_virt_mic_flag = bool(args.virt_mic)
    if args.no_virt_mic:
        args.virt_mic = False
    elif (
        sys.platform.startswith("linux")
        and shutil.which("pactl")
        and (args.gtk_gui or args.web_gui or args.gui)
    ):
        # GTK already did this; web/tk did not — without it, ./run.sh --web-gui never creates the null sink.
        args.virt_mic = True

    if (
        args.virt_mic
        and not _had_virt_mic_flag
        and not args.no_virt_mic
        and (args.gtk_gui or args.web_gui or args.gui)
    ):
        print(
            "[live_vocoder] Linux + pactl: virtual mic enabled for this GUI "
            "(use --no-virt-mic to disable).",
            file=sys.stderr,
        )

    if args.gtk_gui:
        from live_vocoder_gtk import run_gtk_gui

        ic = args.carrier.expanduser() if args.carrier is not None else None
        _gtk_vm_only = (
            args.virt_mic
            and sys.platform.startswith("linux")
            and not args.hear_local
        )
        if args.hear_local:
            _mute_sp, _mute_voc = False, False
        else:
            _mute_sp = bool(args.mute_speakers or _gtk_vm_only)
            _mute_voc = bool(args.mute_vocoded or _gtk_vm_only)
        # Pulse loopback “armed” when VM + headphones path on (matches Monitor checkbox).
        _gtk_hear_loop = (
            bool(args.virt_mic)
            and sys.platform.startswith("linux")
            and not _mute_sp
        )
        raise SystemExit(
            run_gtk_gui(
                initial_carrier=ic if ic is not None and ic.is_file() else None,
                sr=args.sr,
                input_device=args.input_device,
                output_device=args.output_device,
                use_pulse_virt_mic=args.virt_mic,
                pulse_sink_name=args.virt_mic_sink,
                pulse_sink_description=args.virt_mic_desc,
                virt_mic_hear_loopback=_gtk_hear_loop,
                mute_speakers=_mute_sp,
                mute_vocoded=_mute_voc,
                virt_mic_device_hint=args.virt_mic_device,
                reverb_mix=float(np.clip(args.reverb_mix, 0.0, 1.0)),
                reverb_room=float(np.clip(args.reverb_room, 0.0, 1.0)),
            )
        )

    if args.web_gui:
        from live_vocoder_web import run_web_gui

        ic = args.carrier.expanduser() if args.carrier is not None else None
        run_web_gui(
            initial_carrier=ic if ic is not None and ic.is_file() else None,
            sr=args.sr,
            input_device=args.input_device,
            output_device=args.output_device,
            server_name=args.web_host,
            server_port=args.web_port,
            use_pulse_virt_mic=args.virt_mic,
            pulse_sink_name=args.virt_mic_sink,
            pulse_sink_description=args.virt_mic_desc,
            virt_mic_hear_loopback=args.virt_mic_hear,
            mute_speakers=args.mute_speakers,
            mute_vocoded=args.mute_vocoded,
            virt_mic_device_hint=args.virt_mic_device,
            reverb_mix=float(np.clip(args.reverb_mix, 0.0, 1.0)),
            reverb_room=float(np.clip(args.reverb_room, 0.0, 1.0)),
        )
        return

    if args.gui:
        from live_vocoder_gui import run_gui_safe

        ic = args.carrier.expanduser() if args.carrier is not None else None
        run_gui_safe(
            initial_carrier=ic if ic is not None and ic.is_file() else None,
            sr=args.sr,
            input_device=args.input_device,
            output_device=args.output_device,
            use_pulse_virt_mic=args.virt_mic,
            pulse_sink_name=args.virt_mic_sink,
            pulse_sink_description=args.virt_mic_desc,
            virt_mic_hear_loopback=args.virt_mic_hear,
            mute_speakers=args.mute_speakers,
            mute_vocoded=args.mute_vocoded,
            virt_mic_device_hint=args.virt_mic_device,
            reverb_mix=float(np.clip(args.reverb_mix, 0.0, 1.0)),
            reverb_room=float(np.clip(args.reverb_room, 0.0, 1.0)),
        )
        return

    if args.list_devices:
        print(sd.query_devices())
        return

    if args.diagnose_routing:
        from diagnose_routing import run_diagnose

        raise SystemExit(run_diagnose(args.virt_mic_sink, args.virt_mic_desc))

    if args.carrier is None:
        print(
            "No carrier file given. Use:\n"
            "  live_vocoder.py --web-gui          (browser UI; works on Windows/macOS/Linux)\n"
            "  live_vocoder.py --gtk-gui        (Linux; needs PyGObject + GTK 4)\n"
            "  live_vocoder.py --gui            (Tkinter)\n"
            "  live_vocoder.py /path/to/carrier.mp3\n",
            file=sys.stderr,
        )
        sys.exit(2)
    carrier_path = args.carrier.expanduser()
    if not carrier_path.is_file():
        print(f"Carrier file not found: {carrier_path}", file=sys.stderr)
        sys.exit(1)

    sr = args.sr
    hop = args.hop if args.hop is not None else args.n_fft // 4
    if args.n_fft % hop != 0:
        print("n_fft must be divisible by hop", file=sys.stderr)
        sys.exit(1)
    if args.block % hop != 0:
        print(f"Warning: block size {args.block} not multiple of hop {hop}; use e.g. {hop * (max(1, args.block // hop))}", file=sys.stderr)

    print("Loading carrier via ffmpeg…")
    carrier = load_audio_ffmpeg(carrier_path, sr)
    print(f"Carrier: {carrier_path.name}, {len(carrier) / sr:.2f}s @ {sr} Hz mono")

    wet = float(np.clip(args.wet, 0.0, 4.0))
    voc = StreamingVocoder(
        carrier,
        sr,
        n_fft=args.n_fft,
        hop_length=hop,
        carrier_mix=args.carrier_gain,
        dry_mix=args.dry,
        wet_level=wet,
        mode=args.mode,
        n_bands=args.n_bands,
        band_smooth=args.band_smooth,
        mic_hp_hz=args.mic_hpf,
        mic_preemph=args.mic_preemph,
        mod_presence_db=args.presence_db,
        mod_presence_hz=args.presence_hz,
    )

    block = args.block
    # Pad block to full hops for stable callback
    hops_per_block = max(1, block // hop)
    block = hops_per_block * hop

    gain = float(args.gain)

    in_dev = args.input_device if args.input_device is not None else sd.default.device[0]
    out_dev = args.output_device if args.output_device is not None else sd.default.device[1]
    if args.virt_mic and args.output_device is not None:
        print(
            "Note: --virt-mic uses PULSE_SINK; ignoring --output-device for playback.",
            file=sys.stderr,
        )

    try:
        out_info = sd.query_devices(out_dev, "output")
    except Exception:
        out_info = sd.query_devices(out_dev)
    n_out = min(2, int(out_info.get("max_output_channels") or 1))
    n_out = max(1, n_out)

    try:
        in_info = sd.query_devices(in_dev, "input")
    except Exception:
        in_info = sd.query_devices(in_dev)
    print(f"Input:  [{in_dev}] {in_info['name']}")
    if args.virt_mic:
        print(
            f"Output: virtual mic routing active — Pulse sink `{args.virt_mic_sink}` when `pactl` works, "
            f"else a virtual cable output (hint: {args.virt_mic_device!r}). "
            f"In other apps pick **{args.virt_mic_desc} Virtual Mic** / cable output / Monitor of …."
        )
    else:
        print(f"Output: [{out_dev}] {out_info['name']} ({n_out} ch)")
    diag = not args.quiet
    print(
        f"Vocoder mode={args.mode} (bands={args.n_bands}, smooth={args.band_smooth}); "
        f"wet={wet} gain={gain}; "
        f"reverb_mix={float(np.clip(args.reverb_mix, 0.0, 1.0))} "
        f"reverb_room={float(np.clip(args.reverb_room, 0.0, 1.0))}"
        + ("; diagnostics on stderr (~2/s, use --quiet to off)" if diag else "")
    )

    record_sec = float(args.record)
    record_dir = args.record_dir.expanduser()
    rec_n = int(max(0.0, record_sec) * sr)
    rec_mic = np.zeros(rec_n, dtype=np.float32) if rec_n > 0 else None
    rec_voc = np.zeros(rec_n, dtype=np.float32) if rec_n > 0 else None
    rec_pos = 0
    rec_done = threading.Event()
    last_diag = time.monotonic()
    reverb = MonoConvolutionReverb(
        sr,
        mix=float(np.clip(args.reverb_mix, 0.0, 1.0)),
        room=float(np.clip(args.reverb_room, 0.0, 1.0)),
    )

    def _write_wav16(path: Path, x: np.ndarray) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        clip = np.clip(x.astype(np.float64), -1.0, 1.0)
        pcm = (clip * 32767.0).astype(np.int16)
        wavfile.write(str(path), sr, pcm)

    def callback(indata, outdata, frames, time_info, status):
        nonlocal rec_pos, last_diag
        if status:
            print(status, file=sys.stderr)
        mono = indata[:, 0].astype(np.float32)
        if mono.shape[0] != block:
            # Should not happen if block fixed
            return
        y = voc.process_block(mono)
        if y.size != block:
            outdata[:] = 0
            return
        y_play = np.array(y, dtype=np.float32, copy=True)
        if args.mute_vocoded and not (args.virt_mic and virt_setup is not None):
            y_play.fill(0.0)
        y_play = reverb.process(y_play)
        if args.mute_speakers and not args.virt_mic:
            y_play.fill(0.0)
        if diag:
            now = time.monotonic()
            if now - last_diag >= 0.5:
                last_diag = now
                y_pre = y_play.copy()
                np.clip(y_pre * gain, -1.0, 1.0, out=y_pre)
                sys.stderr.write(
                    f"[vocoder] mic_rms={float(np.sqrt(np.mean(mono * mono))):.5f} "
                    f"mic_peak={float(np.max(np.abs(mono))):.5f} "
                    f"out_rms={float(np.sqrt(np.mean(y_pre * y_pre))):.5f} "
                    f"(wet={wet} gain={gain})\n"
                )
                sys.stderr.flush()
        if rec_n > 0 and rec_pos < rec_n:
            take = min(block, rec_n - rec_pos)
            rec_mic[rec_pos : rec_pos + take] = mono[:take]
            y_cap = y_play[:take].copy()
            np.clip(y_cap * gain, -1.0, 1.0, out=y_cap)
            rec_voc[rec_pos : rec_pos + take] = y_cap
            rec_pos += take
            if rec_pos >= rec_n:
                rec_done.set()
        np.clip(y_play * gain, -1.0, 1.0, out=y_play)
        for ch in range(outdata.shape[1]):
            outdata[:, ch] = y_play

    virt_setup = None
    cable_out: int | None = None
    stream_out: int | None = out_dev
    if args.virt_mic:
        from virt_mic_unified import setup_virt_mic_unified, teardown_unified_virt_mic

        # Match vocoder_session._want_monitor_loopback_playing: no monitor loopback if vocoded
        # preview is muted unless dry mic is mixed in (--dry > 0 ≈ hear_self).
        hear_lb = (
            (not no_virt_mic_hear)
            and (not args.mute_speakers)
            and (not args.mute_vocoded or args.dry > 0)
        )
        ok, virt_setup, cable_out, vmsg = setup_virt_mic_unified(
            sink_name=args.virt_mic_sink,
            description=args.virt_mic_desc,
            hear_on_default_sink=hear_lb,
            cable_name_hint=args.virt_mic_device,
        )
        print(vmsg)
        if not ok:
            sys.exit(1)
        stream_out = None if virt_setup is not None else cable_out
        if virt_setup is not None:
            from pulse_portaudio import resolve_stream_device_for_pulse_virt_mic

            pa_idx = resolve_stream_device_for_pulse_virt_mic()
            if pa_idx is not None:
                stream_out = pa_idx
                try:
                    di = sd.query_devices(pa_idx)
                    dn = di.get("name", "?")
                    inch = int(di.get("max_input_channels") or 0)
                    print(
                        f"PortAudio duplex [{pa_idx}] {dn!r} (in_ch={inch}) — "
                        f"PULSE_SINK={args.virt_mic_sink!r} routes playback to the null sink."
                    )
                except Exception:
                    print(
                        f"PortAudio [{pa_idx}] — PULSE_SINK={args.virt_mic_sink!r}."
                    )
            else:
                stream_out = None
                print(
                    "Warning: no ALSA pulse/pipewire PortAudio device; virtual mic may stay silent. "
                    "Install pipewire-alsa and check `python -m sounddevice`.",
                    file=sys.stderr,
                )

    pre_duplex_out = stream_out
    stream_in = in_dev
    duplex_off = os.environ.get("LIVE_VOCODER_PULSE_DUPLEX", "1").lower() in (
        "0",
        "false",
        "no",
        "off",
    )
    input_explicit = args.input_device is not None
    output_explicit = args.output_device is not None
    if sys.platform == "linux" and not duplex_off and not input_explicit:
        from pulse_portaudio import (
            resolve_duplex_pulse_device,
            resolve_stream_device_for_pulse_virt_mic,
        )

        pd = resolve_duplex_pulse_device()
        if args.virt_mic and virt_setup is not None:
            pvm = resolve_stream_device_for_pulse_virt_mic()
            if pvm is not None:
                stream_in = pvm
                stream_out = pvm
            elif pd is not None and stream_out is None:
                stream_in = pd
                stream_out = pd
        elif args.virt_mic and virt_setup is None and cable_out is not None and pd is not None:
            stream_in = pd
        elif not args.virt_mic and not output_explicit and pd is not None:
            stream_in = pd
            stream_out = pd

    if stream_in != in_dev or stream_out != pre_duplex_out:
        try:
            di = sd.query_devices(stream_in)
            do = sd.query_devices(stream_out)
            print(
                f"Duplex-safe stream: in [{stream_in}] {di.get('name', '?')!r} / "
                f"out [{stream_out}] {do.get('name', '?')!r} "
                f"(set LIVE_VOCODER_PULSE_DUPLEX=0 to use separate ALSA devices).",
                flush=True,
            )
        except Exception:
            print(
                f"Duplex stream devices: in={stream_in} out={stream_out} "
                "(LIVE_VOCODER_PULSE_DUPLEX=0 to disable).",
                flush=True,
            )

    lat = os.environ.get("LIVE_VOCODER_STREAM_LATENCY", "high").lower()
    if lat not in ("low", "high"):
        lat = "high"

    stream_kw: dict = {
        "samplerate": sr,
        "blocksize": block,
        "dtype": "float32",
        "channels": (1, n_out),
        "callback": callback,
        "latency": lat,
        "device": (stream_in, stream_out),
    }

    try:
        with sd.Stream(**stream_kw):
            if args.virt_mic and virt_setup is not None:
                from pulse_virtmic import (
                    iter_move_to_virt_sink,
                    monitor_source_name,
                    mute_monitor_loopback_sink_inputs,
                    mute_playback_sink_inputs_not_on_virt_sink,
                    set_sink_input_mute,
                    unload_loopbacks_for_pulse_source,
                    unload_module,
                )

                iter_move_to_virt_sink(args.virt_mic_sink)
                mon = monitor_source_name(args.virt_mic_sink)
                want_lb = (
                    (not no_virt_mic_hear)
                    and (not args.mute_speakers)
                    and (not args.mute_vocoded or args.dry > 0)
                )
                if not want_lb:
                    if virt_setup.loopback_module_id is not None:
                        if unload_module(virt_setup.loopback_module_id):
                            virt_setup.loopback_module_id = None
                            virt_setup.loopback_sink_input_id = None
                    unload_loopbacks_for_pulse_source(mon)
                    mute_monitor_loopback_sink_inputs(args.virt_mic_sink, True)
                elif virt_setup.loopback_sink_input_id is not None:
                    set_sink_input_mute(virt_setup.loopback_sink_input_id, False)
                    mute_monitor_loopback_sink_inputs(args.virt_mic_sink, False)
                mute_playback_sink_inputs_not_on_virt_sink(
                    args.virt_mic_sink,
                    bool(
                        args.mute_speakers
                        or (args.mute_vocoded and args.dry <= 0)
                    ),
                )
            if rec_n > 0:
                print(
                    f"Recording {record_sec:.2f}s → {record_dir} (mic.wav, vocoded.wav, stereo_lr.wav). "
                    "Wait…"
                )
                while not rec_done.is_set():
                    time.sleep(0.05)
                _write_wav16(record_dir / "mic.wav", rec_mic)
                _write_wav16(record_dir / "vocoded.wav", rec_voc)
                lr = np.column_stack((rec_mic, rec_voc))
                record_dir.mkdir(parents=True, exist_ok=True)
                wavfile.write(
                    str(record_dir / "stereo_lr.wav"),
                    sr,
                    (np.clip(lr, -1.0, 1.0) * 32767.0).astype(np.int16),
                )
                print(f"Wrote {record_dir}/mic.wav, vocoded.wav, stereo_lr.wav (L=mic R=vocoded).")
                return
            print(
                f"Running: block={block}, hop={hop}, n_fft={args.n_fft}, wet={wet}, gain={gain}. "
                "Ctrl+C to stop."
            )
            while True:
                time.sleep(1.0)
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        if virt_setup is not None:
            from pulse_virtmic import pactl_available, teardown_pulse_virt_mic_full

            if pactl_available():
                teardown_pulse_virt_mic_full(virt_setup.sink_name)


if __name__ == "__main__":
    main()
