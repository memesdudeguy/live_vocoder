#!/bin/bash
WINEPREFIX=~/.livevocoder
export WINEPREFIX
[ ! -d "$WINEPREFIX" ] && wineboot -u
pactl load-module module-loopback source=virt-input latency_msec=5 2>/dev/null || true
wine live_vocoder.exe
