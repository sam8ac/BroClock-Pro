# Changelog

Notable changes per firmware version. The running version is shown on the
web UI **Firmware** tab and in the `fw` field of `/api/status`.

## v1.0.35 - 2026-10-26

- **Weather refresh unblocked** - the weather/forecast fetchers still required mfb >= 8300 (a TLS-era floor) although they run over plain HTTP now; with the heap at ~7.2k every weather refresh was silently skipped (live: 22h without a single weather request). Plain-HTTP paths now need only a 4k block, crypto gates are relay-aware, and no wedge restart fires while the relay is used.

Soak result before this fix: **22.5h uptime, zero restarts**, prices+ticker every ~2 min and candles every 5 min over the LAN relay.

## v1.0.34 - 2026-10-25

**Root-cause pass - the device no longer uses TLS at all.**

- **LAN CoinGecko relay** (	ools/cgrelay.py): the ESP8266 cannot hold a BearSSL session reliably (handshake needs ~13.5k of a ~14-16k heap) and every crypto API forces HTTPS. A tiny HTTP relay on the always-on PC serves the same CoinGecko JSON over plain HTTP; set it with /api/set?key=relay&value=host:port. Prices, the 3-coin ticker and the candle chart now arrive over HTTP (relay caches 20s, serves stale data on CoinGecko hiccups).
- **Weather/forecast/geocoding over plain HTTP** (Open-Meteo) - that host has no MFLN and its handshake never fit next to the 2048-byte TLS buffer.
- **Price + ticker share one request** (were two TLS sessions per minute, the second always starving).
- **Fewer, gentler restarts**: TLS cadence floors (price 120s, candles 300s), breaker thresholds relaxed to genuine wedges (mfb < 6000), 5s retry storm removed.
- **Weather city name** derived when only coordinates were seeded (key=geo).
- Fixed: warm repaint ran before TJpgDec.setCallback() -> WDT boot loop (v1.0.27).

## v1.0.29 - 2026-10-24

- **Open-Meteo over plain HTTP** - weather, 6-day forecast and geocoding now use http:// (no TLS). That host offers no MFLN and its handshake never fit the ESP8266 heap next to a 2048-byte TLS buffer, so the weather stayed on 'loading' for hours. The plain path buffers the body with getString() (chunked framing is only decoded there). Live: /v1/forecast 200 in ~50ms, current 797B + forecast 616B.
- **One request for price + ticker** - the 3-coin ticker used its own /simple/price call; two TLS sessions per minute starved whichever ran second (ticker stale while the price was fresh). They now share one request.

## v1.0.27 - 2026-10-24

- **Boot WDT loop fixed** - the warm repaint ran before TJpgDec.setCallback() was installed, so drawing the weather icons spun in the decoder on a null callback and the hardware watchdog rebooted the board every ~9s. The callback is now set right after the caches, before the early render.

## v1.0.22 - 2026-10-24

- **Black screen after a flash fixed** - the panel frame is only kept for a *software* restart (ESP.restart: OTA, breaker, /api/reboot). After an external reset (esptool/flash/bootloader) the SPI lines float for seconds and the panel can swallow random data mid-command; skipping its reset then left the glass black while the firmware kept drawing.

## v1.0.21 - 2026-10-24

- **Geocode cache persisted** - after the first successful lookup the coordinates are stored in config.json (gcity/glat/glon/gname/gcc), so the weather path never calls the geocoding service again. The Open-Meteo geocoding host was unreachable from the router, which used to freeze the weather for a whole boot.
- **/api/set?key=geo&value=lat,lon** - seed the cache manually (also handy for OWM users).
- **Persistent TLS client tried and reverted** - it removes the ~6.4k ctor churn, but a failed connect leaves the BearSSL engine allocated and a long-lived object never frees it (heap settled at 7.3k, handshakes kept failing). Per-fetch clients stay.
- **TLS RX buffer** stays 2048: 1024 produced ssl=6 (cert chain too large).

## v1.0.12 - 2026-10-24

- **TLS gate tuned to the post-ctx heap** - WiFiClientSecure allocates its BearSSL context (~6.6k) before the fetch helper runs, so the gate now sees post-ctx numbers and only starts a session at a heap peak (free >= 9800, largest block >= 8900). Overnight data: attempts at mfb 7.6k failed essentially always, at 10k they succeed in ~180ms (session resumption). Skip logging is throttled to 20s.

Known issue: the peak still erodes over ~5 minutes of alloc/free churn (each session allocates/frees the ctx), after which the deadlock breaker does an invisible warm restart. The next step is a persistent BearSSL client to stop the churn.

## v1.0.10 - 2026-10-23

- **Diagnostic**: the connect-failure log now prints the resolved IP (ip=...), so a blackholed Cloudflare A record is distinguishable from a heap failure.

## v1.0.9 - 2026-10-23

TLS gate fixed and failures made diagnosable:

- **Gate measured before the prelude** - v1.0.7/v1.0.8 measured the heap after
  DNS + `setBufferSizes` (~4k) and blocked every attempt.  The gate now runs
  before any prelude allocation (9000 free / 8500 largest block).
- **Failing URL in the log** - `[http] connect fail <path> -> ...` names the
  endpoint, which showed the price fetch (`/api/v3/simple/price`) timing out
  while the candle fetch on the same host succeeds.
- **v1.0.6 + v1.0.7 + v1.0.8 rolled up** (throttling, STA reconnect, gate
  placement).

Known issue (open): while the web UI keeps TCP sockets open, the fetch-time
heap sits near the TLS floor and some price handshakes still time out; the
device retries with back-off and recovers.

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
  render temperatures in Р В Р’В Р вЂ™Р’В Р В РІР‚в„ўР вЂ™Р’В Р В Р’В Р Р†Р вЂљРІвЂћСћР В РІР‚в„ўР вЂ™Р’В Р В Р’В Р вЂ™Р’В Р В Р’В Р Р†Р вЂљР’В Р В Р’В Р вЂ™Р’В Р В Р вЂ Р В РІР‚С™Р РЋРІвЂћСћР В Р’В Р В РІР‚В Р В Р вЂ Р В РІР‚С™Р РЋРІР‚С”Р В Р Р‹Р РЋРІР‚С”Р В Р’В Р вЂ™Р’В Р В РІР‚в„ўР вЂ™Р’В Р В Р’В Р В РІР‚В Р В Р’В Р Р†Р вЂљРЎв„ўР В Р вЂ Р Р†Р вЂљРЎвЂєР РЋРЎвЂєР В Р’В Р вЂ™Р’В Р В Р вЂ Р В РІР‚С™Р Р†РІР‚С›РЎС›Р В Р’В Р Р†Р вЂљРІвЂћСћР В РІР‚в„ўР вЂ™Р’В°C or Р В Р’В Р вЂ™Р’В Р В РІР‚в„ўР вЂ™Р’В Р В Р’В Р Р†Р вЂљРІвЂћСћР В РІР‚в„ўР вЂ™Р’В Р В Р’В Р вЂ™Р’В Р В Р’В Р Р†Р вЂљР’В Р В Р’В Р вЂ™Р’В Р В Р вЂ Р В РІР‚С™Р РЋРІвЂћСћР В Р’В Р В РІР‚В Р В Р вЂ Р В РІР‚С™Р РЋРІР‚С”Р В Р Р‹Р РЋРІР‚С”Р В Р’В Р вЂ™Р’В Р В РІР‚в„ўР вЂ™Р’В Р В Р’В Р В РІР‚В Р В Р’В Р Р†Р вЂљРЎв„ўР В Р вЂ Р Р†Р вЂљРЎвЂєР РЋРЎвЂєР В Р’В Р вЂ™Р’В Р В Р вЂ Р В РІР‚С™Р Р†РІР‚С›РЎС›Р В Р’В Р Р†Р вЂљРІвЂћСћР В РІР‚в„ўР вЂ™Р’В°F. Applies to the Classic, Weather and
  Forecast themes (converted at draw time; cached weather stays metric).
  Stored as `tunit` in config, exposed in `/api/status` and `/api/set`,
  localized in EN/RU/ES/ZH. Existing configs keep Р В Р’В Р вЂ™Р’В Р В РІР‚в„ўР вЂ™Р’В Р В Р’В Р Р†Р вЂљРІвЂћСћР В РІР‚в„ўР вЂ™Р’В Р В Р’В Р вЂ™Р’В Р В Р’В Р Р†Р вЂљР’В Р В Р’В Р вЂ™Р’В Р В Р вЂ Р В РІР‚С™Р РЋРІвЂћСћР В Р’В Р В РІР‚В Р В Р вЂ Р В РІР‚С™Р РЋРІР‚С”Р В Р Р‹Р РЋРІР‚С”Р В Р’В Р вЂ™Р’В Р В РІР‚в„ўР вЂ™Р’В Р В Р’В Р В РІР‚В Р В Р’В Р Р†Р вЂљРЎв„ўР В Р вЂ Р Р†Р вЂљРЎвЂєР РЋРЎвЂєР В Р’В Р вЂ™Р’В Р В Р вЂ Р В РІР‚С™Р Р†РІР‚С›РЎС›Р В Р’В Р Р†Р вЂљРІвЂћСћР В РІР‚в„ўР вЂ™Р’В°C (no migration).
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
