# HTTP API

All endpoints are plain HTTP. No authentication on the LAN (same as the
stock firmware — keep the clock on a trusted network).

Base: `http://DEVICE_IP`

## Status

```bash
curl http://DEVICE_IP/api/status
```

Returns JSON with the live state: `city`, `wprov` (weather source),
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

Keys: `city`, `wprov`, `carousel`, `interval`, `tfmt`, `lang`, `brightness`,
`nmode`, `nfrom`, `nto`, `nbr`, `bgcolor`, `themes`, `hol`, `hctry`,
`chart`, `refresh`, `tz`, `pdel`, `sym`, `syms`.

An empty `city` disables the weather fetch (device shows *no city* instead of
looping on errors). `lang` accepts `en|ru|es|zh` and re-renders the web UI.

## Refresh

```bash
curl http://DEVICE_IP/api/refresh          # weather + market fetch now
```

`refresh` ages the market/candles stamps (bypasses the cache and the failure
back-offs) and pokes the weather fetchers — the same request the Crypto tab’s
**Refresh data now** button fires.

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
