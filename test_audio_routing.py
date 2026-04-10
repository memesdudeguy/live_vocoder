#!/usr/bin/env python3
"""
Unit tests: monitor flags, loopback matching (no other virtual mics), env sink identity.

Run:  python test_audio_routing.py
"""
from __future__ import annotations

import os
import sys
import unittest
from unittest.mock import patch


class LoopbackMatchTests(unittest.TestCase):
    def test_connects_our_monitor_source_equals(self) -> None:
        from pulse_virtmic import _loopback_module_connects_source

        block = (
            "Module #12\n"
            "Name: module-loopback\n"
            "Argument: source=live_vocoder.monitor sink=@DEFAULT_SINK@\n"
        )
        self.assertTrue(
            _loopback_module_connects_source(block, "live_vocoder.monitor")
        )

    def test_rejects_other_app_monitor(self) -> None:
        from pulse_virtmic import _loopback_module_connects_source

        block = (
            "Module #99\n"
            "Name: libpipewire-module-loopback\n"
            "Argument: source=openmod_virtual_input.monitor sink=alsa_output.usb-foo\n"
        )
        self.assertFalse(
            _loopback_module_connects_source(block, "live_vocoder.monitor")
        )

    def test_rejects_non_loopback_module(self) -> None:
        from pulse_virtmic import _loopback_module_connects_source

        block = (
            "Module #1\n"
            "Name: module-null-sink\n"
            "live_vocoder.monitor mentioned in some property\n"
        )
        self.assertFalse(
            _loopback_module_connects_source(block, "live_vocoder.monitor")
        )

    def test_master_equals(self) -> None:
        from pulse_virtmic import _loopback_module_connects_source

        block = (
            "Module #3\n"
            "Name: module-loopback\n"
            "master=foo.monitor source=ignored\n"
        )
        self.assertTrue(_loopback_module_connects_source(block, "foo.monitor"))


class ResolvedSinkTests(unittest.TestCase):
    def test_default_without_env(self) -> None:
        from pulse_virtmic import resolved_pulse_sink_identity

        with patch.dict(
            os.environ,
            {"LIVE_VOCODER_PULSE_SINK": "", "LIVE_VOCODER_PULSE_DESCRIPTION": ""},
            clear=False,
        ):
            sn, sd = resolved_pulse_sink_identity()
        self.assertEqual(sn, "live_vocoder")
        self.assertEqual(sd, "LiveVocoder")

    def test_env_override(self) -> None:
        from pulse_virtmic import resolved_pulse_sink_identity

        with patch.dict(
            os.environ,
            {
                "LIVE_VOCODER_PULSE_SINK": "my_lv_sink",
                "LIVE_VOCODER_PULSE_DESCRIPTION": "My LV",
            },
        ):
            sn, sd = resolved_pulse_sink_identity()
        self.assertEqual(sn, "my_lv_sink")
        self.assertEqual(sd, "My LV")


class VocoderMonitorTests(unittest.TestCase):
    def test_want_loopback_matrix(self) -> None:
        from vocoder_session import VocoderSession

        with patch.dict(
            os.environ,
            {"LIVE_VOCODER_PULSE_SINK": "", "LIVE_VOCODER_PULSE_DESCRIPTION": ""},
            clear=False,
        ):
            s = VocoderSession(use_pulse_virt_mic=True)
        s.virt_mic_hear_loopback = False
        s.mute_speakers = True
        s.mute_vocoded = True
        s.hear_self = False
        self.assertFalse(s._want_monitor_loopback_playing())

        s.mute_speakers = False
        s.virt_mic_hear_loopback = True
        self.assertFalse(s._want_monitor_loopback_playing())

        s.mute_vocoded = False
        self.assertTrue(s._want_monitor_loopback_playing())

    def test_mute_local_off_virt_sink(self) -> None:
        from vocoder_session import VocoderSession

        with patch.dict(
            os.environ,
            {"LIVE_VOCODER_PULSE_SINK": "", "LIVE_VOCODER_PULSE_DESCRIPTION": ""},
            clear=False,
        ):
            s = VocoderSession(use_pulse_virt_mic=True)
        s.mute_speakers = True
        s.mute_vocoded = False
        s.hear_self = False
        self.assertTrue(s._mute_local_off_virt_sink())

        s.mute_speakers = False
        self.assertFalse(s._mute_local_off_virt_sink())

        s.mute_vocoded = True
        s.hear_self = True
        self.assertFalse(s._mute_local_off_virt_sink())


class DiagnoseFormatTests(unittest.TestCase):
    def test_report_non_empty(self) -> None:
        from diagnose_routing import format_diagnosis_report

        text = format_diagnosis_report("live_vocoder", "LiveVocoder")
        self.assertIn("Live Vocoder", text)
        self.assertIn("live_vocoder", text)


def main() -> int:
    loader = unittest.TestLoader()
    suite = loader.loadTestsFromModule(sys.modules[__name__])
    r = unittest.TextTestRunner(verbosity=2)
    result = r.run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
