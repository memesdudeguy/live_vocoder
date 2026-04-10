#!/usr/bin/env python3
"""Browser UI for the live vocoder (Gradio — no system Tk required)."""
from __future__ import annotations

from pathlib import Path

import gradio as gr
import numpy as np

from browser_vocode_stream import BrowserVocodePipeline
from vocoder_session import VocoderSession


def run_web_gui(
    initial_carrier: Path | None = None,
    sr: int = 48000,
    input_device: int | None = None,
    output_device: int | None = None,
    server_name: str = "127.0.0.1",
    server_port: int = 7860,
    use_pulse_virt_mic: bool = False,
    pulse_sink_name: str | None = None,
    pulse_sink_description: str | None = None,
    virt_mic_hear_loopback: bool = False,
    mute_speakers: bool = True,
    mute_vocoded: bool = True,
    virt_mic_device_hint: str | None = None,
    reverb_mix: float = 0.0,
    reverb_room: float = 0.45,
) -> None:
    session = VocoderSession(
        sr=sr,
        input_device=input_device,
        output_device=output_device,
        use_pulse_virt_mic=use_pulse_virt_mic,
        pulse_sink_name=pulse_sink_name,
        pulse_sink_description=pulse_sink_description,
        virt_mic_hear_loopback=virt_mic_hear_loopback,
        mute_speakers=mute_speakers,
        mute_vocoded=mute_vocoded,
        virt_mic_device_hint=virt_mic_device_hint,
        reverb_mix=reverb_mix,
        reverb_room=reverb_room,
    )
    session.try_default_carrier(initial_carrier)

    pipe_holder: list[BrowserVocodePipeline | None] = [None]

    def status() -> str:
        return session.last_status

    def do_load_path(path: str, _upload: str | None) -> str:
        p = (path or "").strip()
        u = _upload
        pipe_holder[0] = None
        if u:
            return session.load_from_path(str(u))
        if p:
            return session.load_from_path(p)
        return session.last_status

    def do_apply() -> str:
        pipe_holder[0] = None
        session.apply_carrier_live()
        return status()

    def on_mute_sp(v: bool) -> str:
        session.set_mute_speakers(v)
        return status()

    def on_virt_hint(s: str) -> str:
        session.set_virt_mic_device_hint(s)
        return status()

    def on_virt_hear(vmh: bool) -> str:
        session.set_virt_mic_hear_loopback(bool(vmh))
        return status()

    def do_start(
        vm: bool,
        vmh: bool,
        msp: bool,
        rm: float,
        rr: float,
    ) -> str:
        session.use_pulse_virt_mic = bool(vm)
        session.set_virt_mic_hear_loopback(bool(vmh))
        session.set_mute_speakers(bool(msp))
        session.set_reverb_mix(float(rm))
        session.set_reverb_room(float(rr))
        session.start()
        return status()

    def do_stop() -> str:
        session.stop()
        return status()

    def on_hear(v: bool) -> str:
        session.set_hear_self(v)
        return status()

    def on_hear_voc(v: bool) -> str:
        session.set_hear_vocoded(v)
        return status()

    def on_gain(v: float) -> str:
        session.set_gain(v)
        return status()

    def on_wet(v: float) -> str:
        session.set_wet(v)
        return status()

    def on_presence(v: float) -> str:
        session.set_presence(v)
        return status()

    def on_reverb_mix(v: float) -> str:
        session.set_reverb_mix(float(v))
        return status()

    def on_reverb_room(v: float) -> str:
        session.set_reverb_room(float(v))
        return status()

    def ensure_browser_pipe() -> BrowserVocodePipeline | None:
        if session.carrier_arr is None:
            return None
        if pipe_holder[0] is None:
            pipe_holder[0] = BrowserVocodePipeline(
                sr=session.sr,
                block=session.block,
                gain=session.gain,
                wet_level=session.wet_level,
                presence_db=session.presence_db,
                reverb_mix=session.reverb_mix,
                reverb_room=session.reverb_room,
            )
            pipe_holder[0].load_carrier(np.asarray(session.carrier_arr, dtype=np.float32))
        return pipe_holder[0]

    def stream_vocode(audio: tuple[int, np.ndarray] | None) -> tuple[int, np.ndarray] | None:
        p = ensure_browser_pipe()
        if p is None:
            return None
        p.set_runtime(
            gain=session.gain,
            wet=session.wet_level,
            presence_db=session.presence_db,
            reverb_mix=session.reverb_mix,
            reverb_room=session.reverb_room,
        )
        out = p.process(
            audio,
            hear_self=session.hear_self,
            hear_vocoded=session.hear_vocoded,
        )
        if out is not None:
            return out
        if audio is None:
            return None
        sr_in, data = audio
        n = int(np.asarray(data).shape[0])
        if n <= 0 or sr_in <= 0:
            return None
        n_out = max(1, int(round(n * session.sr / float(sr_in))))
        silence = np.zeros((n_out, 2), dtype=np.int16)
        return session.sr, silence

    init_path = session.path_hint or ""

    with gr.Blocks(title="Live vocoder") as demo:
        gr.Markdown(
            "# Live vocoder\n"
            "Open this page from the **same machine** that runs Python.\n\n"
            "## Virtual mic (Discord / OBS / system apps)\n"
            "- **Linux (PipeWire/Pulse):** enable **Virtual mic**, click **Start**. Playback must use the "
            "**pulse** or **pipewire** ALSA device (not raw `hw:`) so audio reaches the null sink (`PULSE_SINK`). "
            "`./run.sh` sets `LIVE_VOCODER_PORTAUDIO_OUTPUT` automatically on Linux unless you set "
            "`LIVE_VOCODER_NO_AUTO_PULSE_OUT=1`. Install **pipewire-alsa** if the mic stays silent. "
            "In **Steam**, **OBS**, and the **native Discord** app, pick **LiveVocoder Virtual Mic** "
            "(or **Monitor of LiveVocoder**). Use **Recording** in `pavucontrol` to verify levels.\n"
            "- **This browser tab vs Discord:** Chromium/Firefox **getUserMedia** often lists **only** USB/webcam mics — "
            "PipeWire virtual sources (`live_vocoder_mic`) **may not appear** in the browser’s mic dropdown even when "
            "they exist in `pavucontrol`. That is normal. For Discord voice on the same PC, use the **Discord app** "
            "(not the browser) and select the virtual mic there; or use a **non-Flatpak** browser + xdg-desktop-portal, "
            "or grant Flatpak socket access (below).\n"
            "- **Flatpak Discord / Firefox / Chrome:** if the virtual mic is missing, grant PipeWire socket access "
            "(e.g. `flatpak override --user --filesystem=xdg-run/pipewire-0 <app.id>`) or use distro packages.\n"
            "- **Stale device after an update:** unload old Pulse modules for this sink or reboot PipeWire, then restart the vocoder.\n"
            "- **Windows:** install [VB-Audio Virtual Cable](https://vb-audio.com/Cable/); "
            "if Pulse is missing, audio is sent to **CABLE Input** — choose **CABLE Output** as mic.\n"
            "- **macOS:** install **BlackHole 2ch**; match **BlackHole** as output / input.\n"
            "- Optional **Cable device substring** if auto-detect fails (also env `LIVE_VOCODER_VIRT_DEVICE`).\n\n"
            "## Browser microphone (this tab)\n"
            "Use **Browser mic stream** below after **Load** — the browser will ask for **microphone permission**. "
            "Vocoded audio plays **in the tab** only; it is **not** a system-wide virtual mic (browser security). "
            "For Discord on this PC, use **Start** (server / PortAudio) + Virtual mic, not the browser stream.\n"
        )
        path_in = gr.Textbox(
            label="Carrier file path",
            value=init_path,
            placeholder="/path/to/song.mp3",
        )
        upload = gr.File(label="Or upload audio", type="filepath")
        with gr.Row():
            btn_load = gr.Button("Load carrier")
            btn_apply = gr.Button("Apply live (while streaming)")
        with gr.Row():
            btn_start = gr.Button("▶ Start (server mic + optional virtual mic)", variant="primary")
            btn_stop = gr.Button("■ Stop server stream")
        hear = gr.Checkbox(label="Hear myself (raw mic)", value=False)
        hear_voc = gr.Checkbox(
            label="Hear vocoded output (off = silent; use “Hear myself” for dry mic)",
            value=True,
        )
        virt_mic = gr.Checkbox(
            label=(
                f'Virtual mic: "{session.pulse_sink_description} Virtual Mic" / cable '
                f"({session.pulse_sink_name}_mic)"
            ),
            value=session.use_pulse_virt_mic,
        )
        virt_hear = gr.Checkbox(
            label="Also hear on speakers (Pulse loopback only)",
            value=session.virt_mic_hear_loopback,
        )
        virt_dev = gr.Textbox(
            label="Cable device substring (Windows/macOS fallback if no Pulse)",
            value=session.virt_mic_device_hint or "",
            placeholder="e.g. CABLE or BlackHole",
        )
        mute_sp = gr.Checkbox(
            label="Mute local speakers — vocoded audio still goes to virtual mic",
            value=session.mute_speakers,
        )
        gain = gr.Slider(0.3, 8.0, value=session.gain, step=0.05, label="Output gain")
        wet = gr.Slider(0.0, 2.0, value=session.wet_level, step=0.02, label="Vocoder wet")
        presence = gr.Slider(
            0.0, 10.0, value=session.presence_db, step=0.1, label="Clarity / presence (dB)"
        )
        rev_mix = gr.Slider(
            0.0, 1.0, value=session.reverb_mix, step=0.02, label="Reverb wet (0 = dry)"
        )
        rev_room = gr.Slider(
            0.0, 1.0, value=session.reverb_room, step=0.02, label="Reverb room / decay"
        )
        st = gr.Textbox(label="Status", value=status(), interactive=False, lines=4)

        gr.Markdown(
            "### Browser mic stream (getUserMedia)\n"
            "Click **record** on the microphone, allow access, then speak. "
            "Requires carrier **Load** first. Does not replace **Start** for system virtual mic."
        )
        with gr.Row():
            mic_browser = gr.Audio(
                sources="microphone",
                type="numpy",
                streaming=True,
                label="Browser microphone",
            )
            out_browser = gr.Audio(
                label="Vocoded output (this tab)",
                streaming=True,
                autoplay=True,
                interactive=False,
            )

        btn_load.click(do_load_path, [path_in, upload], st)
        btn_apply.click(do_apply, None, st)
        mute_sp.change(on_mute_sp, mute_sp, st)
        virt_hear.change(on_virt_hear, virt_hear, st)
        virt_dev.change(on_virt_hint, virt_dev, st)
        btn_start.click(do_start, [virt_mic, virt_hear, mute_sp, rev_mix, rev_room], st)
        btn_stop.click(do_stop, None, st)
        hear.change(on_hear, hear, st)
        hear_voc.change(on_hear_voc, hear_voc, st)
        gain.change(on_gain, gain, st)
        wet.change(on_wet, wet, st)
        presence.change(on_presence, presence, st)
        rev_mix.change(on_reverb_mix, rev_mix, st)
        rev_room.change(on_reverb_room, rev_room, st)

        mic_browser.stream(
            stream_vocode,
            inputs=[mic_browser],
            outputs=[out_browser],
            stream_every=0.15,
            time_limit=None,
        )

        gr.Markdown(
            f"_Server audio devices: in `[{session.in_dev}]` · out `[{session.out_dev}]` @ {sr} Hz — "
            "CLI `--input-device` / `--output-device` if needed._"
        )

    try:
        demo.queue(default_concurrency_limit=64)
        demo.launch(
            server_name=server_name,
            server_port=server_port,
            share=False,
            inbrowser=True,
        )
    finally:
        session.shutdown()


if __name__ == "__main__":
    run_web_gui()
