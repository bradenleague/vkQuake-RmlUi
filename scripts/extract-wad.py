#!/usr/bin/env python3
"""Extract graphics from Quake PAK/WAD files to PNG.

Usage:
    extract-wad.py <pak_file> [output_dir]

Extracts gfx.wad lumps from a Quake PAK file and converts QPIC graphics
to individual PNG files using the Quake palette (gfx/palette.lmp).
"""

import struct
import sys
import os
from pathlib import Path
from PIL import Image


def read_pak(pak_path: str) -> dict[str, bytes]:
    """Parse a Quake PAK file, return {filename: data} dict."""
    entries = {}
    with open(pak_path, "rb") as f:
        magic = f.read(4)
        if magic != b"PACK":
            raise ValueError(f"Not a PAK file: {pak_path}")
        dir_offset, dir_size = struct.unpack("<II", f.read(8))
        num_entries = dir_size // 64  # each entry is 64 bytes
        f.seek(dir_offset)
        for _ in range(num_entries):
            name_raw = f.read(56)
            offset, size = struct.unpack("<II", f.read(8))
            name = name_raw.split(b"\x00", 1)[0].decode("ascii", errors="replace")
            pos = f.tell()
            f.seek(offset)
            entries[name.lower()] = f.read(size)
            f.seek(pos)
    return entries


def read_palette(data: bytes) -> list[tuple[int, int, int, int]]:
    """Parse palette.lmp (768 bytes = 256 RGB triplets). Index 255 is transparent."""
    if len(data) < 768:
        raise ValueError(f"Palette too small: {len(data)} bytes")
    palette = []
    for i in range(256):
        r, g, b = data[i * 3 : i * 3 + 3]
        # Index 255 is conventionally transparent in Quake
        a = 0 if i == 255 else 255
        palette.append((r, g, b, a))
    return palette


def read_wad(data: bytes) -> dict[str, tuple[int, bytes]]:
    """Parse a WAD2 file, return {name: (type, data)} dict."""
    magic = data[:4]
    if magic != b"WAD2":
        raise ValueError(f"Not a WAD2 file (got {magic!r})")
    num_lumps, dir_offset = struct.unpack_from("<II", data, 4)
    lumps = {}
    for i in range(num_lumps):
        entry_offset = dir_offset + i * 32
        lump_offset, disk_size, full_size, lump_type, compression = struct.unpack_from(
            "<IIIBb", data, entry_offset
        )
        name_raw = data[entry_offset + 16 : entry_offset + 32]
        name = name_raw.split(b"\x00", 1)[0].decode("ascii", errors="replace").lower()
        lump_data = data[lump_offset : lump_offset + disk_size]
        lumps[name] = (lump_type, lump_data)
    return lumps


def qpic_to_image(
    data: bytes, palette: list[tuple[int, int, int, int]]
) -> Image.Image:
    """Convert a QPIC lump to a PIL Image."""
    width, height = struct.unpack_from("<II", data, 0)
    pixels = data[8 : 8 + width * height]
    if len(pixels) < width * height:
        raise ValueError(f"QPIC truncated: {len(pixels)} < {width * height}")
    img = Image.new("RGBA", (width, height))
    img_data = [palette[b] for b in pixels]
    img.putdata(img_data)
    return img


TYP_QPIC = 0x42


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <pak_file> [output_dir]")
        sys.exit(1)

    pak_path = sys.argv[1]
    output_dir = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("ui/img/sbar")

    print(f"Reading {pak_path}...")
    pak_entries = read_pak(pak_path)
    print(f"  {len(pak_entries)} entries in PAK")

    # Extract palette
    palette_key = "gfx/palette.lmp"
    if palette_key not in pak_entries:
        print(f"ERROR: {palette_key} not found in PAK")
        sys.exit(1)
    palette = read_palette(pak_entries[palette_key])
    print(f"  Palette loaded (256 colors)")

    # Extract WAD
    wad_key = "gfx.wad"
    if wad_key not in pak_entries:
        print(f"ERROR: {wad_key} not found in PAK")
        sys.exit(1)
    lumps = read_wad(pak_entries[wad_key])
    print(f"  {len(lumps)} lumps in gfx.wad")

    # Convert QPIC lumps to PNG
    output_dir.mkdir(parents=True, exist_ok=True)
    count = 0
    skipped = 0
    for name, (lump_type, data) in sorted(lumps.items()):
        if lump_type != TYP_QPIC:
            skipped += 1
            continue
        try:
            img = qpic_to_image(data, palette)
            out_path = output_dir / f"{name}.png"
            img.save(out_path)
            count += 1
            print(f"  {name:20s}  {img.width:3d}x{img.height:<3d}  -> {out_path}")
        except Exception as e:
            print(f"  {name:20s}  FAILED: {e}")
            skipped += 1

    print(f"\nDone: {count} PNGs written to {output_dir}/, {skipped} skipped")


if __name__ == "__main__":
    main()
