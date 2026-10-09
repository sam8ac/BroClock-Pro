#!/usr/bin/env python3
"""
pack_assets.py - extract FF_* (flip clock frames) and ANIM_DATA_* (corner animations)
from the C headers into a raw binary blob for flashing to dedicated flash slots,
and strip them from the headers so the irom footprint shrinks by ~476 KB.

Layout in flash (4MB, offsets are raw flash addresses):
  0x140000 .. FF blob   (243600 bytes)  - 10 digits x up to 3 frames x 56x75 x RGB565
  pad + magic block
  0x17C000 .. ANIM blob (242926 bytes)  - 5 animations, RLE 80x80 4bpp
Both sit below LittleFS (0x300000) and above the app (app <= 0x100000).

Outputs:
  assets.bin                       - whole blob (FF + pad/magic + ANIM)
  src/flash_assets.h               - slot offsets, sizes (no payload)
Modifies (in place):
  src/assets_wx.h                  - FF_0..FF_9 arrays removed, FLIP_FS uses offsets
  src/anim_all.h                   - ANIM_DATA_0..4 removed, AnimTab uses offsets

Run from repo root:  python tools\\pack_assets.py
Only runnable once per header state (arrays must still be present).
"""
import re
import sys
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
OUT_BIN = ROOT / "assets.bin"
OUT_HDR = SRC / "flash_assets.h"

FF_SLOT = 0x00140000          # raw flash offset of FF blob
ANIM_SLOT = 0x0017C000        # raw flash offset of ANIM blob (FF_SLOT + 0x3C000)
FF_COUNT = 10
ANIM_COUNT = 5
MAGIC = b"SDPA"               # marks pad region = packed assets v1

RE_FF_HEX = re.compile(r"0x([0-9A-Fa-f]{1,4})")
RE_ANIM_HEX = re.compile(r"0x([0-9A-Fa-f]{1,2})")


def main():
    wx_path = SRC / "assets_wx.h"
    an_path = SRC / "anim_all.h"
    wx = wx_path.read_text(encoding="utf-8", errors="surrogateescape")
    an = an_path.read_text(encoding="utf-8", errors="surrogateescape")

    # ---- FF frames ------------------------------------------------------
    ff_blobs = []
    for i in range(FF_COUNT):
        name = "FF_%d" % i
        pat = re.compile(r"static const uint16_t %s\[\] PROGMEM = \{.*?\};" % name, re.S)
        m = pat.search(wx)
        if not m:
            print("ERROR: %s not found (already packed?)" % name)
            return 1
        vals = [int(h, 16) for h in RE_FF_HEX.findall(m.group(0))]
        ff_blobs.append(b"".join(int(v).to_bytes(2, "little") for v in vals))

    FRAME = 56 * 75 * 2  # 8400
    ff_sizes = [len(b) for b in ff_blobs]
    if any(s % FRAME for s in ff_sizes):
        print("ERROR: FF blob size not multiple of frame size")
        return 1
    ff_frames = [s // FRAME for s in ff_sizes]
    ff_total = sum(ff_sizes)
    print("FF: sizes=%s frames=%s total=%d" % (ff_sizes, ff_frames, ff_total))
    if FF_SLOT + ff_total > ANIM_SLOT:
        print("ERROR: FF blob overflows ANIM_SLOT")
        return 1

    # ---- ANIM data ------------------------------------------------------
    anim_blobs = []
    for i in range(ANIM_COUNT):
        name = "ANIM_DATA_%d" % i
        pat = re.compile(r"static const uint8_t %s\[\d+\] PROGMEM = \{.*?\};" % name, re.S)
        m = pat.search(an)
        if not m:
            print("ERROR: %s not found (already packed?)" % name)
            return 1
        vals = [int(h, 16) for h in RE_ANIM_HEX.findall(m.group(0))]
        anim_blobs.append(bytes(vals))
    anim_total = sum(len(b) for b in anim_blobs)
    print("ANIM: sizes=%s total=%d" % ([len(b) for b in anim_blobs], anim_total))
    if ANIM_SLOT + anim_total > 0x300000:
        print("ERROR: ANIM blob overlaps LittleFS (0x300000)")
        return 1

    # parse original ANIMS table entries BEFORE stripping: { n, ms, PAL, OFF, DATA }
    tbl_old = re.search(
        r"static const AnimTab ANIMS\[ANIM_N\] PROGMEM = \{(.*?)\};", an, re.S)
    if not tbl_old:
        print("ERROR: ANIMS table not found")
        return 1
    entries = re.findall(
        r"\{\s*(\d+),\s*(\d+),\s*ANIM_PAL_(\d+),\s*ANIM_OFF_(\d+),\s*ANIM_DATA_(\d+)\s*\}",
        tbl_old.group(1))
    if len(entries) != ANIM_COUNT:
        print("ERROR: ANIMS entries parsed %d != %d" % (len(entries), ANIM_COUNT))
        return 1

    # parse original FLIP_FS table: { FF_i, n }
    ff_tbl_old = re.search(r"static const FlipFrames FLIP_FS\[10\] = \{(.*?)\};", wx, re.S)
    if not ff_tbl_old:
        print("ERROR: FLIP_FS table not found")
        return 1
    ff_entries = re.findall(r"\{\s*(FF_\d),\s*(\d+)\s*\}", ff_tbl_old.group(1))
    if len(ff_entries) != FF_COUNT:
        print("ERROR: FLIP_FS entries parsed %d != %d" % (len(ff_entries), FF_COUNT))
    # cross-check frame counts
    for i, (nm, n) in enumerate(ff_entries):
        if int(n) != ff_frames[i]:
            print("ERROR: FLIP_FS[%d].n=%s but %s has %d frames" % (i, n, nm, ff_frames[i]))
            return 1

    # per-frame RLE lengths -> max frame size for RAM buffer
    # ANIM_OFF_x has n+1 entries; last == total data length
    an_off_max = 0
    for i in range(ANIM_COUNT):
        m = re.search(r"static const uint32_t ANIM_OFF_%d\[\d+\] PROGMEM = \{(.*?)\};" % i,
                      an, re.S)
        offs = [int(x) for x in re.findall(r"\d+", m.group(1))]
        if len(offs) != int(entries[i][0]) + 1:
            print("ERROR: ANIM_OFF_%d entries %d != n+1" % (i, len(offs)))
            return 1
        if offs[-1] != len(anim_blobs[i]):
            print("ERROR: ANIM_OFF_%d last=%d != data len %d" % (i, offs[-1], len(anim_blobs[i])))
            return 1
        for j in range(len(offs) - 1):
            an_off_max = max(an_off_max, offs[j + 1] - offs[j])
    print("ANIM max frame = %d bytes" % an_off_max)

    # ---- write assets.bin ----------------------------------------------
    ff_offsets = []
    off = 0
    for b in ff_blobs:
        ff_offsets.append(off)
        off += len(b)
    an_offsets = []
    off = 0
    for b in anim_blobs:
        an_offsets.append(off)
        off += len(b)

    pad_start = ff_total
    pad_len = ANIM_SLOT - (FF_SLOT + ff_total)
    if pad_len < 16:
        print("ERROR: pad region too small for magic")
        return 1
    # magic block in pad region: SDPA | ff_total u32 | anim_total u32 | maxframe u32
    magic_blk = MAGIC + struct.pack("<III", ff_total, anim_total, an_off_max)
    pad = b"\xFF" * pad_len
    pad = magic_blk + pad[len(magic_blk):]

    OUT_BIN.write_bytes(b"".join(ff_blobs) + pad + b"".join(anim_blobs))
    print("assets.bin: %d bytes (FF=%d + pad=%d + ANIM=%d)" % (
        OUT_BIN.stat().st_size, ff_total, pad_len, anim_total))

    # ---- strip headers ---------------------------------------------------
    wx_new, n1 = re.subn(
        r"static const uint16_t FF_\d\[\] PROGMEM = \{.*?\};", "", wx, flags=re.S)
    an_new, n2 = re.subn(
        r"static const uint8_t ANIM_DATA_\d\[\d+\] PROGMEM = \{.*?\};", "", an, flags=re.S)
    if n1 != FF_COUNT or n2 != ANIM_COUNT:
        print("ERROR: strip count mismatch ff=%d anim=%d" % (n1, n2))
        return 1
    wx_new = re.sub(r"\n{3,}", "\n\n", wx_new)
    an_new = re.sub(r"\n{3,}", "\n\n", an_new)

    # ---- rewrite FLIP_FS table (offsets instead of pointers) -------------
    ff_table = "static const FlipFrames FLIP_FS[10] = {\n"
    for i in range(FF_COUNT):
        ff_table += "  { %d, %d },  // %s -> flash 0x%06X\n" % (
            ff_offsets[i], ff_frames[i], ff_entries[i][0], FF_SLOT + ff_offsets[i])
    ff_table += "};"
    old = re.search(r"static const FlipFrames FLIP_FS\[10\] = \{.*?\};", wx_new, re.S)
    wx_new = wx_new[:old.start()] + ff_table + wx_new[old.end():]
    wx_new = wx_new.replace(
        "struct FlipFrames { const uint16_t* px; uint16_t n; };",
        "struct FlipFrames { uint32_t slot; uint16_t n; };  // slot = offset in FF blob")

    # ---- rewrite ANIMS table (data = blob offset) ------------------------
    # NOTE: search the table in an_new (indices of tbl_old are stale after strip)
    tbl_new = re.search(r"static const AnimTab ANIMS\[ANIM_N\] PROGMEM = \{.*?\};",
                        an_new, re.S)
    if not tbl_new:
        print("ERROR: ANIMS table not found after strip")
        return 1
    an_tbl = "static const AnimTab ANIMS[ANIM_N] PROGMEM = {\n"
    for i, (n, ms, pal, offf, _d) in enumerate(entries):
        an_tbl += "  { %s, %s, ANIM_PAL_%s, ANIM_OFF_%s, %d },\n" % (
            n, ms, pal, offf, an_offsets[i])
    an_tbl += "};"
    an_new = an_new[:tbl_new.start()] + an_tbl + an_new[tbl_new.end():]
    an_new = an_new.replace(
        "struct AnimTab { uint8_t n; uint8_t ms; const uint16_t* pal; const uint32_t* off; const uint8_t* data; };",
        "struct AnimTab { uint8_t n; uint8_t ms; const uint16_t* pal; const uint32_t* off; uint32_t data; };  // data = offset in ANIM blob")

    # ---- sanity ----------------------------------------------------------
    if "struct FlipFrames { uint32_t slot;" not in wx_new:
        print("ERROR: FlipFrames struct replacement failed")
        return 1
    if "uint32_t data; };" not in an_new:
        print("ERROR: AnimTab struct replacement failed")
        return 1
    if re.search(r"FF_\d\[\] PROGMEM", wx_new):
        print("ERROR: FF arrays still present")
        return 1
    if re.search(r"ANIM_DATA_\d\[\d+\] PROGMEM", an_new):
        print("ERROR: ANIM_DATA arrays still present")
        return 1

    wx_path.write_text(wx_new, encoding="utf-8", errors="surrogateescape")
    an_path.write_text(an_new, encoding="utf-8", errors="surrogateescape")
    print("stripped: assets_wx.h %d -> %d, anim_all.h %d -> %d" % (
        len(wx), len(wx_new), len(an), len(an_new)))

    # ---- generated header ------------------------------------------------
    lines = [
        "#pragma once",
        "// GENERATED by tools/pack_assets.py - raw flash asset slots (do not edit)",
        "// assets.bin: FF blob @ FF_SLOT (%d B), ANIM blob @ ANIM_SLOT (%d B)" % (
            ff_total, anim_total),
        "// flash it: esptool write_flash 0x%X assets.bin" % FF_SLOT,
        "#define FF_SLOT        0x%08XUL" % FF_SLOT,
        "#define FF_TOTAL       %dUL" % ff_total,
        "#define ANIM_SLOT      0x%08XUL" % ANIM_SLOT,
        "#define ANIM_TOTAL     %dUL" % anim_total,
        "#define ANIM_MAX_FRAME %dUL" % an_off_max,
        "#define FLIP_FRAME_BYTES %dUL" % FRAME,
        "",
    ]
    OUT_HDR.write_text("\n".join(lines), encoding="utf-8")
    print("wrote", OUT_HDR)
    return 0


if __name__ == "__main__":
    sys.exit(main())
