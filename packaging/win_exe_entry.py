"""
PyInstaller entry: double-click runs the browser UI (no argv → --web-gui).
Override with any normal CLI flags, e.g. LiveVocoder.exe --gtk-gui
"""
from __future__ import annotations

import sys


def main() -> None:
    if len(sys.argv) <= 1:
        sys.argv.append("--web-gui")
    from live_vocoder import main as lv_main

    lv_main()


if __name__ == "__main__":
    main()
