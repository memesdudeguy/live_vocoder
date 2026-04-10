#!/usr/bin/env python3
"""Build LiveVocoder.ico + normalized assets/app-icon.png from assets/app-icon.png (or .webp).

Composites artwork on a black plate and clips to a rounded rectangle (transparent outside the corners).

PNG entries in .ico use wPlanes=0 and wBitCount=0 (Explorer compatibility).

Requires Pillow **or** ffmpeg in PATH (black + rounded styling needs Pillow).
"""
from __future__ import annotations

import struct
import subprocess
import sys
from io import BytesIO
from pathlib import Path

_ROOT = Path(__file__).resolve().parent
_ASSETS = _ROOT.parent / "assets"
_OUT_ICO = _ROOT / "LiveVocoder.ico"
_OUT_PNG = _ASSETS / "app-icon.png"


def _find_source() -> Path:
    for name in ("app-icon.webp", "app-icon.png"):
        p = _ASSETS / name
        if p.is_file():
            return p
    raise SystemExit(f"No icon source in {_ASSETS} (expected app-icon.webp or app-icon.png).")


def _style_icon_rgba(im) -> object:
    """Black fill inside a rounded rect; transparent outside; artwork on top (Pillow Image in/out)."""
    from PIL import Image as PImage
    from PIL import ImageChops, ImageDraw

    im = im.convert("RGBA")
    w, h = im.size
    mask = PImage.new("L", (w, h), 0)
    draw = ImageDraw.Draw(mask)
    corner_r = max(2, int(round(min(w, h) * 0.22)))
    draw.rounded_rectangle((0, 0, w, h), radius=corner_r, fill=255)
    zero = PImage.new("L", (w, h), 0)
    bg = PImage.merge("RGBA", (zero, zero, zero, mask))
    r, g, b, a = im.split()
    a_clip = ImageChops.multiply(a, mask)
    fg = PImage.merge("RGBA", (r, g, b, a_clip))
    return PImage.alpha_composite(bg, fg)


def _png_scaled_pillow(src: Path, size: int) -> bytes:
    from PIL import Image

    im = Image.open(src).convert("RGBA")
    im = im.resize((size, size), Image.Resampling.LANCZOS)
    im = _style_icon_rgba(im)
    bio = BytesIO()
    im.save(bio, format="PNG")
    return bio.getvalue()


def _png_scaled_ffmpeg(src: Path, size: int) -> bytes:
    r = subprocess.run(
        [
            "ffmpeg",
            "-nostdin",
            "-hide_banner",
            "-loglevel",
            "error",
            "-i",
            str(src),
            "-vf",
            f"scale={size}:{size}:flags=lanczos",
            "-f",
            "image2pipe",
            "-vcodec",
            "png",
            "-",
        ],
        capture_output=True,
        timeout=120,
    )
    if r.returncode != 0 or not r.stdout.startswith(b"\x89PNG"):
        raise RuntimeError(r.stderr.decode("utf-8", "replace") or "ffmpeg png pipe failed")
    return r.stdout


def _ico_from_pngs(pairs: list[tuple[int, bytes]]) -> bytes:
    n = len(pairs)
    reserved, typ, count = 0, 1, n
    header = struct.pack("<HHH", reserved, typ, count)
    offset = 6 + n * 16
    entries = b""
    blobs = b""
    for w, png in pairs:
        h = w
        sz = len(png)
        entries += struct.pack(
            "<BBBBHHII",
            w if w < 256 else 0,
            h if h < 256 else 0,
            0,
            0,
            0,
            0,
            sz,
            offset,
        )
        blobs += png
        offset += sz
    return header + entries + blobs


def main() -> None:
    _ASSETS.mkdir(parents=True, exist_ok=True)
    src = _find_source()
    sizes = (256, 48, 32, 16)
    pngs: list[tuple[int, bytes]] = []

    try:
        import PIL  # noqa: F401

        use_pil = True
    except ImportError:
        use_pil = False

    if use_pil:
        for s in sizes:
            pngs.append((s, _png_scaled_pillow(src, s)))
    else:
        try:
            for s in sizes:
                pngs.append((s, _png_scaled_ffmpeg(src, s)))
        except (RuntimeError, FileNotFoundError) as e:
            print("Need Pillow (`pip install pillow`) or ffmpeg in PATH to rasterize the icon.", file=sys.stderr)
            raise SystemExit(1) from e

    _OUT_ICO.write_bytes(_ico_from_pngs(pngs))
    print(f"Wrote {_OUT_ICO} ({_OUT_ICO.stat().st_size} bytes)")

    if use_pil:
        from PIL import Image

        im = Image.open(src).convert("RGBA")
        im = im.resize((256, 256), Image.Resampling.LANCZOS)
        im = _style_icon_rgba(im)
        im.save(_OUT_PNG, format="PNG")
    else:
        _OUT_PNG.write_bytes(_png_scaled_ffmpeg(src, 256))
    print(f"Wrote {_OUT_PNG}")


if __name__ == "__main__":
    main()
