"""
Default folder for carrier audio: ~/Documents/LiveVocoderCarriers
"""
from __future__ import annotations

from pathlib import Path

CARRIER_EXTS = frozenset(
    {".mp3", ".wav", ".flac", ".ogg", ".m4a", ".aac", ".opus", ".wma", ".aiff", ".aif"}
)


def carrier_library_dir() -> Path:
    return Path.home() / "Documents" / "LiveVocoderCarriers"


def ensure_carrier_library_dir() -> Path:
    d = carrier_library_dir()
    d.mkdir(parents=True, exist_ok=True)
    return d


def list_carrier_files() -> list[Path]:
    """Sorted audio files in the carrier library folder (non-recursive)."""
    d = carrier_library_dir()
    if not d.is_dir():
        return []
    out: list[Path] = []
    for p in d.iterdir():
        if p.is_file() and p.suffix.lower() in CARRIER_EXTS:
            out.append(p)
    out.sort(key=lambda x: x.name.lower())
    return out
