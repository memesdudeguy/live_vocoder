#!/usr/bin/env python3
"""
Tkinter GUI: pick any carrier file (MP3/WAV/… via ffmpeg), start/stop duplex audio,
and toggle “Hear myself” (raw mic) vs vocoder output.

Requires system Tk (Arch: sudo pacman -S tk). If Tk is missing, use --web-gui instead.
"""
from __future__ import annotations

import sys
from pathlib import Path

from vocoder_session import VocoderSession


def run_gui(
    initial_carrier: Path | None = None,
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
) -> None:
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk

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

    root = tk.Tk()
    root.title("Live vocoder")
    root.minsize(520, 460)

    path_var = tk.StringVar(value=session.path_hint or "")
    status_var = tk.StringVar(value=session.last_status)
    hear_var = tk.BooleanVar(value=session.hear_self)
    hear_voc_var = tk.BooleanVar(value=session.hear_vocoded)
    virt_var = tk.BooleanVar(value=session.use_pulse_virt_mic)
    virt_hear_var = tk.BooleanVar(value=session.virt_mic_hear_loopback)
    mute_sp_var = tk.BooleanVar(value=session.mute_speakers)
    virt_dev_var = tk.StringVar(value=session.virt_mic_device_hint or "")
    gain_var = tk.DoubleVar(value=session.gain)
    wet_var = tk.DoubleVar(value=session.wet_level)
    presence_var = tk.DoubleVar(value=session.presence_db)
    reverb_mix_var = tk.DoubleVar(value=session.reverb_mix)
    reverb_room_var = tk.DoubleVar(value=session.reverb_room)

    def refresh_status() -> None:
        status_var.set(session.last_status)

    def browse() -> None:
        p = filedialog.askopenfilename(
            title="Carrier audio (MP3, WAV, FLAC, …)",
            filetypes=[
                ("Audio", "*.mp3 *.wav *.flac *.ogg *.m4a *.aac"),
                ("All", "*.*"),
            ],
        )
        if p:
            path_var.set(p)
            session.load_from_path(p)
            refresh_status()

    def apply_loaded_path() -> None:
        session.load_from_path(path_var.get())
        session.apply_carrier_live()
        refresh_status()

    def on_hear_change() -> None:
        session.set_hear_self(hear_var.get())

    def on_hear_voc_change() -> None:
        session.set_hear_vocoded(hear_voc_var.get())

    def on_virt_change() -> None:
        session.use_pulse_virt_mic = virt_var.get()

    def on_virt_hear_change() -> None:
        session.set_virt_mic_hear_loopback(virt_hear_var.get())

    def on_mute_sp_change() -> None:
        session.set_mute_speakers(mute_sp_var.get())

    def on_virt_dev_change(*_: object) -> None:
        session.set_virt_mic_device_hint(virt_dev_var.get())

    def on_gain_change(_: str | None = None) -> None:
        session.set_gain(float(gain_var.get()))

    def on_wet_change(_: str | None = None) -> None:
        session.set_wet(float(wet_var.get()))

    def on_presence_change(_: str | None = None) -> None:
        session.set_presence(float(presence_var.get()))

    def on_reverb_mix_change(_: str | None = None) -> None:
        session.set_reverb_mix(float(reverb_mix_var.get()))

    def on_reverb_room_change(_: str | None = None) -> None:
        session.set_reverb_room(float(reverb_room_var.get()))

    def start_audio() -> None:
        session.hear_self = hear_var.get()
        session.hear_vocoded = hear_voc_var.get()
        session.use_pulse_virt_mic = virt_var.get()
        session.set_virt_mic_hear_loopback(virt_hear_var.get())
        session.set_mute_speakers(mute_sp_var.get())
        session.set_virt_mic_device_hint(virt_dev_var.get())
        session.set_gain(float(gain_var.get()))
        session.set_wet(float(wet_var.get()))
        session.set_presence(float(presence_var.get()))
        session.set_reverb_mix(float(reverb_mix_var.get()))
        session.set_reverb_room(float(reverb_room_var.get()))
        if session.carrier_arr is None:
            session.load_from_path(path_var.get())
        if session.carrier_arr is None:
            messagebox.showinfo("Carrier", "Choose a valid audio file first.")
            refresh_status()
            return
        session.start()
        refresh_status()

    def stop_audio() -> None:
        session.stop()
        refresh_status()

    def on_close() -> None:
        session.shutdown()
        root.destroy()

    pad = {"padx": 8, "pady": 4}
    f0 = ttk.Frame(root, padding=10)
    f0.pack(fill=tk.BOTH, expand=True)

    ttk.Label(f0, text="Carrier file (any format ffmpeg supports):").pack(anchor=tk.W)
    row = ttk.Frame(f0)
    row.pack(fill=tk.X, **pad)
    ttk.Entry(row, textvariable=path_var).pack(
        side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 6)
    )
    ttk.Button(row, text="Browse…", command=browse).pack(side=tk.LEFT)
    ttk.Button(row, text="Load / apply", command=apply_loaded_path).pack(
        side=tk.LEFT, padx=(6, 0)
    )

    ttk.Separator(f0, orient=tk.HORIZONTAL).pack(fill=tk.X, pady=8)

    row2 = ttk.Frame(f0)
    row2.pack(fill=tk.X, **pad)
    ttk.Button(row2, text="▶ Start", command=start_audio).pack(side=tk.LEFT, padx=(0, 8))
    ttk.Button(row2, text="■ Stop", command=stop_audio).pack(side=tk.LEFT)

    ttk.Separator(f0, orient=tk.HORIZONTAL).pack(fill=tk.X, pady=8)

    ttk.Checkbutton(
        f0,
        text="Hear myself (raw mic — vocoder still runs in background)",
        variable=hear_var,
        command=on_hear_change,
    ).pack(anchor=tk.W, **pad)

    ttk.Checkbutton(
        f0,
        text="Hear vocoded output (off = silent headphones; still use “Hear myself” for dry mic)",
        variable=hear_voc_var,
        command=on_hear_voc_change,
    ).pack(anchor=tk.W, **pad)

    ttk.Checkbutton(
        f0,
        text=(
            f'Virtual mic (Pulse): choose "{session.pulse_sink_description} Virtual Mic" '
            f'(`{session.pulse_sink_name}_mic`) as input in Discord/OBS'
        ),
        variable=virt_var,
        command=on_virt_change,
    ).pack(anchor=tk.W, **pad)

    ttk.Checkbutton(
        f0,
        text="Also hear vocoder on your speakers (Pulse loopback; uncheck for silent local)",
        variable=virt_hear_var,
        command=on_virt_hear_change,
    ).pack(anchor=tk.W, **pad)

    ttk.Label(
        f0,
        text="Virtual cable name contains (Windows/macOS if no Pulse; optional):",
    ).pack(anchor=tk.W, **pad)
    ttk.Entry(f0, textvariable=virt_dev_var).pack(fill=tk.X, **pad)
    virt_dev_var.trace_add("write", on_virt_dev_change)

    ttk.Checkbutton(
        f0,
        text="Mute local speakers — vocoded audio still goes to virtual mic (Discord/OBS)",
        variable=mute_sp_var,
        command=on_mute_sp_change,
    ).pack(anchor=tk.W, **pad)

    ttk.Label(f0, text="Output gain").pack(anchor=tk.W)
    ttk.Scale(
        f0,
        from_=0.3,
        to=8.0,
        variable=gain_var,
        command=on_gain_change,
        orient=tk.HORIZONTAL,
    ).pack(fill=tk.X, **pad)

    ttk.Label(f0, text="Vocoder wet (voice level in mix)").pack(anchor=tk.W)
    ttk.Scale(
        f0,
        from_=0.0,
        to=2.0,
        variable=wet_var,
        command=on_wet_change,
        orient=tk.HORIZONTAL,
    ).pack(fill=tk.X, **pad)

    ttk.Label(
        f0,
        text="Clarity / presence (modulator HF lift, dB — 0 = off)",
    ).pack(anchor=tk.W)
    ttk.Scale(
        f0,
        from_=0.0,
        to=10.0,
        variable=presence_var,
        command=on_presence_change,
        orient=tk.HORIZONTAL,
    ).pack(fill=tk.X, **pad)

    ttk.Label(f0, text="Reverb wet (0 = dry)").pack(anchor=tk.W)
    ttk.Scale(
        f0,
        from_=0.0,
        to=1.0,
        variable=reverb_mix_var,
        command=on_reverb_mix_change,
        orient=tk.HORIZONTAL,
    ).pack(fill=tk.X, **pad)

    ttk.Label(f0, text="Reverb room / decay (longer tail when higher)").pack(anchor=tk.W)
    ttk.Scale(
        f0,
        from_=0.0,
        to=1.0,
        variable=reverb_room_var,
        command=on_reverb_room_change,
        orient=tk.HORIZONTAL,
    ).pack(fill=tk.X, **pad)

    ttk.Label(f0, textvariable=status_var, wraplength=480).pack(anchor=tk.W, **pad)

    ttk.Label(
        f0,
        text=f"Devices: in [{session.in_dev}] / out [{session.out_dev}] @ {sr} Hz",
        font=("TkDefaultFont", 8),
        foreground="gray",
    ).pack(anchor=tk.W, pady=(12, 0))

    root.protocol("WM_DELETE_WINDOW", on_close)
    root.mainloop()


def run_gui_safe(
    initial_carrier: Path | None = None,
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
) -> None:
    try:
        run_gui(
            initial_carrier=initial_carrier,
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
    except ImportError as e:
        err = str(e).lower()
        if "tkinter" in err or "_tkinter" in err or "libtk" in err:
            print(
                "Tkinter needs the system Tk library.\n"
                "  Arch Linux:  sudo pacman -S tk\n"
                "Or use the browser UI (no Tk):\n"
                "  .venv/bin/python live_vocoder.py --web-gui\n",
                file=sys.stderr,
            )
        raise


if __name__ == "__main__":
    run_gui_safe()
