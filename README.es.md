![BroClock Pro — firmware ESP8266 para relojes SmallTV / SD PRO](assets/banner.svg)

[![Release](https://img.shields.io/github/v/release/sam8ac/BroClock-Pro?label=Release&color=c56a90&style=for-the-badge&logo=star)](https://github.com/sam8ac/BroClock-Pro/releases/latest)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg?style=for-the-badge)](LICENSE)
[![CI](https://img.shields.io/github/actions/workflow/status/sam8ac/BroClock-Pro/ci.yml?branch=main&label=Build&style=for-the-badge&logo=github)](https://github.com/sam8ac/BroClock-Pro/actions/workflows/ci.yml)
![ESP8266](https://img.shields.io/badge/platform-ESP8266-2b6?style=for-the-badge)
![PlatformIO](https://img.shields.io/badge/PlatformIO-ready-orange?style=for-the-badge&logo=platformio)
[![Downloads](https://img.shields.io/github/downloads/sam8ac/BroClock-Pro/total?label=Downloads&style=for-the-badge)](https://github.com/sam8ac/BroClock-Pro/releases)
[![Issues](https://img.shields.io/github/issues/sam8ac/BroClock-Pro?style=for-the-badge)](https://github.com/sam8ac/BroClock-Pro/issues)
![i18n: EN · RU · ES · ZH](https://img.shields.io/badge/i18n-EN%20%C2%B7%20RU%20%C2%B7%20ES%20%C2%B7%20ZH-2b6?style=for-the-badge)

**Idioma de la página:** [English](README.md) · [Русский](README.ru.md) · **Español** · [中文](README.zh.md)

**BroClock Pro** — firmware open source ESP8266 para relojes de
escritorio retro de la familia **GeekMagic SmallTV / SmallTV-Ultra** (incluida
la placa SD PRO): reloj de escritorio con cripto y clima, 8 temas, gráficos de
velas, carrusel, festivos, OTA y una interfaz web en **4 idiomas**.

> 🌐 **Nuestra característica estrella — interfaz totalmente localizada en 4
> idiomas: inglés · ruso · español · chino.**
> No solo el menú: estados, errores, avisos, alertas del escáner Wi-Fi,
> mensajes de subida de fotos y de actualización — todo traducido. El cambio es
> un botón en el panel web, instantáneo, sin recargar; la elección se guarda en
> el dispositivo, se sincroniza con el idioma de la pantalla y funciona 100 %
> sin conexión.

[Instalación](docs/INSTALL.md) · [Hardware](docs/HARDWARE.md) · [HTTP API](docs/HTTP-API.md) · [Releases](https://github.com/sam8ac/BroClock-Pro/releases) · [Issues](https://github.com/sam8ac/BroClock-Pro/issues)

---

Convierte el reloj de escritorio con forma de TV ($20) de adorno en un
dispositivo de información real: **precios de Bitcoin y altcoins con gráficos
de 48 períodos, clima en vivo en cualquier idioma, 8 temas de interfaz
intercambiables, calendarios de festivos, retroiluminación nocturna y control
total desde el navegador del teléfono** — todo sobre el ESP8266 de fábrica
(ESP-12E) y pantalla ST7789 240×240. Sin nube, sin cuenta, licencia MIT.

![BroClock Pro — el reloj con gráfico BTC en pantalla](assets/device.jpg)

*El reloj en acción: gráfico BTC en pantalla.*

## 💻 Lenguajes del proyecto

| Parte | Lenguaje | Dónde |
|---|---|---|
| Firmware (toda la lógica, pantallas, red) | **C++** (Arduino, ESP8266) | [`src/main.cpp`](src/main.cpp) + [`src/*.h`](src) |
| Interfaz web (empotrada en el firmware) | **HTML + CSS + JavaScript** | cadenas raw `PAGE[]`, `SETUP_PAGE[]` en `main.cpp` |
| Herramientas de build de assets/fuentes | **Python + Node.js** | [`tools/`](tools) |
| Sistema de build | **PlatformIO**, versiones fijadas — reproducible | [`platformio.ini`](platformio.ini) |

La interfaz web no carga nada de internet — ni librerías ni fuentes: la sirve
el propio reloj y funciona sin conexión.

## 🌐 Multiidioma — 4 idiomas desde la primera compilación

**EN · RU · ES · ZH** — toda la interfaz, incluidos mensajes de error, avisos,
alertas emergentes y estados. El cambio en la web es instantáneo (sin
recargar), la elección se recuerda en el dispositivo y se sincroniza con el
idioma de la pantalla. Títulos de pestañas, secciones de ajustes, formato de
reloj (12/24), alertas del escáner Wi-Fi, errores de subida de fotos,
geolocalización y actualizaciones — todo traducido.

Este README también está en: [English](README.md) · [Русский](README.ru.md) · [中文](README.zh.md).
La localización del firmware es una sola tabla de diccionario al inicio de
[`src/main.cpp`](src/main.cpp) — añadir un idioma es la contribución más fácil.

## 🕰 ¿Qué es el GeekMagic Clock?

El **GeekMagic Clock** (vendido como GeekMagic SmallTV, SmallTV-Ultra
y placas SD PRO similares) es un gadget de escritorio barato (~$20)
con forma de televisor, **ESP8266** (ESP-12E) y pantalla **ST7789
240×240**. **BroClock Pro** reemplaza el firmware de fábrica: precios
de cripto, clima en vivo, 8 temas, gráficos de velas y panel web.

¿Buscas **firmware para GeekMagic Clock** o **SmallTV custom firmware**?
Estás en el lugar correcto.

## ✨ Características

### 💰 Criptomonedas
- Catálogo **CoinGecko top-250** con búsqueda en vivo y alta manual
- Hasta **4 monedas** en la pantalla principal + hasta **3** en el ticker secundario
- Gráfico de **48 velas** o línea, autoactualización ~60 s
- Las cotizaciones se cachean en flash — tras un reinicio los datos se ven al instante, sin esperar la red
- **«Actualizar datos ahora»** — forzado de precio/velas (ignora caché y backoffs)
- Los fetchers están repartidos en huecos de 4 s: el lote de arranque (velas+precio+cotizaciones) ya no destroza el heap de una vez — bloque libre estable de ~18 KB, sin «heap low» ni reinicios
- Zona horaria por cadena POSIX (pestaña **Ajustes → Reloj e idioma**), sincronía NTP

### 🌤 Clima — dos proveedores, ambos funcionan
- **Open-Meteo** — sin clave ni registro; **OpenWeatherMap** — con clave API; cambio en caliente, sin reinicio
- **Autocompletado de ciudad** (hasta 5 resultados): entiende **cirílico y cualquier latín** — `Vitebsk` encuentra `Vitebsk,BY` igual que `Витебск`
- **«Mi ubicación»** — geolocalización del navegador o por IP; en una instalación limpia la ciudad se rellena sola por la IP del dispositivo (se guarda, el formulario sigue editable)
- Pronóstico a 5 días + condiciones actuales, actualización cada 10 minutos
- La ciudad se guarda como `Nombre,CC` y la aceptan **ambos** proveedores
- **Ciudad vacía por defecto** y una «compuerta»: sin ciudad el dispositivo no martillea los límites de la API, muestra «no city» (en el idioma elegido)

### 🎨 Temas y apariencia
- **8 temas** con tira horizontal de selección y el actual marcado
- Carrusel: cualquier conjunto de temas, intervalo 5–900 s
- Modo noche: rampa de brillo + nivel nocturno aparte
- Color de fondo (tonos predefinidos + paleta), animación de esquina en Classic
- **Tema Foto**: sube una imagen desde el navegador, recorte automático a 240×240, hasta 256 KB, primer fotograma de GIF

![Los 8 temas del proyecto — ciclo GIF](assets/screen.gif)

*Los 8 temas en fila (carrusel).*

### 🧭 Interfaz web
- Pestañas separadas: **Temas · Ajustes · Crypto · Clima · WiFi · Firmware**; «Ajustes» dividido en bloques visuales (carrusel / reloj, zona e idioma / retroiluminación / animación / foto / calendario)
- Diseño adaptable: en el móvil la navegación se convierte en una fila de botones
- Todo se aplica al instante, sin recargar

![Menús de la interfaz web — ciclo GIF](<assets/Web interface.gif>)

*Interfaz web: cambio de pestañas.*

### 📡 WiFi y conexión
- **Portal cautivo** en el primer arranque: punto de acceso `BroClock-PRO` → `192.168.4.1` → elige tu red
- Escáner de redes con niveles de señal; la red guardada se conecta sola
- Cuando ya estás conectado a la red elegida — SSID de solo lectura, contraseña oculta, botón deshabilitado («Conectado»): no puedes romper por accidente una conexión que funciona
- Botón único «Olvidar red»

### 🔒 Seguridad y estabilidad
- **OTA con verificación de huella**: solo se aceptan imágenes de esta familia (chip, flash, pines del display) — un firmware ajeno **no se flasha**, el antiguo sigue funcionando. Un archivo corrupto/renombrado no puede brickar el reloj
- **Compuertas de heap antes de TLS** (7550/12000 bytes) + pausas de 4 s entre fetchers: sin reinicios OOM ni con 4 monedas y velas; RAM — 69 %
- **Arranque rápido**: ventana «silenciosa» de 12 s tras conectar WiFi (DNS/heap se asientan, la página de ajustes se cierra al instante), luego velas/precios/cotizaciones cada 4 s, clima nada más resolver la ciudad; 3 minutos de fallos continuos → autorreinicio
- Las cachés de clima/cripto sobreviven al reinicio; el planificador es seguro con husos horarios/DST y con watchdog
- Las contraseñas nunca se muestran en la UI; las credenciales WiFi viven en el almacenamiento del ESP8266

### 📅 Calendario y más
- Festivos de **más de 40 países**, país autodetectado por IP, caché anual (date.nager.at)

### 🛠 Desarrollo y operaciones
- [Guía completa de instalación](docs/INSTALL.md): 3 SO, flash desde el navegador, backup, rollback, tabla de troubleshooting, FAQ
- **Instalador de Windows de un clic** (`tools\flash_broclock.bat`): encuentra el CH340, flashea y verifica el hash
- **HTTP API** (`/api/status`, `/api/set`, `/api/refresh`, `/api/city`, `/api/wifi`…) — integración con Home Assistant y scripts
- **Build reproducible**: versiones fijadas de plataforma/librerías, parcheo automático del framework (`tools/apply_framework_patches.py`), CI compila ambas revisiones en cada push
- Licencia **MIT** — haz lo que quieras con el reloj

## 🖼 Capturas

La foto del dispositivo y las demos GIF están embebidas arriba; la carpeta completa está en [`assets/`](assets/PHOTOS.md).

## 🧩 Compatibilidad

Probado en **SD PRO** (ambas revisiones de pines) y NodeMCU + ST7789;
eléctricamente compatible con la familia
GeekMagic SmallTV / SmallTV-Ultra.

> ⚠️ **SmallTV-Pro (ESP32) no es compatible.** El primer flash requiere
> USB-UART, después OTA. Tabla completa de dispositivos y pasos:
> [docs/INSTALL.md](docs/INSTALL.md).

## 🚀 Inicio rápido

```bash
# 1. descarga SDP_firmware.bin de Releases y flashea
python -m esptool --port COM5 --baud 460800 write-flash 0x0 SDP_firmware.bin

# 2. conéctate al punto BroClock-PRO, abre http://192.168.4.1
# 3. elige tu red Wi-Fi — el reloj queda en la LAN, IP en pantalla
# 4. siguientes actualizaciones — en la pestaña «Firmware»
```

¿Windows? Ejecuta `tools\flash_broclock.bat` junto al `.bin` — encuentra el
CH340, flashea y verifica el hash:

![flash_broclock.bat — instalador de firmware para Windows](<assets/Installation using bat.JPG>)

## 🔨 Compilar desde el código

```bash
git clone https://github.com/sam8ac/BroClock-Pro.git
cd BroClock-Pro
pio run -e sdpro        # o sdpro_alt para la revisión alternativa
```

## 🗺 Roadmap

- [x] 8 temas + carrusel, UI web en 4 idiomas, pestañas Temas/Ajustes con bloques
- [x] Dos proveedores de clima, autocompletado de ciudad (ru+en), festivos
- [x] OTA con huella, cachés, compuertas de heap, instalador de Windows
- [ ] MQTT / autodescubrimiento de Home Assistant
- [x] Nombres de ciudad en cirílico (Open-Meteo, `language=ru`)
- [ ] Importación de temas por URL

## 🤝 Contribuir

Issues y PRs bienvenidos — checklist en
[.github/PULL_REQUEST_TEMPLATE.md](.github/PULL_REQUEST_TEMPLATE.md).
La localización es el inicio más fácil: todo el diccionario de la UI es una
tabla al inicio de `src/main.cpp`.


## 📜 Licencia

[MIT](LICENSE) © 2026 sam8ac

---

*Palabras clave: GeekMagic Clock · firmware GeekMagic Clock · GeekMagic SmallTV · SmallTV · SmallTV-Ultra · GeekMagic · SD PRO · ESP8266 · ST7789 · reloj retro · crypto ticker · estación meteorológica · PlatformIO · Arduino*
