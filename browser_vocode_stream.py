"""
Gradio browser microphone → vocoder → streaming audio output (getUserMedia path).

System virtual mic (Pulse/cable) is separate; this only handles tab capture + playback.
"""
from __future__ import annotations

import numpy as np

from live_vocoder import MonoConvolutionReverb, StreamingVocoder


def _to_mono_float32(sr_in: int, data: np.ndarray) -> np.ndarray:
    x = np.asarray(data, dtype=np.float32)
    if x.ndim == 2:
        x = x.mean(axis=1)
    elif x.ndim != 1:
        x = x.reshape(-1)
    if x.dtype == np.int16:
        x = x.astype(np.float32) / 32768.0
    elif np.issubdtype(x.dtype, np.integer):
        x = x.astype(np.float32) / np.iinfo(x.dtype).max
    return x


def _resample_linear(x: np.ndarray, sr_in: int, sr_out: int) -> np.ndarray:
    if sr_in == sr_out or x.size == 0:
        return x.astype(np.float32, copy=False)
    n_out = max(1, int(round(x.size * sr_out / sr_in)))
    t_in = np.linspace(0.0, 1.0, num=x.size, endpoint=False, dtype=np.float64)
    t_out = np.linspace(0.0, 1.0, num=n_out, endpoint=False, dtype=np.float64)
    return np.interp(t_out, t_in, x.astype(np.float64)).astype(np.float32)


class BrowserVocodePipeline:
    """Stateful buffer + vocoder for variable-size browser chunks."""

    def __init__(
        self,
        *,
        sr: int,
        block: int,
        gain: float,
        wet_level: float,
        presence_db: float,
        reverb_mix: float = 0.0,
        reverb_room: float = 0.45,
    ) -> None:
        self.sr = sr
        self.block = block
        self.gain = float(gain)
        self.wet_level = float(wet_level)
        self.presence_db = float(presence_db)
        self.reverb_mix = float(np.clip(reverb_mix, 0.0, 1.0))
        self.reverb_room = float(np.clip(reverb_room, 0.0, 1.0))
        self._buf = np.zeros(0, dtype=np.float32)
        self.voc: StreamingVocoder | None = None
        self._reverb: MonoConvolutionReverb | None = None
        self.n_fft = 2048
        self.hop = self.n_fft // 4

    def load_carrier(self, carrier_f32: np.ndarray) -> None:
        car = np.asarray(carrier_f32, dtype=np.float64).copy()
        self.voc = StreamingVocoder(
            car,
            self.sr,
            n_fft=self.n_fft,
            hop_length=self.hop,
            wet_level=self.wet_level,
            mode="bands",
            n_bands=36,
            band_smooth=0.62,
            mod_presence_db=self.presence_db,
        )
        self._reverb = MonoConvolutionReverb(
            self.sr,
            mix=self.reverb_mix,
            room=self.reverb_room,
        )

    def reset(self) -> None:
        self._buf = np.zeros(0, dtype=np.float32)

    def set_runtime(
        self,
        *,
        gain: float,
        wet: float,
        presence_db: float,
        reverb_mix: float | None = None,
        reverb_room: float | None = None,
    ) -> None:
        self.gain = float(gain)
        self.wet_level = float(wet)
        self.presence_db = float(presence_db)
        if reverb_mix is not None:
            self.reverb_mix = float(np.clip(reverb_mix, 0.0, 1.0))
        if reverb_room is not None:
            self.reverb_room = float(np.clip(reverb_room, 0.0, 1.0))
        if self.voc is not None:
            self.voc.wet_level = self.wet_level
            self.voc.mod_presence_db = self.presence_db
        if self._reverb is not None:
            self._reverb.set_mix(self.reverb_mix)
            self._reverb.set_room(self.reverb_room)

    def process(
        self,
        audio: tuple[int, np.ndarray] | None,
        *,
        hear_self: bool,
        hear_vocoded: bool,
    ) -> tuple[int, np.ndarray] | None:
        if audio is None or self.voc is None:
            return None
        sr_in, data = audio
        mono = _to_mono_float32(sr_in, data)
        mono = _resample_linear(mono, int(sr_in), self.sr)
        if mono.size == 0:
            return None
        self._buf = np.concatenate((self._buf, mono))
        outs: list[np.ndarray] = []
        voc = self.voc
        while self._buf.size >= self.block:
            chunk = self._buf[: self.block].copy()
            self._buf = self._buf[self.block :]
            m = chunk.astype(np.float32, copy=False)
            y = voc.process_block(m)
            if y.size != self.block:
                continue
            if hear_self:
                y = chunk.copy()
            elif not hear_vocoded:
                y = np.zeros(self.block, dtype=np.float32)
            if self._reverb is not None:
                y = self._reverb.process(y)
            np.clip(y * self.gain, -1.0, 1.0, out=y)
            outs.append(y)
        if not outs:
            return None
        y_all = np.concatenate(outs)
        stereo = np.column_stack((y_all, y_all))
        pcm = (np.clip(stereo, -1.0, 1.0) * 32767.0).astype(np.int16)
        return self.sr, pcm
