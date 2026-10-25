# HTTP API

All endpoints are plain HTTP. No authentication on the LAN (same as the
stock firmware — keep the clock on a trusted network).

Base: `http://DEVICE_IP`

## Status

```bash
curl http://DEVICE_IP/api/status
```

Returns JSON with the live state: `city`, `wprov` (weather source),
`tunit` (temperature display unit, `0` = °C, `1` = °F),
`fw` (firmware version and build date, e.g. `v1.0.1 (Oct 12 2026)`),
`wifi{}`, `themes[]` (on/off + current), `carousel`, `interval`, `tfmt`,
`lang`, `bright`, `nmode`, `nfrom`, `nto`, `nbr`, `bgcolor`, `hol`,
`hctry`, `market{}`, `quotes[]`, chart state, heap/RAM counters, `err`.

## Set a value

```bash
curl "http://DEVICE_IP/api/set?key=brightness&value=40"

curl "http://DEVICE_IP/api/set?key=city&value=%D0%92%D0%B8%D1%82%D0%B5%D0%B1%D1%81%D0%BA"   # Витебск

curl "http://DEVICE_IP/api/set?key=wprov&value=owm"     # owm = OpenWeatherMap, ometo = Open-Meteo
curl "http://DEVICE_IP/api/set?key=city&value="          # empty = "no city" gate
```

Keys: `city`, `wprov`, `tunit`, `carousel`, `interval`, `tfmt`, `lang`,
`brightness`, `nmode`, `nfrom`, `nto`, `nbr`, `bgcolor`, `themes`, `hol`,
`hctry`, `chart`, `refresh`, `tz`, `pdel`, `sym`, `syms`, `relay`, `geo`.

An empty `city` disables the weather fetch (device shows *no city* instead of
looping on errors). `lang` accepts `en|ru|es|zh` and re-renders the web UI.

## LAN relay for crypto (recommended)

The ESP8266 cannot hold a BearSSL session reliably (a handshake needs ~13.5k
of a ~14-16k heap), and every crypto API now forces HTTPS. `tools/cgrelay.py`
runs on an always-on machine on the same LAN, talks HTTPS to CoinGecko and
serves the same JSON to the clock over plain HTTP:

```bash
python tools/cgrelay.py                    # listens on 0.0.0.0:8099
curl "http://DEVICE_IP/api/set?key=relay&value=192.168.0.104:8099"
```

With a relay set, prices, the 3-coin ticker and the candle chart all arrive
over HTTP — the device performs no TLS at all. An empty `relay` restores the
direct HTTPS path. The relay caches for 20s and serves stale data when
CoinGecko hiccups, so the clock never blanks.

Weather already uses plain HTTP (Open-Meteo), no relay needed.

## Refresh

```bash
curl http://DEVICE_IP/api/refresh          # weather + market fetch now
```

`refresh` ages the market/candles stamps (bypasses the cache and the failure
back-offs) and pokes the weather fetchers — the same request the Crypto tab’s
**Refresh data now** button fires.

## Reboot

```bash
curl http://DEVICE_IP/api/reboot           # software restart
```

A clean restart is a *warm* one: the boot animation is skipped, the panel is
not reset (no white flash) and the clock is repainted from cache — visually it
is just a brief repaint. Cold power-on keeps the full boot animation.

## Weather

```bash
curl "http://DEVICE_IP/api/city?q=Vite"    # city autocomplete (5 results)
curl "http://DEVICE_IP/api/geocode?lat=53.9&lon=27.56"   # coords -> city (keyless)
curl "http://DEVICE_IP/api/geoip"          # resolve by external IP: lat/lon + city/countryCode
```

`/api/city` backs onto Open-Meteo geocoding; Cyrillic queries are asked in
Russian automatically. Results are `Name,CC` — that exact form is what
`city=` expects. `/api/geocode` and `/api/geoip` are keyless (BigDataCloud
reverse geocoder, ip-api) — no OpenWeatherMap subscription needed for
“My location”. On a clean install the device itself fills `city` from
`/api/geoip` once and saves it.

## WiFi

```bash
curl http://DEVICE_IP/api/wifiscan         # {"scanning":1} until done, then {"networks":[...]}
curl -X POST http://DEVICE_IP/api/wifi -H 'Content-Type: application/json' \
  -d '{"ssid":"MyNet","pass":"secret"}'
curl -X POST http://DEVICE_IP/api/wifi -H 'Content-Type: application/json' \
  -d '{"forget":1}'
```

When the form SSID is the connected network the web UI shows it read-only with
a disabled **Подключено** button; anything else makes the form editable again.

## Market / crypto

```bash
curl "http://DEVICE_IP/api/search?q=bitcoin"   # CoinGecko top-250 lookup
curl "http://DEVICE_IP/api/search?q=syms"      # secondary-market coins
```

## OTA / files

```bash
curl -F "file=@BroClock_Pro_firmware.bin" http://DEVICE_IP/update      # OTA (fingerprint-checked)
curl http://DEVICE_IP/api/fs                                     # filesystem info
curl -F "file=@icon.png" http://DEVICE_IP/api/iconup            # upload icon
curl -F "file=@photo.jpg" http://DEVICE_IP/api/pupload          # upload photo theme
```

The web UI's **Прошивка / Firmware** tab wraps `/update` with a progress bar
and a confirmation prompt.
