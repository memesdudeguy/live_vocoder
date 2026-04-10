#!/usr/bin/env python3
"""Write a short mono float32le carrier (48 kHz) for --validate-carrier / VM smoke tests."""
import math
import struct
import sys

SR = 48_000
DUR_SEC = 1.0


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: gen_smoke_carrier_f32.py <out.f32>", file=sys.stderr)
        return 2
    path = sys.argv[1]
    n = int(SR * DUR_SEC)
    samples = [0.05 * math.sin(2 * math.pi * 220.0 * i / SR) for i in range(n)]
    with open(path, "wb") as f:
        f.write(struct.pack(f"{n}f", *samples))
    print(f"wrote {path} ({n} floats, {SR} Hz mono)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
