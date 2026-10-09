#!/usr/bin/env python3
"""Replace one weather icon (key like 01d) in src/assets_wx.h from a PNG.

Each icon ships with an alpha bitmask (1 = icon pixel, 0 = skip): wxJpgOut
paints only mask=1 pixels, so the background shows through for real on any
theme - no colour key, no JPEG ringing artefacts around the transparency.

Usage:
    python tools/gen_wx_icon.py <path/to/icon.png> <key>
e.g.
    python tools/gen_wx_icon.py g:/0/sdpro_fs_extract/ico/01d.png 01d

Rewrites in src/assets_wx.h:
    WJ_BIG_<key>  (65x46)   table len + mask entry
    WM_BIG_<key>  (alpha bitmask)
    WJ_SM_<key>   (45x32)   table len + mask entry
    WM_SM_<key>   (alpha bitmask)
"""
import io
import re
import sys
from pathlib import Path

from PIL import Image

BIG = (65, 46)
SM = (45, 32)
NATIVE = {"humidity": (30, 30), "temperature": (30, 30)}  # thermo/hum slots
QUALITY = 92
ALPHA_MIN = 128            # alpha >= this counts as icon pixel

ROOT = Path(__file__).resolve().parent.parent
WX = ROOT / "src" / "assets_wx.h"


def fit(img: Image.Image, size) -> Image.Image:
    img = img.convert("RGBA")
    if img.size != size:
        scale = min(size[0] / img.width, size[1] / img.height)
        img = img.resize((max(1, round(img.width * scale)),
                          max(1, round(img.height * scale))), Image.LANCZOS)
    out = Image.new("RGBA", size, (0, 0, 0, 0))
    out.paste(img, ((size[0] - img.width) // 2,
                    (size[1] - img.height) // 2), img)
    return out


def keyed_jpeg(img: Image.Image, size) -> bytes:
    rgba = fit(img, size)
    canvas = Image.new("RGB", size, (12, 6, 20))
    canvas.paste(rgba, (0, 0), rgba)
    buf = io.BytesIO()
    canvas.save(buf, "JPEG", quality=QUALITY)
    return buf.getvalue()


def alpha_mask(img: Image.Image, size) -> bytes:
    rgba = fit(img, size)
    w, h = size
    stride = (w + 7) // 8
    out = bytearray(stride * h)
    for y in range(h):
        for x in range(w):
            if rgba.getpixel((x, y))[3] >= ALPHA_MIN:
                out[y * stride + (x >> 3)] |= 0x80 >> (x & 7)
    return bytes(out)


def c_array(name: str, data: bytes) -> str:
    lines = ["static const uint8_t %s[] PROGMEM = {" % name]
    for i in range(0, len(data), 16):
        lines.append(",".join("0x%02X" % b for b in data[i:i + 16]) + ",")
    lines.append("};")
    return "\n".join(lines)


def replace_array(text: str, name: str, body: str, after: str = None) -> str:
    pat = re.compile(r"static const uint8_t %s\[\] PROGMEM = \{.*?\};" % name, re.S)
    new, n = pat.subn(lambda m: body, text, count=1)
    if n == 1:
        return new
    if after is None:
        sys.exit("ERROR: array %s not found" % name)
    pat2 = re.compile(r"(static const uint8_t %s\[\] PROGMEM = \{.*?\};)" % after, re.S)
    m = pat2.search(text)
    if not m:
        sys.exit("ERROR: array %s not found either" % name)
    return text[:m.end()] + "\n\n" + body + text[m.end():]


def replace_entry(text: str, name: str, key: str, ln: int, mask: str) -> str:
    pat = re.compile(r"\{\"[^\"]+\", %s, \d+(?:, \w+)?\}" % name)
    m = pat.search(text)
    if not m:
        sys.exit("ERROR: table entry %s not found" % name)
    repl = '{"%s", %s, %d, %s}' % (key, name, ln, mask)
    return text[:m.start()] + repl + text[m.end():]


def main():
    if len(sys.argv) == 1:
        # batch: every PNG in the extract folder
        src = Path("g:/0/sdpro_fs_extract/ico")
        jobs = sorted(p for p in src.glob("*.png"))
        if not jobs:
            sys.exit("ERROR: no PNGs in %s" % src)
        for png in jobs:
            process(png, png.stem)
        print("patched %s" % WX)
        return
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    png, key = Path(sys.argv[1]), sys.argv[2]
    if not png.is_file():
        sys.exit("ERROR: %s not found" % png)
    process(png, key)
    print("patched %s" % WX)


def process(png: Path, key: str):
    im = Image.open(png)
    size = NATIVE.get(key, BIG)
    big = keyed_jpeg(im, size)
    mbig = alpha_mask(im, size)
    print("%s: %dx%d %d B (+mask %d B)"
          % (key, size[0], size[1], len(big), len(mbig)))

    text = WX.read_text(encoding="utf-8", errors="surrogateescape")
    text = replace_array(text, "WJ_BIG_%s" % key, c_array("WJ_BIG_%s" % key, big))
    text = replace_array(text, "WM_BIG_%s" % key, c_array("WM_BIG_%s" % key, mbig),
                         after="WJ_BIG_%s" % key)
    text = replace_entry(text, "WJ_BIG_%s" % key, key, len(big), "WM_BIG_%s" % key)

    if re.search(r"static const uint8_t WJ_SM_%s\[\]" % re.escape(key), text):
        sm = keyed_jpeg(im, SM)
        msm = alpha_mask(im, SM)
        print("%s: SM  %dx%d %d B (+mask %d B)"
              % (key, SM[0], SM[1], len(sm), len(msm)))
        text = replace_array(text, "WJ_SM_%s" % key, c_array("WJ_SM_%s" % key, sm))
        text = replace_array(text, "WM_SM_%s" % key, c_array("WM_SM_%s" % key, msm),
                             after="WJ_SM_%s" % key)
        text = replace_entry(text, "WJ_SM_%s" % key, key, len(sm), "WM_SM_%s" % key)
    WX.write_text(text, encoding="utf-8", errors="surrogateescape")


if __name__ == "__main__":
    main()

