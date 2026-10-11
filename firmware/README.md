# Firmware

Ready-to-flash images for **BroClock Pro** (GeekMagic Clock / SmallTV / SmallTV-Ultra / SD PRO).

| File | What it is | When to use |
|---|---|---|
| `SDP_firmware.bin` | **Application image** (same as the GitHub Release asset) | Normal install: flash at `0x0`, or upload via the web UI **Firmware** tab |
| `BroClock_Pro_factory.bin` | **Full factory image** (4 MB, app + filesystem + blank config) | Complete restore / first flash of a blank board |

## Flash

```bash
python -m esptool --port COM5 --baud 460800 write-flash 0x0 SDP_firmware.bin
```

On Windows you can double-click `tools\flash_broclock.bat` next to the `.bin` — it finds the CH340, flashes and verifies the hash.

## Full factory image

```bash
python -m esptool --port COM5 --baud 460800 write-flash 0x0 BroClock_Pro_factory.bin
```

This wipes Wi-Fi credentials, API keys and all settings — use it only when you want a clean slate.

## Notes

- Images are **keyless**: no OpenWeatherMap / CoinGecko keys embedded. Weather works via Open-Meteo out of the box; enter your own API keys in the web UI (Settings).
- Always flash at **0x0**.
- Latest release: <https://github.com/sam8ac/BroClock-Pro/releases>
