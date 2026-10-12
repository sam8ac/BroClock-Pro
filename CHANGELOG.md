# Changelog

Notable changes per firmware version. The running version is shown on the
web UI **Firmware** tab and in the `fw` field of `/api/status`.

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
