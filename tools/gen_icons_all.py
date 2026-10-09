#!/usr/bin/env python3
"""Build icons.bin for the SD PRO icon cache.

One 32x32 RGB565 record (2048 bytes, background baked as 0x0822) for every
coin of the local catalog (src/coins_local.h), in catalog order:

    "SDPI" | uint16 count | uint16 recsize | count * 2048 bytes (little-endian)

Usage:  python gen_icons_all.py     (needs network + Pillow)
Output: ../icons.bin  -> upload with:
    curl -F "file=@icons.bin" http://<device>/api/iconup
"""
import json
import re
import struct
import sys
import time
import urllib.request
from pathlib import Path

from PIL import Image, ImageOps

W = H = 32
REC = W * H * 2
BG = (8, 8, 16)          # 0x0822 - matches the baked background of icons.h
BG565 = 0x0822
UA = {"User-Agent": "Mozilla/5.0 (SDPRO icon builder)"}

ROOT = Path(__file__).resolve().parent.parent
CATALOG = ROOT / "src" / "coins_local.h"
OUT = ROOT / "icons.bin"
MARKETS = ("https://api.coingecko.com/api/v3/coins/markets"
           "?vs_currency=usd&order=market_cap_desc&per_page=250&page=1")


def get(url, tries=5):
    for i in range(tries):
        try:
            req = urllib.request.Request(url, headers=UA)
            with urllib.request.urlopen(req, timeout=30) as r:
                return r.read()
        except Exception as e:
            wait = 15 * (i + 1)
            print(f"  retry {i + 1}/{tries} in {wait}s: {e}")
            time.sleep(wait)
    return None


def catalog_ids():
    txt = CATALOG.read_text(encoding="utf-8")
    ids = re.findall(r'\{\s*"([a-z0-9._-]+)"\s*,\s*"[^"]*"\s*,\s*"[^"]*"\s*\}', txt)
    if len(ids) < 200:
        sys.exit(f"parsed only {len(ids)} ids - check coins_local.h format")
    return ids


def rgb565(px):
    r, g, b = px[:3]
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def make_record(img):
    img = img.convert("RGBA")
    img = ImageOps.contain(img, (W, H), Image.LANCZOS)
    canvas = Image.new("RGBA", (W, H), BG + (255,))
    canvas.paste(img, ((W - img.width) // 2, (H - img.height) // 2), img)
    out = []
    for y in range(H):
        for x in range(W):
            out.append(rgb565(canvas.getpixel((x, y))))
    return struct.pack(f"<{W * H}H", *out)


def main():
    ids = catalog_ids()
    print(f"catalog: {len(ids)} coins")

    print("fetching image urls from CoinGecko ...")
    raw = get(MARKETS)
    if not raw:
        sys.exit("markets request failed")
    markets = json.loads(raw)
    urls = {m["id"]: m.get("image") for m in markets}
    print(f"got {len(urls)} image urls")

    records = []
    missing = []
    for i, cid in enumerate(ids):
        url = urls.get(cid)
        if not url:
            missing.append(cid)
            records.append(struct.pack(f"<{W * H}H", *([BG565] * (W * H))))
            continue
        data = get(url, tries=3)
        if not data:
            missing.append(cid)
            records.append(struct.pack(f"<{W * H}H", *([BG565] * (W * H))))
            continue
        try:
            from io import BytesIO
            img = Image.open(BytesIO(data))
            records.append(make_record(img))
        except Exception as e:
            print(f"  decode failed {cid}: {e}")
            missing.append(cid)
            records.append(struct.pack(f"<{W * H}H", *([BG565] * (W * H))))
        if (i + 1) % 25 == 0:
            print(f"  {i + 1}/{len(ids)}")
        time.sleep(0.15)          # stay polite on the free tier

    blob = b"SDPI" + struct.pack("<HH", len(records), W * H) + b"".join(records)
    OUT.write_bytes(blob)
    print(f"wrote {OUT} ({len(blob)} bytes), missing: {missing or 'none'}")


if __name__ == "__main__":
    main()
