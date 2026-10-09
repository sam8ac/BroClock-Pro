# Flashing

> **First flash requires a USB-UART adapter.** After that, updates are plain
> web-UI uploads (OTA).

## 0. Backup (recommended)

Read the whole 4 MB flash first so you can always go back:

```bash
esptool.py --port COM5 --baud 460800 read-flash 0 0x400000 backup.bin
```

## 1. Get a firmware image

Download from [Releases](../../releases/latest):

| File | When to use |
|---|---|
| `SDP_firmware.bin` | **App image** — normal first flash and OTA (≈880 KB) |
| `BroClock_Pro_factory.bin` | Full 4 MB image (app + assets + splash + LittleFS) — write at `0x0` if you want a factory-fresh card |

## 2. First flash (UART)

```bash
# Python + esptool
pip install esptool

python -m esptool --port COM5 --baud 460800 \
  write-flash 0x0 SDP_firmware.bin
```

Some boards need `--before default_reset --after hard_reset`.

**Windows one-click:** run `tools\flash_broclock.bat` — it finds the
`USB-SERIAL CH340` port, backs up nothing, writes the image and verifies the
hash.

Wiring the adapter to the clock: `3V3`, `GND`, `RX→TX`, `TX→RX`. GPIO0 must be
held to GND during power-up to enter the bootloader on some boards — most SD PRO
units boot straight into the bootloader with the stock USB-TTL header.

## 3. Bootstrap

1. After the flash the device reboots and starts the hotspot **`BroClock-PRO`**
   (open network, no password).
2. Connect to it and open **<http://192.168.4.1>**.
3. Pick your network, enter the password, **Save and connect**.
4. The device restarts on your LAN; its IP is shown on the clock face and at
   the top of the web UI.

## 4. OTA updates (day-to-day)

Open the web UI → **Прошивка / Firmware** tab → select `.bin` → **Обновить
прошивку**. Only images of this build are accepted: the device checks a
fingerprint (chip, flash size, display pins) inside the file and rejects
foreign images **without writing**, so a wrong upload cannot brick the clock —
the old firmware keeps running.

## 5. Full factory reflash

```bash
python -m esptool --port COM5 --baud 460800 \
  write-flash 0x0 BroClock_Pro_factory.bin
```

4 MB image: app at `0x0`, assets at `0x140000`, splash at `0x1B8000`,
LittleFS (icons) at `0x300000`. Wipes the saved config — bootstrap again.

## Serial monitor

`115200 8N1`, tool of your choice (`pio device monitor`, PuTTY, Arduino IDE).
Boot log shows the fingerprint, cache state and Wi-Fi result; runtime logs
`[wx]`, `[fc]`, `[candle]` for weather/forecast/crypto fetches.
