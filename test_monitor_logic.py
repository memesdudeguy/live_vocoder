#!/usr/bin/env python3
"""Self-test: monitor / mute-vocoded flags match VocoderSession (no audio hardware needed)."""
from __future__ import annotations

import sys


def main() -> int:
    from vocoder_session import VocoderSession

    # GTK default: VM-only (no local monitor)
    s = VocoderSession(
        use_pulse_virt_mic=True,
        virt_mic_hear_loopback=False,
        mute_speakers=True,
        mute_vocoded=True,
    )
    assert not s._want_monitor_loopback_playing(), "vm-only default"

    s.mute_speakers = False
    s.virt_mic_hear_loopback = True
    s.mute_vocoded = True
    s.hear_self = False
    assert not s._want_monitor_loopback_playing(), "monitor+voc-muted+no dry"

    s.mute_vocoded = False
    assert s._want_monitor_loopback_playing(), "voc preview on"

    s.mute_vocoded = True
    s.hear_self = True
    assert s._want_monitor_loopback_playing(), "dry sidetone keeps loopback"

    # CLI-style: same as hear_lb in live_vocoder.main
    def cli_want_lb(
        *,
        no_virt_mic_hear: bool,
        mute_speakers: bool,
        mute_vocoded: bool,
        dry: float,
    ) -> bool:
        return (
            (not no_virt_mic_hear)
            and (not mute_speakers)
            and (not mute_vocoded or dry > 0)
        )

    assert not cli_want_lb(
        no_virt_mic_hear=False,
        mute_speakers=True,
        mute_vocoded=True,
        dry=0.0,
    )
    assert not cli_want_lb(
        no_virt_mic_hear=False,
        mute_speakers=False,
        mute_vocoded=True,
        dry=0.0,
    )
    assert cli_want_lb(
        no_virt_mic_hear=False,
        mute_speakers=False,
        mute_vocoded=False,
        dry=0.0,
    )
    assert cli_want_lb(
        no_virt_mic_hear=False,
        mute_speakers=False,
        mute_vocoded=True,
        dry=0.05,
    )

    print("test_monitor_logic: all assertions passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
