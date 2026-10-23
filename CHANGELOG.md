# Changelog

Notable changes per firmware version. The running version is shown on the
web UI **Firmware** tab and in the `fw` field of `/api/status`.

## v1.0.6 - 2026-10-23

Data-reliability pass after a 7-hour soak (log analysis):

- **Cache-write throttling** - price cache <= 5 min, candles <= 10 min, weather
  <= 15 min; a changed coin/range/city still writes immediately. A WiFi
  sys-task stack smash was observed right after a flash commit, and one commit
  per fetch was a lot of chances to hit it.
- **STA reconnect on a wedged link** - after 5 real TLS failures in a row the
  station reconnects (plain HTTP from the device still worked while TLS
  "connection failed" instantly, i.e. the lwip socket state was stuck). A
  successful fetch resets the counter.
- **Quieter logs** - `price FAIL` / `candle FAIL` are printed for real attempts
  only, not for back-off skips.

## v1.0.5 - 2026-10-23

Warm restarts are now visually seamless:

- **No panel reset on a clean restart** - `tools/patch_tftespi.py` (pre-build)
  lets the firmware skip the ST7789 reset when the previous run ended cleanly
  (`Software/System restart`, `External System`). The panel keeps its GRAM, so
  the old clock stays on the glass until the repaint: no white flash, no black
  frame. After a crash the SPI bus may have been interrupted mid-command, so
  there the full reset still runs.
- **Repaint from cache before the WiFi wait** - the card caches are loaded
  right after the filesystem mount, so a warm restart shows the clock within
  ~0.3s instead of leaving blank glass during the connect sequence.
- **`/api/reboot`** - software restart on demand (also used to test the warm
  path without opening the USB port, which resets the board by itself).

## v1.0.2 - 2026-10-23

Stability pass - fewer restarts, and (where unavoidable) invisible ones:

- **Escalating TLS back-off** - after a failed connection/DNS lookup the next
  attempt waits 30s, then 1m, 2m, 4m, 5m (jittered) instead of retrying every
  ~5s. That retry churn shredded the largest free block and ended in
  "Unhandled C++ exception: OOM" reboots. A manual *Refresh* always bypasses
  the back-off; a successful fetch resets it. Skipped attempts no longer count
  as wedged fetchers, so a plain ISP outage no longer reboots the clock.
- **Deferred flash commits** - LittleFS cache writes (chart/weather) wait for
  a quiet window (no TLS for 4s, largest block >= 12K). A commit right on the
  heels of a TLS session is the observed hardware-WDT crash shape inside
  `lfs_dir_commitcrc`. Writes are idempotent, so a deferral costs nothing.
- **Warm-restart fast path** - on any reset that is not a cold power-on
  (crash, watchdog, OTA) the boot animation and splash are skipped: the
  ST7789 keeps its GRAM across the ESP reset, so the last frame stays on the
  glass until the first repaint. A recovery now looks like a brief repaint
  instead of a 1-2s boot animation. Cold power-on keeps the animation.

## v1.0.1 - 2026-10-12

- **Temperature units** - new *Temperature units* setting on the Weather tab:
  render temperatures in °C or °F. Applies to the Classic, Weather and
  Forecast themes (converted at draw time; cached weather stays metric).
  Stored as `tunit` in config, exposed in `/api/status` and `/api/set`,
  localized in EN/RU/ES/ZH. Existing configs keep °C (no migration).
- **Installed firmware version** - the Firmware tab shows the running
  firmware version and build date; also exposed as `fw` in `/api/status`.
- Repository: added this CHANGELOG; `docs/HTTP-API.md` documents `tunit`
  and `fw`.

## v1.0.0 - 2026-10-11

First public release ([release notes](https://github.com/sam8ac/BroClock-Pro/releases/tag/v1.0.0)):

- **Keyless firmware** - no embedded API keys; weather works out of the box
  via Open-Meteo, optional OpenWeatherMap / CoinGecko Demo keys in the web UI
- 8 themes + carousel, web UI in EN/RU/ES/ZH
- CoinGecko top-250 catalog, 48-period candles, cached quotes
- 5-day forecast, city autocomplete, holidays for 40+ countries
- OTA with family fingerprint check (foreign builds rejected safely)
- Photo theme, night mode, Windows one-click flasher
