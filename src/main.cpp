// SD PRO custom firmware
// Themes: Clock + Crypto ticker (price, change, candle/line chart)
// Data: CoinGecko (HTTPS). Config: LittleFS /config.json. OTA: /update

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <ESP8266HTTPClient.h>
#include <Updater.h>
#include <StreamString.h>
#include <WiFiClientSecureBearSSL.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <time.h>
#include <lwip/dns.h>
#include "icons.h"
#include "anim_all.h"
#include "boot_winxp.h"
#include "assets_wx.h"
#include "flash_assets.h"
#include <TJpg_Decoder.h>
#include "coins_local.h"
#include "cal_ru.h"
#include "font_ru.h"

static TFT_eSPI tft;
static ESP8266WebServer server(80);

// ---------------------------------------------------------------- palette
static uint16_t C_BG = 0x0822;   // screen background, runtime-tunable (cfg.bgcolor)
#define C_PANEL    0x10C6   // chart panel
#define C_BORDER   0x2947   // borders
#define C_GRID     0x18E3   // grid lines
#define C_HDR      0x1084   // header bar
#define C_PRICE_BG 0xFFE0   // yellow price box
#define C_PRICE_FG 0x0000   // black on yellow
#define C_UP       0x07E0   // green
#define C_DOWN     0xF800   // red
#define C_UP_D     0x03E0   // dark green: badge background (white text stays readable)
#define C_DOWN_D   0x9000   // dark red:   badge background
#define C_ORANGE   0xFD20   // stock: minutes / seconds / city
#define C_MON_P    0xFA75   // pastel red (date month)
#define C_LINE     0xFB06   // orange line
#define C_TXT      0xEF7D   // light grey
#define C_GREY     0x632C   // neutral grey ~ (100,100,100), subdued but readable
#define C_DIM      0x6B4D   // dim grey
#define C_CYAN     0x07FF
#define C_WHITE    0xFFFF
#define C_CLOUD    0x8C51   // cloud fill
#define C_MIST     0x528A   // fog lines

// ---------------------------------------------------------------- config
struct Cfg {
  String theme   = "ticker";  // current theme key
  String enabled = "classic,flip,weather,forecast,ticker,crypto3,calendar,photo";  // all themes on first boot
  bool   carousel= true;
  uint16_t interval = 45;     // seconds between themes
  String sym     = "bitcoin";
  String range   = "1d";      // 1d | 7d (30d removed: CoinGecko origin too slow for its body)
  String chart   = "candle";  // candle | line
  uint8_t brightness = 100;   // 0..100 percent (stock scale)
  uint16_t refresh = 60;      // data refresh seconds
  String tz      = "MSK-3";   // POSIX TZ
  String city    = "";        // empty by default: picked via autocomplete/My location
  String syms    = "bitcoin,ethereum,solana";  // up to 3 ids for "Crypto all"
  uint8_t gifnum = 0;      // classic corner animation 0..3
  uint16_t bgcolor = 0x0822;  // RGB565 screen background
  String wifiSsid;         // saved network for auto-connect
  String wifiPass;
  bool   hol   = false;    // highlight state holidays (calendar theme)
  String hctry;            // ISO-3166 alpha-2 country ("" = auto-detect via geo)
  bool   nmode = false;    // night backlight window enabled
  String nfrom = "23:00";  // HH:MM - night starts
  String nto   = "07:00";  // HH:MM - night ends (may cross midnight)
  uint8_t nbr  = 20;       // night brightness percent (0..100)
  String tfmt  = "24";     // clock format: "24" | "12" (AM/PM)
  String lang  = "en";     // UI language: "ru" | "en" | "es" | "zh" (default English)
  String wprov = "owm";    // weather source: "owm" | "ometo"
} cfg;

// 0 = ru, 1 = en, 2 = es, 3 = zh
static inline int langIdx(){
  if(cfg.lang == "en") return 1;
  if(cfg.lang == "es") return 2;
  if(cfg.lang == "zh") return 3;
  return 0;
}
// true when the Russian UI is selected (content decisions)
static inline bool ru(){ return cfg.lang == "ru"; }
// true when text must go through the custom FontRu renderer: every language
// except EN (the built-in GFX fonts carry ASCII only, so es/zh need FontRu)
static inline bool gfxFont(){ return cfg.lang != "en"; }
// pick a string by language: ru, en, es, zh.  PSTR keeps all four literals in
// flash (irom0): plain literals land in .rodata/DRAM, and the es/zh additions
// pushed the fetch-time free heap under the 9000 gate.
#define LG(r,e,s,z) pickLangL(PSTR(r),PSTR(e),PSTR(s),PSTR(z))
static inline const char* pickLangL(const char* r, const char* e,
                                    const char* s, const char* z){
  switch(langIdx()){ case 1: return e; case 2: return s; case 3: return z; default: return r; }
}
// weather/geocoder API language suffix for the selected language ("" for EN)
static inline const char* langQ(){
  if(cfg.lang == "ru") return "&lang=ru";
  if(cfg.lang == "es") return "&lang=es";
  if(cfg.lang == "zh") return "&lang=zh_cn";
  return "";
}
// bare Open-Meteo geocoder language code
static inline const char* geoLang(){
  if(cfg.lang == "ru") return "ru";
  if(cfg.lang == "es") return "es";
  if(cfg.lang == "zh") return "zh";
  return "en";
}
// IANA zone (ip-api) -> POSIX TZ.  No DST rules: a fixed offset is fine for
// a desk clock, the dropdown still offers full control afterwards.
static const char* posixFromIana(const String& iana, int offSec){
  static const char* T[][2] = {
    {"Europe/Minsk","MSK-3"},{"Europe/Kaliningrad","EET-2"},{"Europe/Moscow","MSK-3"},
    {"Europe/Kyiv","EET-2"},{"Europe/Kiev","EET-2"},{"Europe/Warsaw","CET-1"},
    {"Europe/Berlin","CET-1"},{"Europe/Paris","CET-1"},{"Europe/Madrid","CET-1"},
    {"Europe/Rome","CET-1"},{"Europe/Belgrade","CET-1"},{"Europe/Istanbul","+03"},
    {"Europe/Athens","EET-2"},{"Europe/Bucharest","EET-2"},{"Europe/Lisbon","WET0"},
    {"Europe/London","GMT0"},{"Asia/Almaty","ALMT-6"},{"Asia/Yekaterinburg","+05"},
    {"Asia/Yerevan","+04"},{"Asia/Baku","+04"},{"Asia/Tbilisi","+04"},
    {"Asia/Omsk","+06"},{"Asia/Novosibirsk","+07"},{"Asia/Krasnoyarsk","+07"},
    {"Asia/Irkutsk","+08"},{"Asia/Yakutsk","+09"},{"Asia/Vladivostok","+10"},
    {"Asia/Jerusalem","IST-2"},{nullptr,nullptr}
  };
  if(iana.length()){
    for(int i = 0; T[i][0]; i++) if(iana == T[i][0]) return T[i][1];
  }
  int o = offSec / 3600;                      // fall back to the raw offset
  static char b[12];
  if(o == 0) return "UTC0";
  snprintf(b, sizeof(b), "%s%d", o > 0 ? "UTC-" : "UTC+", o > 0 ? o : -o);
  return b;
}
// BigDataCloud reverse-geocoder language (falls back to en for anything else)
static inline String geoLocalityLang(){
  if(cfg.lang == "ru" || cfg.lang == "es" || cfg.lang == "zh") return String(cfg.lang);
  return String("en");
}
// OWM local_names key used by the fallback geocoder
static inline const char* owmLocKey(){
  if(cfg.lang == "es") return "es";
  if(cfg.lang == "zh") return "zh_cn";
  return "ru";
}

// cut a String to n bytes without splitting a UTF-8 sequence (Cyrillic = 2 bytes)
static String cutB(const String& s, size_t n){
  if(s.length() <= n) return s;
  size_t e = n;
  while(e > 0 && ((uint8_t)s[e] & 0xC0) == 0x80) e--;
  return s.substring(0, e);
}

// fetch errors are written in English -> show them in the selected language,
// both on the TFT (loading/waiting slots) and in /api/status
static String errLoc(const String& e){
  if(cfg.lang != "en" && e.length()){
    // all four languages in flash: the kv struct + strings used to sit in
    // .rodata/DRAM, and the es/zh additions pushed the fetch gate under 9000
    static const char KE[][20] PROGMEM __attribute__((aligned(4))) = {
  "no data", "no quotes", "empty body", "empty", "rate limited (429)", "dns timeout",
  "begin failed", "no memory (oom)", "geocode", "fc geocode", "fc parse", "fc no list", "fc oom",
  "ohlc empty", "empty ohlc", "ohlc oom", "ohlc eof", "ohlc truncated", "holidays empty",
  "holidays parse", "connection refused", "connection lost", "connection failed",
  "connection closed", "stream disconnected", "no response", "read timeout", "timeout",
  "unknown error", "no city",
    };
    static const char KR[][36] PROGMEM __attribute__((aligned(4))) = {
  "нет данных", "нет котировок", "пустой ответ", "пусто", "лимит (429)", "таймаут DNS",
  "нет соединения", "не хватает памяти", "геокодер", "геокодер", "прогноз: разбор",
  "прогноз: пусто", "прогноз: память", "нет свечей", "нет свечей", "свечи: память", "свечи: обрыв",
  "свечи: обрезаны", "праздники: пусто", "праздники: ошибка", "нет соединения", "обрыв соединения",
  "обрыв связи", "связь закрыта", "поток оборван", "нет ответа", "таймаут чтения", "таймаут",
  "неизвестная ошибка", "город не задан",
    };
    static const char KS[][28] PROGMEM __attribute__((aligned(4))) = {
  "sin datos", "sin cotizaciones", "cuerpo vacío", "vacío", "límite (429)", "tiempo de espera DNS",
  "sin conexión", "sin memoria", "geocodificador", "geocodificador", "análisis del pronóstico",
  "pronóstico: vacío", "pronóstico: memoria", "sin velas", "sin velas", "velas: memoria",
  "velas: corte", "velas: truncadas", "festivos: vacío", "festivos: error", "conexión rechazada",
  "conexión perdida", "fallo de conexión", "conexión cerrada", "flujo cortado", "sin respuesta",
  "tiempo de espera de lectura", "tiempo de espera", "error desconocido", "sin ciudad",
    };
    static const char KZ[][16] PROGMEM __attribute__((aligned(4))) = {
  "无数据", "无报价", "空响应", "空", "限流 (429)", "DNS 超时", "无连接", "内存不足", "地理编码", "地理编码", "预报: 解析", "预报: 空",
  "预报: 内存", "无K线", "无K线", "K线: 内存", "K线: 中断", "K线: 截断", "假日: 空", "假日: 错误", "连接被拒", "连接丢失", "连接失败",
  "连接关闭", "流中断", "无响应", "读取超时", "超时", "未知错误", "未设置城市",
    };
    for(size_t i = 0; i < sizeof(KE) / sizeof(KE[0]); i++){
      // ROM strcmp byte-loads its irom operand -> LoadStoreError on KE[i];
      // copy the row to the stack first (memcpy_P is word-based, proven safe)
      char row[sizeof(KE[0])];
      memcpy_P(row, KE[i], sizeof(row));
      if(e == row){
        if(langIdx() == 2) return String((const char*)KS[i]);
        if(langIdx() == 3) return String((const char*)KZ[i]);
        return String((const char*)KR[i]);
      }
    }
    if(e.startsWith("json "))   return String("JSON: ") + e.substring(5);
    if(e.startsWith("fc http ")) return String(LG("прогноз: HTTP", "fc HTTP",
                                                  "pronóstico: HTTP", "预报: HTTP")) + e.substring(8);
  }
  return e;
}

// ---------------------------------------------------------------- themes
struct ThemeDef { const char* key; const char* name; };
static const ThemeDef THEMES[] = {
  {"classic",  "Classic"},
  {"flip",     "Flip Clock"},
  {"weather",  "Weather"},
  {"forecast", "Weather forecast"},
  {"ticker",   "Crypto ticker"},
  {"crypto3",  "Crypto all"},
  {"calendar", "Calendar"},
  {"photo",    "Photo"},
};
static const int THEME_N = sizeof(THEMES) / sizeof(THEMES[0]);
static const char THEME_RU[][24] PROGMEM __attribute__((aligned(4))) = {
  "Классика", "Flip-часы", "Погода", "Прогноз", "Крипто-тикер", "Все крипто", "Календарь", "Фото",
};
static const char THEME_ES[][16] PROGMEM __attribute__((aligned(4))) = {
  "Clásico", "Reloj Flip", "Clima", "Pronóstico", "Ticker cripto", "Todo cripto", "Calendario",
  "Foto",
};
static const char THEME_ZH[][16] PROGMEM __attribute__((aligned(4))) = {
  "经典", "翻页时钟", "天气", "预报", "加密行情", "全部加密", "日历", "照片",
};
static const char* themeName(int i){
  switch(langIdx()){ case 1: return THEMES[i].name; case 2: return THEME_ES[i];
                     case 3: return THEME_ZH[i]; default: return THEME_RU[i]; }
}
static const char RU_WDF[][24] PROGMEM __attribute__((aligned(4))) = {
  "Воскресенье", "Понедельник", "Вторник", "Среда", "Четверг", "Пятница", "Суббота",
};
static const char EN_WDF[][12] PROGMEM __attribute__((aligned(4))) = {
  "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday",
};
static const char ES_WDF[][12] PROGMEM __attribute__((aligned(4))) = {
  "Domingo", "Lunes", "Martes", "Miércoles", "Jueves", "Viernes", "Sábado",
};
static const char ZH_WDF[][8] PROGMEM __attribute__((aligned(4))) = {
  "周日", "周一", "周二", "周三", "周四", "周五", "周六",
};
static const char RUS_WDF[][8] PROGMEM __attribute__((aligned(4))) = {
  "Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб",
};   // short: forecast rows
static const char ENS_WDF[][4] PROGMEM __attribute__((aligned(4))) = {
  "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat",
};
static const char ESS_WDF[][8] PROGMEM __attribute__((aligned(4))) = {
  "dom", "lun", "mar", "mié", "jue", "vie", "sáb",
};
static const char ZHS_WDF[][4] PROGMEM __attribute__((aligned(4))) = {
  "日", "一", "二", "三", "四", "五", "六",
};
static const char RU_MON[][8] PROGMEM __attribute__((aligned(4))) = {
  "янв", "фев", "мар", "апр", "мая", "июн", "июл", "авг", "сен", "окт", "ноя", "дек",
};
static const char ENS_MON[][4] PROGMEM __attribute__((aligned(4))) = {
  "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec",
};
static const char ESS_MON[][4] PROGMEM __attribute__((aligned(4))) = {
  "ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sep", "oct", "nov", "dic",
};
static const char ZHS_MON[][8] PROGMEM __attribute__((aligned(4))) = {
  "1月", "2月", "3月", "4月", "5月", "6月", "7月", "8月", "9月", "10月", "11月", "12月",
};
static inline const char* dayLong(int i){
  switch(langIdx()){ case 1: return EN_WDF[i]; case 2: return ES_WDF[i];
                     case 3: return ZH_WDF[i]; default: return RU_WDF[i]; }
}
static inline const char* dayShort(int i){
  switch(langIdx()){ case 1: return ENS_WDF[i]; case 2: return ESS_WDF[i];
                     case 3: return ZHS_WDF[i]; default: return RUS_WDF[i]; }
}
static inline const char* monShort(int i){
  switch(langIdx()){ case 1: return ENS_MON[i]; case 2: return ESS_MON[i];
                     case 3: return ZHS_MON[i]; default: return RU_MON[i]; }
}
// "Today" / "Tomorrow" / short weekday for the forecast rows (n = 1..5)
static String fcDayName(int n, int wd){
  if(n == 1) return String(LG("Сегодня", "Today", "Hoy", "今天"));
  if(n == 2) return String(LG("Завтра", "Tomorrow", "Mañana", "明天"));
  return String(dayShort(wd));
}

static bool themeExists(const String& k){
  for(int i = 0; i < THEME_N; i++) if(k == THEMES[i].key) return true;
  return false;
}
static bool themeEnabled(const String& k){
  // no temporary ","+cfg.enabled+"," String: this runs from the 5s status poll
  // and every render, and the concat churned the heap for no reason
  int p = cfg.enabled.indexOf(k, 0);
  while(p >= 0){
    bool lok = (p == 0) || cfg.enabled[p - 1] == ',';
    bool rok = (p + (int)k.length() == (int)cfg.enabled.length()) ||
               cfg.enabled[p + k.length()] == ',';
    if(lok && rok) return true;
    p = cfg.enabled.indexOf(k, p + 1);
  }
  return false;
}
static int enabledCount(){
  int n = 0;
  for(int i = 0; i < THEME_N; i++) if(themeEnabled(THEMES[i].key)) n++;
  return n;
}
static void nextEnabledTheme(){
  int start = -1;
  for(int i = 0; i < THEME_N; i++) if(cfg.theme == THEMES[i].key){ start = i; break; }
  for(int s = 1; s <= THEME_N; s++){
    int i = (start + s + THEME_N) % THEME_N;
    if(themeEnabled(THEMES[i].key)){
      cfg.theme = THEMES[i].key;
      return;
    }
  }
}

// redraw mask: full clear only when the theme itself changes
#define RM_FULL  0x01
#define RM_HEAD  0x02
#define RM_PRICE 0x04
#define RM_CHART 0x08
#define RM_FOOT  0x10
#define RM_SEC   0x20   // seconds only (classic) - no full-region repaint
#define RM_ALL   0x3F
static uint8_t rm = RM_ALL;
static bool dirty = true;
static void mark(uint8_t m){ rm |= m; dirty = true; }

static const char* SYMS[][2] = {
  {"bitcoin","BTC"}, {"ethereum","ETH"}, {"solana","SOL"}, {"cardano","ADA"},
  {"dogecoin","DOGE"}, {"ripple","XRP"}, {"litecoin","LTC"}, {"binancecoin","BNB"},
  {"tron","TRX"}, {"avalanche-2","AVAX"}, {"polkadot","DOT"}, {"chainlink","LINK"}
};
static const int SYM_N = sizeof(SYMS)/sizeof(SYMS[0]);

static String labelOf(const String& id){
  for(int i=0;i<SYM_N;i++) if(id==SYMS[i][0]) return SYMS[i][1];
  for(uint16_t i = 0; i < COINS_LOCAL_N; i++){     // full catalog fallback
    CoinRef c;
    memcpy_P(&c, &COINS_LOCAL[i], sizeof(c));
    if(c.sym && id.equals(c.id)){ String s(c.sym); s.toUpperCase(); return s; }
  }
  return id;
}

// ---------------------------------------------------------------- market
#define MAXCD 48
#define MAXLN 120
static struct { float o,h,l,c; } cds[MAXCD];
static int cdN = 0;
static float lns[MAXLN];
static int lnN = 0;
static float mkPrice = 0, mkChg = 0;
static bool mkOk = false;
static uint32_t mkAt = 0;          // millis() of last good fetch
static uint32_t lastFetchTry = 0;
static uint32_t lastTlsTry = 0;      // last httpGetStart entry (breaker: fetchers alive?)
static uint32_t coolUntil = 0;        // backoff after HTTP 429 / OOM
static uint8_t  oomN = 0;             // consecutive BearSSL OOM failures
static uint32_t lastHttpMs = 0;       // duration of the last connect+GET (diagnostics)
static const char* lastPhase = "-";   // which fetch ran last, and how long it took
static uint32_t lastPhaseMs = 0;
static int  cdLen = 0;                // OHLC payload diagnostics
static bool cdChunk = false;
static String mkErr;
static bool  httpSkip = false; // last attempt was DEFERRED (heap gate / NTP wait),
                               // not a real failure: callers keep the old state
static String mkErrReal;       // last REAL market-path failure (price/candles/quotes).
                               // mkErr is shared with weather/geocode and must not
                               // paint the crypto line red for their errors

static uint32_t bootMs = 0;
static uint32_t themeSince = 0;

// persistent cache (LittleFS): the last good payload survives reboots, so the
// screen paints real data immediately and boot needs no CoinGecko round trip
static bool cacheLoadAll();
static void cacheSaveWx();
static void cacheSaveFc();
static void cacheSaveQ3();

// ---------------------------------------------------------------- helpers
static inline int yOf(float v, float lo, float hi, int y, int h){
  if(hi <= lo) return y + h/2;
  float t = (v - lo) / (hi - lo);
  if(t < 0) t = 0;
  if(t > 1) t = 1;
  return y + h - 1 - (int)(t * (h - 1));
}

static String trimZeros(const String& s){
  int dot = s.indexOf('.');
  if(dot < 0) return s;
  int last = s.length() - 1;
  while(last > dot + 2 && s[last] == '0') last--;   // keep 2 decimals minimum
  return s.substring(0, last + 1);
}

// decimals needed for ~4 significant digits on sub-unit prices - matches how
// CoinGecko itself renders (0.04875, NOT the raw API float 0.0487523 with
// 7 decimals - the panel used to print every digit the API returned).
// Old m+6 gave 7 decimals for a 0.048-coin; m+4 still keeps tiny prices
// readable (0.00000429 stays 0.00000429) without the absurd tails.
static int decFor(float p){
  int m = 0;
  float q = p;
  while(q < 0.1f && m < 8){ q *= 10.0f; m++; }
  int d = m + 4;                    // 4 significant digits (0.04875, not 0.0487523)
  return d > 9 ? 9 : d;
}

static String fmtPrice(float p){
  if(p >= 1000000) return String(p,0);
  if(p >= 1000)    return String(p,0);
  if(p >= 10)      return trimZeros(String(p,2));
  if(p >= 1)       return trimZeros(String(p,3));
  return trimZeros(String(p, decFor(p)));
}

// price for the big panel: most precise candidate that still fits, tried on
// font 7 first (the 7-seg panel keeps its 6-char rule) and then on the
// narrower fonts - a tiny price like 0.00000429 steps down a font instead of
// collapsing to "0.00".
static String priceBig(float p, uint8_t& fontOut){
  fontOut = 7;
  if(!(p > 0.0f)) return "--";
  // sub-unit prices show their FULL precision - the font steps down instead
  // of the decimals (0.05233 must stay 0.05233, not shrink to "0.0523").
  // The font is picked from the UNTRIMMED worst-case width: picking it from
  // the trimmed string made a trailing zero flip the length (0.0485 vs
  // 0.04856) and the font jumped 7<->6 every other tick on the same coin.
  if(p < 1.0f){
    String full = String(p, decFor(p));
    const uint8_t pref[3] = {7, 6, 4};
    fontOut = 4;
    for(int f = 0; f < 3; f++)
      if(tft.textWidth(full, pref[f]) <= 168){ fontOut = pref[f]; break; }
    return trimZeros(full);
  }
  // PANEL FONT RULE (experimentally tuned on the real device, widths are
  // exposed as pw4/pw6/pw7 in /api/status, panel = 168px):
  //   >=1 keeps the original rule - most precise decimal that fits font7.
  int maxD = 5, minD = 0;
  const uint8_t pref[3] = {7, 6, 4};
  for(int f = 0; f < 3; f++)
    for(int d = maxD; d >= minD; d--){
      String s = trimZeros(String(p, d));
      if(tft.textWidth(s, pref[f]) <= 168){ fontOut = pref[f]; return s; }
    }
  // nothing fit (13+ chars): full precision on the smallest font anyway
  fontOut = 4;
  return trimZeros(String(p, 2));
}

static String two(int v){ return v<10 ? String("0")+String(v) : String(v); }

// 12-hour clock helpers (web select: 24 | 12)
static bool is12(){ return cfg.tfmt == "12"; }
static int hour12(int h){ return h % 12 ? h % 12 : 12; }
static const char* ampm(int h){ return h < 12 ? "AM" : "PM"; }

static bool timeOk(struct tm& ti){
  time_t t = time(nullptr);
  localtime_r(&t, &ti);
  return ti.tm_year + 1900 >= 2020;
}

// ---------------------------------------------------------------- network
static void applyDns(){
  ip_addr_t a, b;
  IP_ADDR4(&a, 1, 1, 1, 1);
  IP_ADDR4(&b, 9, 9, 9, 9);
  dns_setserver(0, &a);
  dns_setserver(1, &b);
}

// provisioning portal DNS: answer ONLY the connectivity-probe domains every OS
// uses to detect a captive portal (Android generate_204, Apple captive.apple.com,
// Firefox detectportal, Windows NCSI, ...) with the AP address, and NXDOMAIN for
// everything else.  The naive wildcard ("*" -> AP ip) hijacked every background
// fetch/HSTS upgrade: the phone's captive view hopped to
// https://www.google.com/gen_204 and download managers saved html "files" from
// random CDNs before the user could pick a network.
static WiFiUDP dnsUdp;
static bool dnsPortalOn = false;

static bool dnsIsProbe(const String& h){
  static const char* suf[] = {
    "gstatic.com", "google.com", "g.cn",                 // Android / Chromium
    "apple.com",                                         // iOS / macOS
    "firefox.com",                                       // Firefox
    "msftconnecttest.com", "msftncsi.com",               // Windows NCSI
    "android.com", "ubuntu.com"                          // Android alt / Ubuntu
  };
  for(unsigned i = 0; i < sizeof(suf) / sizeof(suf[0]); i++){
    String s(suf[i]);
    if(h == s || h.endsWith("." + s)) return true;
  }
  return false;
}

static void portalStart(){
  if(dnsPortalOn || WiFi.getMode() == WIFI_STA) return;
  if(!dnsUdp.begin(53)){
    Serial.println("[wifi] captive DNS: udp bind failed");
    return;
  }
  dnsPortalOn = true;
  Serial.println("[wifi] captive DNS on (probe domains only)");
}
static void portalStop(){
  if(!dnsPortalOn) return;
  dnsUdp.stop();
  dnsPortalOn = false;
  Serial.println("[wifi] captive DNS off");
}
static void dnsPoll(){
  if(!dnsPortalOn) return;
  int n = dnsUdp.parsePacket();
  if(n < 12 || n > 512) return;
  uint8_t b[512];
  if(dnsUdp.read(b, n) != n) return;
  if(b[2] & 0x80) return;                               // not a query
  if(((b[4] << 8) | b[5]) != 1) return;                 // single question only
  String host;
  int i = 12;
  while(i < n){
    uint8_t l = b[i++];
    if(l == 0) break;
    if(l > 63 || i + l > n) return;
    if(host.length()) host += '.';
    for(uint8_t k = 0; k < l; k++){
      char c = (char)b[i + k];
      host += (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    i += l;
  }
  if(!host.length() || i + 4 > n) return;
  int qEnd = i + 4;                                     // end of the question
  uint16_t qType = ((uint16_t)b[i] << 8) | b[i + 1];
  bool hit = dnsIsProbe(host);
  bool answer = hit && (qType == 1 || qType == 255);    // A / ANY
  uint8_t out[560];
  memcpy(out, b, qEnd);                                 // header + question only
  out[2] = b[2] | 0xC4;                                 // QR | AA | RD echo
  out[3] = 0x80 | (hit ? 0 : 3);                        // RA, rcode NOERROR/NXDOMAIN
  out[6] = 0;  out[7] = answer ? 1 : 0;                 // ANCOUNT
  out[8] = out[9] = out[10] = out[11] = 0;              // NSCOUNT / ARCOUNT
  int olen = qEnd;
  if(answer){
    static const uint8_t rec[12] = { 0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01,
                                     0x00, 0x00, 0x00, 0x00, 0x00, 0x04 };
    memcpy(out + olen, rec, 12); olen += 12;
    IPAddress ap = WiFi.softAPIP();
    out[olen++] = ap[0]; out[olen++] = ap[1];
    out[olen++] = ap[2]; out[olen++] = ap[3];
  }
  dnsUdp.beginPacket(dnsUdp.remoteIP(), dnsUdp.remotePort());
  dnsUdp.write(out, olen);
  dnsUdp.endPacket();
}

// "/api/v3/coins/..." tail of a URL, for error messages
static String urlPathTail(const String& u){
  int h = u.indexOf("://");
  if(h >= 0){
    int p = u.indexOf('/', h + 3);
    if(p >= 0) return u.substring(p);
  }
  return u;
}

// The webserver's accept path (WiFiServer::_accept, lwip sys context) does a
// plain operator new for every incoming connection.  While a TLS session is
// open the engine holds the heap at ~2k, so a poller/browser SYN arriving in
// that window threw bad_alloc -> "Unhandled C++ exception: OOM" abort (seen
// as Software restarts at up=10-15s and mid-run).  Close the listener for the
// whole fetch window (httpGetStart -> caller done) and reopen after; the
// flag makes double close/open a no-op and keeps handleGeoReverse's
// in-handler fetch from reopening early.
static bool srvDown = false;
static bool srvSkip = false;               // in-handler fetch: closing would kill
                                           // the client we still owe a response
static void srvClose(){ if(!srvDown){ server.stop(); srvDown = true; } }
static void srvOpen(){ if(srvDown){ server.begin(); srvDown = false; } }

// Quiet window after a link-up: no TLS from the loop fetchers while the DNS
// stack, the heap and the captive/portal page flow settle.  A clean install
// used to fire candle+price+quotes ~2s after the STA came up: every fetch
// died as "dns timeout" or shredded the heap into minutes of "heap low".
static uint32_t fetchHoldUntil = 0;
static bool fetchHold(){
  // a phone on the setup page: no TLS while the browser needs the server
  if((WiFi.getMode() & WIFI_AP) && WiFi.softAPgetStationNum() > 0) return true;
  if(!fetchHoldUntil) return false;
  if(millis() >= fetchHoldUntil){ fetchHoldUntil = 0; return false; }
  return true;
}

// shared TLS GET setup: configures the client, issues the request and handles
// the 429 backoff.  Returns false with mkErr set; `code` carries the status.
static bool httpGetStart(BearSSL::WiFiClientSecure& cli, HTTPClient& http,
                         const String& url, int& code){
  uint32_t t0 = millis();
  httpSkip = false;
  lastTlsTry = t0;
  if(time(nullptr) < 1600000000UL){
    // NTP not synced yet (cold install): every handshake fails on the cert
    // validity window (observed fast "connection failed" loop for minutes
    // until the fail-wedge reboot).  Wait for the clock instead of burning
    // attempts - the SDK retries NTP on its own.
    static uint32_t nw = 0;
    if(millis() - nw > 30000UL){ nw = millis(); Serial.println("[http] skip: no NTP time"); }
    httpSkip = true;
    mkErr = "no time";
    coolUntil = millis() + 2000UL;
    if(!srvSkip) srvOpen();
    return false;
  }
  if(!srvSkip) srvClose();                 // no accepts while the engine eats the heap
  // pre-resolve DNS with a hard cap: the lookup inside WiFiClient::connect()
  // has its own (much longer) timeout - a dropped router reply blocked
  // loop() for ~19s (observed as heartbeat gaps; one crash landed inside
  // dns_gethostbyname).  The answer lands in the lwip cache, so the real
  // connect below resolves instantly; a miss backs off instead of freezing.
  {
    int hs = url.indexOf("://");
    String host = (hs >= 0) ? url.substring(hs + 3) : url;
    int sl = host.indexOf('/');
    if(sl >= 0) host = host.substring(0, sl);
    if(host.length()){
      IPAddress dip;
      uint32_t td = millis();
      if(WiFi.hostByName(host.c_str(), dip, 3000) <= 0){
        delay(500);                  // link just up / heap shifted: one retry
        if(WiFi.hostByName(host.c_str(), dip, 2500) <= 0){
          mkErr = "dns timeout";
        coolUntil = millis() + 5000UL;
        Serial.printf("[dns] %s FAIL %lums\n", host.c_str(), (unsigned long)(millis() - td));
        return false;
        }
      }
      if(millis() - td > 500)
        Serial.printf("[dns] %s %lums\n", host.c_str(), (unsigned long)(millis() - td));
    }
  }
  cli.setInsecure();
  // CoinGecko sits behind Cloudflare, OWM answers with a single LE cert - 3k RX
  // is plenty (MFLN) and keeps the handshake around ~15k.  Session resumption
  // (per host) makes every reconnect after the first one far cheaper - without
  // it each fetch re-parsed the whole cert chain.  The dead binance branch was
  // removed here: no code ever builds such a URL, but its Session object still
  // cost ~150B of DRAM the TLS stack needs.
  static BearSSL::Session sessCG, sessOWM;
  BearSSL::Session* sess = &sessOWM;
  if(url.indexOf("api.coingecko.com") >= 0) sess = &sessCG;
  cli.setSession(sess);
  // MFLN keeps records small; 2048 RX still holds the fragmented Cloudflare
  // cert but trims the handshake peak by ~1k.  That headroom is what keeps the
  // WiFi task from OOM-crashing in ieee80211_setup_ratetable when the heap is
  // at its floor during a fetch.
  cli.setBufferSizes(2048, 512);
  http.setTimeout(6000);                  // keep loop() bounded: ESP8266 soft WDT = 8s
                                           // (also covers DNS resolve + TCP connect)
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  // A handshake that dies with the engine allocated (OOM-class fail, free
  // < 4k / ssl -1000) drops mfb below the 12k fetch gate - observed 8664 -
  // and the gate then blocks every further attempt, so the boot ran stale
  // ~10 minutes until the breaker rebooted.  end() frees the whole engine;
  // a retry in place starts from the reshuffled heap and usually connects
  // (observed: fail 15063ms -> in-place retry 986ms).
  // Measured on device: the cli+DNS prelude costs ~5.6-7.4k from the biggest
  // block, so mid-run this sees ~9.1-9.5k free / 6.9-9.2k mfb; a GET succeeded
  // from free=10.4k/mfb=8.2k while engine+iobufs+TCP ate 7.7-9.8k out of the
  // biggest block (single-block eat observed: 8832 -> 1704 = 7128).  Below
  // ~7.4k starting mfb a BearSSL new fails mid-chain - with exceptions off
  // that is an abort-reboot loop (observed: post-engine remainder < 216B).
  // BUT the gate must not outrun the healthy baseline: a wedge state sat at
  // mfb 7664-7856 with free ~10.3k for minutes and 8100 skipped every attempt
  // until the breaker rebooted (the "mystery reboots").  7550 passes all
  // observed stuck values while keeping +206B over the abort line at the
  // floor (transient ~7.0k post-teardown states still skip, now retrying in
  // 2.5s instead of 6s so they catch the next mfb rebound).
  {
    uint32_t f0 = ESP.getFreeHeap(), m0 = ESP.getMaxFreeBlockSize();
    if(f0 < 9000UL || m0 < 7550UL){
      httpSkip = true;
      mkErr = "heap low";
      coolUntil = millis() + 2500UL;
      lastTlsTry = 0;                       // not a real attempt: let the breaker fire
      Serial.printf("[http] skip free=%u mfb=%u\n", (unsigned)f0,
                    (unsigned)m0);
      return false;
    }
    Serial.printf("[http] try free=%u mfb=%u\n", (unsigned)f0,
                  (unsigned)m0);
  }
  uint8_t attempt = 0;
  for(;;){
    if(!http.begin(cli, url)){ mkErr = "begin failed"; return false; }
    http.addHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
    http.addHeader("Accept", "application/json");
    http.addHeader("Accept-Encoding", "identity");
    code = http.GET();
    lastHttpMs = millis() - t0;
    ESP.wdtFeed();
    if(code >= 0){ oomN = 0; break; }      // the handshake fit: reset the OOM ladder
    char eb[64] = {0};
    int se = cli.getLastSSLError(eb, sizeof(eb));
    String full = HTTPClient::errorToString(code) + " (" + String(code) + ") ssl=" + String(se);
    if(eb[0]) full += " " + String(eb);
    full += " h=" + String(ESP.getFreeHeap()) + "/" + String(ESP.getMaxFreeBlockSize());
    Serial.printf("[http] connect fail -> %s\n", full.c_str());
    bool oomish = (se == -1000 || ESP.getFreeHeap() < 4000);
    http.end();
    // HTTPClient::end() skips _client->stop() when the socket never reached
    // connected() state, so the TLS engine would stay allocated (~14k) and
    // the retry started from 2k free -> BearSSL's new() threw "Unhandled C++
    // exception: OOM" and the device reboot-looped.  stop() -> _freeSSL()
    // releases the engine even for a half-open connection.
    cli.stop();
    if(oomish){
      uint32_t fA = ESP.getFreeHeap(), mA = ESP.getMaxFreeBlockSize();
      Serial.printf("[http] post-stop free=%u mfb=%u\n", (unsigned)fA, (unsigned)mA);
      // retry only when BOTH metrics say a fresh handshake fits: total free
      // alone let a retry start against a shredded heap (engine is one big
      // block -> new() -> OOM abort)
      if(++attempt < 3 && fA > 12000 && mA > 12000){
        Serial.printf("[http] retry %d\n", (int)attempt);
        delay(200);
        continue;
      }
      // heap race with the webserver/browser: short message + escalating
      // retry - 5s, 5s, then every 20s.  A flat 45s backoff would leave the
      // price blank far too long after picking a coin, while an endless 5s
      // loop would keep the error on the display as long as the browser holds
      // the heap (it clears on the first success).  The free-heap check also
      // catches TCP "connection failed" when lwip can't even allocate the
      // socket next to the TLS buffers - same root cause as the BearSSL OOM.
      oomN++;
      // se==0 + free<4000 is the observed OUTER network-wave shape (three
      // "connection failed" in a row while a PC-curl works) - the engine and
      // the prelude hold the heap down there, there is no allocator failure.
      // Saying "no memory" put a scary false error on the weather panel;
      // keep the oomish fast-retry ladder but tell the user the truth.
      mkErr = (se == -1000) ? "no memory (oom)" : "connection failed";
      coolUntil = millis() + (oomN >= 3 ? 60000UL : 2500UL);
      return false;
    }
    // non-heap connect fail: full text stays in the serial log, the panel
    // gets a localizable short message (long "code ssl h=.." never matched)
    mkErr = (code == HTTPC_ERROR_CONNECTION_FAILED) ? "connection failed" : full;
    coolUntil = millis() + (code == HTTPC_ERROR_CONNECTION_FAILED ? 15000UL : 45000UL);
    return false;
  }
  if(code == 429){
    http.end();
    mkErr = "rate limited (429)";
    coolUntil = millis() + 120000UL;  // CoinGecko free tier: back off 2 minutes
    Serial.println("[http] 429 -> backoff 120s");
    return false;
  }
  return true;
}

// HTTP/1.1 chunked framing decoded on the fly, so a reply can be parsed
// straight from the socket.  Measured after a handshake: the engine ate the
// big block and the free heap is a single ~300-550B island - the old buffered
// path's reserve ladder (1536+) never fit there, so every chunked reply
// (CoinGecko is always chunked) failed with "lowheap (oom)" and the price
// froze.  A chunk header is a few bytes and the parsers walk any Stream, so
// the body never needs a contiguous buffer at all.
class ChunkedStream : public Stream {
public:
  void attach(WiFiClient* c){ _c = c; _state = 0; _remain = 0; _done = (c == nullptr); }
  size_t count() const { return _bytes; }
  int available() override{
    if(_pbSet) return 1;
    if(!_c || _done) return 0;
    if(_remain > 0) return _c->available();
    return 1;                      // a size line / terminator may be pending
  }
  int read() override{
    if(_pbSet){ _pbSet = false; return _pb; }
    if(!_c || _done) return -1;
    for(;;){
      if(_remain > 0){
        int b = waitByte();
        if(b < 0) return -1;
        _bytes++; _remain--;
        if(_remain == 0) _state = 2;      // swallow the trailing CRLF next
        return b;
      }
      if(_state == 2){
        if(waitByte() != '\r' || waitByte() != '\n'){ _done = true; return -1; }
        _state = 0;
        continue;
      }
      // chunk-size line: hex digits [; extension] CRLF
      char line[24];
      bool nl = false;
      int n = 0;
      for(; n < (int)sizeof(line) - 1; n++){
        int b = waitByte();
        if(b < 0) break;
        line[n] = (char)b;
        if(b == '\n'){ nl = true; break; }
      }
      if(!nl){ _done = true; return -1; }
      line[n + 1] = 0;
      char* sc = strchr(line, ';');
      if(sc) *sc = 0;
      _remain = strtoul(line, nullptr, 16);
      if(_remain == 0){ _done = true; return -1; }   // terminating chunk
      _state = 1;
    }
  }
  int peek() override{
    if(_pbSet) return _pb;
    int b = read();
    if(b >= 0){ _pb = b; _pbSet = true; }
    return b;
  }
  void flush() override {}
  size_t write(uint8_t) override { return 0; }
  size_t write(const uint8_t*, size_t n) override { return n; }
  using Print::write;

private:
  // blocks out inter-packet gaps like Stream::timedRead does, but only when
  // the socket really has nothing (read() itself is non-blocking)
  int waitByte(){
    uint32_t start = millis();
    for(;;){
      int b = _c->read();
      if(b >= 0) return b;
      if(!_c->connected() && _c->available() == 0) return -1;
      if(millis() - start >= _timeout) return -1;
      delay(1);
    }
  }
  WiFiClient* _c       = nullptr;
  uint32_t    _remain  = 0;
  uint8_t     _state   = 0;         // 0 = size line, 1 = body, 2 = eat CRLF
  bool        _done    = true;
  bool        _pbSet   = false;
  int         _pb      = -1;
  size_t      _bytes   = 0;
};

static bool httpJsonOnce(const String& url, JsonDocument& doc, const char* filter = nullptr){
  uint32_t heapBefore = ESP.getFreeHeap();
  BearSSL::WiFiClientSecure cli;
  HTTPClient http;
  int code = 0;
  if(!httpGetStart(cli, http, url, code)) return false;
  if(code != 200){
    char eb[64] = {0};
    int se = cli.getLastSSLError(eb, sizeof(eb));
    mkErr = HTTPClient::errorToString(code);
    mkErr += " (" + String(code) + ") ssl=" + String(se);
    if(eb[0]) mkErr += " " + String(eb);
    http.end();
    return false;
  }
  Serial.printf("[http] %d heap %u->%u mfb=%u\n", code, heapBefore,
                ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());

  // Small bodies (OWM current weather ~2.5k, price, quotes, holidays): buffer
  // the payload FIRST and only then tear the TLS session down, so the document
  // is parsed against the full heap instead of beside the ~10.5k TLS footprint
  // (stream-parsing there failed with NoMemory on a fragmented heap).
  int clen = http.getSize();
  if(clen > 0 && clen <= 3072){
    StreamString body;
    if(!body.reserve((size_t)clen + 1)){
      Serial.printf("[json] body reserve fail mfb=%u -> stream\n",
                    (unsigned)ESP.getMaxFreeBlockSize());
    } else {
    int wr = http.writeToStream(&body);
    http.end();
    // http.end() can leave the TLS engine alive when disconnect() sees the
    // socket already closed; the engine then lives until this function
    // returns and the parse below runs beside it - measured: OWM wx died
    // with "buffered fail NoMemory heap=2736" (engine-held) while hb showed
    // 16.6k two seconds later (dtors).  Force the teardown so the document
    // is parsed against the full heap, as the comment above promises.
    cli.stop();
    ESP.wdtFeed();
    if(wr < 0){
      mkErr = HTTPClient::errorToString(wr) + " (" + String(wr) + ")";
      return false;
    }
    if(body.length() == 0){ mkErr = "empty body"; return false; }
    DeserializationError e;
    if(filter){
      JsonDocument fd;
      if(deserializeJson(fd, filter) == DeserializationError::Ok)
        e = deserializeJson(doc, body.c_str(), body.length(),
                            DeserializationOption::Filter(fd));
      else
        e = deserializeJson(doc, body.c_str(), body.length());
    } else {
      e = deserializeJson(doc, body.c_str(), body.length());
    }
    if(e){
      mkErr = String("json ") + e.c_str() + " @" + urlPathTail(url);
      Serial.printf("[json] buffered fail %s heap=%u\n", e.c_str(), ESP.getFreeHeap());
      return false;
    }
    mkErr = "";
    return true;
    }
  }

  // Large bodies (CoinGecko OHLC, OWM forecast): parse straight from the socket
  // so the payload never sits in RAM next to the parsed document - on an 8266
  // with ~18k free heap the copy path alone caused NoMemory / Stream write
  // errors.  Known content length (identity encoding); when the reserve above
  // did not fit, this is the fallback for small content-length replies too.
  if(http.getSize() > 0){
    JsonDocument fd;
    DeserializationError e;
    if(filter && deserializeJson(fd, filter) == DeserializationError::Ok)
      e = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(fd));
    else
      e = deserializeJson(doc, http.getStream());
    char eb[64] = {0};
    int se = cli.getLastSSLError(eb, sizeof(eb));
    http.end();
    ESP.wdtFeed();
    if(e){
      mkErr = String("json ") + e.c_str() + " @" + urlPathTail(url);
      if(eb[0]) mkErr += " ssl=" + String(se);
      Serial.printf("[json] stream fail %s heap=%u\n", e.c_str(), ESP.getFreeHeap());
      return false;
    }
    mkErr = "";
    return true;
  }

  // chunked (no content length): decode the framing on the fly and parse
  // straight from the socket.  Measured beside the open TLS session: the
  // engine holds the big block and the free heap is a single ~300-550B
  // island, so every body buffer - even 1536B - failed to reserve and each
  // CoinGecko reply (they are all chunked) died as "lowheap (oom)" while the
  // handshake itself was fine.  A Stream parse needs no body memory at all.
  {
    ChunkedStream cs;
    cs.attach(http.getStreamPtr());
    cs.setTimeout(2500);
    DeserializationError e;
    {
      JsonDocument fd;
      bool haveF = filter &&
                   (deserializeJson(fd, filter) == DeserializationError::Ok);
      if(haveF)
        e = deserializeJson(doc, cs, DeserializationOption::Filter(fd));
      else
        e = deserializeJson(doc, cs);
    }
    size_t got = cs.count();
    http.end();
    ESP.wdtFeed();
    if(e){
      mkErr = String("json ") + e.c_str() + " @" + urlPathTail(url);
      Serial.printf("[json] chunked %s after %u bytes heap=%u mfb=%u\n",
                    e.c_str(), (unsigned)got, (unsigned)ESP.getFreeHeap(),
                    (unsigned)ESP.getMaxFreeBlockSize());
      return false;
    }
    if(got == 0){ mkErr = "empty body"; return false; }
    mkErr = "";
    return true;
  }
}

static bool httpJson(const String& url, JsonDocument& doc, const char* filter = nullptr){
  // a single attempt: loop() must stay responsive (soft WDT = 8s), and a
  // failure is retried by the caller's normal interval anyway
  if(httpJsonOnce(url, doc, filter)) return true;
  // the mfb gate can trip on a sibling teardown dip (observed 7288 vs the
  // 7550 floor, rebounding to 7968 within ~200ms) - a whole chain
  // (weather -> geocode -> prefix) would fall through on such a miss, so
  // give the gate case one short retry before giving up
  if(mkErr == "heap low" || mkErr.startsWith("connection failed")){
    delay(300);                  // reshuffled heap: the retry usually connects
    if(httpJsonOnce(url, doc, filter)) return true;
  }
  if(!mkErr.startsWith("rate limited")) delay(200);
  return false;
}

static String cgBase(){ return String("https://api.coingecko.com/api/v3"); }
static int daysOf(){
  return 1;                       // range is hardcoded to 1d (selector removed)
}

static bool fetchPrice(){
  JsonDocument d;
  String url = cgBase() + "/simple/price?ids=" + cfg.sym +
               "&vs_currencies=usd&include_24hr_change=true";
  if(!httpJson(url, d)) return false;
  JsonObject o = d[cfg.sym].as<JsonObject>();
  if(o.isNull()) { mkErr = "no data"; return false; }
  mkPrice = o["usd"] | 0.0f;
  mkChg   = o["usd_24h_change"] | 0.0f;
  mkOk = true;
  mkAt = millis();
  return true;
}

// CoinGecko /ohlc answers with a flat [[ts,o,h,l,c], ...].  days=7 is ~336 rows
// -> a parsed document needs ~17k which simply does not exist next to the TLS
// buffer, so the stream is walked directly and only every stride-th row is kept
// (stride is derived from the content length).  Memory use stays O(MAXCD).
// read-only Stream over a RAM buffer, so the row parser can be reused for
// bodies that had to be buffered (chunked replies: HTTPClient decodes them
// only in getString()/writeToStream, not via getStream())
class BufStream : public Stream {
public:
  BufStream(const char* b, size_t n) : _b(b), _n(n), _i(0) {}
  int available() override { return (int)(_n - _i); }
  int read() override { return _i < _n ? (uint8_t)_b[_i++] : -1; }
  int peek() override { return _i < _n ? (uint8_t)_b[_i] : -1; }
  void flush() override {}
  size_t write(uint8_t) override { return 0; }
private:
  const char* _b;
  size_t _n, _i;
};

static bool fetchCandles(){
  int days = daysOf();
  // Binance klines dropped: its handshake (no MFLN, ~19k+) does not fit this
  // board next to the browser (ssl=6 "record too large" at h~1k).  CoinGecko
  // with MFLN needs ~14.7k and works; the body may still truncate on the slow
  // origin - the truncation reject + 45s backoff + cache failover handle it.
  String url = cgBase() + "/coins/" + cfg.sym + "/ohlc?vs_currency=usd&days=" + days;
  BearSSL::WiFiClientSecure cli;
  HTTPClient http;
  int code = 0;
  uint32_t tC = millis();
  if(!httpGetStart(cli, http, url, code)){
    Serial.printf("[candle] GET fail %lums %s\n", (unsigned long)(millis() - tC), mkErr.c_str());
    return false;
  }
  Serial.printf("[candle] GET %d in %lums (http %ums)\n", code,
                (unsigned long)(millis() - tC), lastHttpMs);
  if(code != 200){
    mkErr = HTTPClient::errorToString(code) + " (" + String(code) + ")";
    http.end();
    return false;
  }
  int len = http.getSize();
  cdChunk = (len <= 0);
  ChunkedStream cs;
  if(len <= 0){
    // chunked transfer: decode the framing on the fly and parse straight from
    // the socket - beside the open TLS session the heap is a single ~300-550B
    // island (the engine took the big block), so the old 6k/3k/1.5k reserve
    // ladder never fit and EVERY candle fetch aborted to cache as
    // "lowheap (oom)" while the handshake itself was fine.  No body buffer.
    // CoinGecko's origin streams in bursts with multi-second pauses; each
    // silent gap blocks loop() for up to this timeout (observed: two ~12s
    // gaps = a 21s freeze that looks like a hung device).  4.5s catches the
    // normal bursts; a slower origin then falls back to the cached chart
    // (truncation backoff rests 45s) instead of freezing the UI.
    http.getStreamPtr()->setTimeout(4500);
    cs.attach(http.getStreamPtr());
    cs.setTimeout(4500);
  }
  cdLen = len > 0 ? len : 0;
  // Candle granularity is FIXED per range on CoinGecko: days=1 -> 30min
  // (48 rows), days=7/14/30 -> 4h (42/84/180 rows).  Deriving the stride
  // from body length (old ~45B/row guess) broke for tiny prices - PEPE rows
  // run ~110B, the estimate said 3x more rows than exist and threw away
  // 2/3 of the candles (8 fat bars on the chart).  Days-based is exact.
  int stride = 1;
  if(days >= 14) stride = ((days * 6) + MAXCD - 1) / MAXCD;   // 4h candles
  if(stride < 1) stride = 1;

  Stream& s = cdChunk ? (Stream&)cs : http.getStream();
  // A 6s per-byte timeout froze the web UI for 7-13s whenever the body (or the
  // keep-alive tail) trickled in: every silent gap cost a full timeout.  2.5s
  // caps the stall; a genuinely dead stream then fails over to the cache.
  // Chunked keeps the writer's 4.5s: the body itself trickles through here.
  s.setTimeout(cdChunk ? 4500 : 2500);

  // skip whitespace up to the opening '[' of the root array
  bool started = false;
  char tmp;
  for(int i = 0; i < 8 && !started; i++){
    if(s.readBytes(&tmp, 1) != 1){ http.end(); mkErr = "ohlc eof"; return false; }
    if(tmp == '[') started = true;
  }

  cdN = 0;
  int idx = 0;
  char row[96];
  bool rootClosed = false;
  for(;;){
    // next row starts at '[' or the root array closes at ']'
    bool got = false;
    for(int i = 0; i < 80; i++){
      if(s.readBytes(&tmp, 1) != 1) break;
      if(tmp == '['){ got = true; break; }
      if(tmp == ']'){ rootClosed = true; break; }
    }
    if(rootClosed) break;
    if(!got) break;
    size_t n = s.readBytesUntil(']', row, sizeof(row) - 1);
    row[n] = 0;
    if(idx % stride == 0 && cdN < MAXCD){
      float v[5];
      char* p = row;
      bool ok = true;
      for(int k = 0; k < 5 && ok; k++){
        char* endp = nullptr;
        v[k] = strtof(p, &endp);
        if(endp == p){ ok = false; break; }
        if(k < 4){
          p = strchr(endp, ',');
          if(p) p++;
          else ok = false;
        }
      }
      if(ok) cds[cdN++] = {v[1], v[2], v[3], v[4]};
    }
    idx++;
    if((idx & 63) == 0) ESP.wdtFeed();
  }
  http.end();
  if(cdChunk){ len = (int)cs.count(); cdLen = len; }
  ESP.wdtFeed();
  if(!rootClosed && idx > 0){
    // the stream died mid-body: cdN holds a PARTIAL parse of a cut body -
    // drawing it showed a handful of oversized candles (the "8 fat bars").
    // Drop the partial data; the caller falls back to the cache.
    cdN = 0;
    mkErr = "ohlc truncated";
    return false;
  }
  if(cdN < 2){ mkErr = "empty ohlc"; return false; }
  Serial.printf("[ohlc] bytes=%d rows=%d stride=%d kept=%d heap=%u\n",
                len, idx, stride, cdN, ESP.getFreeHeap());
  return true;
}

static bool buildLineFromCandles(){
  lnN = 0;
  for(int i = 0; i < cdN && lnN < MAXLN; i++) lns[lnN++] = cds[i].c;
  return lnN >= 2;
}

// ---------------------------------------------------------------- multi quote
#define NQ 3
struct Quote { String id, sym, name; float price, chg; bool ok; };
static Quote q3[NQ];
static bool  q3Ok = false;
static uint32_t q3At = 0, q3Try = 0;

static void symsSplit(const String& csv, String* out, int& n){
  n = 0;
  String item = "";
  for(uint8_t c = 0; c <= csv.length(); c++){
    char ch = (c < csv.length()) ? csv[c] : ',';
    if(ch == ','){
      item.trim();
      if(item.length() && n < NQ) out[n++] = item;
      item = "";
    } else item += ch;
  }
}

// symbol + display name from the PROGMEM catalog; falls back to the id itself
static void localCoinInfo(const String& id, String& sym, String& name){
  for(uint16_t i = 0; i < COINS_LOCAL_N; i++){
    CoinRef c;
    memcpy_P(&c, &COINS_LOCAL[i], sizeof(c));
    if(id == c.id){
      sym = String(c.sym);
      name = String(c.name);
      sym.toUpperCase();
      return;
    }
  }
  sym = id; sym.toUpperCase();
  name = id;
}

// /simple/price answers with ~100 bytes per id; the old /coins/markets pulled
// every field CoinGecko has (~50k) which neither fits the heap nor the budget
static bool fetchQuotes(){
  if(!cfg.syms.length()) return false;
  JsonDocument d;
  String url = cgBase() + "/simple/price?ids=" + cfg.syms +
               "&vs_currencies=usd&include_24hr_change=true";
  if(!httpJson(url, d)) return false;

  String want[NQ];
  int nw = 0;
  symsSplit(cfg.syms, want, nw);
  for(int i = 0; i < NQ; i++){ q3[i] = Quote(); }

  int n = 0;
  for(int i = 0; i < nw; i++){
    JsonObject o = d[want[i]].as<JsonObject>();
    if(o.isNull()) continue;
    localCoinInfo(want[i], q3[i].sym, q3[i].name);
    q3[i].id = want[i];
    q3[i].price = o["usd"] | 0.0f;
    q3[i].chg = o["usd_24h_change"] | 0.0f;
    q3[i].ok = true;
    n++;
  }
  if(!n){ mkErr = "no quotes"; return false; }
  q3Ok = true; q3At = millis();
  cacheSaveQ3();
  Serial.printf("[q3] %d quotes for %s\n", n, cfg.syms.c_str());
  return true;
}

static void maybeFetchQuotes(){
  uint32_t t = millis();
  if(fetchHold()) return;         // link just up: DNS/heap settle window
  if(t < coolUntil) return;
  if(cfg.theme != "crypto3") return;
  if(q3At && t - q3At < (uint32_t)cfg.refresh * 1000UL) return;
  if(t - q3Try < 20000UL) return;
  if(ESP.getMaxFreeBlockSize() < 8300UL) return;   // no contiguous room for TLS
  q3Try = t;
  bool qok = fetchQuotes();
  if(qok){ mkErrReal = ""; mark(RM_PRICE); }
  else if(!httpSkip) mkErrReal = mkErr;
  coolUntil = millis() + 4000UL;   // space the next fetcher even on failure
  lastPhase = "q3"; lastPhaseMs = millis() - t;
}

// ---------------------------------------------------------------- weather
static String urlencode(const String& s){
  String o = "";
  o.reserve(s.length() + 8);
  for(size_t i = 0; i < s.length(); i++){
    char c = s[i];
    if((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
       (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~'){
      o += c;
    } else {
      char b[8];
      snprintf(b, sizeof(b), "%%%02X", (unsigned char)c);
      o += b;
    }
  }
  return o;
}

static const char* OWM_KEY = "";

// blocking read with timeout (Stream::available loop keeps the WDT fed via
// delay()); -1 on timeout/EOF
static int sr(Stream& s){
  uint32_t st = millis();
  while(!s.available()){
    if(millis() - st > 6000UL) return -1;
    delay(1);
  }
  return s.read();
}

// slide through the stream until `needle` is matched (7 bytes max)
static bool streamFind(Stream& s, const char* needle){
  int n = strlen(needle), pos = 0, c;
  while((c = sr(s)) >= 0){
    if(c == (uint8_t)needle[pos]){
      if(++pos == n) return true;
    } else {
      pos = (c == (uint8_t)needle[0]) ? 1 : 0;
    }
  }
  return false;
}

// system timezone offset in seconds, derived from the configured TZ - no NTP
// required (localtime vs gmtime breakdown of the same instant)
static int sysTzOffset(){
  time_t n = time(nullptr);
  struct tm a, b;
  gmtime_r(&n, &a);
  localtime_r(&n, &b);
  int d = (b.tm_hour - a.tm_hour) * 3600 + (b.tm_min - a.tm_min) * 60;
  d += (b.tm_yday - a.tm_yday) * 86400 + (b.tm_year - a.tm_year) * 31536000;
  return d;
}

static bool  wxOk = false;
static String wxErr;
static String wxCity;
static float wxTemp = 0, wxFeels = 0, wxHum = 0, wxWind = 0;
static float wxPress = 0, wxTmax = 0, wxTmin = 0;
static int   wxCloudPct = 0, wxVis = -1;
static int   wxCode = 0;
static String wxMain = "-", wxDesc = "-", wxCountry = "";
static uint32_t wxAt = 0, wxTry = 0;
// a failed refetch must not blank the screen when the previous data is still
// in RAM: wxOk=false means "last fetch failed", wxAt>0 means "we HAVE data".
// Panels show the stale values (+ age in the footer) instead of an error.
static bool wxShowOk(){ return wxOk || wxAt > 0; }

struct DayFc { String name; float hi, lo; int pop, code;
               int dd, mm, noon; String main, desc; };
static DayFc fcd[5];
static int   fcn = 0;
static uint32_t fcAt = 0, fcTry = 0;

static String owmBase(){ return String("https://api.openweathermap.org/data/2.5"); }

static const char* WX_CUR_FILTER =
  "{\"name\":true,\"sys\":{\"country\":true},"
  "\"main\":{\"temp\":true,\"feels_like\":true,\"humidity\":true,"
  "\"pressure\":true,\"temp_max\":true,\"temp_min\":true},"
  "\"weather\":[{\"id\":true,\"main\":true,\"description\":true}],"
  "\"wind\":{\"speed\":true},\"clouds\":{\"all\":true},\"visibility\":true}";

// Open-Meteo reports WMO codes; icons (drawWxIcon) and wxRuDesc speak
// OpenWeatherMap codes, so every code is mapped to its OWM twin here.
static int wmo2owm(int w, const char** main, const char** desc){
  struct M { int wmo, owm; const char* mn; const char* ds; };
  static const M T[] = {
    {0, 800, "Clear", "clear sky"},           {1, 801, "Clear", "few clouds"},
    {2, 802, "Clouds", "scattered clouds"},   {3, 804, "Clouds", "overcast clouds"},
    {45, 741, "Mist", "fog"},                 {48, 741, "Mist", "fog"},
    {51, 500, "Drizzle", "light drizzle"},    {53, 501, "Drizzle", "drizzle"},
    {55, 502, "Drizzle", "heavy intensity drizzle"},
    {56, 501, "Drizzle", "freezing drizzle"}, {57, 502, "Drizzle", "freezing drizzle"},
    {61, 500, "Rain", "light rain"},          {63, 501, "Rain", "moderate rain"},
    {65, 502, "Rain", "heavy rain"},          {66, 501, "Rain", "freezing rain"},
    {67, 502, "Rain", "freezing rain"},
    {71, 600, "Snow", "light snow"},          {73, 601, "Snow", "snow"},
    {75, 602, "Snow", "heavy snow"},          {77, 600, "Snow", "snow grains"},
    {80, 500, "Rain", "light rain"},          {81, 501, "Rain", "moderate rain"},
    {82, 503, "Rain", "violent rain"},        {85, 600, "Snow", "light shower snow"},
    {86, 602, "Snow", "heavy shower snow"},   {95, 200, "Thunderstorm", "thunderstorm"},
    {96, 202, "Thunderstorm", "thunderstorm with hail"},
    {99, 202, "Thunderstorm", "thunderstorm with hail"},
  };
  for(const M& m : T){
    if(m.wmo == w){ *main = m.mn; *desc = m.ds; return m.owm; }
  }
  *main = "Clouds"; *desc = "overcast clouds";
  return 804;
}

// ---- Open-Meteo geocoder state, shared: the OWM source below also needs the
// language-aware spelling of the city (OWM's /weather answers `name` localised
// for ru only - es/zh come back English, verified against the live API)
static String omGeoQ, omGeoName, omGeoCc;
static float omGeoLat = 0, omGeoLon = 0;
static bool omGeoOk = false;
static void omGeoInvalidate(){ omGeoOk = false; }
static bool omGeocode();

static bool fetchWeatherOwm(){
  {
    JsonDocument d;
    String url = owmBase() + "/weather?q=" + urlencode(cfg.city) +
                 "&units=metric&appid=" + OWM_KEY + langQ();
    if(!httpJson(url, d, WX_CUR_FILTER)){
      if(!httpSkip){ wxErr = mkErr; wxOk = false; }   // deferred: keep old state
      return false;
    }
    wxCity = String((const char*)(d["name"] | cfg.city.c_str()));
    JsonObject cur = d["main"].as<JsonObject>();
    wxTemp  = cur["temp"] | 0.0f;
    wxFeels = cur["feels_like"] | 0.0f;
    wxHum   = cur["humidity"] | 0.0f;
    wxPress = cur["pressure"] | 0.0f;
    wxTmax  = cur["temp_max"] | 0.0f;
    wxTmin  = cur["temp_min"] | 0.0f;
    wxWind  = (d["wind"]["speed"] | 0.0f) * 3.6f;      // m/s -> km/h
    wxCloudPct = d["clouds"]["all"] | 0;
    wxVis   = d["visibility"] | -1;
    wxCountry = String((const char*)(d["sys"]["country"] | ""));
    JsonArray wa = d["weather"].as<JsonArray>();
    if(wa.size()){
      wxCode = wa[0]["id"] | 0;
      wxMain = String((const char*)(wa[0]["main"] | "-"));
      wxDesc = String((const char*)(wa[0]["description"] | "-"));
    }
  } // response document released HERE: the geocoder's TLS below runs beside
    // only the BearSSL stack thunk (6200B), not +2-4k of parsed /weather -
    // measured: keeping it alive left mfb=7288 at the gate and the prefix
    // request (>=7550) got skipped
  // OWM localises `name` for ru itself (and en comes back English), so only
  // es/zh need the language-aware geocoder (cached per city+lang); if every
  // lookup fails keep the raw API name so the screen still shows something.
  if(langIdx() >= 2 && omGeocode()) wxCity = omGeoName;
  wxErr = ""; wxOk = true; wxAt = millis();
  cacheSaveWx();
  Serial.printf("[wx] %s %.1fC %s code=%d\n", wxCity.c_str(), wxTemp, wxDesc.c_str(), wxCode);
  return true;
}

// rough km distance - only for ranking nearby geocoder candidates
static float geoDistKm(float la1, float lo1, float la2, float lo2){
  float dx = (lo2 - lo1) * 111.0f * cosf(la1 * 0.01745329f);
  float dy = (la2 - la1) * 111.0f;
  return sqrtf(dx * dx + dy * dy);
}

// Exact lookups reject OWM's spelling ("Vitsebsk" is known to OWM only, the
// standard "Vitebsk" to Open-Meteo only); when OWM then carries no local name
// for the UI language (zh has none for it) retry with a 3-char prefix of the
// query and keep the hit that lies in the same country within 25 km of the
// OWM coordinates - that is where 维捷布斯克 comes from.
static bool omGeoPrefix(){
  String pref = cutB(cfg.city, 3);
  if(pref.length() < 3) return false;          // too short to narrow anything down
  JsonDocument d;
  String url = String("https://geocoding-api.open-meteo.com/v1/search?name=") +
               urlencode(pref) + "&count=10&language=" + geoLang() + "&format=json";
  static const char* F =
    "{\"results\":[{\"name\":true,\"latitude\":true,\"longitude\":true,\"country_code\":true}]}";
  if(!httpJson(url, d, F)) return false;
  JsonArray ra = d["results"].as<JsonArray>();
  float best = 25.0f;
  bool hit = false;
  for(size_t i = 0; i < ra.size(); i++){
    JsonObject r = ra[i].as<JsonObject>();
    const char* cc = r["country_code"] | (const char*)"";
    if(omGeoCc.length() && strcmp(cc, omGeoCc.c_str())) continue;
    float dist = geoDistKm(omGeoLat, omGeoLon,
                           r["latitude"] | 0.0f, r["longitude"] | 0.0f);
    if(dist < best){
      best = dist; hit = true;
      omGeoName = String((const char*)(r["name"] | cfg.city.c_str()));
      omGeoLat = r["latitude"] | 0.0f;
      omGeoLon = r["longitude"] | 0.0f;
    }
  }
  if(hit) Serial.printf("[omgeo] prefix '%s' -> %s (%.1fkm)\n",
                        pref.c_str(), omGeoName.c_str(), best);
  return hit;
}

static bool omGeocode(){
  if(omGeoOk && omGeoQ == cfg.city) return true;
  // 1) OWM geocoder first: it accepts Latin AND Cyrillic, resolves both of
  //    OWM's spellings ("Vitsebsk") and supplies coords/cc in one handshake.
  //    Open-Meteo exact ran first and answered HTTP 200 with an EMPTY
  //    result for "Vitsebsk" - that wasted handshake left mfb=7320 for the
  //    prefix retry below and the gate (>=7550) skipped it, so the English
  //    name got cached on zh.
  String u2 = String("https://api.openweathermap.org/geo/1.0/direct?q=") +
              urlencode(cfg.city) + "&limit=1&appid=" + OWM_KEY + langQ();
  String F2 = String("[{\"name\":true,\"local_names\":{\"") + owmLocKey() +
              "\":true},\"lat\":true,\"lon\":true,\"country\":true}]";
  bool haveOwm = false, haveRn = false;
  String owmName, rnS;
  {
    JsonDocument g;
    if(httpJson(u2, g, F2.c_str())){
      JsonObject o = g[0].as<JsonObject>();
      if(!o.isNull()){
        omGeoLat = o["lat"] | 0.0f;
        omGeoLon = o["lon"] | 0.0f;
        omGeoCc = String((const char*)(o["country"] | ""));
        const char* rn = o["local_names"][owmLocKey()] | (const char*)nullptr;
        if(rn){ rnS = String(rn); haveRn = true; }
        owmName = String((const char*)(o["name"] | cfg.city.c_str()));
        haveOwm = true;
      }
    }
  } // g released: the prefix/exact request below must not run beside it
  if(haveOwm){
    if(langIdx() == 1 || haveRn){
      omGeoName = (langIdx() != 1) ? rnS : owmName;
      omGeoQ = cfg.city; omGeoOk = true;
      return true;
    }
    // OWM knows the place but keeps no local name for the UI language ->
    // the prefix bridge above; otherwise fall back to OWM's own spelling.
    if(omGeoPrefix()){ omGeoQ = cfg.city; omGeoOk = true; return true; }
    if(mkErr.length()){          // gate/timeout, not "no match" - retry later
      Serial.printf("[omgeo] retry later: %s\n", mkErr.c_str());
      return false;              // left uncached: the next fetch runs it again
    }
    omGeoName = owmName;
    omGeoQ = cfg.city; omGeoOk = true;
    return true;
  }
  // 2) exact Open-Meteo lookup (the ometo source needs its coords; also the
  //    fallback when the OWM geocoder request itself failed).  Autocomplete
  //    stores "Name,CC" for disambiguation - Open-Meteo's name= wants the bare
  //    name, so a trailing ",XX" is dropped here.
  String q = cfg.city;
  int ci = q.lastIndexOf(',');
  if(ci > 0 && q.length() - ci == 3 && isAlpha(q[ci + 1]) && isAlpha(q[ci + 2]))
    q = q.substring(0, ci);
  JsonDocument d;
  String url = String("https://geocoding-api.open-meteo.com/v1/search?name=") +
               urlencode(q) + "&count=1&language=" + geoLang() + "&format=json";
  static const char* F =
    "{\"results\":[{\"name\":true,\"latitude\":true,\"longitude\":true,\"country_code\":true}]}";
  if(httpJson(url, d, F)){
    JsonObject r = d["results"][0].as<JsonObject>();
    if(!r.isNull()){
      omGeoLat = r["latitude"] | 0.0f;
      omGeoLon = r["longitude"] | 0.0f;
      omGeoName = String((const char*)(r["name"] | cfg.city.c_str()));
      omGeoCc = String((const char*)(r["country_code"] | ""));
      omGeoQ = cfg.city; omGeoOk = true;
      return true;
    }
  }
  Serial.printf("[omgeo] fail: %s\n", mkErr.length() ? mkErr.c_str() : "not found");
  return false;
}

// ---- Open-Meteo source (no API key; accepts the city as typed - Latin OR
// Cyrillic - and answers with a localisation, so the display gets "Витебск")
static bool fetchWeatherOmeto(){
  if(!omGeocode()){ if(!httpSkip){ wxErr = "geocode"; wxOk = false; } return false; }
  JsonDocument d;
  String url = String("https://api.open-meteo.com/v1/forecast?latitude=") +
               String(omGeoLat, 4) + "&longitude=" + String(omGeoLon, 4) +
               "&current=temperature_2m,apparent_temperature,relative_humidity_2m,"
               "weather_code,wind_speed_10m,cloud_cover,surface_pressure"
               "&daily=temperature_2m_max,temperature_2m_min&timezone=auto"
               "&forecast_days=1";
  static const char* F = "{\"current\":true,\"daily\":true}";
  if(!httpJson(url, d, F)){
    if(!httpSkip){ wxErr = mkErr; wxOk = false; }
    return false;
  }
  JsonObject cur = d["current"].as<JsonObject>();
  if(cur.isNull()){ wxErr = "empty"; wxOk = false; return false; }
  wxCity = omGeoName;
  wxCountry = omGeoCc;
  wxTemp  = cur["temperature_2m"] | 0.0f;
  wxFeels = cur["apparent_temperature"] | 0.0f;
  wxHum   = cur["relative_humidity_2m"] | 0.0f;
  wxPress = cur["surface_pressure"] | 0.0f;
  wxWind  = cur["wind_speed_10m"] | 0.0f;      // already km/h
  wxCloudPct = cur["cloud_cover"] | 0;
  wxVis   = -1;                                 // not exposed by this source
  const char* mn = "Clouds"; const char* ds = "overcast clouds";
  wxCode = wmo2owm(cur["weather_code"] | 3, &mn, &ds);
  wxMain = String(mn); wxDesc = String(ds);
  JsonArray hiA = d["daily"]["temperature_2m_max"].as<JsonArray>();
  JsonArray loA = d["daily"]["temperature_2m_min"].as<JsonArray>();
  wxTmax = hiA.size() ? (hiA[0] | 0.0f) : wxTemp;
  wxTmin = loA.size() ? (loA[0] | 0.0f) : wxTemp;
  wxErr = ""; wxOk = true; wxAt = millis();
  cacheSaveWx();
  Serial.printf("[wx:ometo] %s %.1fC %s code=%d\n", wxCity.c_str(), wxTemp, wxDesc.c_str(), wxCode);
  return true;
}

static bool fetchWeather(){
  return (cfg.wprov == "ometo") ? fetchWeatherOmeto() : fetchWeatherOwm();
}

// tiny JSON helpers over an already-extracted element buffer: they return
// defaults when the key is absent and never allocate
static float jsNum(const String& s, const char* key, float def){
  int p = s.indexOf(key);
  if(p < 0) return def;
  return (float)strtod(s.c_str() + p + (int)strlen(key), nullptr);
}
static bool jsStr(const String& s, const char* key, char* out, int n){
  out[0] = 0;
  int p = s.indexOf(key);
  if(p < 0) return false;
  const char* b = s.c_str() + p + strlen(key);
  int i = 0;
  while(*b && *b != '"' && i < n - 1) out[i++] = *b++;
  out[i] = 0;
  return i > 0;
}

// The OWM forecast body is ~16k of JSON whose filtered Document alone needs
// ~12k - that combination reboots the 8266.  Parse the stream by hand: find
// "list", pull each element into a bounded buffer and fold the rows into the
// five day buckets as we go, so peak RAM is the TLS buffers plus 700 bytes.
static bool fetchForecastOwm(){
  String url = owmBase() + "/forecast?q=" + urlencode(cfg.city) +
               "&units=metric&appid=" + OWM_KEY + langQ();
  BearSSL::WiFiClientSecure cli;
  HTTPClient http;
  int code = 0;
  if(!httpGetStart(cli, http, url, code)) return false;
  if(code != 200){
    mkErr = "fc http " + String(code);
    http.end();
    return false;
  }
  Stream& s = http.getStream();
  s.setTimeout(6000);
  if(!streamFind(s, "\"list\"")){
    http.end();
    mkErr = "fc no list";
    return false;
  }
  int tzoff = sysTzOffset();

  fcn = 0;
  long curKey = -999999999L;
  int noonDist = 99;
  static String el;
  if(el.reserve(700)){
    char win[6];      // sliding window to spot the "city" object = end of list
    int wlen = 0;
    int c;
    bool eof = false;
    while(!eof){
      // scan to the start of the next element, or bail at "city"
      bool elem = false;
      wlen = 0;
      while((c = sr(s)) >= 0){
        if(c == '{'){ elem = true; break; }
        if(wlen < 6) win[wlen++] = (char)c;
        else { memmove(win, win + 1, 5); win[5] = (char)c; }
        if(wlen == 6 && memcmp(win, "\"city\"", 6) == 0) break;
      }
      if(!elem){ eof = true; break; }

      // read one element, depth-tracked, into el (700 byte cap)
      el = "";
      int depth = 1;
      for(;;){
        c = sr(s);
        if(c < 0){ eof = true; break; }
        if(c == '{') depth++;
        else if(c == '}'){
          if(--depth == 0) break;
        }
        if(el.length() < 700) el += (char)c;
      }
      if(eof) break;

      long dt = (long)jsNum(el, "\"dt\":", 0);
      if(dt == 0) continue;               // not a list row (or truncated)
      float tp = jsNum(el, "\"temp\":", -999.0f);
      float pv = jsNum(el, "\"pop\":", 0.0f);
      int code0 = (int)jsNum(el, "\"id\":", 0);
      char wm[16] = {0}, wd[40] = {0};
      jsStr(el, "\"main\":\"", wm, sizeof(wm));
      jsStr(el, "\"description\":\"", wd, sizeof(wd));

      time_t t = (time_t)dt + tzoff;
      struct tm lt;
      gmtime_r(&t, &lt);
      long key = (long)lt.tm_year * 1000 + lt.tm_yday;
      if(key != curKey){
        if(fcn >= 5) break;               // five day buckets are full
        fcn++;
        curKey = key;
        noonDist = 99;
        DayFc& b = fcd[fcn - 1];
        b.name = fcDayName(fcn, lt.tm_wday);
        b.hi = -999.0f; b.lo = 999.0f; b.pop = 0; b.code = 0;
        b.dd = lt.tm_mday; b.mm = lt.tm_mon + 1; b.noon = 0;
        b.main = ""; b.desc = "";
      }
      if(tp <= -900.0f) continue;         // temp missing in this row
      DayFc& b = fcd[fcn - 1];
      if(tp > b.hi) b.hi = tp;
      if(tp < b.lo) b.lo = tp;
      int pop = (int)(pv * 100.0f + 0.5f);
      if(pop > b.pop) b.pop = pop;
      int dh = lt.tm_hour - 12; if(dh < 0) dh = -dh;
      if(dh < noonDist){
        noonDist = dh;
        b.code = code0;
        b.noon = (int)lroundf(tp);
        b.main = wm;
        b.desc = wd;
      }
    }
  } else {
    mkErr = "fc oom";
  }
  http.end();
  if(fcn > 0){
    fcAt = millis();
    cacheSaveFc();
    Serial.printf("[fc] %d days tz=%d\n", fcn, tzoff);
    return true;
  }
  if(mkErr == "") mkErr = "fc parse";
  return false;
}

// Sakamoto's weekday from a civil date, 0 = Sunday (no TZ math involved)
static int wdayYMD(int y, int m, int d){
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if(m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static bool fetchForecastOmeto(){
  if(!omGeocode()){ mkErr = "fc geocode"; return false; }
  JsonDocument d;
  String url = String("https://api.open-meteo.com/v1/forecast?latitude=") +
               String(omGeoLat, 4) + "&longitude=" + String(omGeoLon, 4) +
               "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
               "precipitation_probability_max&timezone=auto&forecast_days=6";
  static const char* F = "{\"daily\":true}";
  if(!httpJson(url, d, F)) return false;      // httpJson sets mkErr itself
  JsonArray days = d["daily"]["time"].as<JsonArray>();
  JsonArray hiA  = d["daily"]["temperature_2m_max"].as<JsonArray>();
  JsonArray loA  = d["daily"]["temperature_2m_min"].as<JsonArray>();
  JsonArray popA = d["daily"]["precipitation_probability_max"].as<JsonArray>();
  JsonArray codeA = d["daily"]["weather_code"].as<JsonArray>();
  fcn = 0;
  int n = days.size();
  if(n > 5) n = 5;
  for(int i = 0; i < n; i++){
    const char* date = days[i] | "";
    if(strlen(date) < 10) continue;
    int y = atoi(date), m = atoi(date + 5), dd = atoi(date + 8);
    if(m < 1 || m > 12 || dd < 1 || dd > 31) continue;
    DayFc& b = fcd[fcn];
    b.name = fcDayName(fcn + 1, wdayYMD(y, m, dd));
    b.hi = hiA[i] | 0.0f;
    b.lo = loA[i] | 0.0f;
    b.pop = (int)(popA[i] | 0.0f);
    const char* mn = "Clouds"; const char* ds = "overcast clouds";
    b.code = (int16_t)wmo2owm(codeA[i] | 3, &mn, &ds);
    b.main = mn; b.desc = ds;
    b.dd = dd; b.mm = m;
    b.noon = (int)lroundf((b.hi + b.lo) * 0.5f);
    fcn++;
  }
  if(fcn > 0){
    fcAt = millis();
    cacheSaveFc();
    Serial.printf("[fc:ometo] %d days\n", fcn);
    return true;
  }
  if(mkErr == "") mkErr = "fc parse";
  return false;
}

static bool fetchForecast(){
  return (cfg.wprov == "ometo") ? fetchForecastOmeto() : fetchForecastOwm();
}

// ---------------------------------------------------------------- disk cache
// Format: packed binary header + payload arrays.  A cache entry is only reused
// when the settings it was taken under are still in effect (sym/range, city,
// syms), otherwise it is dropped.
static const uint32_t C_MAGIC = 0x53444331UL;   // "SDC1"
static const uint16_t C_VER   = 2;

struct __attribute__((packed)) MkCache {
  uint32_t magic; uint16_t ver; uint16_t cdN; uint16_t lnN;
  uint32_t ts;                        // epoch seconds, 0 = time was unknown
  float price, chg;
  char sym[40], range[8];
};

struct __attribute__((packed)) WxC {
  uint32_t magic; uint16_t ver; uint16_t rsv; uint32_t ts;
  float temp, feels, hum, press, wind, tmax, tmin;
  int32_t vis, cloud, code;
  char city[48], country[4], main[16], desc[48];
  char qcity[48];                   // the query key (cfg.city); city[] may be a
                                    // localised display name (lang=ru "Витебск")
  char wprov[8];                    // which source produced this ("owm"/"ometo")
  char lang[4];                     // interface language at save time
};

struct __attribute__((packed)) FcC {
  uint32_t magic; uint16_t ver; uint16_t n; uint32_t ts;
  char city[48];
  char wprov[8];                    // source that produced the rows
  char lang[4];                     // day names are language specific
};

struct __attribute__((packed)) FcDay {
  float hi, lo;
  int16_t pop, code, dd, mm, noon;
  char name[24], main[16], desc[40];   // 24: Cyrillic costs 2 bytes/char ("Сегодня")
};

struct __attribute__((packed)) Q3C {
  uint32_t magic; uint16_t ver; uint16_t n; uint32_t ts;
  char syms[128];
};

struct __attribute__((packed)) Q3It {
  float price, chg;
  uint8_t ok; uint8_t pad[3];
  char id[40], sym[10], name[40];
};

// age in seconds derived from the stored epoch; 0 when time was unknown or the
// stamp is unusable (device clock not synced yet, future/ancient value)
static uint32_t cacheAgeSec(uint32_t ts){
  if(!ts) return 0;
  uint32_t nowE = (uint32_t)time(nullptr);
  if(!nowE || nowE < ts || nowE - ts > 604800UL) return 0;
  return nowE - ts;
}

// map an age onto the millis()-based stamp, oldest age = 0 (means "stale,
// refresh on the next normal tick") so uint32 wrap is harmless
static uint32_t stampFor(uint32_t ageSec){
  uint32_t ageMs = ageSec * 1000UL;
  return (millis() > ageMs) ? (millis() - ageMs) : 0;
}

// ---- per-coin price cache (/prices.bin) ---------------------------------
// One 36B record per coin id.  On a coin switch the last known price is
// shown instantly - with its real age in the footer - instead of "--" plus
// a "no memory (oom)" message while a fragmented heap keeps failing the
// TLS handshake.  The normal fetch still refreshes it every cfg.refresh.
// Small file, merge-rewritten on each successful price fetch.
struct PriceRec { char id[24]; float price; float chg; uint32_t ts; };  // 36B
#define PRICE_CACHE "/prices.bin"
#define PRICE_REC_MAX 300

static bool priceCacheLookup(const String& sym, float& price, float& chg, uint32_t& ts){
  File f = LittleFS.open(PRICE_CACHE, "r");
  if(!f) return false;
  bool hit = false;
  PriceRec r;
  while(f.read((uint8_t*)&r, sizeof(r)) == (int)sizeof(r)){
    r.id[sizeof(r.id) - 1] = 0;
    if(sym.equals(r.id) && r.price > 0){
      price = r.price; chg = r.chg; ts = r.ts;
      hit = true;
      break;
    }
  }
  f.close();
  return hit;
}

static void priceCacheStore(){
  if(!mkOk || mkPrice <= 0 || cfg.sym.length() >= 24) return;
  if(ESP.getMaxFreeBlockSize() < 9000UL) return;   // FS store on a torn heap
  LittleFS.remove(PRICE_CACHE ".new");            // orphan from a torn store
  File src = LittleFS.open(PRICE_CACHE, "r");
  File out = LittleFS.open(PRICE_CACHE ".new", "w");
  if(!out){ if(src) src.close(); return; }
  PriceRec cur, r;
  memset(&r, 0, sizeof(r));
  strlcpy(r.id, cfg.sym.c_str(), sizeof(r.id));
  r.price = mkPrice; r.chg = mkChg;
  r.ts = (uint32_t)time(nullptr);
  bool replaced = false;
  int n = 0;
  while(src && n < PRICE_REC_MAX && src.read((uint8_t*)&cur, sizeof(cur)) == (int)sizeof(cur)){
    cur.id[sizeof(cur.id) - 1] = 0;
    if(cfg.sym.equals(cur.id) || cur.price <= 0){   // replace ours / drop junk
      if(!replaced){ out.write((const uint8_t*)&r, sizeof(r)); replaced = true; }
    } else {
      out.write((const uint8_t*)&cur, sizeof(cur));
    }
    n++;
  }
  if(src) src.close();
  if(!replaced && n < PRICE_REC_MAX) out.write((const uint8_t*)&r, sizeof(r));
  out.close();
  LittleFS.remove(PRICE_CACHE);
  LittleFS.rename(PRICE_CACHE ".new", PRICE_CACHE);
}

// ---- multi-coin chart cache (/charts.bin) -------------------------------
// Up to 5 coin+range candle sets.  Switching between recently viewed coins
// paints the chart instantly from flash instead of staring at an empty panel
// while a fresh (and often truncated) CoinGecko ohlc body trickles in.
// Prices live in /prices.bin; this file only holds candles.
struct __attribute__((packed)) ChartRec {
  char id[24];
  char range[4];
  uint8_t cdN;                        // 0..MAXCD
  uint8_t pad[3];
  float cd[MAXCD * 4];                // {o,h,l,c} x MAXCD
};                                    // 809B
#define CHART_CACHE "/charts.bin"
#define CHART_REC_MAX 5

static bool chartCacheLookup(const String& sym, const String& range){
  File f = LittleFS.open(CHART_CACHE, "r");
  if(!f) return false;
  bool hit = false;
  ChartRec r;
  while(f.read((uint8_t*)&r, sizeof(r)) == (int)sizeof(r)){
    r.id[sizeof(r.id) - 1] = 0;
    r.range[sizeof(r.range) - 1] = 0;
    if(sym.equals(r.id) && range.equals(r.range) && r.cdN >= 2 && r.cdN <= MAXCD){
      memcpy(cds, r.cd, r.cdN * sizeof(cds[0]));
      cdN = r.cdN;
      buildLineFromCandles();
      hit = true;
      break;
    }
  }
  f.close();
  return hit;
}

static void chartCacheStore(){
  if(cdN < 2 || cfg.sym.length() >= 24) return;
  if(ESP.getMaxFreeBlockSize() < 9000UL) return;   // FS store on a torn heap
  LittleFS.remove(CHART_CACHE ".new");            // orphan from a torn store
  File src = LittleFS.open(CHART_CACHE, "r");
  File out = LittleFS.open(CHART_CACHE ".new", "w");
  if(!out){ if(src) src.close(); return; }
  ChartRec r, cur;
  memset(&r, 0, sizeof(r));
  strlcpy(r.id, cfg.sym.c_str(), sizeof(r.id));
  strlcpy(r.range, cfg.range.c_str(), sizeof(r.range));
  r.cdN = (uint8_t)cdN;
  memcpy(r.cd, cds, cdN * sizeof(cds[0]));
  bool replaced = false;
  int n = 0;
  while(src && n < CHART_REC_MAX && src.read((uint8_t*)&cur, sizeof(cur)) == (int)sizeof(cur)){
    cur.id[sizeof(cur.id) - 1] = 0;
    cur.range[sizeof(cur.range) - 1] = 0;
    if((cfg.sym.equals(cur.id) && cfg.range.equals(cur.range)) || cur.cdN < 2){
      if(!replaced){ out.write((const uint8_t*)&r, sizeof(r)); replaced = true; }
    } else {
      out.write((const uint8_t*)&cur, sizeof(cur));
    }
    n++;
  }
  if(src) src.close();
  if(!replaced && n < CHART_REC_MAX) out.write((const uint8_t*)&r, sizeof(r));
  out.close();
  LittleFS.remove(CHART_CACHE);
  bool rn = LittleFS.rename(CHART_CACHE ".new", CHART_CACHE);
  Serial.printf("[cache] chartStore sym=%s cd=%d replaced=%d n=%d rename=%d\n",
                cfg.sym.c_str(), cdN, (int)replaced, n, (int)rn);
}

static void cacheSaveWx(){
  if(!wxOk) return;
  if(ESP.getMaxFreeBlockSize() < 9000UL) return;   // FS store on a torn heap
  File f = LittleFS.open("/wxc.bin", "w");
  if(!f) return;
  WxC c;
  memset(&c, 0, sizeof(c));
  c.magic = C_MAGIC; c.ver = C_VER;
  c.ts = (uint32_t)time(nullptr);
  c.temp = wxTemp; c.feels = wxFeels; c.hum = wxHum; c.press = wxPress;
  c.wind = wxWind; c.tmax = wxTmax; c.tmin = wxTmin;
  c.vis = wxVis; c.cloud = wxCloudPct; c.code = wxCode;
  strlcpy(c.city, wxCity.c_str(), sizeof(c.city));
  strlcpy(c.qcity, cfg.city.c_str(), sizeof(c.qcity));
  strlcpy(c.wprov, cfg.wprov.c_str(), sizeof(c.wprov));
  strlcpy(c.lang, cfg.lang.c_str(), sizeof(c.lang));
  strlcpy(c.country, wxCountry.c_str(), sizeof(c.country));
  strlcpy(c.main, wxMain.c_str(), sizeof(c.main));
  strlcpy(c.desc, wxDesc.c_str(), sizeof(c.desc));
  f.write((const uint8_t*)&c, sizeof(c));
  f.close();
}

static bool cacheLoadWx(){
  if(!LittleFS.exists("/wxc.bin")) return false;
  File f = LittleFS.open("/wxc.bin", "r");
  if(!f) return false;
  WxC c;
  bool ok = (f.read((uint8_t*)&c, sizeof(c)) == (int)sizeof(c)) &&
            c.magic == C_MAGIC && c.ver == C_VER;
  if(ok && String(c.qcity) != cfg.city) ok = false;   // city changed since
  if(ok && String(c.wprov) != cfg.wprov) ok = false;  // source changed since
  if(ok && String(c.lang) != cfg.lang) ok = false;    // language changed since
  if(ok){
    wxTemp = c.temp; wxFeels = c.feels; wxHum = c.hum; wxPress = c.press;
    wxWind = c.wind; wxTmax = c.tmax; wxTmin = c.tmin;
    wxVis = c.vis; wxCloudPct = c.cloud; wxCode = c.code;
    wxCity = String(c.city); wxCountry = String(c.country);
    wxMain = String(c.main); wxDesc = String(c.desc);
    wxOk = true; wxErr = "";
    wxAt = stampFor(cacheAgeSec(c.ts));
    Serial.printf("[cache] wx %s %.1fC %s age=%lus\n",
                  c.city, wxTemp, c.desc, (unsigned long)cacheAgeSec(c.ts));
  }
  f.close();
  return ok;
}

static void cacheSaveFc(){
  if(fcn <= 0) return;
  File f = LittleFS.open("/fcc.bin", "w");
  if(!f) return;
  FcC h;
  memset(&h, 0, sizeof(h));
  h.magic = C_MAGIC; h.ver = C_VER; h.n = fcn;
  h.ts = (uint32_t)time(nullptr);
  strlcpy(h.city, cfg.city.c_str(), sizeof(h.city));
  strlcpy(h.wprov, cfg.wprov.c_str(), sizeof(h.wprov));
  strlcpy(h.lang, cfg.lang.c_str(), sizeof(h.lang));
  f.write((const uint8_t*)&h, sizeof(h));
  for(int i = 0; i < fcn; i++){
    FcDay d;
    memset(&d, 0, sizeof(d));
    d.hi = fcd[i].hi; d.lo = fcd[i].lo;
    d.pop = fcd[i].pop; d.code = fcd[i].code;
    d.dd = fcd[i].dd; d.mm = fcd[i].mm; d.noon = fcd[i].noon;
    strlcpy(d.name, fcd[i].name.c_str(), sizeof(d.name));
    strlcpy(d.main, fcd[i].main.c_str(), sizeof(d.main));
    strlcpy(d.desc, fcd[i].desc.c_str(), sizeof(d.desc));
    f.write((const uint8_t*)&d, sizeof(d));
  }
  f.close();
}

static bool cacheLoadFc(){
  if(!LittleFS.exists("/fcc.bin")) return false;
  File f = LittleFS.open("/fcc.bin", "r");
  if(!f) return false;
  FcC h;
  bool ok = (f.read((uint8_t*)&h, sizeof(h)) == (int)sizeof(h)) &&
            h.magic == C_MAGIC && h.ver == C_VER && h.n >= 1 && h.n <= 5;
  if(ok && String(h.city) != cfg.city) ok = false;
  if(ok && String(h.wprov) != cfg.wprov) ok = false;
  if(ok && String(h.lang) != cfg.lang) ok = false;
  if(ok){
    for(int i = 0; i < h.n; i++){
      FcDay d;
      if(f.read((uint8_t*)&d, sizeof(d)) != (int)sizeof(d)){ ok = false; break; }
      fcd[i].hi = d.hi; fcd[i].lo = d.lo;
      fcd[i].pop = d.pop; fcd[i].code = d.code;
      fcd[i].dd = d.dd; fcd[i].mm = d.mm; fcd[i].noon = d.noon;
      fcd[i].name = String(d.name);
      fcd[i].main = String(d.main);
      fcd[i].desc = String(d.desc);
    }
    if(ok){
      fcn = h.n;
      fcAt = stampFor(cacheAgeSec(h.ts));
      Serial.printf("[cache] fc %d days age=%lus\n", fcn, (unsigned long)cacheAgeSec(h.ts));
    }
  }
  f.close();
  return ok;
}

static void cacheSaveQ3(){
  if(!q3Ok) return;
  static uint32_t lastWrite = 0;
  uint32_t now = millis();
  if(lastWrite && now - lastWrite < 180000UL) return;
  lastWrite = now;
  File f = LittleFS.open("/q3c.bin", "w");
  if(!f) return;
  Q3C h;
  memset(&h, 0, sizeof(h));
  h.magic = C_MAGIC; h.ver = C_VER; h.n = NQ;
  h.ts = (uint32_t)time(nullptr);
  strlcpy(h.syms, cfg.syms.c_str(), sizeof(h.syms));
  f.write((const uint8_t*)&h, sizeof(h));
  for(int i = 0; i < NQ; i++){
    Q3It it;
    memset(&it, 0, sizeof(it));
    it.price = q3[i].price; it.chg = q3[i].chg;
    it.ok = q3[i].ok ? 1 : 0;
    strlcpy(it.id, q3[i].id.c_str(), sizeof(it.id));
    strlcpy(it.sym, q3[i].sym.c_str(), sizeof(it.sym));
    strlcpy(it.name, q3[i].name.c_str(), sizeof(it.name));
    f.write((const uint8_t*)&it, sizeof(it));
  }
  f.close();
}

static bool cacheLoadQ3(){
  if(!LittleFS.exists("/q3c.bin")) return false;
  File f = LittleFS.open("/q3c.bin", "r");
  if(!f) return false;
  Q3C h;
  bool ok = (f.read((uint8_t*)&h, sizeof(h)) == (int)sizeof(h)) &&
            h.magic == C_MAGIC && h.ver == C_VER && h.n == NQ;
  if(ok && String(h.syms) != cfg.syms) ok = false;
  if(ok){
    int got = 0;
    for(int i = 0; i < NQ; i++){
      Q3It it;
      if(f.read((uint8_t*)&it, sizeof(it)) != (int)sizeof(it)){ ok = false; break; }
      q3[i].id = String(it.id);
      q3[i].sym = String(it.sym);
      q3[i].name = String(it.name);
      q3[i].price = it.price;
      q3[i].chg = it.chg;
      q3[i].ok = it.ok != 0;
      if(q3[i].ok) got++;
    }
    if(ok && got){
      q3Ok = true;
      q3At = stampFor(cacheAgeSec(h.ts));
      Serial.printf("[cache] q3 %d quotes age=%lus\n", got, (unsigned long)cacheAgeSec(h.ts));
    }
  }
  f.close();
  return ok;
}

// restore every cache blob; entries tied to stale settings drop out on their own
// ---------------------------------------------------------------- holidays
// State holidays from date.nager.at (free, no key) compressed to one
// year-bitmask in /hol.bin; the country is auto-detected via ipwho.is when
// the config keeps an empty code.  All fetches are one-shot, rare and gated
// from loop()'s poll slots - never run from inside a web request handler.
#define HOL_CACHE "/hol.bin"
#define HOL_VER   3                 // own layout version (C_VER shift must not eat it)
struct HolCache { uint32_t magic; uint16_t ver; uint16_t year; char ctry[4]; uint8_t am; uint8_t bits[46]; };
static bool     holOk   = false;
static uint16_t holYear = 0;
static uint16_t holVer  = 0;      // bumped on every holiday-state change (redraw key)
static int      holN    = 0;      // distinct holiday dates inside holBits
static uint8_t  holBits[46];      // 366 bits, non-leap day-of-year bitmask
static String   hctryGeo;         // auto-detected country (runtime, not saved)
static uint32_t holTry  = 0;      // last attempt millis (backoff gate)
static uint32_t holDelayUntil = 0;// settle after /api/set (browser still busy)

static int holIdx(int m, int d){          // month 1..12 -> day-of-year index
  static const uint16_t cum[12] = {0,31,59,90,120,151,181,212,243,273,304,334};
  return cum[m - 1] + d - 1;
}
static bool holBit(int m, int d){
  int i = holIdx(m, d);
  return (holBits[i >> 3] >> (i & 7)) & 1;
}
static void holCacheSave(const String& cc){
  HolCache h;
  memset(&h, 0, sizeof(h));
  h.magic = C_MAGIC; h.ver = HOL_VER;
  h.year  = holYear;
  strlcpy(h.ctry, cc.c_str(), sizeof(h.ctry));
  h.am = cfg.hctry.length() ? 0 : 1;   // fetched via auto-geo or picked by hand
  memcpy(h.bits, holBits, sizeof(h.bits));
  File f = LittleFS.open(HOL_CACHE, "w");
  if(!f) return;
  f.write((const uint8_t*)&h, sizeof(h));
  f.close();
}
static bool holCacheLoad(){
  if(!LittleFS.exists(HOL_CACHE)) return false;
  File f = LittleFS.open(HOL_CACHE, "r");
  if(!f) return false;
  HolCache h;
  bool ok = (f.read((uint8_t*)&h, sizeof(h)) == (int)sizeof(h)) &&
            h.magic == C_MAGIC && h.ver == HOL_VER;
  f.close();
  if(!ok) return false;
  // reject a cache that belongs to another country or to the other selection mode
  if(cfg.hctry.length() && (h.am || cfg.hctry != String(h.ctry))) return false;
  if(!cfg.hctry.length() && !h.am) return false;
  if(!cfg.hctry.length() && String(h.ctry).length() == 2) hctryGeo = String(h.ctry);  // show it after reboot
  holYear = h.year;
  memcpy(holBits, h.bits, sizeof(holBits));
  holN = 0;
  for(unsigned i = 0; i < sizeof(holBits); i++){
    uint8_t b = holBits[i];
    while(b){ holN += b & 1; b >>= 1; }
  }
  holOk = true;
  holVer++;
  Serial.printf("[hol] cache %s %u: %d days\n", h.ctry, holYear, holN);
  return true;
}
static bool fetchGeoCountry(){
  JsonDocument d;
  if(!httpJsonOnce("https://ipwho.is/", d)) return false;
  String cc = d["country_code"].as<String>();
  cc.trim(); cc.toUpperCase();
  if(cc.length() != 2) return false;
  hctryGeo = cc;
  Serial.printf("[hol] geo country=%s\n", cc.c_str());
  return true;
}
static bool fetchHolidays(const String& cc, int yr){
  String url = "https://date.nager.at/api/v3/PublicHolidays/" + String(yr) + "/" + cc;
  JsonDocument d;
  if(!httpJsonOnce(url, d, "[{\"date\":true}]")) return false;
  if(!d.is<JsonArray>() || d.size() == 0){ mkErr = "holidays empty"; return false; }
  memset(holBits, 0, sizeof(holBits));
  int n = 0;
  for(JsonVariant v : d.as<JsonArray>()){
    const char* s = v["date"].as<const char*>();
    if(!s || strlen(s) < 10) continue;
    int m  = (s[5] - '0') * 10 + (s[6] - '0');
    int dd = (s[8] - '0') * 10 + (s[9] - '0');
    if(m < 1 || m > 12 || dd < 1 || dd > 31) continue;
    int i = holIdx(m, dd);
    uint8_t mask = 1 << (i & 7);
    if(!(holBits[i >> 3] & mask)){ holBits[i >> 3] |= mask; n++; }
  }
  if(!n){ mkErr = "holidays parse"; return false; }
  holYear = yr; holN = n; holOk = true;
  holVer++;
  holCacheSave(cc);
  Serial.printf("[hol] %s %d: %d days\n", cc.c_str(), yr, n);
  return true;
}
// next holiday on/after (mon,mday) in the cached year - "MM.DD" or ""
static String holNextStr(int mon, int mday){
  if(!holOk) return "";
  static const uint8_t dim[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  for(int m = mon; m <= 12; m++){
    int maxd = dim[m - 1];
    if(m == 2 && ((holYear % 4 == 0 && holYear % 100 != 0) || holYear % 400 == 0)) maxd = 29;
    int start = (m == mon) ? mday : 1;
    for(int d = start; d <= maxd; d++){
      if(holBit(m, d)){
        char b[24];                       // GCC worst-case int sizing for %02d
        snprintf(b, sizeof(b), "%02d.%02d", m, d);
        return String(b);
      }
    }
  }
  return "";
}
static void maybeFetchHolidays(){
  if(fetchHold()) return;         // link just up: quiet window
  if(!cfg.hol) return;
  uint32_t t = millis();
  if(t < holDelayUntil) return;                // just-settled /api/set, browser busy
  time_t now = time(nullptr);
  struct tm ti;
  localtime_r(&now, &ti);
  int yr = ti.tm_year + 1900;
  if(yr < 2020) return;                         // NTP not synced yet
  if(holOk && holYear == (uint16_t)yr) return;  // fresh for this year
  if(holTry && t - holTry < 30000UL) return;    // one retry every 30 seconds
  if(ESP.getMaxFreeBlockSize() < 8300UL) return;
  holTry = t;
  String cc = cfg.hctry.length() == 2 ? cfg.hctry : hctryGeo;
  if(!cc.length()){
    if(!fetchGeoCountry()) { coolUntil = millis() + 4000UL; return; }
    cc = hctryGeo;
    coolUntil = millis() + 3000UL;   // let the heap rebound before the nager TLS
  }
  if(fetchHolidays(cc, yr)) mark(RM_PRICE | RM_CHART);   // repaint the calendar
  coolUntil = millis() + 4000UL;      // space the next fetcher
}

static bool cacheLoadAll(){
  bool any = false;
  any |= chartCacheLookup(cfg.sym, cfg.range);
  float p, ch; uint32_t ts;
  if(priceCacheLookup(cfg.sym, p, ch, ts)){
    mkPrice = p; mkChg = ch; mkOk = true;
    mkAt = stampFor(cacheAgeSec(ts));
    any = true;
  }
  cacheLoadWx();
  cacheLoadFc();
  cacheLoadQ3();
  holCacheLoad();
  if(any) mkErr = "";
  return any;
}

// repeated OOM failures cannot fix themselves on a fragmented heap - the
// breaker in loop() restarts the device once the situation lasts too long
static uint8_t oomFailN = 0;
static void oomFailCheck(const char* what){
  if(mkErr.indexOf("NoMemory") >= 0 || mkErr.indexOf("no memory") >= 0){
    if(++oomFailN >= 4 && (millis() - bootMs) > 900000UL){
      Serial.printf("[heap] repeated OOM in %s -> restart\n", what);
      delay(100); Serial.flush(); ESP.restart();
    }
  } else oomFailN = 0;
}

static void maybeAutoGeo();
static void saveCfg();

static void maybeFetchWx(){
  uint32_t t = millis();
  if(fetchHold()) return;            // link just up: quiet window
  if(t < coolUntil) return;          // 429/OOM/settle window: no TLS while sockets are busy
  if(!cfg.city.length()){ maybeAutoGeo(); return; }   // clean install: resolve by IP first
  // OWM is a separate provider: CoinGecko's 429 cooldown must not hold it back
  // weather and forecast are each throttled by their own cache age AND a shared
  // 15s probe gate, and only ONE request runs per call (forecast also needs an
  // established current-weather to seed its baseline).  This keeps OWM usage low.
  bool haveCity = cfg.city.length() > 0;   // no city picked: nothing to fetch
  if(!haveCity && wxErr != "no city") wxErr = "no city";
  // wxTry/fcTry==0 means NEVER tried: fetch now (the 12s hold already
  // spaces it from boot) - the old `t - 0 >= 60000` delayed the first
  // weather pull to uptime 60s on any cache-less boot
  bool wantW = haveCity && (wxOk ? (t - wxAt >= 600000UL) : true) &&
              (!wxTry || t - wxTry >= 60000UL);
  bool wantF = haveCity && (fcn > 0 ? (t - fcAt >= 1800000UL) : true) &&
              (!fcTry || t - fcTry >= 60000UL);
  if(wantW){
    if(ESP.getMaxFreeBlockSize() < 8300UL) return;   // TLS below ~8.3k dies hard
    wxTry = t;
    if(fetchWeather()) mark(RM_HEAD | RM_PRICE | RM_CHART | RM_FOOT);
    else { mark(RM_PRICE | RM_CHART | RM_FOOT); oomFailCheck("wx");
           wxTry = millis() - (httpSkip ? 55000UL : 45000UL); }  // 5s if deferred, else 15s
    lastPhase = "wx"; lastPhaseMs = millis() - t;
  } else if(wantF && wxOk){   // forecast is a large payload: only when needed
    if(ESP.getMaxFreeBlockSize() < 8300UL) return;
    fcTry = t;
    if(fetchForecast()) mark(RM_HEAD | RM_PRICE | RM_CHART);   // HEAD: night/day temps
    else oomFailCheck("fc");
    lastPhase = "fc"; lastPhaseMs = millis() - t;
  }
}

// force an immediate refresh because the underlying data changed (city/symbols).
// full reset so the stale cache is thrown away.
static void pokeWx(){
  // backdate instead of zeroing: `t - wxAt >= 600000` with wxAt=0 only opens
  // after 10 min of uptime (uint wrap gives exactly 600001 for any t) - the
  // refresh button was a no-op on any younger boot.  fc same at 30 min.
  uint32_t t = millis();
  wxAt = t - 600001UL;
  fcAt = t - 1800001UL;
  wxTry = fcTry = t - 61000UL;   // 61s: maybeFetchWx gates at 60s -> next tick fetches
  q3At = t - (uint32_t)cfg.refresh * 1000UL - 1000UL;
  q3Try = t - 21000UL;   // 21s: maybeFetchQuotes gates at 20s
}

// soft nudge: refresh only the pieces that have no data yet.
// switching themes must never discard a valid cache (avoids a burst of HTTP + 429).
static void bumpWx(){
  if(!wxOk) { wxAt = 0; wxTry = millis() - 61000UL; }
  if(fcn == 0) { fcAt = 0; fcTry = millis() - 61000UL; }
  if(!q3Ok) { q3At = 0; q3Try = millis() - 21000UL; }
}

// A clean install ships an empty city: ask the device's outward IP for the
// city name (keyless ip-api over plain http - no TLS buffer needed) and store
// it as "Name,CC".  An address without a city name falls back to the keyless
// reverse geocoder (same call /api/geocode makes).  Retries at most every 20
// minutes until it succeeds once; manual entry always wins afterwards.
static void maybeAutoGeo(){
  static uint32_t tryAt = 0;
  uint32_t t = millis();
  if(tryAt && t - tryAt < 1200000UL) return;      // 20 min backoff between tries
  if(WiFi.status() != WL_CONNECTED) return;       // nothing to ask yet
  if(ESP.getMaxFreeBlockSize() < 8300UL) return; // same TLS floor as the fetchers
  tryAt = t;
  WiFiClient c;
  c.setTimeout(6000);
  if(!c.connect("ip-api.com", 80)) return;
  c.print("GET /json/?fields=status,lat,lon,city,countryCode,timezone,offset HTTP/1.1\r\n"
          "Host: ip-api.com\r\nConnection: close\r\n\r\n");
  String raw;
  uint32_t t0 = millis();
  while(millis() - t0 < 6000){
    while(c.available()) raw += (char)c.read();
    if(raw.length() > 2048) break;
    if(!c.connected() && !c.available()) break;
    delay(1);
  }
  c.stop();
  int h = raw.indexOf("\r\n\r\n");
  if(h < 0) return;
  JsonDocument d;
  if(deserializeJson(d, raw.substring(h + 4))) return;
  if(strcmp(d["status"] | "", "success") != 0) return;
  String nm = String((const char*)(d["city"] | ""));
  String cc = String((const char*)(d["countryCode"] | ""));
  if(!nm.length()){                       // no city name: reverse the coords
    float la = d["lat"] | 0.0f, lo = d["lon"] | 0.0f;
    if(la > -90.0f && la < 90.0f && lo > -180.0f && lo < 180.0f){
      String url = String("https://api.bigdatacloud.net/data/reverse-geocode-client?latitude=") +
                   String(la, 4) + "&longitude=" + String(lo, 4) +
                   "&localityLanguage=" + geoLocalityLang();
      static const char* F =
        "{\"city\":true,\"locality\":true,\"principalSubdivision\":true,\"countryCode\":true}";
      JsonDocument r;
      srvSkip = true;
      bool gok = httpJson(url, r, F);
      srvSkip = false;
      if(!gok) return;
      const char* cn = r["city"] | "";
      if(!cn || !*cn) cn = r["locality"] | "";
      if(!cn || !*cn) cn = r["principalSubdivision"] | "";
      if(cn && *cn) nm = String(cn);
      cc = String((const char*)(r["countryCode"] | ""));
    }
  }
  nm.trim();
  if(!nm.length()) return;
  String v = nm + (cc.length() ? ("," + cc) : "");
  if(v.length() > 40) v = v.substring(0, 40);
  cfg.city = v;                // same tail as handleSet city
  wxOk = false; fcn = 0;
  wxErr = "";
  omGeoInvalidate();
  pokeWx();
  if(cfg.tz == "MSK-3"){          // untouched factory default: adopt the IP zone
    const char* pz = posixFromIana(String((const char*)(d["timezone"] | "")),
                                    d["offset"] | 0);
    if(pz && *pz && strcmp(pz, cfg.tz.c_str()) != 0){
      cfg.tz = pz;
      configTime(cfg.tz.c_str(), "pool.ntp.org", "time.google.com", "time.cloudflare.com");
      Serial.printf("[geo] auto tz: %s\n", pz);
    }
  }
  saveCfg();
  Serial.printf("[geo] auto city: %s\n", cfg.city.c_str());
}

// price only - a single lightweight HTTP round trip so loop() stays under the
// 8s soft WDT even on a slow link.
static void fetchData(bool force){
  uint32_t now = millis();
  static uint32_t dSkip = 0;     // continuous "fragmented heap" window
  static uint32_t failSince = 0; // continuous-failure window (dns/connection cycles)
  if(fetchHold()) return;         // link just up: DNS/heap settle window
  if(now < coolUntil) return;
  uint32_t every = (uint32_t)cfg.refresh * 1000UL;
  if(!force && mkOk && now - mkAt < every) return;
  if(!force && !mkOk && now - lastFetchTry < 20000UL) return;
  // OOM guard BEFORE consuming any backoff state: the CoinGecko TLS stack
  // fits in ~12k contiguous (proven live), below that BearSSL sometimes dies
  // with a hard Exception(29) instead of a clean ssl=-1000 - that path retries
  // in 5s with a short message (httpGetStart).
  if(ESP.getMaxFreeBlockSize() < 8300UL){
    if(!dSkip) dSkip = now;
    else if(now - dSkip > 150000UL){
      Serial.println("[data] heap wedge 2.5min -> restart");
      delay(50); Serial.flush(); ESP.restart();
    }
    Serial.println("[data] skip: fragmented heap"); return;
  }
  dSkip = 0;
  lastFetchTry = now;
  Serial.printf("[data] price %s\n", cfg.sym.c_str());
  uint32_t t0 = millis();
  bool ok = fetchPrice();
  lastPhase = "price"; lastPhaseMs = millis() - t0;
  if(ok){
    failSince = 0;
    mkErrReal = "";
    priceCacheStore();
    coolUntil = millis() + 4000UL;   // space the next TLS: back-to-back fetches shred the heap
    mark(RM_HEAD | RM_PRICE | RM_FOOT);
    Serial.printf("[data] price=%.4f chg=%.2f\n", mkPrice, mkChg);
  } else {
    if(!failSince) failSince = now;
    else if(now - failSince > 180000UL && (now - bootMs) > 240000UL){
      // a stuck dns/connection cycle keeps every fetch (and the wedge
      // detectors below) from ever succeeding - reboot once data is 3min stale
      Serial.println("[data] failing 3min -> restart");
      delay(50); Serial.flush(); ESP.restart();
    }
    if(!httpSkip) mkErrReal = mkErr;
    mark(RM_PRICE | RM_FOOT);
    if(mkErr.startsWith("no memory") ||
       mkErr.startsWith("connection failed") ||
       mkErr == "heap low") lastFetchTry = millis() - 15000UL;  // retry in 5s
    Serial.printf("[data] price FAIL %s\n", mkErr.c_str());
  }
}

// candles are a heavier payload - polled separately so they never stack on top
// of the price request (keeps each loop pass to a single HTTP call).
static bool candlesForce = false;
static uint32_t truncUntil = 0;             // long backoff after a truncated body
static bool candlesBootTried = false;       // one attempt on pristine boot heap

static void fetchCandlesData(){
  uint32_t now = millis();
  static uint32_t cSkip = 0;
  if(fetchHold()) return;         // link just up: DNS/heap settle window
  if(now < coolUntil) return;
  if(now < truncUntil) return;
  // First attempt of the boot runs at up>=8s on PRISTINE heap: by the time
  // the mkAt+60s cadence opens (up~62s) the carousel/webserver have shredded
  // the largest block to mfb~400 and the full-handshake session cannot hold
  // the 2-4k body (observed: "[candle] reserve fail mfb=408" forever).  At
  // up=8s the free heap is one big block - engine floors at 3.5k+ and the
  // same fetch fits (observed: GET 200 in 182ms).
  bool early = !candlesBootTried && now >= 8000UL;
  if(!early && !candlesForce && mkOk && cdN > 0 && now - mkAt < (uint32_t)cfg.refresh * 1000UL) return;
  static uint32_t lastTry = 0;
  if(!early && !candlesForce && lastTry && now - lastTry < 20000UL) return;   // failed fetch backoff
  // same guard as fetchData, placed BEFORE the flags are consumed so a
  // skipped pass keeps candlesForce and retries in the next loop iteration
  if(ESP.getMaxFreeBlockSize() < 8300UL){
    if(!cSkip) cSkip = now;
    else if(now - cSkip > 150000UL){
      Serial.println("[candle] heap wedge 2.5min -> restart");
      delay(50); Serial.flush(); ESP.restart();
    }
    Serial.println("[candle] skip: fragmented heap"); return;
  }
  cSkip = 0;
  candlesBootTried = true;
  candlesForce = false;
  lastTry = now;
  uint32_t t0 = millis();
  bool ok = fetchCandles();
  lastPhase = "candles"; lastPhaseMs = millis() - t0;
  if(ok) buildLineFromCandles();
  if(ok){
    mkErrReal = "";
    chartCacheStore();
    coolUntil = millis() + 4000UL;   // space the next TLS (same as fetchData)
    mark(RM_CHART);
    Serial.printf("[candle] cd=%d ln=%d\n", cdN, lnN);
  } else {
    if(mkErr.startsWith("ohlc truncated")){
      truncUntil = now + 45000UL;      // stop hammering a stalling stream
      lastTry = now;                   // truncUntil gates the next attempt
    } else if(mkErr.startsWith("no memory") || mkErr.startsWith("connection failed")){
      lastTry = millis() - 15000UL;    // retry in 5s
    }
    if(!httpSkip) mkErrReal = mkErr;
    if(cdN == 0 && chartCacheLookup(cfg.sym, cfg.range)) mark(RM_CHART);   // last good chart back
    Serial.printf("[candle] FAIL %s\n", mkErr.c_str());
  }
}

// ---------------------------------------------------------------- config io
// keeps only keys that still exist as themes; guarantees at least one entry
static void sanitizeEnabled(){
  String out = "", item = "";
  for(uint8_t c = 0; c <= cfg.enabled.length(); c++){
    char ch = (c < cfg.enabled.length()) ? cfg.enabled[c] : ',';
    if(ch == ','){
      item.trim();
      if(item.length() && themeExists(item)){
        if(out.length()) out += ",";
        out += item;
      }
      item = "";
    } else item += ch;
  }
  if(!out.length()){
    for(int i = 0; i < THEME_N; i++){
      if(out.length()) out += ",";
      out += THEMES[i].key;
    }
  }
  cfg.enabled = out;
  if(!themeExists(cfg.theme) || !themeEnabled(cfg.theme)){
    cfg.theme = "";
    for(int i = 0; i < THEME_N; i++) if(themeEnabled(THEMES[i].key)){ cfg.theme = THEMES[i].key; break; }
    if(!themeExists(cfg.theme)) cfg.theme = THEMES[0].key;
  }
}

// up to 3 comma separated CoinGecko ids, never empty
static String sanitizeSyms(const String& s){
  String out = "", item = "";
  int n = 0;
  for(uint8_t c = 0; c <= s.length(); c++){
    char ch = (c < s.length()) ? s[c] : ',';
    if(ch == ','){
      item.trim();
      item.replace(" ", "");
      item.toLowerCase();
      if(item.length() && n < 3){
        if(out.length()) out += ",";
        out += item;
        n++;
      }
      item = "";
    } else item += ch;
  }
  return out;
}

// a CoinGecko id: lowercase alnum plus - _ .
static bool validId(const String& s){
  if(s.length() < 1 || s.length() > 40) return false;
  for(size_t i = 0; i < s.length(); i++){
    char c = s[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
    if(!ok) return false;
  }
  return true;
}

// RGB565 -> "#RRGGBB" for the web UI color picker
static String rgb565ToHex(uint16_t c){
  uint8_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
  r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2);
  char buf[8];
  snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
  return String(buf);
}

// stock assets bake the default background (0x0822) into palettes/bitmaps;
// remap those pixels to the currently selected background
static inline uint16_t bgPx(uint16_t c){ return (c == 0x0822) ? C_BG : c; }

static inline void bgFillRect(int x, int y, int w, int h){ tft.fillRect(x, y, w, h, C_BG); }
static inline void bgFillRoundRect(int x, int y, int w, int h, int r){
  tft.fillRoundRect(x, y, w, h, r, C_BG);
}
static inline void bgFillScreen(){ tft.fillScreen(C_BG); }

// ---------------------------------------------------------------- RU text
// Do not pass Cyrillic UTF-8 through TFT_eSPI::drawString(): on this ESP8266
// build its UTF-8 path corrupts the byte sequence.  The Calendar theme already
// uses the reliable solution: read 1-bit glyphs from PROGMEM and paint pixels
// directly.  font_ru.h uses the same GFXglyph bitmap layout, so the renderer
// below bypasses TFT_eSPI's text decoder entirely.
static inline uint16_t ruNext(const char*& p){
  uint8_t c = (uint8_t)*p++;
  if(c < 0x80) return c;
  if((c & 0xE0) == 0xC0){
    uint8_t b = (uint8_t)*p++;
    return (uint16_t)((c & 0x1F) << 6) | (b & 0x3F);
  }
  if((c & 0xF0) == 0xE0){
    uint8_t b = (uint8_t)*p++, d = (uint8_t)*p++;
    return (uint16_t)((c & 0x0F) << 12) | ((b & 0x3F) << 6) | (d & 0x3F);
  }
  if((c & 0xF8) == 0xF0){
    // The UI has no supplementary-plane glyphs; consume the sequence safely.
    p += 3;
    return 0;
  }
  return 0;
}
static inline const GFXfont* ruFace(int px){ return (px >= 20) ? &FontRu26 : &FontRu16; }
static inline int ruAscent(int px){ return (px >= 20) ? 18 : 11; }

// CJK side tables emitted by genfont.js: sparse (sorted cp -> glyph) because
// a contiguous GFXfont range for 0x4E00..0x9FFF would cost hundreds of KB
static bool zhFind(int px, uint16_t cp, const GFXglyph** gg, const uint8_t** bb){
  const bool big = (px >= 20);
  const uint16_t* cps = big ? FontZh26Cps : FontZh16Cps;
  const GFXglyph*  gl  = big ? FontZh26Glyphs : FontZh16Glyphs;
  const uint8_t*   bm  = big ? FontZh26Bitmaps : FontZh16Bitmaps;
  uint16_t n = big ? FontZh26N : FontZh16N;
  uint16_t lo = 0, hi = n;
  while(lo < hi){
    uint16_t mid = (uint16_t)(lo + hi) >> 1;
    uint16_t v = pgm_read_word(&cps[mid]);
    if(v == cp){ *gg = &gl[mid]; if(bb) *bb = bm; return true; }
    if(v < cp) lo = (uint16_t)(mid + 1); else hi = mid;
  }
  return false;
}

static int ruWidth(const String& s, int px, int track = 0){
  const GFXfont* f = ruFace(px);
  uint16_t first = pgm_read_word(&f->first), last = pgm_read_word(&f->last);
  const char* p = s.c_str();
  int w = 0;
  while(*p){
    uint16_t cp = ruNext(p);
    const GFXglyph* g = nullptr;
    if(cp >= first && cp <= last)
      g = &((const GFXglyph*)pgm_read_dword(&f->glyph))[cp - first];
    else if(cp < 0x2E80 || !zhFind(px, cp, &g, nullptr)) continue;
    w += pgm_read_byte(&g->xAdvance) + track;
  }
  return w;
}

static int ruDraw(int x, int y, const String& s, uint16_t fg, int px, int track = 0){
  const GFXfont* f = ruFace(px);
  uint16_t first = pgm_read_word(&f->first), last = pgm_read_word(&f->last);
  const uint8_t* mainBits = (const uint8_t*)pgm_read_dword(&f->bitmap);
  const char* p = s.c_str();
  int pen = x, baseline = y + ruAscent(px);
  tft.startWrite();
  while(*p){
    uint16_t cp = ruNext(p);
    const GFXglyph* g = nullptr;
    const uint8_t* bits = mainBits;
    if(cp >= first && cp <= last)
      g = &((const GFXglyph*)pgm_read_dword(&f->glyph))[cp - first];
    else if(cp < 0x2E80 || !zhFind(px, cp, &g, &bits)) continue;
    uint32_t off = pgm_read_dword(&g->bitmapOffset);
    uint8_t w = pgm_read_byte(&g->width), h = pgm_read_byte(&g->height);
    int8_t xo = (int8_t)pgm_read_byte(&g->xOffset);
    int8_t yo = (int8_t)pgm_read_byte(&g->yOffset);
    uint8_t bit = 0, byte = 0;
    for(uint8_t yy = 0; yy < h; yy++){
      for(uint8_t xx = 0; xx < w; xx++){
        if(bit == 0){ byte = pgm_read_byte(bits + off++); bit = 0x80; }
        if(byte & bit) tft.drawPixel(pen + xo + xx, baseline + yo + yy, fg);
        bit >>= 1;
      }
    }
    pen += pgm_read_byte(&g->xAdvance) + track;
  }
  tft.endWrite();
  return pen - x;
}
// left-aligned localized caption; falls back to the built-in font in EN
static int txt(int x, int y, const String& s, uint16_t fg, int px){
  if(gfxFont()) return ruDraw(x, y, s, fg, px);
  uint8_t f = (px >= 20) ? 4 : 2;
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(fg, C_BG);
  tft.drawString(s, x, y, f);
  return tft.textWidth(s, f);
}
// width of a caption exactly as txt() would draw it (for multi-part rows)
static int txtW(const String& s, int px){
  if(gfxFont()) return ruWidth(s, px);
  return tft.textWidth(s, (px >= 20) ? 4 : 2);
}
// horizontally centred caption; the free font is top-left drawn so y is nudged
// up by roughly half the cap height to keep the optical centre
static void txtC(int cx, int y, const String& s, uint16_t fg, int px){
  if(gfxFont()){
    int w = ruWidth(s, px);
    ruDraw(cx - w / 2, y - (px >= 20 ? 9 : 6), s, fg, px);
  } else {
    uint8_t f = (px >= 20) ? 4 : 2;
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(fg, C_BG);
    tft.drawString(s, cx, y, f);
  }
}
// right-aligned caption
static void txtR(int rx, int y, const String& s, uint16_t fg, int px){
  if(gfxFont()){
    int w = ruWidth(s, px);
    ruDraw(rx - w, y - (px >= 20 ? 9 : 6), s, fg, px);
  } else {
    uint8_t f = (px >= 20) ? 4 : 2;
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(fg, C_BG);
    tft.drawString(s, rx, y, f);
  }
}

// "HH:MM" -> minutes since 00:00, or -1 when malformed
static int parseHM(const String& s){
  int c = s.indexOf(':');
  if(c < 1 || c > 2 || s.length() > 5) return -1;
  int h = s.substring(0, c).toInt();
  int m = s.substring(c + 1).toInt();
  if(h < 0 || h > 23 || m < 0 || m > 59) return -1;
  return h * 60 + m;
}
static bool nightActive(){
  if(!cfg.nmode) return false;
  time_t t = time(nullptr);
  if(t < 1600000000UL) return false;            // NTP not synced yet
  struct tm ti;
  localtime_r(&t, &ti);
  int a = parseHM(cfg.nfrom), b = parseHM(cfg.nto);
  if(a < 0 || b < 0 || a == b) return false;
  int nowm = ti.tm_hour * 60 + ti.tm_min;
  if(a < b) return nowm >= a && nowm < b;
  return nowm >= a || nowm < b;                 // window crosses midnight
}

static void saveCfg(){
  JsonDocument d;
  d["theme"] = cfg.theme;
  d["enabled"] = cfg.enabled;
  d["carousel"] = cfg.carousel;
  d["interval"] = cfg.interval;
  d["sym"] = cfg.sym;
  d["chart"] = cfg.chart;
  d["brightness"] = cfg.brightness;
  d["refresh"] = cfg.refresh;
  d["tz"] = cfg.tz;
  d["city"] = cfg.city;
  d["syms"] = cfg.syms;
  d["gifnum"] = cfg.gifnum;
  d["bgcolor"] = cfg.bgcolor;
  d["wifiSsid"] = cfg.wifiSsid;
  d["wifiPass"] = cfg.wifiPass;
  d["hol"] = cfg.hol;
  d["hctry"] = cfg.hctry;
  d["nmode"] = cfg.nmode;
  d["nfrom"] = cfg.nfrom;
  d["nto"] = cfg.nto;
  d["nbr"] = cfg.nbr;
  d["tfmt"] = cfg.tfmt;
  d["lang"] = cfg.lang;
  d["wprov"] = cfg.wprov;
  File f = LittleFS.open("/config.json", "w");
  if(!f) return;
  serializeJson(d, f);
  f.close();
}

static void loadCfg(){
  File f = LittleFS.open("/config.json", "r");
  if(!f) return;
  JsonDocument d;
  DeserializationError e = deserializeJson(d, f);
  f.close();
  if(e) return;
  if(d["theme"].is<const char*>())   cfg.theme = d["theme"].as<String>();
  if(d["enabled"].is<const char*>()){
    String e = d["enabled"].as<String>();
    if(e.length()) cfg.enabled = e;
  }
  if(d["carousel"].is<bool>())       cfg.carousel = d["carousel"].as<bool>();
  if(d["interval"].is<uint16_t>()){
    int n = d["interval"].as<int>();
    if(n >= 5 && n <= 900) cfg.interval = (uint16_t)n;   // 0 would spin themes
  }
  if(d["sym"].is<const char*>())     cfg.sym = d["sym"].as<String>();
  if(d["chart"].is<const char*>())   cfg.chart = d["chart"].as<String>();
  if(d["brightness"].is<int>())      cfg.brightness = d["brightness"].as<int>();
  if(d["refresh"].is<uint16_t>()){
    int n = d["refresh"].as<int>();
    if(n >= 10 && n <= 3600) cfg.refresh = (uint16_t)n;
  }
  if(d["tz"].is<const char*>())     cfg.tz = d["tz"].as<String>();
  if(d["city"].is<const char*>())    cfg.city = d["city"].as<String>();
  if(d["syms"].is<const char*>()){
    String s = sanitizeSyms(d["syms"].as<String>());
    if(s.length()) cfg.syms = s;
  }
  if(d["gifnum"].is<int>()){
    int g = d["gifnum"].as<int>();
    if(g >= 0 && g < ANIM_N) cfg.gifnum = g;
  }
  if(d["bgcolor"].is<int>()) cfg.bgcolor = d["bgcolor"].as<uint16_t>();
  else if(d["bgcolor"].is<const char*>()){
    String s = d["bgcolor"].as<String>();   // legacy "#RRGGBB"
    s.trim(); if(s.startsWith("#")) s = s.substring(1);
    if(s.length() == 6) cfg.bgcolor = (uint16_t)strtoul(s.c_str(), nullptr, 16);
  }
  C_BG = cfg.bgcolor;                    // applied to every draw call
  if(d["wifiSsid"].is<const char*>()) cfg.wifiSsid = d["wifiSsid"].as<String>();
  if(d["wifiPass"].is<const char*>()) cfg.wifiPass = d["wifiPass"].as<String>();
  if(d["hol"].is<bool>())             cfg.hol = d["hol"].as<bool>();
  if(d["hctry"].is<const char*>()){
    String s = d["hctry"].as<String>();
    s.trim(); s.toUpperCase();
    if(s.length() == 2) cfg.hctry = s;
  }
  if(d["nmode"].is<bool>())        cfg.nmode = d["nmode"].as<bool>();
  if(d["nfrom"].is<const char*>()){
    String s = d["nfrom"].as<String>();
    s.trim(); if(parseHM(s) >= 0) cfg.nfrom = s;
  }
  if(d["nto"].is<const char*>()){
    String s = d["nto"].as<String>();
    s.trim(); if(parseHM(s) >= 0) cfg.nto = s;
  }
  if(d["nbr"].is<int>()){
    int b = d["nbr"].as<int>();
    if(b >= 0 && b <= 100) cfg.nbr = b;
  }
  if(d["tfmt"].is<const char*>()){
    String s = d["tfmt"].as<String>();
    if(s == "12" || s == "24") cfg.tfmt = s;
  }
  if(d["lang"].is<const char*>()){
    String s = d["lang"].as<String>();
    if(s == "ru" || s == "en" || s == "es" || s == "zh") cfg.lang = s;
  }
  if(d["wprov"].is<const char*>()){
    String s = d["wprov"].as<String>();
    if(s == "owm" || s == "ometo") cfg.wprov = s;
  }

  sanitizeEnabled();          // drops themes that no longer exist (dial/simple)
  if(cfg.brightness > 100) cfg.brightness = 100;   // old configs stored 0..255
}

static void applyBrightness(){
  static int lastPct = -1;
  int b = nightActive() ? cfg.nbr : cfg.brightness;   // night window wins
  if(b > 100) b = 100;
  if(b == lastPct) return;                            // PWM untouched when idle
  lastPct = b;
  analogWriteRange(1023);
  analogWriteFreq(1000);
  // backlight is active-low (P-MOSFET): duty 0 = fully on
  int duty = (100 - b) * 1023 / 100;
  analogWrite(TFT_BL, duty);
}

// ---------------------------------------------------------------- drawing
static void drawWxIcon(int cx, int cy, int r, int code, int force = -1);
static const WxJpg* wxJpgFind(const WxJpg* tab, int n, const char* key);
// thermo/humidity 30x30 icons drawn through their alpha mask at x,y
static void drawWxJpg30(const WxJpg* jpg, int x, int y);
static String signedT(float v);

// ---- classic: stock weather clock layout (city / metric / time / date / bars)
static inline uint16_t lerp565(uint16_t a, uint16_t b, float t){
  int ar = a >> 11, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  int br = b >> 11, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  int r = ar + (int)((br - ar) * t);
  int g = ag + (int)((bg - ag) * t);
  int bl = ab + (int)((bb - ab) * t);
  return (r << 11) | (g << 5) | bl;
}

static void clRing(int cx, int cy, int r, uint16_t col){
  tft.drawCircle(cx, cy, r, col);
  if(r > 1) tft.drawCircle(cx, cy, r - 1, col);
}

// draws <num> + degree ring + "C", returns the width used
static int clTempLine(int x, int y, const String& num, uint16_t col, int font){
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(col, C_BG);
  tft.drawString(num, x, y, font);
  int w = tft.textWidth(num, font);
  int r = (font == 2) ? 4 : 5;
  clRing(x + w + r + 3, y + (font == 2 ? 5 : 7), r, col);
  tft.drawString("C", x + w + 2 * r + 8, y, font);
  return w + 2 * r + 24;
}

static void clBar(int x, int y, int w, int h, bool tempScale){
  for(int i = 0; i < w; i++){
    float t = (float)i / (float)(w - 1);
    uint16_t c = tempScale ? lerp565(0x07FF, 0xF800, t) : lerp565(0x07FF, 0x001F, t);
    tft.fillRect(x + i, y, 1, h, c);
  }
}

// stock Classic corner animation (0.gif from the flash dump), bottom right corner
#define ANIM_X 160
#define ANIM_Y 160
static uint8_t animFrame = 0;
static uint32_t animNext = 0;

// one RLE frame staged from the raw flash slot (blob @ ANIM_SLOT); the staging
// buffer is malloc'd only for the duration of a draw and freed right after, so
// the ~3.6k stays available to the TLS handshake between renders
static const uint8_t* animStage(uint8_t* buf, const AnimTab& a, uint8_t f){
  uint32_t o0 = pgm_read_dword(&a.off[f]);
  uint32_t o1 = pgm_read_dword(&a.off[f + 1]);
  uint32_t addr = ANIM_SLOT + pgm_read_dword(&a.data) + o0;
  uint32_t al = addr & ~3u;            // spi_flash_read wants 4B-aligned addr
  ESP.flashRead(al, buf, (addr - al) + (o1 - o0));
  return buf + (addr - al);
}

// ymin = first screen row to touch; the foot strip gets refilled separately
static void clAnim(int ymin){
  const uint8_t aidx = cfg.gifnum < ANIM_N ? cfg.gifnum : 0;
  const AnimTab& a = ANIMS[aidx];
  uint8_t* buf = (uint8_t*)malloc(ANIM_MAX_FRAME + 4);
  if(!buf) return;                     // heap busy: skip this frame, no harm
  const uint8_t* p = animStage(buf, a, animFrame);
  const uint8_t* e = p + ANIM_MAX_FRAME;
  uint16_t line[ANIM_W];
  int i = 0, y = 0;
  bool sw = tft.getSwapBytes();
  tft.setSwapBytes(true);                  // palette is little-endian RGB565
  tft.startWrite();
  while(y < ANIM_H && p < e){
    uint8_t b = *p++;
    int n = b >> 4, v = b & 15;
    if(n == 0){ n = *p++; v = *p++ & 15; }
    while(n--){
      line[i++] = bgPx(pgm_read_word(&a.pal[v]));
      if(i == ANIM_W){
        int sy = ANIM_Y + y;
        if(sy >= ymin) tft.pushImage(ANIM_X, sy, ANIM_W, 1, line);
        i = 0;
        if(++y >= ANIM_H) break;
      }
    }
  }
  tft.endWrite();
  tft.setSwapBytes(sw);
  free(buf);
}

// ---- boot splash animation (winXP.gif, fullscreen 240x240 4bpp RLE) ----
// streamed from flash through a small buffer to keep heap/RAM footprint tiny
#define BOOT_SLOT 0x001B8000UL
static uint8_t  bootBuf[512];
static uint32_t bootAddr, bootEnd;
static uint16_t bootBi, bootBn;

static void bootOpen(uint8_t f){
  bootAddr = BOOT_SLOT + pgm_read_dword(&BOOT_OFF[f]);
  bootEnd  = BOOT_SLOT + pgm_read_dword(&BOOT_OFF[f + 1]);
  bootBi = 0; bootBn = 0;
}

static bool bootNextByte(uint8_t& out){
  if(bootBi >= bootBn){
    if(bootAddr >= bootEnd) return false;
    uint32_t n = bootEnd - bootAddr; if(n > sizeof(bootBuf)) n = sizeof(bootBuf);
    uint32_t al = bootAddr & ~3u;            // align address down for spi_flash_read
    uint32_t off = bootAddr - al;            // leading bytes to skip
    uint32_t rd = n + off; if(rd > sizeof(bootBuf)) rd = sizeof(bootBuf);
    ESP.flashRead(al, bootBuf, rd);
    bootBi = off; bootBn = rd;
    bootAddr = al + rd;
  }
  out = bootBuf[bootBi++];
  return true;
}

// draw frame f fullscreen (240x240)
static void bootShow(uint8_t f){
  bootOpen(f);
  bool sw = tft.getSwapBytes();
  tft.setSwapBytes(true);                    // palette is little-endian RGB565
  uint16_t line[BOOT_W];
  int i = 0, y = 0;
  tft.startWrite();
  while(y < BOOT_H){
    uint8_t b, n1, n2;
    if(!bootNextByte(b)) break;
    int n = b >> 4, v = b & 15;
    if(n == 0){ if(!bootNextByte(n1) || !bootNextByte(n2)) break; n = n1; v = n2 & 15; }
    while(n--){
      line[i++] = pgm_read_word(&BOOT_PAL[v]);
      if(i == BOOT_W){
        tft.pushImage(0, y, BOOT_W, 1, line);
        i = 0;
        if(++y >= BOOT_H) break;
      }
    }
  }
  tft.endWrite();
  tft.setSwapBytes(sw);
}

static uint8_t bootFrame = 0;
static uint32_t bootNext = 0;
static void bootTick(uint32_t now){
  if(now < bootNext) return;
  bootNext = now + BOOT_MS;
  bootShow(bootFrame);
  if(++bootFrame >= BOOT_N) bootFrame = 0;
}

static void animTick(uint32_t now){
  if(cfg.theme != "classic"){ animNext = now; return; }
  const uint8_t aidx = cfg.gifnum < ANIM_N ? cfg.gifnum : 0;
  const AnimTab& a = ANIMS[aidx];
  if(now < animNext) return;
  animNext = now + pgm_read_byte(&a.ms);
  if(++animFrame >= pgm_read_byte(&a.n)) animFrame = 0;
  clAnim(0);
}

static void clsDrawMetric(int x, int y){
  tft.setTextDatum(TL_DATUM);
  if(!wxShowOk()){
    tft.setTextColor(C_DIM, C_BG);
    txt(x, y, wxErr.length() ? cutB(errLoc(wxErr), 22)
                             : String(LG("загрузка...", "loading...", "cargando...", "加载中...")), C_DIM, 16);
    return;
  }
  int k = (int)((millis() / 5000UL) % 5);
  if(gfxFont()){
    String s;
    if(k == 0)      s = String(LG("Макс ", "Max ", "Máx ", "最高 ")) + String((int)lroundf(wxTmax)) + "°";
    else if(k == 1) s = String(LG("Мин ", "Min ", "Mín ", "最低 ")) + String((int)lroundf(wxTmin)) + "°";
    else if(k == 2) s = String(LG("Д ", "P ", "P ", "气压 ")) + String((int)wxPress) + LG(" гПа", " hPa", " hPa", " hPa");
    else if(k == 3) s = String(LG("Ветер ", "Wind ", "Viento ", "风 ")) + String((int)wxWind) + LG(" км/ч", " km/h", " km/h", " km/h");
    else            s = String(LG("Облачность ", "Cloud ", "Nubes ", "云量 ")) + String(wxCloudPct) + "%";
    txt(x, y, s, C_WHITE, 16);
    return;
  }
  tft.setTextColor(C_WHITE, C_BG);
  if(k == 0)      clTempLine(x, y, String("Max Temp ") + String((int)lroundf(wxTmax)), C_WHITE, 2);
  else if(k == 1) clTempLine(x, y, String("Min Temp ") + String((int)lroundf(wxTmin)), C_WHITE, 2);
  else if(k == 2) tft.drawString(String("P ") + String((int)wxPress) + " hPa", x, y, 2);
  else if(k == 3) tft.drawString(String("Wind ") + String((int)wxWind) + " km/h", x, y, 2);
  else            tft.drawString(String("Cloud: ") + String(wxCloudPct) + " %", x, y, 2);
}

// fake bold for the bitmap fonts (1,2,4,6,7): one pass with the bg colour
// paints a clean box, then the glyphs are re-blitted WITHOUT the bg at 1px
// offsets - the stacked fg pixels thicken the strokes.  A second full draw
// (fg+bg, like the old country-badge trick) does NOT work: the per-glyph bg
// fill erases the first pass.  thick=+/-1px in both axes for the big fonts.
static void drawBold(const String& s, int32_t x, int32_t y, uint8_t font,
                     uint16_t fg, uint16_t bg, uint8_t datum, bool thick = false){
  tft.setTextDatum(datum);
  tft.setTextColor(fg, bg);
  tft.drawString(s, x, y, font);
  tft.setTextColor(fg);
  tft.drawString(s, x, y, font);
  if(thick) tft.drawString(s, x - 1, y, font);
  tft.drawString(s, x + 1, y, font);
  if(thick) tft.drawString(s, x, y - 1, font);
  tft.drawString(s, x, y + 1, font);
}

static void clHead(){
  bgFillRect(0, 0, 240, 56);
  int bw = wxCountry.length() ? tft.textWidth(wxCountry, 2) + 14 : 0;
  int bx = 175 - bw;                                  // 10% (24px) gap to the icon
  if(bx < 110) bx = 110;
  if(!wxCountry.length()) bx = 175;
  String city = wxShowOk() ? wxCity : cfg.city;
  int maxw = bx - 14;                                 // stop before the badge
  int cw = gfxFont() ? ruWidth(city, 26) : tft.textWidth(city, 4);
  while(city.length() > 3 && cw > maxw){
    city.remove(city.length() - 1);
    while(city.length() && (((uint8_t)city[city.length() - 1] & 0xC0) == 0x80))
      city.remove(city.length() - 1);                 // never split a UTF-8 pair
    cw = gfxFont() ? ruWidth(city, 26) : tft.textWidth(city, 4);
  }
  txt(8, 4, city, C_ORANGE, 26);                      // ru: FontRu26, en: font4

  if(wxCountry.length()){
    tft.setTextDatum(TL_DATUM);
    int w = tft.textWidth(wxCountry, 2) + 14;
    int x = 175 - w;                                 // 10% (24px) gap to the icon
    if(x < 110) x = 110;
    tft.fillRoundRect(x, 4, w, 22, 5, C_UP_D);          // dark-green region badge
    drawBold(wxCountry, x + w / 2, 15, 2, C_WHITE, C_UP_D, MC_DATUM);
  }
  if(wxShowOk()) drawWxIcon(216, 18, 17, wxCode);
  // condition pill (moved here from the clock row - the clock owns it now)
  String cond = wxShowOk() ? wxMain : String("-");
  if(cond.length() > 9) cond = cond.substring(0, 9);
  int pw = tft.textWidth(cond, 2) + 14;
  int px = 232 - pw;
  tft.fillRoundRect(px, 36, pw, 20, 5, C_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_BG, C_WHITE);
  tft.drawString(cond, px + pw / 2, 47, 2);
  clsDrawMetric(8, 32);
}

static void clTime(){
  bgFillRect(0, 56, 240, 75);
  struct tm ti;
  bool ok = timeOk(ti);
  String hh = "--", mm = "--";
  if(ok){
    hh = is12() ? String(hour12(ti.tm_hour)) : two(ti.tm_hour);
    mm = two(ti.tm_min);
  }
  // font 7 (48px 7-seg, bold segments): "12:34" ~140px centered;
  // font 8 (251px) was clipping both screen edges
  int w = tft.textWidth(hh, 7) + tft.textWidth(":", 7) + tft.textWidth(mm, 7);
  int x = (240 - w) / 2;
  tft.setTextDatum(TL_DATUM);
  drawBold(hh, x, 69, 7, C_WHITE, C_BG, TL_DATUM, true);
  x += tft.textWidth(hh, 7);
  drawBold(":", x, 69, 7, C_WHITE, C_BG, TL_DATUM, true);
  x += tft.textWidth(":", 7);
  drawBold(mm, x, 69, 7, C_ORANGE, C_BG, TL_DATUM, true);
  if(ok && is12()){
    x += tft.textWidth(mm, 7) + 6;
    drawBold(ampm(ti.tm_hour), x, 96, 2, C_WHITE, C_BG, TL_DATUM, true);
  }
}

static const char* CL_WDF[] = {"Sunday","Monday","Tuesday","Wednesday",
                               "Thursday","Friday","Saturday"};
static const char* CL_MON[] = {"jan","feb","mar","apr","may","jun",
                               "jul","aug","sep","oct","nov","dec"};

// full weekday name under the temp/humidity line; right edge 158 keeps it
// out of the corner gif rect (x>=160, repaints every frame)
static void clWeekday(){
  struct tm ti;
  if(!timeOk(ti)) return;
  if(gfxFont()){
    // 26px matches the en face (font4); the two long names are squeezed with a
    // small negative tracking so they still end before the corner gif (x<=158)
    String s = dayLong(ti.tm_wday);
    int tr = 0, w = ruWidth(s, 26, tr);
    if(w > 156){
      int n = 0;
      for(unsigned i = 0; i < s.length(); i++) if(((uint8_t)s[i] & 0xC0) != 0x80) n++;
      tr = -((w - 156 + n - 1) / n);
      w = ruWidth(s, 26, tr);
    }
    ruDraw(158 - w, 169, s, C_ORANGE, 26, tr);   // baseline 187 ~ same centre as en
    tft.setTextDatum(TL_DATUM);
    return;
  }
  String s = CL_WDF[ti.tm_wday];
  int f = (tft.textWidth(s, 4) <= 156) ? 4 : 2;
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(C_ORANGE, C_BG);
  tft.drawString(s, 158, 179, f);
  tft.setTextDatum(TL_DATUM);
}

// date row: font4 (32px) - "25 may 2026" ~142px is the widest, ends at x=149
// (corner gif starts at 160). The row crosses y=212 into the footer band, so it
// clears its own rect and is also redrawn from the RM_FOOT path.
static void clDate(){
  bgFillRect(8, 196, 142, 34);   // rows 196..229, clear old/stale glyphs
  tft.setTextDatum(TL_DATUM);
  struct tm ti;
  if(timeOk(ti)){
    if(gfxFont()){
      String d = String(ti.tm_mday), yr = String(ti.tm_year + 1900);
      if(langIdx() == 3){                      // zh: "2026年7月24日" drawn at 16px
        ruDraw(8, 201, yr + "年" + String(ti.tm_mon + 1) + "月" + d + "日", C_WHITE, 16);
        return;
      }
      String mo = monShort(ti.tm_mon);
      ruDraw(8, 197, d + " " + mo + " " + yr, C_WHITE, 26);
      ruDraw(8 + ruWidth(d + " ", 26), 197, mo, C_MON_P, 26);   // month in pastel red
      return;
    }
    String d = String(ti.tm_mday), mo = CL_MON[ti.tm_mon],
           yr = String(ti.tm_year + 1900);            // "4 oct 2026"
    tft.setTextColor(C_WHITE, C_BG);
    tft.drawString(d + " " + mo + " " + yr, 8, 197, 4);
    // repaint the month in pastel red at its exact offset
    int mx = 8 + tft.textWidth(d + " ", 4);
    tft.setTextColor(C_MON_P, C_BG);
    tft.drawString(mo, mx, 197, 4);
    tft.setTextColor(C_WHITE, C_BG);
  } else {
    // font4 "waiting for NTP" would run into the corner gif; keep the small font
    txt(8, 201, LG("ожидание NTP", "waiting for NTP", "esperando NTP", "等待NTP"), C_DOWN, 16);
  }
}

static void clChart(){
  bgFillRect(0, 131, 240, 81);
  clAnim(0);                    // corner first, text is painted on top of it

  // one line: temp | humidity. Everything with x>=160 must stay above y=160
  // (the corner gif repaints its 160..240 rect every frame)
  const WxJpg* ti30 = wxJpgFind(WX_BIG, WX_BIG_N, "temperature");
  const WxJpg* hu30 = wxJpgFind(WX_BIG, WX_BIG_N, "humidity");
  drawWxJpg30(ti30, 3, 131);
  clBar(36, 141, 40, 12, true);
  if(wxShowOk()) clTempLine(80, 138, String((int)lroundf(wxTemp)), C_WHITE, 2);
  else { tft.setTextColor(C_DIM, C_BG); tft.drawString("--", 80, 138, 2); }

  drawWxJpg30(hu30, 128, 131);
  clBar(161, 141, 39, 12, false);
  // TL datum + right-aligned x: MR would vertically centre the string on y=138
  // and shift it 8px up vs the temp value (TL, top=138)
  tft.setTextDatum(TL_DATUM);
  if(wxShowOk()){
    tft.setTextColor(C_WHITE, C_BG);
    String hs = String((int)wxHum) + "%";
    tft.drawString(hs, 237 - tft.textWidth(hs, 2), 138, 2);
  } else {
    tft.setTextColor(C_DIM, C_BG);
    tft.drawString("--", 237 - tft.textWidth("--", 2), 138, 2);
  }

  // full weekday directly under the line, then the date
  clWeekday();
  clDate();
}

static void drawClassic(uint8_t m){
  if(m & RM_FULL) m |= RM_HEAD | RM_PRICE | RM_CHART | RM_FOOT | RM_SEC;
  if(m & RM_FOOT)  bgFillRect(0, 213, 240, 27);   // before clChart: it owns the corner
  if(m & RM_HEAD)  clHead();
  if(m & RM_PRICE) clTime();
  if(m & RM_CHART) clChart();
  else if(m & RM_FOOT){ clDate(); clAnim(213); }   // footer fill wipes date bottom
}

static void drawBadges(int x, int y, int w, int h, const String& txt, uint16_t bg, uint16_t fg){
  tft.fillRoundRect(x, y, w, h, 4, bg);
  drawBold(txt, x + w/2, y + h/2 + 1, 2, fg, bg, MC_DATUM);
}

static void drawCandles(int x, int y, int w, int h){
  if(cdN < 1) return;
  float lo = 1e9f, hi = -1e9f;
  for(int i = 0; i < cdN; i++){
    if(cds[i].l < lo) lo = cds[i].l;
    if(cds[i].h > hi) hi = cds[i].h;
  }
  if(hi <= lo){ hi = lo + 1; }
  float pad = (hi - lo) * 0.06f;
  lo -= pad; hi += pad;

  for(int g = 1; g <= 3; g++){
    int gy = y + (h * g) / 4;
    tft.drawFastHLine(x, gy, w, C_GRID);
  }

  float slot = (float)w / cdN;
  int bw = (int)slot - 2; if(bw < 2) bw = 2; if(bw > 14) bw = 14;
  for(int i = 0; i < cdN; i++){
    int cx = x + (int)(slot * i + slot / 2);
    int yo = yOf(cds[i].o, lo, hi, y, h);
    int yc = yOf(cds[i].c, lo, hi, y, h);
    int yh = yOf(cds[i].h, lo, hi, y, h);
    int yl = yOf(cds[i].l, lo, hi, y, h);
    uint16_t col = cds[i].c >= cds[i].o ? C_UP : C_DOWN;
    tft.drawFastVLine(cx, yh, yl - yh + 1, col);
    int top = yo < yc ? yo : yc;
    int bot = yo > yc ? yo : yc;
    if(bot - top < 1) bot = top + 1;
    tft.fillRect(cx - bw/2, top, bw, bot - top + 1, col);
  }

  tft.setTextDatum(TR_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(C_WHITE, C_PANEL);
  tft.drawString(fmtPrice(hi - pad), x + w - 3, y + 1, 2);
  tft.drawString(fmtPrice(lo + pad), x + w - 3, y + h - 9, 2);
}

static void drawLineChart(int x, int y, int w, int h){
  if(lnN < 2) return;
  float lo = 1e9f, hi = -1e9f;
  for(int i = 0; i < lnN; i++){
    if(lns[i] < lo) lo = lns[i];
    if(lns[i] > hi) hi = lns[i];
  }
  if(hi <= lo){ hi = lo + 1; }
  float pad = (hi - lo) * 0.06f;
  lo -= pad; hi += pad;

  for(int g = 1; g <= 3; g++){
    int gy = y + (h * g) / 4;
    tft.drawFastHLine(x, gy, w, C_GRID);
  }

  float step = (float)(w - 1) / (lnN - 1);
  int px = x, py = yOf(lns[0], lo, hi, y, h);
  for(int i = 1; i < lnN; i++){
    int cx = x + (int)(step * i);
    int cy = yOf(lns[i], lo, hi, y, h);
    tft.drawLine(px, py, cx, cy, C_LINE);
    px = cx; py = cy;
  }

  // last value marker
  tft.fillCircle(px, py, 3, C_LINE);

  tft.setTextDatum(TR_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(C_WHITE, C_PANEL);
  tft.drawString(fmtPrice(hi - pad), x + w - 3, y + 1, 2);
  tft.drawString(fmtPrice(lo + pad), x + w - 3, y + h - 9, 2);
}

// ---- icon cache: one 32x32 RGB565 record per catalog coin in /icons.bin
// (built by tools/gen_icons_all.py, uploaded via /api/iconup).  Embedded
// PROGMEM icons win, the cache is the fallback before the letter badge.
// One open file handle - the previous coin's record is closed on switch.
static const char* ICO_CACHE = "/icons.bin";
static File icF;
static int  icFIdx = -1;   // catalog index currently open in icF
static int  icCnt  = -1;   // header record count (-1 = not probed yet)

static int catalogIdx(const String& id){
  for(uint16_t i = 0; i < COINS_LOCAL_N; i++){
    CoinRef c;
    memcpy_P(&c, &COINS_LOCAL[i], sizeof(c));
    if(c.id && id.equals(c.id)) return (int)i;
  }
  return -1;
}

// open/position the cache at record idx; false = no cache for this coin
static bool icSeek(int idx){
  if(icCnt < 0){
    icCnt = 0;
    File f = LittleFS.open(ICO_CACHE, "r");
    if(f){
      char mag[4] = {0};
      uint16_t cnt = 0, rec = 0;
      if(f.read((uint8_t*)mag, 4) == 4 && !memcmp(mag, "SDPI", 4) &&
         f.read((uint8_t*)&cnt, 2) == 2 &&
         f.read((uint8_t*)&rec, 2) == 2 && rec == ICO_W * ICO_H)
        icCnt = cnt;
      f.close();
    }
  }
  if(idx < 0 || idx >= icCnt) return false;
  uint32_t off = 8 + (uint32_t)idx * ICO_W * ICO_H * 2;   // 2048B per record
  if(icFIdx == idx && icF) return icF.seek(off);
  icF.close();
  icFIdx = -1;
  icF = LittleFS.open(ICO_CACHE, "r");
  if(!icF) return false;
  if(!icF.seek(off)){ icF.close(); return false; }
  icFIdx = idx;
  return true;
}

static void drawCoinIcon(int x, int y){
  const uint16_t* ic = nullptr;
  bool ram = false;
  uint16_t row[ICO_W];
  for(int i = 0; i < ICO_N; i++) if(cfg.sym == ICO_KEYS[i]){ ic = ICO_TABLE[i]; break; }
  if(!ic){
    int idx = catalogIdx(cfg.sym);
    if(idx >= 0){                               // LittleFS cache attempt
      ram = icSeek(idx);                        // positions record at 8+idx*2048
      // blank cache record (missing asset): treat as cache miss -> badge
      if(ram){
        bool allbg = true;
        icF.read((uint8_t*)row, ICO_W * 2);
        for(int i = 0; i < ICO_W; i++) if(row[i] != 0x0822){ allbg = false; break; }
        if(allbg){ icF.close(); icFIdx = -1; ram = false; }
        else icF.seek(8 + (uint32_t)idx * ICO_W * ICO_H * 2);  // rewind to row 0
      }
    }
  }
  if(!ic && !ram){
    // unknown symbol: keep the old letter badge
    uint16_t iconCol = mkOk ? (mkChg >= 0 ? C_UP : C_DOWN) : C_DIM;
    tft.fillCircle(x + ICO_W / 2, y + ICO_H / 2, 11, iconCol);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_BG, iconCol);
    tft.drawString(labelOf(cfg.sym).substring(0, 1), x + ICO_W / 2, y + ICO_H / 2 + 1, 4);
    return;
  }
  bool sw = tft.getSwapBytes();
  tft.setSwapBytes(true);              // pushImage then matches drawString/fillRect
  for(int r = 0; r < ICO_H; r++){
    if(ram) icF.read((uint8_t*)row, sizeof(row));
    else    memcpy_P(row, ic + r * ICO_W, sizeof(row));
    if(C_BG != 0x0822)                  // icons bake the default bg in
      for(int i = 0; i < ICO_W; i++) row[i] = bgPx(row[i]);
    tft.pushImage(x, y + r, ICO_W, 1, row);
  }
  if(ram){ icF.close(); icFIdx = -1; } // don't hold FS buffers between draws
  tft.setSwapBytes(sw);
}

static void tkHead(){
  tft.fillRect(0, 0, 240, 36, C_HDR);
  drawCoinIcon(2, 2);
  String lab = labelOf(cfg.sym);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_WHITE, C_HDR);
  tft.drawString(lab, 40, 5, 4);

  struct tm ti;
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(C_TXT, C_HDR);
  if(timeOk(ti)){
    String t = (is12() ? String(hour12(ti.tm_hour)) : two(ti.tm_hour)) + ":" + two(ti.tm_min);
    int xr = 234;
    if(is12()){
      const char* ap = ampm(ti.tm_hour);
      int apw = tft.textWidth(ap, 1);
      // squeeze AM/PM (font1) only when the label side still leaves room
      if(40 + tft.textWidth(lab, 4) + 8 + tft.textWidth(t, 4) + apw + 4 <= 234){
        xr = 234 - apw - 4;
        tft.drawString(ap, 234, 22, 1);   // bottom-aligned under the font-4 cap
      }
    }
    tft.drawString(t, xr, 4, 4);
  }
}

static void tkPrice(){
  bgFillRect(0, 36, 240, 60);
  tft.fillRoundRect(6, 39, 174, 52, 6, C_PRICE_BG);
  uint8_t pf = 7;
  String priceS = "--";
  if(mkOk) priceS = priceBig(mkPrice, pf);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_PRICE_FG, C_PRICE_BG);
  tft.drawString(priceS, 93, 65, pf);          // one font, plain (not bold)

  String chgS = mkOk ? (String(mkChg >= 0 ? "+" : "") + String(mkChg, 1) + "%") : "--";
  drawBadges(184, 39, 50, 24, chgS, mkChg >= 0 ? C_UP_D : C_DOWN_D, C_WHITE);
  drawBadges(184, 67, 50, 24, cfg.range, C_PANEL, C_CYAN);
}

static void tkChart(){
  bgFillRoundRect(6, 96, 228, 116, 6);
  tft.drawRoundRect(6, 96, 228, 116, 6, C_BORDER);
  if(cfg.chart == "candle") drawCandles(10, 100, 220, 108);
  else                      drawLineChart(10, 100, 220, 108);

  if(!mkOk){
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_DOWN, C_BG);
    String e = mkErrReal.length() ? errLoc(mkErrReal)
                               : String(LG("нет данных", "no data", "sin datos", "无数据"));
    if(e.length() > 16) e = cutB(e, 16);   // keep font4 inside the screen
    tft.drawString(e, 120, 154, 4);
  }
}

static void tkFoot(){
  bgFillRect(0, 212, 240, 28);
  tft.setTextDatum(TL_DATUM);
  String age = mkOk ? String((millis() - mkAt) / 1000) + LG("с назад", "s ago", "s atrás", "秒前")
                    : LG("ожидание", "waiting", "esperando", "等待中");
  String left = (cfg.chart == "candle" ? LG("СВЕЧИ ", "CANDLES ", "VELAS ", "K线 ")
                                       : LG("ЛИНИЯ ", "LINE ", "LÍNEA ", "线 ")) + age;
  txt(8, 220, left, C_DIM, 16);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString("coingecko", 232, 220, 2);
}

static void drawTicker(uint8_t m){
  if(m & RM_FULL) m |= RM_HEAD | RM_PRICE | RM_CHART | RM_FOOT;
  if(m & RM_HEAD)  tkHead();
  if(m & RM_PRICE) tkPrice();
  if(m & RM_CHART) tkChart();
  if(m & RM_FOOT)  tkFoot();
}

// ---------------------------------------------------------------- crypto all
static String fmtUsd(float p){
  if(p >= 1000.0f){
    long v = (long)(p + 0.5f);
    String s = String(v), o = "";
    int c = 0;
    for(int i = s.length() - 1; i >= 0; i--){
      o = String(s[i]) + o;
      c++;
      if(c % 3 == 0 && i > 0) o = "," + o;
    }
    return "$" + o;
  }
  if(p >= 1.0f) return "$" + String(p, 2);
  return "$" + trimZeros(String(p, decFor(p)));   // full precision below $1
}

static String fmtPct(float v){
  String s = String(v, 2);
  s.replace(".", ",");
  return "24h " + String(v >= 0 ? "+" : "") + s + "%";
}

static void drawCrypto3(uint8_t m){
  if(m & RM_FULL) m |= RM_HEAD | RM_PRICE | RM_CHART | RM_FOOT;
  if(m & RM_HEAD){
    bgFillRect(0, 0, 240, 38);
    tft.fillCircle(16, 19, 5, C_UP);
    txt(28, 11, LG("Крипта", "Markets", "Mercados", "行情"), C_WHITE, 16);

    String ip = WiFi.isConnected() ? WiFi.localIP().toString() : String("AP");
    int w = tft.textWidth(ip, 2) + 18;
    int x = 232 - w;
    tft.fillRoundRect(x, 6, w, 25, 8, C_HDR);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_WHITE, C_HDR);
    tft.drawString(ip, x + w / 2, 19, 2);
  }
  if(m & RM_PRICE){
    bgFillRect(0, 38, 240, 202);
    String want[NQ];
    int nw = 0;
    symsSplit(cfg.syms, want, nw);
    for(int i = 0; i < NQ; i++){
      int y = 40 + i * 66;
      bgFillRoundRect(6, y - 2, 228, 60, 6);
      tft.drawRoundRect(6, y - 2, 228, 60, 6, C_BORDER);
      tft.setTextDatum(TL_DATUM);
      if(!q3[i].ok){
        tft.setTextColor(C_DIM, C_BG);
        String hint = (i < nw) ? want[i] : String("-");
        if(i == 0 && !q3Ok) hint = mkErrReal.length() ? cutB(errLoc(mkErrReal), 22)
                                                  : String(LG("загрузка...", "loading...", "cargando...", "加载中..."));
        tft.drawString(hint, 10, y + 16, 4);
        continue;
      }
      tft.setTextColor(q3[i].chg >= 0 ? C_UP : C_DOWN, C_BG);
      tft.drawString(q3[i].sym, 10, y + 2, 4);
      tft.setTextColor(C_WHITE, C_BG);
      tft.setTextDatum(TR_DATUM);
      tft.drawString(fmtUsd(q3[i].price), 230, y + 2, 4);

      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(C_WHITE, C_BG);
      tft.drawString(q3[i].name, 10, y + 34, 2);
      tft.setTextColor(q3[i].chg >= 0 ? C_UP : C_DOWN, C_BG);
      tft.setTextDatum(TR_DATUM);
      tft.drawString(fmtPct(q3[i].chg), 230, y + 36, 2);
    }
  }
  if(m & RM_FOOT) bgFillRect(0, 236, 240, 4);
}

// ---------------------------------------------------------------- flip clock (animated GIF digits)
#define FLIP_W 56
#define FLIP_H 75
#define FLIP_Y 60
#define FLIP_N 4
static const int FLIP_X[FLIP_N] = {4, 60, 124, 180};

static uint16_t flipLine[FLIP_W];
static int  flipActive = -1;              // digit slot being played, -1 = idle
static int  flipTotal = 1, flipFrames = 0;
static int  flipQueue[FLIP_N];
static int  flipQh = 0, flipQt = 0;
static uint32_t flipNext = 0;
struct FlipDigit { uint8_t cur; bool queued; };
static FlipDigit flipD[FLIP_N] = {{255, false}, {255, false}, {255, false}, {255, false}};

// push one pre-decoded 56x75 frame (native RGB565, PROGMEM) at digit slot pos
static void flipShow(uint8_t pos, uint8_t f){
  uint8_t v = flipD[pos].cur;
  if(v > 9) return;
  const FlipFrames& g = FLIP_FS[v];
  if(f >= g.n) f = g.n - 1;
  uint32_t addr = FF_SLOT + g.slot + (uint32_t)f * FLIP_FRAME_BYTES;
  bool sw = tft.getSwapBytes();
  tft.setSwapBytes(true);
  for(int y = 0; y < FLIP_H; y++){
    ESP.flashRead(addr + (uint32_t)y * FLIP_W * 2, (uint8_t*)flipLine, FLIP_W * 2);
    if(C_BG != 0x0822)                       // frames bake the default bg in
      for(int i = 0; i < FLIP_W; i++) flipLine[i] = bgPx(flipLine[i]);
    tft.pushImage(FLIP_X[pos], FLIP_Y + y, FLIP_W, 1, flipLine);
  }
  tft.setSwapBytes(sw);
}

// static digit = last frame of playback order
static void flipDrawFinal(uint8_t pos){
  uint8_t v = flipD[pos].cur;
  if(v > 9) return;
  flipShow(pos, (uint8_t)(FLIP_FS[v].n - 1));
}

static void flipQueuePush(uint8_t d){
  if(flipD[d].queued || flipQt - flipQh >= FLIP_N) return;
  flipQueue[flipQt % FLIP_N] = d;
  flipQt++;
  flipD[d].queued = true;
}

static void flipReset(){
  flipActive = -1;
  flipQh = flipQt = 0;
  for(int i = 0; i < FLIP_N; i++){ flipD[i].cur = 255; flipD[i].queued = false; }
}

static void flipTick(uint32_t now){
  if(now < flipNext) return;
  if(flipActive >= 0){
    flipFrames++;
    if(flipFrames >= flipTotal){
      flipD[flipActive].queued = false;
      flipActive = -1;
      flipNext = now + 30;
      return;
    }
    flipShow((uint8_t)flipActive, (uint8_t)flipFrames);
    flipNext = now + FLIP_FRAME_MS;
    return;
  }
  if(flipQh < flipQt){
    uint8_t d = (uint8_t)flipQueue[flipQh % FLIP_N];
    flipQh++;
    uint8_t v = flipD[d].cur;
    if(v > 9){ flipD[d].queued = false; flipNext = now; return; }
    flipTotal = FLIP_FS[v].n;
    if(flipTotal > 1){
      flipActive = d;
      flipFrames = 0;
      flipShow(d, 0);
      flipNext = now + FLIP_FRAME_MS;
    } else {
      flipD[d].queued = false;
      flipNext = now;
    }
  }
}

static void flipPanel(int x, int y, int w, int h, const String& txt){
  tft.fillRoundRect(x, y, w, h, 8, C_PANEL);
  tft.drawRoundRect(x, y, w, h, 8, C_BORDER);
  tft.drawFastHLine(x + 6, y + h / 2, w - 12, C_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_WHITE, C_PANEL);
  tft.drawString(txt, x + w / 2, y + h / 2, 7);
}

static void drawFlip(uint8_t m){
  if(m & RM_FULL){
    m |= RM_HEAD | RM_PRICE | RM_CHART | RM_FOOT;
    flipReset();
  }
  if(m & RM_PRICE){
    struct tm ti;
    bool ok = timeOk(ti);
    if(!ok){
      if(flipActive >= 0 || flipQh < flipQt) flipReset();
      bgFillRect(0, FLIP_Y, 240, FLIP_H);
      for(int i = 0; i < FLIP_N; i++)
        flipPanel(FLIP_X[i], FLIP_Y, FLIP_W, FLIP_H, "--");
    } else {
      String s = (is12() ? two(hour12(ti.tm_hour)) : two(ti.tm_hour)) + two(ti.tm_min);
      for(int i = 0; i < FLIP_N; i++){
        uint8_t d = (uint8_t)(s[i] - '0');
        if(d == flipD[i].cur) continue;
        bool first = flipD[i].cur == 255;
        flipD[i].cur = d;
        if(first) flipDrawFinal((uint8_t)i);
        else {
          bgFillRect(FLIP_X[i], FLIP_Y, FLIP_W, FLIP_H);
          flipQueuePush((uint8_t)i);
        }
      }
    }
  }
  if(m & RM_CHART){
    bgFillRect(0, 152, 240, 54);
    struct tm ti;
    tft.setTextColor(C_GREY, C_BG);
    if(timeOk(ti)){
      String wd, dt;
      switch(langIdx()){
        case 1: { static const char* WD[] = {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
                  static const char* MO[] = {"January","February","March","April","May","June","July",
                                             "August","September","October","November","December"};
                  wd = WD[ti.tm_wday];
                  dt = String(ti.tm_mday) + " " + MO[ti.tm_mon];
                  break; }
        case 2: { static const char* EDM[] = {"de enero","de febrero","de marzo","de abril","de mayo","de junio",
                                              "de julio","de agosto","de septiembre","de octubre","de noviembre","de diciembre"};
                  wd = ES_WDF[ti.tm_wday];
                  dt = String(ti.tm_mday) + " " + EDM[ti.tm_mon];
                  break; }
        case 3: wd = ZH_WDF[ti.tm_wday];
                dt = String(ti.tm_mon + 1) + "月" + String(ti.tm_mday) + "日";
                break;
        default: { static const char* FM[] = {"января","февраля","марта","апреля","мая","июня",
                                              "июля","августа","сентября","октября","ноября","декабря"};
                   wd = RU_WDF[ti.tm_wday];
                   dt = String(ti.tm_mday) + " " + FM[ti.tm_mon];
                   break; }
      }
      // Two lines, 26px in BOTH languages: the old single line was font4 for
      // EN (clipped: "Wednesday, 22 September" > 240px) and FontRu16 for RU
      // (too small next to the EN case). Weekday above, date below.
      txtC(120, 165, wd, C_GREY, 26);
      txtC(120, 190, dt, C_GREY, 26);
      tft.setTextDatum(MC_DATUM);
      if(is12()){
        String ap = ampm(ti.tm_hour);
        int dw = gfxFont() ? ruWidth(dt, 26) : tft.textWidth(dt, 4);
        if(120 + dw / 2 + 6 + tft.textWidth(ap, 2) <= 234){
          tft.setTextDatum(TR_DATUM);
          tft.setTextColor(C_GREY, C_BG);
          tft.drawString(ap, 234, 182, 2);   // right of the date line, optical pair
          tft.setTextDatum(MC_DATUM);
        }
      }
    }
  }
  if(m & RM_FOOT){
    bgFillRect(0, 206, 240, 34);        // footer bg = theme bg
    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(2);
    if(mkOk){
      tft.setTextColor(C_CYAN, C_BG);
      tft.drawString(labelOf(cfg.sym), 8, 214, 2);
      tft.setTextColor(C_WHITE, C_BG);
      tft.setTextDatum(TR_DATUM);
      tft.drawString("$" + fmtPrice(mkPrice), 232, 216, 2);
    } else {
      tft.setTextColor(C_DIM, C_BG);
      txt(8, 216, LG("нет данных", "no market data", "sin datos", "无数据"), C_DIM, 16);
    }
  }
}

// ---------------------------------------------------------------- weather icon
static void wxCloud(int cx, int cy, int r, uint16_t col){
  tft.fillCircle(cx - r / 3, cy + r / 8, r / 2, col);
  tft.fillCircle(cx + r / 3, cy + r / 8, r / 2, col);
  tft.fillCircle(cx, cy - r / 6, (int)(r * 0.45f), col);
  tft.fillRect(cx - r * 2 / 3, cy + r / 8, r * 4 / 3, r / 3, col);
}

static void wxSun(int cx, int cy, int r, uint16_t col){
  for(int i = 0; i < 8; i++){
    float a = i * 45.0f * PI / 180.0f;
    int x1 = cx + (int)((r + 3) * cosf(a)), y1 = cy + (int)((r + 3) * sinf(a));
    int x2 = cx + (int)((r + 9) * cosf(a)), y2 = cy + (int)((r + 9) * sinf(a));
    tft.drawLine(x1, y1, x2, y2, col);
  }
  tft.fillCircle(cx, cy, r, col);
}

// ---------------------------------------------------------------- weather icon (JPEG from stock firmware)
static bool jpgPlain = false;   // photo theme: keep original pixels (no bg remap)
// alpha mask of the icon currently decoding (nullptr = legacy keyed icons).
// mask=1 pixels are pushed as-is, mask=0 pixels are NOT drawn at all -> the
// live background shows through: no baked rectangle, no colour-key ringing.
static const uint8_t* iconMask = nullptr;
static int iconMx = 0, iconMy = 0, iconMw = 0, iconMh = 0, iconMs = 0;
// 8-bit loads from irom/IRAM fault with Exception 3 on this build, 32-bit
// loads work: read an aligned dword and extract the byte (addr may be any
// alignment; up to 3 padding bytes past the array are harmless reads).
static inline uint8_t iconMaskByte(uint32_t idx){
  const volatile uint8_t* a = iconMask + idx;
  uint32_t v = *(const volatile uint32_t*)((uintptr_t)a & ~(uintptr_t)3);
  return (uint8_t)(v >> (((uintptr_t)a & 3) << 3));
}
static bool wxJpgOut(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap){
  bool sw = tft.getSwapBytes();
  tft.setSwapBytes(true);                 // TJpg outputs little-endian RGB565
  if(iconMask && !jpgPlain){
    for(int yy = 0; yy < (int)h; yy++){
      uint16_t* row = bitmap + (uint32_t)yy * w;
      int sy = y + yy - iconMy;
      if(sy < 0 || sy >= iconMh){ tft.pushImage(x, y + yy, w, 1, row); continue; }
      int run = -1;
      for(int xx = 0; xx <= (int)w; xx++){
        bool on = false;
        if(xx < (int)w){
          int sx = x + xx - iconMx;
          if(sx >= 0 && sx < iconMw)
            on = (iconMaskByte((uint32_t)sy * iconMs + ((uint32_t)sx >> 3))
                  >> (7 - (sx & 7))) & 1;
        }
        if(on){ if(run < 0) run = xx; }
        else if(run >= 0){
          tft.pushImage(x + run, y + yy, xx - run, 1, row + run);
          run = -1;
        }
      }
    }
    tft.setSwapBytes(sw);
    return true;
  }
  if(!jpgPlain && C_BG != 0x0822){                 // icons bake the default bg in
    uint16_t* p = bitmap;
    for(uint32_t i = 0; i < (uint32_t)w * h; i++) p[i] = bgPx(p[i]);
  }
  tft.pushImage(x, y, w, h, bitmap);
  tft.setSwapBytes(sw);
  return true;
}

static const WxJpg* wxJpgFind(const WxJpg* tab, int n, const char* key){
  for(int i = 0; i < n; i++) if(!strcmp(tab[i].key, key)) return &tab[i];
  return nullptr;
}

static void drawWxJpg30(const WxJpg* jpg, int x, int y){
  if(!jpg) return;
  iconMask = jpg->mask;
  iconMx = x; iconMy = y; iconMw = 30; iconMh = 30; iconMs = 4;
  TJpgDec.drawJpg(x, y, jpg->data, jpg->len);
  iconMask = nullptr;
}

static const char* wxIconKey(int code, bool night){
  if(code == 800) return night ? "01n" : "01d";
  if(code == 801) return night ? "02n" : "02d";
  if(code == 802) return "03d";
  if(code == 803 || code == 804) return "04d";
  if(code >= 200 && code < 300) return "11d";
  if(code >= 300 && code < 400) return "09d";
  if(code >= 500 && code < 600) return "10d";
  if(code >= 600 && code < 700) return "13d";
  if(code >= 700 && code < 800) return "50d";
  return nullptr;
}

// Russian weather description from the OWM condition code
static const char* wxRuDesc(int code){
  if(code == 800) return PSTR("ясно");
  if(code == 801) return PSTR("почти ясно");
  if(code == 802) return PSTR("переменная облачность");
  if(code == 803) return PSTR("облачно");
  if(code == 804) return PSTR("пасмурно");
  if(code >= 200 && code < 300) return PSTR("гроза");
  if(code >= 300 && code < 400) return PSTR("морось");
  if(code >= 500 && code < 600) return PSTR("дождь");
  if(code >= 600 && code < 700) return PSTR("снег");
  if(code >= 700 && code < 800) return PSTR("туман");
  return PSTR("-");
}

// Spanish / Chinese weather descriptions from the OWM condition code
static const char* wxEsDesc(int code){
  if(code == 800) return PSTR("despejado");
  if(code == 801) return PSTR("pocas nubes");
  if(code == 802) return PSTR("nubes dispersas");
  if(code == 803) return PSTR("nublado");
  if(code == 804) return PSTR("muy nublado");
  if(code >= 200 && code < 300) return PSTR("tormenta");
  if(code >= 300 && code < 400) return PSTR("llovizna");
  if(code >= 500 && code < 600) return PSTR("lluvia");
  if(code >= 600 && code < 700) return PSTR("nieve");
  if(code >= 700 && code < 800) return PSTR("niebla");
  return PSTR("-");
}
static const char* wxZhDesc(int code){
  if(code == 800) return PSTR("晴");
  if(code == 801) return PSTR("少云");
  if(code == 802) return PSTR("多云");
  if(code == 803) return PSTR("阴");
  if(code == 804) return PSTR("阴天");
  if(code >= 200 && code < 300) return PSTR("雷雨");
  if(code >= 300 && code < 400) return PSTR("毛毛雨");
  if(code >= 500 && code < 600) return PSTR("雨");
  if(code >= 600 && code < 700) return PSTR("雪");
  if(code >= 700 && code < 800) return PSTR("雾");
  return PSTR("-");
}
// localized description for the UI (EN shows the API text as-is)
static const char* wxLocDesc(int code){
  if(langIdx() == 2) return wxEsDesc(code);
  if(langIdx() == 3) return wxZhDesc(code);
  return wxRuDesc(code);
}

static void drawWxIcon(int cx, int cy, int r, int code, int force){
  // stock bitmaps: ico2 45x32 for small slots, ico 65x46 for the big weather slot
  bool big = (r >= 25);
  const WxJpg* tab = big ? WX_BIG : WX_SM;
  int tn = big ? WX_BIG_N : WX_SM_N;
  struct tm ti;
  // force: -1 = pick by clock, 0 = day variant, 1 = night variant
  bool night = (force >= 0) ? (force == 1)
                            : (timeOk(ti) && (ti.tm_hour < 6 || ti.tm_hour >= 21));
  const char* key = wxIconKey(code, night);
  const WxJpg* jpg = key ? wxJpgFind(tab, tn, key) : nullptr;
  if(jpg){
    int w = big ? 65 : 45, h = big ? 46 : 32;
    iconMask = jpg->mask;
    iconMx = cx - w / 2; iconMy = cy - h / 2;
    iconMw = w; iconMh = h; iconMs = (w + 7) >> 3;
    TJpgDec.drawJpg(cx - w / 2, cy - h / 2, jpg->data, jpg->len);
    iconMask = nullptr;
    return;
  }
  if(code == 800){
    wxSun(cx, cy, r / 2, C_PRICE_BG);
  } else if(code >= 801 && code <= 804){
    wxSun(cx - r / 3, cy - r / 3, r / 3, C_PRICE_BG);
    wxCloud(cx, cy + r / 6, r, C_CLOUD);
  } else if(code >= 200 && code < 300){
    wxCloud(cx, cy - r / 6, r, C_CLOUD);
    tft.fillTriangle(cx - r / 5, cy + r / 6, cx + r / 3, cy + r / 5,
                     cx - r / 6, cy + r, C_PRICE_BG);
  } else if(code >= 500 && code < 600){
    wxCloud(cx, cy - r / 6, r, C_CLOUD);
    for(int i = 0; i < 3; i++){
      int x = cx - r / 2 + i * r / 2;
      tft.drawLine(x, cy + r / 5, x - r / 6, cy + r * 4 / 5, C_CYAN);
      tft.drawLine(x + 1, cy + r / 5, x - r / 6 + 1, cy + r * 4 / 5, C_CYAN);
    }
  } else if(code >= 300 && code < 400){
    wxCloud(cx, cy - r / 6, r, C_CLOUD);
    for(int i = 0; i < 3; i++) tft.fillCircle(cx - r / 3 + i * r / 3, cy + r * 3 / 5, 2, C_CYAN);
  } else if(code >= 600 && code < 700){
    wxCloud(cx, cy - r / 6, r, C_CLOUD);
    for(int i = 0; i < 3; i++){
      int x = cx - r / 3 + i * r / 3, y = cy + r * 3 / 5;
      tft.drawLine(x - 3, y, x + 3, y, C_WHITE);
      tft.drawLine(x, y - 3, x, y + 3, C_WHITE);
    }
  } else if(code >= 700 && code < 800){
    for(int i = 0; i < 4; i++){
      int y = cy - r / 2 + i * r / 3;
      tft.drawLine(cx - r, y, cx + r - (i % 2) * r / 3, y, C_MIST);
    }
  } else {
    wxCloud(cx, cy, r, C_CLOUD);
  }
}

static String signedT(float v){
  int t = (int)lroundf(v);
  return (t < 0 ? String("") : String("+")) + String(t);
}

// ---------------------------------------------------------------- weather theme
static void drawWeather(uint8_t m){
  if(m & RM_FULL) m |= RM_HEAD | RM_PRICE | RM_CHART | RM_FOOT;
  if(m & RM_HEAD){
    bgFillRect(0, 0, 240, 54);
    // top row: night low left-aligned, day high right-aligned (IP removed)
    // OWM's current-weather temp_min/temp_max track the current temp and come
    // out equal - the real night/day numbers live in today's forecast row
    if(wxShowOk()){
      float tN = (fcn > 0) ? fcd[0].lo : wxTmin;
      float tD = (fcn > 0) ? fcd[0].hi : wxTmax;
      String a = signedT(tN) + "`", b = signedT(tD) + "`";
      const char* l1 = LG("ночь ", "night ", "noche ", "夜 ");
      const char* l2 = LG("день ", "day ", "día ", "白天 ");
      int w1 = txtW(l1, 16), w2 = txtW(l2, 16);
      int wb = txtW(b, 16);
      txt(8, 4, l1, C_DIM, 16);
      txt(8 + w1, 4, a, C_WHITE, 16);
      txt(232 - (w2 + wb), 4, l2, C_DIM, 16);
      txt(232 - wb, 4, b, C_WHITE, 16);
    }
    // city back in its original place
    txtC(120, 36, wxShowOk() ? wxCity : cfg.city, C_CYAN, 26);
  }
  if(m & RM_PRICE){
    bgFillRect(0, 54, 240, 74);
    tft.setTextDatum(MC_DATUM);
    if(wxShowOk()){
      // main centre temperature coloured by sign: >0 red, ==0 white, <0 cyan
      uint16_t tc = wxTemp > 0 ? C_DOWN : (wxTemp < 0 ? C_CYAN : C_WHITE);
      tft.setTextColor(tc, C_BG);
      String s = signedT(wxTemp);
      tft.drawString(s, 120, 92, 7);
      int w = tft.textWidth(s, 7);
      tft.drawCircle(120 + w / 2 + 9, 92 - 17, 6, tc);
      tft.drawCircle(120 + w / 2 + 9, 92 - 17, 7, C_BG);
      tft.drawCircle(120 + w / 2 + 9, 92 - 17, 6, tc);
    } else {
      tft.setTextColor(C_DOWN, C_BG);
      txtC(120, 92, wxErr.length() ? cutB(errLoc(wxErr), 18)
                                   : String(LG("загрузка", "loading", "cargando", "加载中")), C_DOWN, 26);
    }
  }
  if(m & RM_CHART){
    bgFillRect(0, 128, 240, 40);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_TXT, C_BG);
    String desc = wxShowOk() ? (langIdx() == 1 ? wxDesc : String(wxLocDesc(wxCode))) : String("-");
    desc = cutB(desc, 22);
    txtC(120, 148, desc, C_TXT, 26);
  }
  if(m & RM_FOOT){
    bgFillRect(0, 168, 240, 72);
    tft.drawFastHLine(8, 172, 224, C_BORDER);
    if(wxShowOk()) drawWxIcon(46, 205, 30, wxCode);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(C_DIM, C_BG);
    if(wxShowOk()){
      const WxJpg* ti30 = wxJpgFind(WX_BIG, WX_BIG_N, "temperature");
      const WxJpg* hu30 = wxJpgFind(WX_BIG, WX_BIG_N, "humidity");
      drawWxJpg30(ti30, 84, 175);
      drawWxJpg30(hu30, 84, 209);
      tft.setTextColor(C_TXT, C_BG);  tft.drawString(signedT(wxFeels), 118, 190, 2);
      tft.setTextColor(C_TXT, C_BG);  tft.drawString(String((int)wxHum) + "%", 118, 218, 2);
      tft.setTextColor(C_TXT, C_BG);
      if(gfxFont())
        txt(164, 190, String(LG("ветер ", "wind ", "viento ", "风 ")) + String((int)wxWind)
                      + LG(" км/ч", "km", " km/h", " km/h"), C_TXT, 16);
      else
        tft.drawString("wind " + String((int)wxWind) + "km", 164, 190, 2);
      tft.setTextColor(C_DIM, C_BG);
      String ago = wxAt ? String((millis() - wxAt) / 1000) + LG("с назад", "s ago", "s atrás", "秒前")
                        : String("-");
      txt(164, 210, ago, C_DIM, 16);      // TL top = old ML middle (218) minus 8
    } else {
      tft.setTextColor(C_DOWN, C_BG);
      txt(84, 190, wxErr.length() ? cutB(errLoc(wxErr), 14)
                                  : String(LG("ожидание", "wait", "espera", "等待")), C_DOWN, 16);
    }
  }
}

// ---------------------------------------------------------------- forecast theme
static void drawForecast(uint8_t m){
  if(m & RM_FULL) m |= RM_HEAD | RM_PRICE | RM_CHART | RM_FOOT;
  if(m & RM_HEAD){
    bgFillRect(0, 0, 240, 54);
    txtR(232, 6, LG("5 дней", "5 days", "5 días", "5 天"), C_DIM, 16);
    txtC(120, 36, wxShowOk() ? wxCity : cfg.city, C_CYAN, 26);
  }
  if(m & RM_PRICE || m & RM_CHART){
    bgFillRect(0, 54, 240, 186);
    if(fcn <= 0){
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(C_DOWN, C_BG);
      txtC(120, 150, wxErr.length() ? cutB(errLoc(wxErr), 20)
                                     : String(LG("загрузка ...", "loading ...", "cargando...", "加载中...")), C_DOWN, 26);
      return;
    }
    for(int i = 0; i < fcn; i++){
      int cy = 72 + i * 36;
      tft.drawFastHLine(8, 54 + i * 36, 224, C_BORDER);
      DayFc& b = fcd[i];
      tft.setTextDatum(ML_DATUM);
      tft.setTextColor(C_WHITE, C_BG);
      txt(8, cy - 16, b.name, C_WHITE, 16);      // font2 hides Cyrillic; 16px caps
      int dw = txtW(b.name, 16);
      // precipitation right of the day name - the old bottom position was
      // crossed by the next row separator (it read as "broken humidity")
      tft.setTextDatum(TL_DATUM);
      tft.setTextColor(C_CYAN, C_BG);
      tft.drawString(String(b.pop) + "%", 8 + dw + 6, cy - 16, 2);
      drawWxIcon(116, cy, 15, b.code);
      tft.setTextDatum(MR_DATUM);
      tft.setTextColor(C_TXT, C_BG);
      tft.drawString(signedT(b.hi) + "/" + signedT(b.lo), 234, cy, 4);
      tft.setTextDatum(ML_DATUM);
    }
    tft.drawFastHLine(8, 54 + fcn * 36, 224, C_BORDER);
  }
  if(m & RM_FOOT){
    bgFillRect(0, 234, 240, 6);
  }
}

// ---------------------------------------------------------------- calendar
// built-in TFT fonts are ASCII-only: the Cyrillic month / weekday captions
// are pre-rendered 1-bit glyphs (cal_ru.h, generated by gen_cal.ps1) and
// painted pixel by pixel here.
static void drawBmL(const uint8_t* bm, int x0, int yc, uint16_t fg){
  int w = pgm_read_byte(bm), h = pgm_read_byte(bm + 1);
  int y0 = yc - (h >> 1);
  int bpr = (w + 7) >> 3;
  const uint8_t* bits = bm + 2;
  tft.startWrite();
  for(int y = 0; y < h; y++){
    const uint8_t* row = bits + y * bpr;
    for(int x = 0; x < w; x++){
      if(pgm_read_byte(row + (x >> 3)) & (0x80 >> (x & 7)))
        tft.drawPixel(x0 + x, y0 + y, fg);
    }
  }
  tft.endWrite();
}

// pastel title colour by meteorological season (northern hemisphere)
static uint16_t seasonColor(int mon){
  if(mon == 11 || mon <= 1) return 0x9E3C;   // Dec-Feb: pastel blue   (winter)
  if(mon <= 4)              return 0xAED5;   // Mar-May: pastel green  (spring)
  if(mon <= 7)              return 0xF56A;   // Jun-Aug: pastel salmon (summer)
  return 0xFE20;                             // Sep-Nov: egg-yolk yellow (autumn)
}

static void drawCalendar(uint8_t m){
  time_t t = time(nullptr);
  struct tm ti;
  localtime_r(&t, &ti);
  bool tok = (t > 1600000000UL);            // NTP not synced yet
  // Repaint only when the grid really changes (day, holidays, hol toggle).
  // Minute/price/weather marks fire every minute and used to full-redraw the
  // whole screen each time - the calendar visibly flickered several times a
  // minute. RM_FULL (theme/lang/bg switches) still forces the redraw.
  static uint32_t lastKey = 0xFFFFFFFFUL;
  uint32_t key = tok ? (((uint32_t)(ti.tm_year + 1900) << 16) |
                        ((uint32_t)(ti.tm_mon + 1) << 8) |
                         (uint32_t)ti.tm_mday) : 0;
  key = key * 131u + holVer +
        (cfg.hol ? 0x80000000UL : 0) + (holOk ? 0x40000000UL : 0);
  if(!(m & RM_FULL) && key == lastKey) return;
  lastKey = key;
  bgFillRect(0, 0, 240, 240);
  int yr = ti.tm_year + 1900;
  tft.setTextDatum(MC_DATUM);
  if(tok){
    uint16_t tc = seasonColor(ti.tm_mon);
    tft.setTextColor(tc, C_BG);
    String ys(yr);
    int yw = tft.textWidth(ys, 4);
    if(ru()){
      const uint8_t* mb = (const uint8_t*)pgm_read_ptr(&CAL_M[ti.tm_mon]);
      int mw = pgm_read_byte(mb);
      int x0 = 120 - (mw + 14 + yw) / 2;
      drawBmL(mb, x0, 17, tc);
      tft.drawString(ys, x0 + mw + 14 + yw / 2, 17, 4);
    } else {
      // cal_ru.h bitmaps are Russian-only - draw the month as a caption
      // instead (short form if too wide)
      String a, b;                       // a = left part, b = right part
      switch(langIdx()){
        case 2: { static const char* MF[12] = {"enero", "febrero", "marzo", "abril",
                                               "mayo", "junio", "julio", "agosto",
                                               "septiembre", "octubre", "noviembre", "diciembre"};
                  a = MF[ti.tm_mon]; b = ys; break; }
        case 3: a = ys + "年"; b = String(ti.tm_mon + 1) + "月"; break;   // "2026年7月"
        default: { static const char* MF[12] = {"January", "February", "March", "April",
                                                "May", "June", "July", "August",
                                                "September", "October", "November", "December"};
                   a = MF[ti.tm_mon]; b = ys; break; }
      }
      int aw = txtW(a, 26);
      if(langIdx() != 3 && aw + 14 + txtW(b, 26) > 224){
        a = a.substring(0, 3);            // "September" -> "Sep"
        aw = txtW(a, 26);
      }
      int bw = txtW(b, 26);
      int x0 = 120 - (aw + 14 + bw) / 2;
      txtC(x0 + aw / 2, 17, a, tc, 26);
      txtC(x0 + aw + 14 + bw / 2, 17, b, tc, 26);
    }
  } else {
    tft.setTextColor(C_WHITE, C_BG);
    tft.drawString("--", 120, 17, 4);
  }
  tft.drawFastHLine(8, 33, 224, C_WHITE);    // title / weekdays separator
  for(int c = 0; c < 7; c++){
    uint16_t fg = c == 5 ? C_ORANGE : (c == 6 ? C_DOWN : C_TXT);
    if(ru()){
      const uint8_t* wb = (const uint8_t*)pgm_read_ptr(&CAL_W[c]);
      drawBmL(wb, 24 + c * 32 - (pgm_read_byte(wb) >> 1), 45, fg);
    } else {
      static const char* EW[7]  = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
      static const char* ESW[7] = {"lun", "mar", "mié", "jue", "vie", "sáb", "dom"};
      static const char* ZHW[7] = {"一", "二", "三", "四", "五", "六", "日"};
      const char* s = (langIdx() == 2) ? ESW[c] : (langIdx() == 3) ? ZHW[c] : EW[c];
      txtC(24 + c * 32, 45, s, fg, 16);
    }
  }
  tft.drawFastHLine(8, 55, 224, C_WHITE);    // weekdays / dates separator
  if(!tok) return;
  int dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if((yr % 4 == 0 && yr % 100 != 0) || yr % 400 == 0) dim[1] = 29;
  int nd = dim[ti.tm_mon];
  // weekday of the 1st, Monday-first, derived from today (no mktime needed)
  int firstCol = (((ti.tm_wday + 6) % 7) - ((ti.tm_mday - 1) % 7) + 7) % 7;
  bool useHol = cfg.hol && holOk && holYear == (uint16_t)yr;
  for(int d = 1; d <= nd; d++){
    int idx = firstCol + d - 1;
    int row = idx / 7, col = idx % 7;
    if(row > 5) break;
    int cx = 24 + col * 32;
    int cy = 72 + row * 30;
    bool red = (col >= 5) || (useHol && holBit(ti.tm_mon + 1, d));
    String ds(d);
    if(red){
      // bold: two transparent passes 1px apart - a bg-filled draw would erase
      // the first pass instead of thickening it (fg == bg disables the fill)
      tft.setTextColor(C_DOWN, C_DOWN);
      tft.drawString(ds, cx, cy, 2);
      tft.drawString(ds, cx + 1, cy, 2);
    } else {
      tft.setTextColor(C_WHITE, C_BG);
      tft.drawString(ds, cx, cy, 2);
    }
    if(d == ti.tm_mday){                    // today: green 24x24 square frame
      tft.drawRect(cx - 12, cy - 12, 24, 24, C_UP);
      tft.drawRect(cx - 11, cy - 11, 22, 22, C_UP);
    }
  }
}

// ---------------------------------------------------------------- photo theme
// One image: the browser normalizes whatever the user picks (JPEG/PNG/WebP/
// GIF...) to a single JPEG, the device cover-fits it onto the 240x240 screen.
// Multi-frame GIF animation was removed entirely.
#define PH_JPG  "/photo.jpg"
#define PH_META "/phmeta"          // "1 w h"
#define PH_TMP  "/phup.tmp"
#define PH_AMAX 60                 // legacy: only to clean up old /anim frames

static uint8_t  phKind = 0;        // 0 = none, 1 = photo
static int      phW = 0, phH = 0;  // photo size
static uint32_t phSz = 0;          // bytes stored in LittleFS

static String phPath(int i){
  char b[20];
  snprintf(b, sizeof(b), "/anim/f%03d.jpg", i);
  return String(b);
}

static void phWipe(){
  LittleFS.remove(PH_JPG);
  LittleFS.remove(PH_META);
  for(int i = 0; i < PH_AMAX; i++) LittleFS.remove(phPath(i));
}

// walk the JPEG markers up to the frame header: real dimensions + progressive
// flag (TJpg only decodes baseline, so SOF2 must be rejected at upload time)
static bool jpgDims(File& f, int& w, int& h, bool& prog){
  w = h = 0;
  prog = false;
  uint8_t m[8];
  if(!f.seek(0)) return false;
  if(f.read(m, 2) != 2 || m[0] != 0xFF || m[1] != 0xD8) return false;
  uint32_t pos = 2;
  while(pos < 262144UL){          // SOF always lives before the scan data
    if(!f.seek(pos)) return false;
    if(f.read(m, 1) != 1) return false;
    pos++;
    if(m[0] != 0xFF) continue;    // stray byte, keep syncing
    uint8_t c;
    do {
      if(f.read(m, 1) != 1) return false;
      pos++;
      c = m[0];
    } while(c == 0xFF);           // fill bytes after 0xFF
    if(c == 0xD8 || c == 0x01 || (c >= 0xD0 && c <= 0xD7)) continue;  // no length
    if(c == 0xD9) return false;   // EOI before any frame header
    if(f.read(m, 2) != 2) return false;
    uint16_t seg = ((uint16_t)m[0] << 8) | m[1];
    if(seg < 2) return false;
    bool sof = (c >= 0xC0 && c <= 0xCF && c != 0xC4 && c != 0xC8 && c != 0xCC);
    if(sof){
      if(f.read(m, 5) != 5) return false;          // precision, H(2), W(2)
      h = ((int)m[1] << 8) | m[2];
      w = ((int)m[3] << 8) | m[4];
      prog = (c == 0xC2);
      return (w > 0 && h > 0);
    }
    pos += seg;                   // seg counts its own 2 length bytes
  }
  return false;
}

static void loadPhotoMeta(){
  phKind = 0; phW = phH = 0; phSz = 0;
  File f = LittleFS.open(PH_META, "r");
  if(!f) return;
  String s = f.readStringUntil('\n');
  f.close();
  int p1 = s.indexOf(' ');
  if(p1 < 0) return;
  int k = s.substring(0, p1).toInt();
  if(k == 1){
    int p2 = s.indexOf(' ', p1 + 1);
    if(p2 < 0) return;
    phW = s.substring(p1 + 1, p2).toInt();
    phH = s.substring(p2 + 1).toInt();
    File jf = LittleFS.open(PH_JPG, "r");
    if(jf){ phSz = jf.size(); jf.close(); }
    if(phW > 0 && phH > 0 && phSz) phKind = 1;
  } else if(k == 2){
    phWipe();                        // legacy animation: drop it, photos only now
  }
}

static void drawPhoto(uint8_t m){
  if(!(m & RM_FULL)) return;               // repaint only on full redraws
  if(phKind == 1){
    // cover-fit: shrink by powers of two until the short side reaches the
    // screen, then center-crop (pushImage clips the overflow)
    int w = phW, h = phH, sc = 1;
    while(sc < 8 && (w / sc < 240 || h / sc < 240)) sc <<= 1;
    if(w / sc < 240 && h / sc < 240) sc = 1;  // tiny picture: never shrink
    int ox = (240 - w / sc) / 2;
    int oy = (240 - h / sc) / 2;
    jpgPlain = true;
    TJpgDec.setJpgScale(sc);
    TJpgDec.drawFsJpg(ox, oy, PH_JPG, LittleFS);
    TJpgDec.setJpgScale(1);
    jpgPlain = false;
    return;
  }
  tft.setTextDatum(MC_DATUM);
  txtC(120, 104, LG("НЕТ ФОТО", "NO PHOTO", "SIN FOTO", "无照片"), C_ORANGE, 26);
  txtC(120, 144, LG("загрузите фото", "upload an image", "sube una imagen", "上传图片"), C_TXT, 16);
  txtC(120, 166, LG("в «Темы» > «Фото»", "in Themes > Photo", "en Temas > Foto", "主题 > 照片"), C_TXT, 16);
}

static void handlePhJpg(){
  if(phKind != 1){ server.send(404, "application/json", "{\"error\":\"none\"}"); return; }
  File f = LittleFS.open(PH_JPG, "r");
  if(!f){ server.send(404, "application/json", "{\"error\":\"none\"}"); return; }
  server.sendHeader("Cache-Control", "no-store");
  server.streamFile(f, "image/jpeg");
  f.close();
}

static void drawSetupScreen(){
  bgFillScreen();
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  // first-time steps; font3 is not compiled in this TFT_eSPI build so font4
  // (26px) is the step up from font2 (16px); widths measured with the font
  // width tables - all lines fit the 240px panel, vertical gaps kept tight.
  tft.setTextFont(4);
  tft.setTextColor(0xFFFF, C_BG);
  tft.drawString("1. Join network", tft.width() / 2, 25);
  tft.setTextColor(C_UP, C_BG);
  tft.drawString("BroClock-PRO", tft.width() / 2, 60);
  tft.setTextFont(2);
  tft.setTextColor(0xF800, C_BG);
  tft.drawString("Setup page opens by itself", tft.width() / 2, 92);
  tft.drawString(String("for manual opening ") + WiFi.softAPIP().toString(),
                 tft.width() / 2, 117);
  tft.setTextFont(4);
  tft.setTextColor(0xFFFF, C_BG);
  tft.drawString("2. Connect your", tft.width() / 2, 154);
  tft.setTextColor(0xFFE0, C_BG);
  tft.drawString("BroClock-PRO", tft.width() / 2, 191);
  tft.setTextFont(2);
  tft.setTextColor(0xF800, C_BG);
  tft.drawString("to WiFi", tft.width() / 2, 223);
}

static void render(){
  // no saved network: the setup screen owns the panel, drop any redraw marks
  if(!cfg.wifiSsid.length()){ rm = 0; dirty = false; return; }
  ESP.wdtFeed();
  uint8_t m = rm;
  if(m & RM_FULL) bgFillScreen();
  if(cfg.theme == "ticker")      drawTicker(m);
  else if(cfg.theme == "crypto3") drawCrypto3(m);
  else if(cfg.theme == "calendar") drawCalendar(m);
  else if(cfg.theme == "flip")   drawFlip(m);
  else if(cfg.theme == "weather") drawWeather(m);
  else if(cfg.theme == "forecast") drawForecast(m);
  else if(cfg.theme == "photo")  drawPhoto(m);
  else                           drawClassic(m);
  ESP.wdtFeed();
  rm = 0;
  dirty = false;
}

// ---------------------------------------------------------------- web
static const char PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>BroClock Pro</title>
<style>body{font-family:system-ui;background:#0b0e14;color:#e8e8e8;margin:0;padding:14px}
h1{font-size:19px;margin:0 0 4px}h2{font-size:13px;color:#7fa;margin:0 0 8px;text-transform:uppercase}
.c{background:#151a22;border:1px solid #232a35;border-radius:10px;padding:13px;margin:11px 0}
.grp{background:#0f131a;border:1px solid #2a3342;border-radius:9px;padding:2px 12px 11px;margin-top:12px}
.gh{font-size:12px;color:#7fa;font-weight:700;text-transform:uppercase;letter-spacing:.4px;border-bottom:1px dashed #2a3342;padding:9px 0 7px;margin:0 0 2px}
label{display:block;color:#8ab;font-size:12px;margin:9px 0 3px}
select,input{width:100%;padding:9px;border-radius:6px;border:1px solid #333;background:#0f131a;color:#eee;font-size:15px;box-sizing:border-box}
button{width:100%;padding:11px;border:0;border-radius:6px;background:#2b6;color:#042;font-weight:700;font-size:15px;margin-top:13px}
button:disabled{opacity:.55;cursor:default}
#wssid[readonly]{opacity:.65;cursor:default}
a{color:#6cf}b{color:#fff}
.row{display:flex;gap:9px;align-items:stretch}.row>div{flex:1;min-width:0}.row>button{margin-top:0}
.row input,.row select{box-sizing:border-box;min-width:0;padding-left:6px;padding-right:6px}
input[type=time]{min-width:0;padding-left:5px;padding-right:5px}
.pwr{display:flex;gap:7px;align-items:stretch}
.pwr input{flex:1;min-width:0}
.eye{flex:0 0 auto;width:40px;padding:0;margin:0;border:1px solid #333;border-radius:6px;background:#0f131a;cursor:pointer;display:flex;align-items:center;justify-content:center;-webkit-tap-highlight-color:transparent}
.eye:active{border-color:#2b6}
.tl{display:flex;flex-direction:column;gap:2px}
.cb{display:flex;align-items:center;gap:11px;padding:9px 10px;background:#0f131a;
 border:1px solid #232a35;border-radius:7px;color:#dde;font-size:15px;margin:5px 0;cursor:pointer}
.cb input{width:21px;height:21px;accent-color:#34c759;margin:0;flex:0 0 auto}
.cb.cur{border-color:#34c759;box-shadow:0 0 0 1px #34c759 inset}
.cb em{margin-left:auto;font-style:normal;color:#7fa;font-size:12px;text-transform:uppercase}
input[type=range]{padding:0;height:26px;accent-color:#2b6}
.sr{margin-top:5px}
.dim{color:#678;font-size:13px;padding:6px 2px}
.qr{display:flex;gap:9px;align-items:center;padding:9px 10px;background:#0f131a;
 border:1px solid #232a35;border-radius:6px;margin-top:4px;cursor:pointer;font-size:14px}
.qr:hover{border-color:#2b6}.qr.sel{border-color:#2b6;background:#15222e;box-shadow:0 0 0 1px #2b6 inset}.qr b{color:#2b6}.qr span{color:#cdd}.qr i{margin-left:auto;font-style:normal;color:#678;font-size:12px}
.chips{display:flex;flex-wrap:wrap;gap:7px;margin-top:4px}
.chip{background:#0f131a;border:1px solid #2b6;border-radius:14px;padding:6px 11px;font-size:14px;color:#dfe}
.chip b{cursor:pointer;color:#f66;padding-left:7px;font-weight:700}
button.sm{padding:9px;font-size:13px;background:#23304a;color:#9cf;font-weight:600}
.lay{display:flex;gap:14px;align-items:flex-start;max-width:980px;margin:0 auto}
nav{flex:0 0 174px;background:#151a22;border:1px solid #232a35;border-radius:10px;
 padding:10px;position:sticky;top:14px}
nav .lg{font-weight:700;color:#7fa;font-size:17px;padding:6px 8px 12px}
nav .lg span{display:block;font-weight:400;color:#678;font-size:10px;
 text-transform:uppercase;letter-spacing:1px}
nav a{display:block;padding:10px 11px;border-radius:7px;color:#9ab;text-decoration:none;
 font-size:15px;margin:2px 0;cursor:pointer}
nav a:hover{background:#0f131a;color:#dff}
nav a.on{background:#2b6;color:#042;font-weight:700}
main{flex:1;min-width:0}
.bar{height:15px;background:#0f131a;border:1px solid #232a35;border-radius:6px;
 overflow:hidden;margin-top:9px}
.bar i{display:block;height:100%;width:0;background:#2b6;transition:width .15s}
.gifs{display:flex;gap:9px;flex-wrap:wrap;margin-top:6px}
.gifpv{cursor:pointer;text-align:center;padding:6px;background:#0f131a;border:1px solid #232a35;border-radius:8px}
.gifpv.on{border-color:#2b6;box-shadow:0 0 0 1px #2b6 inset}
.gifpv span{display:block;color:#678;font-size:11px;margin-top:4px}
.gifpv canvas{display:block;image-rendering:pixelated;width:80px;height:80px}
#wform{margin-top:9px;padding:9px;background:#0f131a;border:1px solid #2b6;border-radius:7px}
.bgsw{display:flex;gap:10px;align-items:center;flex-wrap:wrap}
.bgswat{width:52px;height:52px;border-radius:9px;border:2px solid #2b6;cursor:pointer;
 box-shadow:0 0 0 1px #333 inset;flex:0 0 auto;display:flex;align-items:flex-end;justify-content:center;overflow:hidden}
.bgswat.on,.bgswpal.on{outline:2px solid #6cf;outline-offset:1px}
.bgswat i,.bgswpal i{display:block;font-style:normal;font-size:9px;color:#fff;white-space:nowrap;
 text-shadow:0 1px 2px #000;padding:2px 3px;background:rgba(0,0,0,.45);border-radius:4px;margin:0 0 3px}
.bgswpal{width:52px;height:52px;border-radius:50%;border:2px solid #2b6;cursor:pointer;
 box-shadow:0 0 0 1px #333 inset;flex:0 0 auto;display:flex;align-items:flex-end;justify-content:center;overflow:hidden;position:relative}
.bglive{position:relative}
.bglive input[type=color]{position:absolute;left:0;top:0;width:100%;height:100%;
 opacity:0;padding:0;border:0;cursor:pointer;z-index:3}
.bglive i{position:relative;z-index:2}
@media(max-width:760px){
 .lay{flex-direction:column}
 nav{position:static;flex:none;width:100%;display:flex;flex-wrap:wrap;gap:4px;padding:8px}
 nav .lg{width:100%;padding:4px 8px 8px}
 nav a{margin:0;flex:1 1 auto;text-align:center;white-space:nowrap;padding:9px 6px}
}</style>
<script>
var LANG=(function(){try{return localStorage.getItem('sdp_lang')}catch(e){return null}})()||'en';
var LANGS=['en','es','zh'];
/* static UI vocabulary: ru text -> [en, es, zh].  i18n() walks the document and
   swaps every text node / title / placeholder that matches a key here, so the
   markup stays Russian by default and flips instantly with the language. */
var T={
'12 часов (AM/PM)':['12 hours (AM/PM)','12 horas (AM/PM)','12小时 (AM/PM)'],
'24 часа':['24 hours','24 horas','24小时'],
'Open-Meteo (без ключа, город и с русским названием)':['Open-Meteo (no key; the city may be typed in any language)','Open-Meteo (sin clave; la ciudad puede escribirse en cualquier idioma)','Open-Meteo（无需密钥，城市可用任意语言输入）'],
'POSIX-строка применяется сразу; синхронизация — NTP (пул UTC).':['The POSIX string applies right away; time syncs via NTP (UTC pool).','La cadena POSIX se aplica al instante; la hora se sincroniza por NTP (pool UTC).','POSIX 字符串立即生效；时间通过 NTP（UTC 池）同步。'],
'Австралия':['Australia','Australia','澳大利亚'],'Австрия':['Austria','Austria','奥地利'],
'Авто — определить по гео':['Auto — detect by geolocation','Automático — detectar por geolocalización','自动 — 按定位确定'],
'Анимация':['Animation','Animación','动画'],'Анимация в правом углу (тема Classic)':['Corner animation (Classic theme)','Animación en la esquina (tema Clásico)','角落动画（经典主题）'],
'Аргентина':['Argentina','Argentina','阿根廷'],'Беларусь':['Belarus','Bielorrusia','白俄罗斯'],
'Бельгия':['Belgium','Bélgica','比利时'],'Болгария':['Bulgaria','Bulgaria','保加利亚'],
'Бразилия':['Brazil','Brasil','巴西'],'Введите свой город':['Enter your city','Introduce tu ciudad','输入您的城市'],
'Великобритания':['United Kingdom','Reino Unido','英国'],
'Венгрия':['Hungary','Hungría','匈牙利'],
'Вторичный рынок · до 3 монет':['Secondary market · up to 3 coins','Mercado secundario · hasta 3 monedas','二级市场 · 最多3种币'],
'Выбрать файл…':['Choose file…','Elegir archivo…','选择文件…'],
'Выделять праздничные дни красным':['Highlight holidays in red','Resaltar los días festivos en rojo','节日以红色高亮'],
'Германия':['Germany','Alemania','德国'],'Город':['City','Ciudad','城市'],'График':['Chart','Gráfico','图表'],
'Греция':['Greece','Grecia','希腊'],'Дания':['Denmark','Dinamarca','丹麦'],
'Забыть сохранённую сеть':['Forget saved network','Olvidar la red guardada','忘记已保存的网络'],
'Загрузить':['Upload','Subir','上传'],
'Загрузка .bin прошивки по воздуху. Конфигурация и данные не трогаются. Принимаются только образы этой сборки — в файле ищется отпечаток (чип, flash, пины дисплея); чужая прошивка отклоняется без записи, старая продолжит работать.':['Upload a .bin firmware over the air. Configuration and data stay untouched. Only images of this build are accepted — the file is checked for a fingerprint (chip, flash, display pins); a foreign build is rejected without writing and the old firmware keeps running.','Sube un .bin por el aire. La configuración y los datos no se tocan. Solo se aceptan imágenes de esta compilación: el archivo se comprueba con una huella (chip, flash, pines de pantalla); otra compilación se rechaza sin escribir y la antigua seguirá funcionando.','通过空中传输上传 .bin 固件。配置和数据不受影响。仅接受本构建的镜像——文件会校验指纹（芯片、Flash、显示引脚）；其他固件不写入即被拒绝，旧固件继续运行。'],
'Израиль':['Israel','Israel','以色列'],
'Имя сети (SSID)':['Network name (SSID)','Nombre de la red (SSID)','网络名称 (SSID)'],
'Индия':['India','India','印度'],
'Интервал карусели, сек':['Carousel interval, sec','Intervalo del carrusel, seg','轮播间隔，秒'],
'Ирландия':['Ireland','Irlanda','爱尔兰'],'Исландия':['Iceland','Islandia','冰岛'],
'Испания':['Spain','España','西班牙'],'Источник данных':['Data source','Fuente de datos','数据源'],
'Италия':['Italy','Italia','意大利'],'Казахстан':['Kazakhstan','Kazajistán','哈萨克斯坦'],
'Календарь':['Calendar','Calendario','日历'],'Календарь: государственные праздники':['Calendar: public holidays','Calendario: festivos públicos','日历：法定假日'],
'Канада':['Canada','Canadá','加拿大'],
'Карусель (переключение тем)':['Carousel (theme rotation)','Carrusel (rotación de temas)','轮播（主题切换）'],
'Карусель тем':['Theme carousel','Carrusel de temas','主题轮播'],
'Китай':['China','China','中国'],
'Клик по результату добавляет монету. При трёх монетах самая старая заменяется.':['Click a result to add the coin. With three coins the oldest one is replaced.','Al hacer clic en un resultado se añade la moneda. Con tres monedas la más antigua se reemplaza.','点击结果可添加币种。已有三种时会替换最早的一种。'],
'Корея':['South Korea','Corea del Sur','韩国'],'Латвия':['Latvia','Letonia','拉脱维亚'],
'Линия':['Line','Línea','线'],'Литва':['Lithuania','Lituania','立陶宛'],'Мексика':['Mexico','México','墨西哥'],
'Молдова':['Moldova','Moldavia','摩尔多瓦'],
'Монета: поиск по каталогу (топ-250 CoinGecko)':['Coin: search the catalog (CoinGecko top-250)','Moneda: buscar en el catálogo (top-250 de CoinGecko)','币种：在目录中搜索（CoinGecko 前250）'],
'Моё местоположение':['My location','Mi ubicación','我的位置'],
'Найти и добавить монету':['Find and add a coin','Buscar y añadir una moneda','查找并添加币种'],
'Настройки':['Settings','Ajustes','设置'],
'Начало':['Start','Inicio','开始'],'Нидерланды':['Netherlands','Países Bajos','荷兰'],
'Норвегия':['Norway','Noruega','挪威'],'Ночная яркость':['Night brightness','Brillo nocturno','夜间亮度'],
'Ночной режим подсветки':['Backlight night mode','Modo nocturno de la luz','背光夜间模式'],
'Обновить данные сейчас':['Refresh data now','Actualizar los datos ahora','立即刷新数据'],
'Обновить погоду сейчас':['Refresh weather now','Actualizar el clima ahora','立即刷新天气'],
 'Обновление…':['Refreshing…','Actualizando…','刷新中…'],
'Обновить прошивку':['Update firmware','Actualizar el firmware','更新固件'],
'Обновление данных, сек':['Data refresh, sec','Actualización de datos, seg','数据刷新，秒'],
'Окончание':['End','Fin','结束'],
'Отметьте темы, которые участвуют в карусели':['Tick the themes that take part in the carousel','Marca los temas que participan en el carrusel','勾选参与轮播的主题'],
'Палитра':['Palette','Paleta','调色板'],'Пароль':['Password','Contraseña','密码'],
'Погода':['Weather','Clima','天气'],
'Погода: ошибка обновления':['Weather: update failed','Clima: error de actualización','天气：更新失败'],
 'Погода: данные загружаются…':['Weather: loading…','Clima: cargando…','天气：加载中…'],
'Погода обновлена':['Weather updated','Clima actualizado','天气已更新'],
'Котировки: ошибка обновления':['Market data: error','Datos de mercado: error','行情：更新失败'],
 'Котировки: данные загружаются…':['Market data: loading…','Datos de mercado: cargando…','行情：加载中…'],
'Котировки обновлены':['Market data updated','Datos de mercado actualizados','行情已更新'],
'Погода обновляется автоматически каждые 10 минут.':['Weather refreshes automatically every 10 minutes.','El clima se actualiza automáticamente cada 10 minutos.','天气每10分钟自动刷新。'],
'Подключено':['Connected','Conectado','已连接'],
'Подсветка':['Backlight','Iluminación','背光'],'Польша':['Poland','Polonia','波兰'],'Португалия':['Portugal','Portugal','葡萄牙'],
'Праздники берутся с date.nager.at один раз в год и хранятся на карте; страна «Авто» определяется по внешнему IP.':['Holidays come from date.nager.at once a year and are stored on the card; the «Auto» country is resolved from your external IP.','Los festivos provienen de date.nager.at una vez al año y se guardan en la tarjeta; el país «Automático» se resuelve por tu IP externa.','假日数据每年一次取自 date.nager.at 并保存在卡上；「自动」国家由外部 IP 确定。'],
'Прогноз':['Forecast','Pronóstico','预报'],'Прошивка':['Firmware','Firmware','固件'],
'Россия':['Russia','Rusia','俄罗斯'],'Румыния':['Romania','Rumanía','罗马尼亚'],'США':['USA','EE. UU.','美国'],
'Свечи':['Candles','Velas','K线'],'Сербия':['Serbia','Serbia','塞尔维亚'],'Синий':['Blue','Azul','蓝色'],
'Сканировать сети':['Scan networks','Escanear redes','扫描网络'],'Словакия':['Slovakia','Eslovaquia','斯洛伐克'],
'Снижать яркость в указанные часы':['Lower brightness during the set hours','Reducir el brillo en las horas indicadas','在指定时间降低亮度'],
'Сохранить и подключить':['Save and connect','Guardar y conectar','保存并连接'],
'Сохранённая сеть подключается автоматически при старте.':['The saved network connects automatically at boot.','La red guardada se conecta automáticamente al arrancar.','保存的网络会在启动时自动连接。'],
'Страна праздников':['Holiday country','País de los festivos','假日国家'],
'Тема Фото — картинка или анимация':['Photo theme — image or animation','Tema Foto — imagen o animación','照片主题 — 图片或动图'],
'Темы':['Themes','Temas','主题'],'Турция':['Turkey','Turquía','土耳其'],
'Тёмно-серый':['Dark grey','Gris oscuro','深灰色'],'Удалить':['Delete','Eliminar','删除'],
'Указать id вручную':['Enter id manually','Escribir el id a mano','手动输入 id'],
'Украина':['Ukraine','Ucrania','乌克兰'],
'Файл прошивки .bin':['Firmware file .bin','Archivo de firmware .bin','固件文件 .bin'],
'Финляндия':['Finland','Finlandia','芬兰'],'Формат часов':['Time format','Formato de hora','时间格式'],
'Форматы:':['Formats:','Formatos:','格式：'],'Фото':['Photo','Foto','照片'],'Франция':['France','Francia','法国'],
'Цвет фона экрана':['Screen background colour','Color de fondo de la pantalla','屏幕背景色'],
'Часовой пояс':['Time zone','Zona horaria','时区'],'Часы и язык':['Clock & language','Reloj e idioma','时钟和语言'],'Чехия':['Czechia','Chequia','捷克'],
'Чили':['Chile','Chile','智利'],'Чёрный':['Black','Negro','黑色'],'Швейцария':['Switzerland','Suiza','瑞士'],
'Швеция':['Sweden','Suecia','瑞典'],'Экран':['Screen','Pantalla','屏幕'],'Эстония':['Estonia','Estonia','爱沙尼亚'],
'ЮАР':['South Africa','Sudáfrica','南非'],
'Язык интерфейса':['Interface language','Idioma de la interfaz','界面语言'],'Япония':['Japan','Japón','日本'],
'Яркость подсветки':['Backlight brightness','Brillo de la luz','背光亮度'],'вкл':['on','Sí','开'],
'выкл':['off','No','关'],
'и др. — изображение автоматически вписывается в экран 240×240 (обрезка по центру) и загружается как JPEG. Лимит файла 256 КБ. Анимация GIF больше не разрезается на кадры: первый кадр сохранится как обычное фото. Новое изображение заменяет предыдущее.':['etc. — the image auto-fits the 240×240 screen (center crop) and uploads as JPEG. File limit 256 KB. GIF animations are no longer split into frames: the first frame is saved as a regular photo. A new image replaces the previous one.','etc. — la imagen se ajusta automáticamente a la pantalla 240×240 (recorte centrado) y se sube como JPEG. Límite de archivo 256 KB. Las animaciones GIF ya no se dividen en fotogramas: el primer fotograma se guarda como foto normal. Una imagen nueva reemplaza a la anterior.','等 — 图片自动适配240×240屏幕（居中裁剪）并作为 JPEG 上传。文件上限256 KB。GIF 动图不再拆分为帧：第一帧保存为普通照片。新图片会替换上一张。'],
'Глубокий синий':['Deep blue','Azul profundo','深蓝色'],
'Custom — ваш цвет; клик открывает палитру':['Custom — your colour; click to open the palette','Personalizado — tu color; haz clic para abrir la paleta','自定义 — 你的颜色；点击打开调色板'],
'Палитра — живой предпросмотр цвета':['Palette — live colour preview','Paleta — vista previa en vivo del color','调色板 — 实时颜色预览'],
'пусто — открытая сеть':['empty — open network','vacía — red abierta','空白 — 开放网络'],
 'пусто — оставить текущий':['empty — keep current','vacía — mantener actual','空白 — 保留当前']
};
var tmap=new WeakMap();
/* ru markup is the source of truth: pick the translated slice for `core` */
function ttr(core){
  if(LANG==='ru')return core;
  var t=T[core];if(!t)return core;
  var i=LANGS.indexOf(LANG);
  return (i>=0&&t[i])?t[i]:core;
}
/* inline 4-language pick for code-built strings: ru, en, es, zh */
function lx(r,e,s,z){return LANG==='en'?e:LANG==='es'?s:LANG==='zh'?z:r;}
function i18n(){
  if(!document.body)return;
  var w=document.createTreeWalker(document.body,NodeFilter.SHOW_TEXT,{acceptNode:function(n){
    var p=n.parentNode;
    if(!p||p.nodeName==='SCRIPT'||p.nodeName==='STYLE')return NodeFilter.FILTER_REJECT;
    return NodeFilter.FILTER_ACCEPT;}});
  var n;
  while(n=w.nextNode()){
    var orig=tmap.get(n);
    if(orig===undefined){
      if(T[n.nodeValue.trim()]===undefined)continue;
      orig=n.nodeValue; tmap.set(n,orig);
    }
    var i0=0; while(i0<orig.length&&/\s/.test(orig[i0]))i0++;
    var i1=orig.length; while(i1>i0&&/\s/.test(orig[i1-1]))i1--;
    var core=orig.slice(i0,i1);
    var v=orig.slice(0,i0)+ttr(core)+orig.slice(i1);
    if(n.nodeValue!==v)n.nodeValue=v;
  }
  var els=document.querySelectorAll('[title],[placeholder]');
  for(var e=0;e<els.length;e++)['title','placeholder'].forEach(function(a){
    var el=els[e];
    if(!el.hasAttribute(a))return;
    var orig=el.getAttribute('data-iorig-'+a);
    if(orig===null){orig=el.getAttribute(a);el.setAttribute('data-iorig-'+a,orig);}
    if(T[orig]===undefined)return;
    var v=ttr(orig);
    if(el.getAttribute(a)!==v)el.setAttribute(a,v);
  });
}
/* dynamic strings (messages, alerts, lists) - written straight in the right
   language at generation time */
var TT={
now:['сейчас','now','ahora','现在'],
alertTheme:['Нужна хотя бы одна тема','Pick at least one theme','Elige al menos un tema','请至少选择一个主题'],
nothing:['ничего не найдено','nothing found','nada encontrado','未找到内容'],
searchErr:['ошибка поиска','search error','error de búsqueda','搜索错误'],
noData:['нет данных','no data','sin datos','无数据'],
customPosix:['свой POSIX','custom POSIX','POSIX personalizado','自定义 POSIX'],
scanning:['Сканирование…','Scanning…','Escaneando…','扫描中…'],
scanBtn:['Сканировать сети','Scan networks','Escanear redes','扫描网络'],
noNets:['сети не найдены','no networks found','no se encontraron redes','未找到网络'],
scanErr:['ошибка сканирования','scan error','error de escaneo','扫描错误'],
geoCoords:['Определяю координаты…','Getting coordinates…','Obteniendo coordenadas…','正在获取坐标…'],
geoIpWait:['Гео браузера запрещено, определяю по IP устройства…','Browser geolocation denied, resolving by device IP…','La geolocalización del navegador está denegada; resolviendo por la IP del dispositivo…','浏览器定位被拒绝，正在通过设备 IP 解析…'],
geoIpFail:['Не удалось определить по IP: ','Could not resolve by IP: ','No se pudo resolver por IP: ','无法通过 IP 解析：'],
geoIpErr:['Ошибка определения по IP','IP lookup failed','Error al resolver por IP','IP 解析失败'],
geoCityOk:['Город определён: ','City resolved: ','Ciudad resuelta: ','已解析城市：'],
geoCityNo:['Город не найден: ','City not found: ','Ciudad no encontrada: ','未找到城市：'],
geoErr:['Ошибка геокодера','Geocoder error','Error del geocodificador','地理编码错误'],
wifiNet:['Сеть: ','Network: ','Red: ','网络：'],
wifiOn:['подключён · IP ','connected · IP ','conectado · IP ','已连接 · IP '],
wifiOff:['не подключён','not connected','no conectado','未连接'],
wifiAp:[' · точка доступа BroClock-PRO активна',' · access point BroClock-PRO active',' · punto de acceso BroClock-PRO activo',' · 热点 BroClock-PRO 已启动'],
wifiSaved:['Сохранено: ','Saved: ','Guardado: ','已保存：'],
wifiAlertName:['Укажите имя сети','Enter the network name','Escribe el nombre de la red','请输入网络名称'],
wifiAlertSaved:['Сохранено. Идёт подключение — устройство сменит адрес, когда подключится.','Saved. Connecting — the device will change its address once connected.','Guardado. Conectando — el dispositivo cambiará de dirección cuando se conecte.','已保存。正在连接 — 设备连接后会更换地址。'],
 wifiForget:['Забыть сохранённую сеть?','Forget the saved network?','¿Olvidar la red guardada?','忘记已保存的网络？'],
 wifiForgetDone:['Устройство забыто. Подключитесь к точке доступа BroClock-PRO (без пароля) и откройте 192.168.4.1','Network forgotten. Connect to the BroClock-PRO hotspot (no password) and open 192.168.4.1','Red olvidada. Conéctate al punto de acceso BroClock-PRO (sin contraseña) y abre 192.168.4.1','网络已忘记 · 请连接热点 BroClock-PRO（无密码） · 192.168.4.1'],
fwPick:['Выберите файл .bin','Choose a .bin file','Elige un archivo .bin','请选择 .bin 文件'],
fwConfirm:['Прошить ','Flash ','¿Flash ','刷写 '],
fwBytes:[' байт)? Экран замрёт примерно на 30 секунд.',' bytes)? The screen freezes for about 30 seconds.',' bytes)? La pantalla se congela unos 30 segundos.',' 字节）？屏幕将冻结约30秒。'],
fwDone:['Готово, устройство перезагружается…','Done, the device is restarting…','Listo, el dispositivo se reinicia…','完成，设备正在重启…'],
fwErr:['Ошибка ','Error ','Error ','错误 '],
linkBreak:['обрыв связи','connection lost','conexión perdida','连接中断'],
phFirst:['Сначала выберите файл','Pick a file first','Elige primero un archivo','请先选择文件'],
phFit:['Подгоняю под экран 240×240…','Fitting to the 240×240 screen…','Ajustando a la pantalla 240×240…','正在适配240×240屏幕…'],
phSend:['Загружаю фото…','Uploading the photo…','Subiendo la foto…','正在上传照片…'],
phOk:['Фото загружено ✓','Photo uploaded ✓','Foto subida ✓','照片已上传 ✓'],
phErr:['Ошибка: ','Error: ','Error: ','错误：'],
phEmpty:['пустое изображение','empty image','imagen vacía','空图片'],
phLoad:['Загрузка: ','Loading: ','Carga: ','加载：'],
phWrite:['ошибка записи','write error','error de escritura','写入错误'],
phFs:['не хватает места на файловой системе','not enough space on the file system','no hay espacio en el sistema de archivos','文件系统空间不足'],
phBig:['файл больше лимита (см. описание выше)','file over the limit (see the note above)','el archivo supera el límite (ver la nota arriba)','文件超出限制（见上方说明）'],
phDims:['разрешение не подходит','resolution not suitable','la resolución no es adecuada','分辨率不合适'],
phProg:['прогрессивный JPEG не поддерживается — пересохраните как baseline','progressive JPEG not supported — re-save as baseline','JPEG progresivo no admitido — vuelve a guardarlo como baseline','不支持渐进式 JPEG — 请另存为基线 JPEG'],
phFormat:['не удалось прочитать изображение','could not read the image','no se pudo leer la imagen','无法读取图片'],
phRename:['ошибка сохранения','save error','error al guardar','保存错误'],
phFrames:['анимации больше не поддерживаются — обновите страницу','animations are no longer supported — refresh the page','las animaciones ya no se admiten — actualiza la página','不再支持动图 — 请刷新页面'],
phEmptyFile:['файл не получен','file not received','archivo no recibido','未收到文件'],
phAbort:['загрузка прервана','upload aborted','carga interrumpida','上传中止'],
network:['сеть','network','red','网络']
};
function tt(k){var e=TT[k];if(!e)return k;var i=(LANG==='ru')?0:LANGS.indexOf(LANG)+1;
  return (i>0&&e[i])?e[i]:e[1];}
function j(u){return fetch(u).then(r=>r.json())}
function st(){j('/api/status').then(s=>{applySettings(s);renderWifi(s);});}
setInterval(st,5000);st();
function doRefresh(b){
  if(b.disabled)return;
  var o=b.textContent;b.disabled=true;
  b.textContent=ttr('Обновление…');
  fetch('/api/refresh').then(r=>r.json()).catch(function(){}).then(function(){
    var n=0,iv=setInterval(function(){st();if(++n>=6){clearInterval(iv);b.disabled=false;b.textContent=o;}},1500);
  });
}
function set(k,v){fetch('/api/set?key='+k+'&value='+encodeURIComponent(v)).then(r=>r.json()).then(x=>{if(x.need)location.reload();else st()})}
var tlDrawn=false;
function syncThemes(list,cur){
  if(!list||!document.getElementById('tl'))return;
  if(!tlDrawn){
    var h='';
    for(var i=0;i<list.length;i++)
      h+='<label class=cb id="cb_'+list[i].key+'"><input type=checkbox data-k="'+list[i].key+
         '"'+(list[i].on?' checked':'')+' onchange="themeChanged()"><span>'+list[i].name+'</span>'+
         '<em>'+(list[i].key==cur?tt('now'):'')+'</em></label>';
    document.getElementById('tl').innerHTML=h; tlDrawn=true; return;
  }
  for(var i=0;i<list.length;i++){
    var el=document.querySelector('#tl input[data-k="'+list[i].key+'"]');
    if(el)el.checked=list[i].on;
    var lb=document.getElementById('cb_'+list[i].key);
    if(lb){var em=lb.querySelector('em'); if(em)em.textContent=(list[i].key==cur?tt('now'):'');}
  }
}
function themeChanged(){
  var els=document.querySelectorAll('#tl input'),on=[];
  for(var i=0;i<els.length;i++) if(els[i].checked) on.push(els[i].getAttribute('data-k'));
  if(!on.length){alert(tt('alertTheme'));return;}
  set('themes',on.join(','));
}
var brTouched=false, selTouched=false, colTouched=false, symsCsv='', sTimers={}, cityList=[], cityPicked=false;
var gifDrawn=false, gifPvCur=-1;
function drawGifs(cur){
  var el=document.getElementById('gifpv'); if(!el)return;
  gifPvCur=cur;
  if(!gifDrawn){
    gifDrawn=true;
    var h='';
    for(var i=0;i<4;i++)
      h+='<div class="gifpv" data-i="'+i+'"><canvas id="gifc'+i+'" width=80 height=80></canvas><span>'+(i+1)+'</span></div>';
    el.innerHTML=h;
    el.addEventListener('click',function(e){
      var d=e.target.closest?e.target.closest('.gifpv'):null;
      if(!d)return;
      set('gifnum',d.getAttribute('data-i'));
    });
    var g=0;
    (function next(){if(g>=4)return;loadOne(g);g++;setTimeout(next,80);})();
  }else{
    var items=el.querySelectorAll('.gifpv');
    for(var i=0;i<items.length;i++)
      items[i].className='gifpv'+(i==cur?' on':'');
  }
  function loadOne(g){
    fetch('/api/anim?gif='+g+'&frame=0').then(function(r){return r.arrayBuffer()}).then(function(b){
      var u=new Uint16Array(b),cv=document.getElementById('gifc'+g);
      if(!cv){return;}
      var ctx=cv.getContext('2d'),id=ctx.createImageData(80,80);
      var bg=document.getElementById('bgc').value||'#000410';
      var br=parseInt(bg.slice(1,3),16),gg=parseInt(bg.slice(3,5),16),bb=parseInt(bg.slice(5,7),16);
      for(var i=0;i<6400;i++){
        var v=u[i],r=(v>>11)&31,gt=(v>>5)&63,bl=v&31;
        if(v===0x0822){r=br;gt=gg;bl=bb}
        id.data[i*4]=(r<<3)|(r>>2);
        id.data[i*4+1]=(gt<<2)|(gt>>4);
        id.data[i*4+2]=(bl<<3)|(bl>>2);
        id.data[i*4+3]=255;
      }
      ctx.putImageData(id,0,0);
    }).catch(function(){});
  }
}
function qSearch(t){
  var inp=document.getElementById('q_'+t), box=document.getElementById('r_'+t);
  if(!inp||!box)return;
  var v=inp.value.trim();
  if(sTimers[t])clearTimeout(sTimers[t]);
  if(v.length<2){box.innerHTML='';return;}
  sTimers[t]=setTimeout(function(){
    fetch('/api/search?q='+encodeURIComponent(v)).then(function(r){return r.json()}).then(function(j){
      var h='',cs=(j&&j.coins)||[];
      for(var i=0;i<cs.length;i++){
        var c=cs[i];
        h+='<div class=qr onclick="pick(\''+t+'\',\''+c.id+'\')">'+
           String(c.symbol||'').toUpperCase()+'<span>'+c.name+'</span>'+
           (c.rank?'<i>#'+c.rank+'</i>':'')+'</div>';
      }
      box.innerHTML=h||'<div class=dim>'+tt('nothing')+'</div>';
    }).catch(function(){box.innerHTML='<div class=dim>'+tt('searchErr')+'</div>';});
  },350);
}
function pick(t,id){
  id=String(id||'').toLowerCase().trim();
  if(!id)return;
  if(t==='sym'){
    set('sym',id);
    var q=document.getElementById('q_sym'); if(q)q.value=id;
  }else{
    var cur=(symsCsv||'').split(',').filter(function(x){return x});
    if(cur.indexOf(id)>=0)return;
    if(cur.length>=3)cur.shift();
    cur.push(id);
    set('syms',cur.join(','));
    var q2=document.getElementById('q_syms'); if(q2)q2.value='';
  }
  var b=document.getElementById('r_'+t); if(b)b.innerHTML='';
}
var EYE='<svg viewBox="0 0 24 24" width="19" height="19"><path d="M12 5.5C5.7 5.5 2.2 12 2.2 12S5.7 18.5 12 18.5 21.8 12 21.8 12 18.3 5.5 12 5.5z" fill="none" stroke="#8ab" stroke-width="1.8"/><circle cx="12" cy="12" r="3.1" fill="none" stroke="#8ab" stroke-width="1.8"/></svg>';
var EYEX='<svg viewBox="0 0 24 24" width="19" height="19"><path d="M12 5.5C5.7 5.5 2.2 12 2.2 12S5.7 18.5 12 18.5 21.8 12 21.8 12 18.3 5.5 12 5.5z" fill="none" stroke="#567" stroke-width="1.8"/><circle cx="12" cy="12" r="3.1" fill="none" stroke="#567" stroke-width="1.8"/><line x1="4.5" y1="19.5" x2="19.5" y2="4.5" stroke="#f66" stroke-width="2"/></svg>';
function pwTog(id,b){
  var i=document.getElementById(id);
  var on=i.type=='password';
  i.type=on?'text':'password';
  b.innerHTML=on?EYEX:EYE;
  i.focus();
}
function citySuggest(){
  var inp=document.getElementById('cityi'), box=document.getElementById('r_city');
  if(!inp||!box)return;
  cityPicked=false; // fresh typing: a manual edit always gets sent
  var v=inp.value.trim();
  if(sTimers.city)clearTimeout(sTimers.city);
  if(v.length<2){box.innerHTML='';return;}
  sTimers.city=setTimeout(function(){
    fetch('/api/city?q='+encodeURIComponent(v)).then(function(r){return r.json()}).then(function(j){
      cityList=(j&&j.cities)||[];
      var h='';
      for(var i=0;i<cityList.length;i++){
        var c=cityList[i];
        h+='<div class=qr onmousedown="cityPick('+i+')">'+String(c.n||c.v||'')+
           (c.d?'<span>'+c.d+'</span>':'')+'</div>';
      }
      box.innerHTML=h||'<div class=dim>'+tt('nothing')+'</div>';
    }).catch(function(){box.innerHTML='<div class=dim>'+tt('searchErr')+'</div>';});
  },350);
}
function cityPick(i){
  var c=cityList[i]; if(!c||!c.v)return;
  cityPicked=true;               // mousedown fires before blur: the coming
  var inp=document.getElementById('cityi');  // onchange must not re-send it
  if(inp)inp.value=c.v;
  set('city',c.v);
  var b=document.getElementById('r_city'); if(b)b.innerHTML='';
}
function cityChanged(el){
  if(cityPicked){cityPicked=false;return;}
  set('city',el.value);
  var b=document.getElementById('r_city'); if(b)b.innerHTML='';
}
function dropSym(i){
  var cur=(symsCsv||'').split(',').filter(function(x){return x});
  if(cur.length<=1)return;
  cur.splice(i,1); set('syms',cur.join(','));
}
function renderChips(){
  var el=document.getElementById('chips'); if(!el)return;
  var cur=(symsCsv||'').split(',').filter(function(x){return x});
  var h='';
  for(var i=0;i<cur.length;i++)
    h+='<span class=chip>'+cur[i]+'<b onclick="dropSym('+i+')">&times;</b></span>';
  el.innerHTML=h||'<span class=dim>—</span>';
}
function applyBrightness(v){
  var e=document.getElementById('br'); if(!e||brTouched)return;
  e.value=v; document.getElementById('bv').textContent=v+'%';
}
function setVal(id,v,guard){
  var e=document.getElementById(id); if(!e)return;
  if(guard&&document.activeElement===e)return;
  if(String(e.value)!==String(v))e.value=v;
}
function renderFetchState(s){
  var wm=document.getElementById('wxmsg');
  if(wm){
    var we=s.wxerr||'';
    var wwait=(!we&&(!s.wxok||!s.wxage||s.wxage=='-'));
    wm.textContent=(we?ttr('Погода: ошибка обновления')+' — '+we
                     :wwait?ttr('Погода: данные загружаются…')
                           :ttr('Погода обновлена')+' · '+(s.wxage||''));
    wm.style.color=we?'#f66':(wwait?'#2b6':'#7fa');
    wm.style.fontWeight=wwait?'bold':'normal';
  }
  var mm=document.getElementById('mkmsg');
  if(mm){
    var me=s.err||'';
    var mwait=(!me&&s.mkok===false);
    mm.textContent=(me?ttr('Котировки: ошибка обновления')+' — '+me
                     :mwait?ttr('Котировки: данные загружаются…')
                           :ttr('Котировки обновлены')+' · '+(s.age||''));
    mm.style.color=me?'#f66':(mwait?'#2b6':'#7fa');
    mm.style.fontWeight=mwait?'bold':'normal';
  }
}
function applySettings(s){
  renderFetchState(s);
  var lg=s.lang||'en';
  try{localStorage.setItem('sdp_lang',lg)}catch(e){}
  if(lg!==LANG){
    LANG=lg; tlDrawn=false;                 // theme list/tz notes need a rebuild
    var tzo=document.getElementById('tzi'); if(tzo)tzo.options.length=0;
    var gm=document.getElementById('geomsg'); if(gm)gm.textContent='';
  }
  i18n();
  syncThemes(s.themes, s.theme);
  applyBrightness(s.brightness);
  symsCsv=s.syms||''; renderChips();
  setVal('chi', s.chart, 1);
  setVal('tfmt', s.tfmt||'24', 1);
  setVal('langsel', s.lang||'en', 1);
  setVal('wprovsel', s.wprov||'owm', 1);
  var ct=document.querySelector('a[data-sec=crypto]');
  if(ct) ct.textContent=lx('Крипто','Crypto','Cripto','加密');
  setVal('carb', s.carousel?'1':'0');
  setVal('q_sym', s.sym, 1);
  setVal('inti', s.interval);
  setVal('refi', s.refresh);
  setVal('cityi', s.city, 1);
  setVal('tzi', fillTz(s.tz), 1);
  var e=document.getElementById('bgc');
  if(e&&s.bgcolor&&!colTouched) e.value=s.bgcolor;
  drawBgSwatches(s.bgcolor);
  var ncb=document.getElementById('nmode'); if(ncb)ncb.checked=!!s.nmode;
  setVal('nfrom', s.nfrom||'23:00', 1);
  setVal('nto', s.nto||'07:00', 1);
  setVal('nbr', s.nbr, 1);
  var nbe=document.getElementById('nbr'), nb=document.getElementById('nbv');
  if(nb&&nbe)nb.textContent=nbe.value+'%';
  var nm=document.getElementById('nmsg');
  if(nm)nm.textContent=s.nmode
    ? ((s.nact?(lx('Сейчас ночь — яркость ','Night now — brightness ','Es de noche — brillo ','当前夜间 — 亮度 ')+s.nbr+'%')
               :(lx('Обычная яркость ','Normal brightness ','Brillo normal ','常规亮度 ')+s.brightness+'%'))
       +(lx(' · окно ',' · window ',' · ventana ',' · 时段 ')+s.nfrom+'–'+s.nto))
    : lx('Ночной режим выключен','Night mode off','Modo nocturno desactivado','夜间模式已关闭');
  if(document.getElementById('gifpv')) drawGifs(s.gifnum||0);
  var hcb=document.getElementById('holi'); if(hcb)hcb.checked=!!s.hol;
  setVal('holc', s.hctry||'auto', 1);
  var hm=document.getElementById('holmsg');
  if(hm){
    if(s.hol){
      var MN=lx(['января','февраля','марта','апреля','мая','июня','июля','августа','сентября','октября','ноября','декабря'],
                ['January','February','March','April','May','June','July','August','September','October','November','December'],
                ['de enero','de febrero','de marzo','de abril','de mayo','de junio','de julio','de agosto','de septiembre','de octubre','de noviembre','de diciembre'],
                []);
      var MNN=lx(['январь','февраль','март','апрель','май','июнь','июль','август','сентябрь','октябрь','ноябрь','декабрь'],
                 ['January','February','March','April','May','June','July','August','September','October','November','December'],
                 ['enero','febrero','marzo','abril','mayo','junio','julio','agosto','septiembre','octubre','noviembre','diciembre'],
                 ['1月','2月','3月','4月','5月','6月','7月','8月','9月','10月','11月','12月']);
      var mi=((s.mon||0)>0?s.mon:(new Date().getMonth()+1))-1;
      var p=s.holNext?s.holNext.split('.'):[];
      var nx='';
      if(p.length){
        var mo=parseInt(p[0],10)||0, dd=parseInt(p[1],10)||0;
        nx=(LANG==='zh')?(mo+'月'+dd+'日'):(dd+' '+(MN&&MN[mo-1]?MN[mo-1]:''));
      }
      var parts=[lx('Страна: ','Country: ','País: ','国家：')+(s.hctry||
                    (lx('авто (','auto (','auto (','自动 (')+(s.hctryG||'…')+')')),
                 lx('дат: ','dates: ','fechas: ','日期：')+(MNN[mi]||'')];
      if(s.holY)parts.push(String(s.holY));
      if(nx)parts.push(lx('ближайший: ','next: ','siguiente: ','下次：')+nx);
      hm.textContent=parts.join(' · ');
    }else hm.textContent=lx('Выключено — праздники не подсвечиваются','Off — holidays are not highlighted','Apagado — los festivos no se resaltan','已关闭 — 假日不高亮');
  }
  var pfm=document.getElementById('phmsg');
  if(pfm&&!phBusy){
    if(s.pf&&s.pf.k==='jpg')pfm.textContent=lx('Загружено: фото ','Loaded: photo ','Foto cargada: ','已加载照片 ')+
      s.pf.w+'×'+s.pf.h+' · '+Math.round(s.pf.s/1024)+lx(' КБ',' KB',' KB',' KB');
    else pfm.textContent=lx('Файл не загружен','No file uploaded','No se ha subido ningún archivo','未上传文件');
  }
  var pv=document.getElementById('phpv');
  if(pv&&!phBusy){
    if(s.pf&&s.pf.k==='jpg'&&s.pf.s){
      var u='/api/phjpg?t='+s.pf.s;
      if(pv.getAttribute('src')!==u)pv.setAttribute('src',u);
      pv.style.display='block';
    }else{ pv.removeAttribute('src'); pv.style.display='none'; }
  }
}
var BG_PRESETS=['#000000','#050505','#051428'];
function bgSet(hex){
  colTouched=false;
  var e=document.getElementById('bgc');
  if(e) e.value=hex;
  var p=document.getElementById('bgpal');
  if(p) p.style.background=hex;
  set('bgcolor',hex);
}
function fillTz(cur){
  var el=document.getElementById('tzi'); if(!el)return cur;
  if(!el.options.length||el.dataset.nl!==LANG){
    while(el.options.length)el.remove(0);
    el.dataset.nl=LANG;
    var notes=(LANG==='zh')
      ?{'0':'格林尼治','1':'西欧','2':'中欧',
        '3':'莫斯科、明斯克、伊斯坦布尔、基辅','4':'埃里温、巴库、第比利斯','5':'叶卡捷琳堡',
        '6':'鄂木斯克','7':'克拉斯诺亚尔斯克','8':'伊尔库茨克、北京','9':'雅库茨克、东京',
        '10':'符拉迪沃斯托克、悉尼','11':'库页岛','12':'马加丹、奥克兰',
        '-3':'布宜诺斯艾利斯、圣保罗','-4':'大西洋加拿大','-5':'纽约、多伦多',
        '-6':'芝加哥、墨西哥城','-7':'丹佛','-8':'洛杉矶、安克雷奇',
        '-9':'安克雷奇','-10':'檀香山','-11':'中途岛'}
      :(LANG==='es')
      ?{'0':'Greenwich','1':'Europa occidental','2':'Europa central',
        '3':'Moscú, Minsk, Estambul, Kiev','4':'Ereván, Bakú, Tiflis','5':'Ekaterimburgo',
        '6':'Omsk','7':'Krasnoyarsk','8':'Irkutsk, Pekín','9':'Yakutsk, Tokio',
        '10':'Vladivostok, Sídney','11':'Sajalín','12':'Magadán, Auckland',
        '-3':'Buenos Aires, São Paulo','-4':'Canadá atlántico','-5':'Nueva York, Toronto',
        '-6':'Chicago, Ciudad de México','-7':'Denver','-8':'Los Ángeles, Anchorage',
        '-9':'Anchorage','-10':'Honolulu','-11':'Midway'}
      :(LANG==='en')
      ?{'0':'Greenwich','1':'Western Europe','2':'Central Europe',
        '3':'Moscow, Minsk, Istanbul, Kyiv','4':'Yerevan, Baku, Tbilisi','5':'Yekaterinburg',
        '6':'Omsk','7':'Krasnoyarsk','8':'Irkutsk, Beijing','9':'Yakutsk, Tokyo',
        '10':'Vladivostok, Sydney','11':'Sakhalin','12':'Magadan, Auckland',
        '-3':'Buenos Aires, Sao Paulo','-4':'Atlantic Canada','-5':'New York, Toronto',
        '-6':'Chicago, Mexico City','-7':'Denver','-8':'Los Angeles','-9':'Anchorage',
        '-10':'Honolulu','-11':'Midway'}
      :{'0':'Гринвич','1':'Западная Европа','2':'Центральная Европа',
        '3':'Москва, Минск, Стамбул, Киев','4':'Ереван, Баку, Тбилиси','5':'Екатеринбург',
        '6':'Омск','7':'Красноярск','8':'Иркутск, Пекин','9':'Якутск, Токио',
        '10':'Владивосток, Сидней','11':'Сахалин','12':'Магадан, Окленд',
        '-3':'Буэнос-Айрес, Сан-Паулу','-4':'Атлантика Канады','-5':'Нью-Йорк, Торонто',
        '-6':'Чикаго, Мехико','-7':'Денвер','-8':'Лос-Анджелес','-9':'Анкоридж',
        '-10':'Гонолулу','-11':'Мидуэй'};
    for(var o=-12;o<=14;o++){
      var t=document.createElement('option');
      // POSIX offset counts west of GMT, so UTC+N is written as "UTC-N"
      t.value = o===0 ? 'UTC0' : ('UTC'+(o>0?'-':'+')+Math.abs(o));
      t.textContent='UTC'+(o>=0?'+':'')+o+(notes[o]?(' · '+notes[o]):'');
      el.appendChild(t);
    }
  }
  if(cur){
    var found=false;
    for(var i=0;i<el.options.length;i++) if(el.options[i].value===cur) found=true;
    if(!found){
      // "MSK-3" and friends describe the same zone as the POSIX value "UTC-3"
      // (POSIX offset counts WEST, so UTC+3 is written "UTC-3"): match on that
      // value so the dropdown never grows a "custom POSIX" entry for them
      var m=cur.match(/([-+])(\d+)$/);
      if(m){
        var human=(m[1]==='-'?1:-1)*parseInt(m[2],10);   // POSIX sign is inverted
        var want=(human===0?'UTC0':('UTC'+(human>0?'-':'+')+Math.abs(human)));
        for(var i=0;i<el.options.length;i++) if(el.options[i].value===want){ cur=want; found=true; break; }
      }
    }
    if(!found){
      var t=document.createElement('option');
      t.value=cur; t.textContent=cur+' · '+tt('customPosix');
      el.appendChild(t);
    }
  }
  return cur;
}
// browser geolocation -> ESP reverse-geocodes it -> city is applied like a
// manual entry (fetches new weather right away).  Plain-http origins are
// blocked from navigator.geolocation by every modern browser, so on failure
// we fall back to a device-side IP lookup (/api/geoip) with the same flow.
function geoCity(btn){
  if(!navigator.geolocation){ geoIp(btn); return; }
  btn.disabled=true;
  var msg=document.getElementById('geomsg');
  if(msg)msg.textContent=tt('geoCoords');
  navigator.geolocation.getCurrentPosition(function(p){
    applyGeo(btn, p.coords.latitude, p.coords.longitude);
  },function(){
    geoIp(btn);
  },{timeout:10000,maximumAge:60000});
}
function geoIp(btn){
  var msg=document.getElementById('geomsg');
  btn.disabled=true;
  if(msg)msg.textContent=tt('geoIpWait');
  fetch('/api/geoip').then(function(r){return r.json()}).then(function(j){
    if(j&&j.city){                       // ip-api names the city itself: no second hop
      var cv=j.city+(j.countryCode?(','+j.countryCode):'');
      var ei=document.getElementById('cityi'); if(ei)ei.value=cv;
      if(msg)msg.textContent=tt('geoCityOk')+j.city+(j.countryCode?(' ('+j.countryCode+')'):'');
      btn.disabled=false; set('city',cv);
    }else if(j&&typeof j.lat==='number'&&typeof j.lon==='number'){
      applyGeo(btn, j.lat, j.lon);
    }else{
      btn.disabled=false;
      if(msg)msg.textContent=tt('geoIpFail')+(j&&j.error?j.error:tt('noData'));
    }
  }).catch(function(){
    btn.disabled=false;
    if(msg)msg.textContent=tt('geoIpErr');
  });
}
function applyGeo(btn, la, lo){
  var msg=document.getElementById('geomsg');
  fetch('/api/geocode?lat='+Number(la).toFixed(4)+'&lon='+Number(lo).toFixed(4))
  .then(function(r){return r.json()}).then(function(j){
    btn.disabled=false;
    if(j&&j.city){
      var cv=j.city+(j.country?','+j.country:'');  // "Name,CC" resolves for both sources
      var e=document.getElementById('cityi'); if(e)e.value=cv;
      if(msg)msg.textContent=tt('geoCityOk')+j.city+(j.country?(' ('+j.country+')'):'');
      set('city',cv);
    }else if(msg){ msg.textContent=tt('geoCityNo')+(j&&j.error?j.error:tt('noData')); }
  }).catch(function(){ btn.disabled=false; if(msg)msg.textContent=tt('geoErr'); });
}
function drawBgSwatches(cur){
  cur=String(cur||'').toUpperCase();
  var ids=['bgsw0','bgsw1','bgsw2'];
  for(var i=0;i<3;i++){
    var el=document.getElementById(ids[i]); if(!el)continue;
    el.className='bgswat'+(cur===BG_PRESETS[i].toUpperCase()?' on':'');
  }
  // 4th square ("Custom"): lit and painted with the colour itself when the
  // colour matches none of the presets, neutral otherwise
  var known=false;
  for(var i=0;i<3;i++) if(cur===BG_PRESETS[i].toUpperCase()) known=true;
  var sw=document.getElementById('bgsw3');
  if(sw){ sw.className='bgswat'+(!known?' on':'');
    sw.style.background=(!known&&cur)?cur:'#22262e'; }
  // palette circle to the right of the squares: always shows current colour
  var pal=document.getElementById('bgpal');
  if(pal){ pal.className='bgswpal bglive'; pal.style.background=cur||'#000'; }
}
function esc(s){return String(s==null?'':s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/"/g,'&quot;')}
function renderWifi(s){
  var e=document.getElementById('wstat');
  if(!e||!s.wifi)return;
  var w=s.wifi;
  e.innerHTML=tt('wifiNet')+'<b>'+esc(w.ssid||'—')+'</b> · '+(w.connected
    ?(tt('wifiOn')+'<b><a href="http://'+w.ip+'">'+w.ip+'</a></b> · '+w.rssi+' dBm')
    :('<span style="color:#f66">'+tt('wifiOff')+'</span>'+(w.ap?tt('wifiAp'):'')))
    +(w.saved&&w.ssid!=w.saved?('<br>'+tt('wifiSaved')+'<b>'+esc(w.saved)+'</b>'):'');
  setVal('wssid', w.saved||'', 1);
  wifiCur=(w.connected&&w.ssid)?String(w.ssid):'';
  syncWifiForm();
}
var wifiCur='';
function syncWifiForm(){
  var e=document.getElementById('wssid'); if(!e)return;
  var same=!!(wifiCur&&e.value.trim()===wifiCur);
  e.readOnly=same;
  var pw=document.getElementById('wpassw'); if(pw)pw.hidden=false;
  var pe=document.getElementById('wpass');
  var typed=!!(pe&&pe.value);
  if(pe)pe.placeholder=ttr(same?'пусто — оставить текущий':'пусто — открытая сеть');
  var b=document.getElementById('wsave'); if(b)b.disabled=same&&!typed;
  var s1=document.getElementById('wsav1'), s2=document.getElementById('wsav2');
  if(s1)s1.hidden=same&&!typed; if(s2)s2.hidden=!(same&&!typed);
}
function pickWifi(el){
  var f=document.getElementById('wform'); if(!f)return;
  var prev=document.querySelector('#wlist .qr.sel');
  if(prev)prev.classList.remove('sel');
  el.classList.add('sel');
  el.parentNode.insertBefore(f, el.nextSibling);
  document.getElementById('wssid').value=el.getAttribute('data-s');
  document.getElementById('wpass').value='';
  syncWifiForm();
  if(!document.getElementById('wpassw').hidden)document.getElementById('wpass').focus();
}
function scanWifi(){
  var b=document.getElementById('scanbtn');
  b.disabled=true; b.textContent=tt('scanning');
  function poll(){
    j('/api/wifiscan').then(function(r){
      if(r&&r.scanning){ setTimeout(poll,900); return; }
      b.disabled=false; b.textContent=tt('scanBtn');
      var ns=(r&&r.networks)||[];
      ns.sort(function(a,b){return b.rssi-a.rssi});
      var h='';
      for(var i=0;i<ns.length;i++){
        var s=String(ns[i].ssid||'');
        h+='<div class=qr data-s="'+esc(s)+'" onclick="pickWifi(this)"><b>'+(ns[i].secure?'🔒':'○')+
           '</b><span>'+esc(s)+'</span><i>'+ns[i].rssi+' dBm</i></div>';
      }
      var wl=document.getElementById('wlist'), f=document.getElementById('wform');
      if(f&&wl) wl.parentNode.insertBefore(f, wl.nextSibling);
      wl.innerHTML=h||'<div class=dim>'+tt('noNets')+'</div>';
    }).catch(function(){b.disabled=false;b.textContent=tt('scanBtn');
      document.getElementById('wlist').innerHTML='<div class=dim>'+tt('scanErr')+'</div>';});
  }
  poll();
}
function wifiPost(obj,cb){
  fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify(obj)}).then(function(r){return r.json()}).then(cb);
}
function saveWifi(){
  var ssid=document.getElementById('wssid').value.trim();
  var pass=document.getElementById('wpass').value;
  if(!ssid){alert(tt('wifiAlertName'));return;}
  wifiPost({ssid:ssid,pass:pass},function(x){
    alert(tt('wifiAlertSaved'));
    st();
  });
}
function forgetWifi(){
  if(!confirm(tt('wifiForget')))return;
  wifiPost({forget:1},function(x){
    document.getElementById('wpass').value='';
    if(x&&x.ok&&x.forget){alert(tt('wifiForgetDone'));return;}
    st();
  });
}
function fwUp(){
  var f=document.getElementById('fw').files[0];
  if(!f){alert(tt('fwPick'));return;}
  if(!confirm(tt('fwConfirm')+f.name+' ('+f.size+tt('fwBytes')))return;
  var x=new XMLHttpRequest();
  x.open('POST','/update');
  var pb=document.getElementById('fwpb'), msg=document.getElementById('fwmsg');
  pb.style.width='0%';
  x.upload.onprogress=function(e){
    if(e.lengthComputable) pb.style.width=Math.round(e.loaded/e.total*100)+'%';
  };
  x.onload=function(){
    if(x.status==200){msg.innerHTML='<span style="color:#4f4">'+tt('fwDone')+'</span>';
      setTimeout(function(){location.href='/'},15000);}
    else msg.innerHTML='<span style="color:#f66">'+tt('fwErr')+x.status+': '+esc(x.responseText)+'</span>';
  };
  x.onerror=function(){msg.innerHTML='<span style="color:#f66">'+tt('linkBreak')+'</span>';};
  var fd=new FormData();
  var safe=f.name.replace(/^.*[\\\/]/,'').replace(/[^A-Za-z0-9._-]/g,'');
  fd.append('firmware', f, safe.indexOf('SDP')===0?safe:'SDP_'+safe);
  x.send(fd);
}
var phBusy=false;
function phEl(id){return document.getElementById(id);}
function phOut(t){var e=phEl('phmsg');if(e&&!phBusy)e.textContent=t;}
function phErrTxt(c){
  var M={write:'phWrite',fs:'phFs',big:'phBig',dims:'phDims',prog:'phProg',
    format:'phFormat',rename:'phRename',frames:'phFrames',
    empty:'phEmptyFile',abort:'phAbort',xnet:'linkBreak'};
  return M[c]?tt(M[c]):c;
}
function phSend(url,fd){
  return new Promise(function(res,rej){
    var x=new XMLHttpRequest();
    x.open('POST',url,true);
    x.upload.onprogress=function(e){
      if(!e.lengthComputable)return;
      var el=phEl('phmsg'), pb=phEl('phpb');
      var p=Math.round(e.loaded/e.total*100);
      if(pb)pb.style.width=p+'%';
      if(el)el.textContent=tt('phLoad')+p+'%';
    };
    x.onload=function(){
      var j={};
      try{j=JSON.parse(x.responseText);}catch(e){}
      if(x.status==200&&j.ok)res(j); else rej({code:j.error||('http '+x.status)});
    };
    x.onerror=function(){rej({code:'xnet'});};
    x.send(fd);
  });
}
function phToJpeg(file){
  return new Promise(function(res,rej){
    var img=new Image(), u=URL.createObjectURL(file);
    img.onload=function(){
      var w=img.naturalWidth,h=img.naturalHeight;
      URL.revokeObjectURL(u);
      if(w===240&&h===240&&file.type==='image/jpeg'&&file.size<=200000){res(file);return;}
      var cv=document.createElement('canvas'); cv.width=240; cv.height=240;
      var ctx=cv.getContext('2d');
      ctx.fillStyle='#000'; ctx.fillRect(0,0,240,240);
      var s=Math.max(240/w,240/h);
      ctx.drawImage(img,(240-w*s)/2,(240-h*s)/2,w*s,h*s);
      cv.toBlob(function(b){b?res(b):rej({code:'format'});},'image/jpeg',0.9);
    };
    img.onerror=function(){URL.revokeObjectURL(u);rej({code:'format'});};
    img.src=u;
  });
}
async function phUp(){
  var inp=phEl('phf');
  if(!inp||!inp.files||!inp.files.length){phOut(tt('phFirst'));return;}
  var f=inp.files[0];
  inp.value='';
  phBusy=true;
  var el=phEl('phmsg'), pb=phEl('phpb');
  if(pb)pb.style.width='0%';
  function say(t){if(el)el.textContent=t;}
  try{
    say(tt('phFit'));
    var blob=await phToJpeg(f);
    say(tt('phSend'));
    var fd2=new FormData();
    fd2.append('file',blob,'photo.jpg');
    await phSend('/api/pupload?kind=jpg',fd2);
    say(tt('phOk'));
  }catch(e){
    say(tt('phErr')+(e&&e.msg?e.msg:phErrTxt(e&&e.code?e.code:tt('network'))));
  }
  phBusy=false;
  st();
}
function showSec(id){
  if(id==='tick'||id==='c3') id='crypto';
  var secs=document.querySelectorAll('.sec');
  for(var i=0;i<secs.length;i++) secs[i].hidden=(secs[i].id!=='sec_'+id);
  var as=document.querySelectorAll('#nav a');
  for(var i=0;i<as.length;i++)
    as[i].className=(as[i].getAttribute('data-sec')==id)?'on':'';
  if(history.replaceState)history.replaceState(null,'','#'+id);
}
document.addEventListener('DOMContentLoaded',function(){
  i18n();
  document.getElementById('nav').addEventListener('click',function(e){
    var a=e.target.closest?e.target.closest('a'):null;
    if(!a)return;
    e.preventDefault();
    showSec(a.getAttribute('data-sec'));
  });
  var id=(location.hash||'#th').slice(1);
  if(id==='tick'||id==='c3') id='crypto';
  if(id==='st') id='th';                 // Status tab removed
  if(!document.getElementById('sec_'+id))id='th';
  showSec(id);
});
</script></head><body>
<div class=lay>
<nav id=nav>
<div class=lg>BroClock Pro<span>+ Crypto</span></div>
<a data-sec=th class=on>Темы</a>
<a data-sec=crypto>Crypto</a>
<a data-sec=wx>Погода</a>
<a data-sec=wifi>WiFi</a>
<a data-sec=fw>Прошивка</a>
<a data-sec=settings>Настройки</a>
</nav>
<main>
<div class="c sec" id=sec_th hidden><h2>Темы</h2>
<label>Отметьте темы, которые участвуют в карусели</label>
<div id=tl class=tl>…</div>
</div>
<div class="c sec" id=sec_settings hidden><h2>Настройки</h2>
<div class=grp><div class=gh>Карусель тем</div>
<label>Карусель (переключение тем)</label>
<select id=carb onchange="set('carousel',this.value)">
<option value=1>вкл</option><option value=0>выкл</option></select>
<label>Интервал карусели, сек</label>
<input id=inti type=number min=5 max=900 onchange="set('interval',this.value)">
</div>
<div class=grp><div class=gh>Часы и язык</div>
<label>Формат часов</label>
<select id=tfmt onchange="set('tfmt',this.value)">
<option value=24>24 часа</option>
<option value=12>12 часов (AM/PM)</option>
</select>
<label>Часовой пояс</label><select id=tzi onchange="set('tz',this.value)"></select>
<div class=dim>POSIX-строка применяется сразу; синхронизация — NTP (пул UTC).</div>
<label>Язык интерфейса</label>
<select id=langsel onchange="set('lang',this.value)">
<option value=en>English</option>
<option value=ru>Русский</option>
<option value=es>Español</option>
<option value=zh>中文</option>
</select>
</div>
<div class=grp><div class=gh>Подсветка</div>
<label>Яркость подсветки <b id=bv style=color:#fff>100%</b></label>
<input id=br type=range min=0 max=100 value=100
 oninput="brTouched=true;bv.textContent=this.value+'%'"
 onchange="set('brightness',this.value)">
<label>Ночной режим подсветки</label>
<label><input type=checkbox id=nmode style="width:auto;height:auto;accent-color:#34c759;margin-right:7px;vertical-align:middle" onchange="set('nmode',this.checked?'1':'0')"> Снижать яркость в указанные часы</label>
<div class=row>
<div><label>Начало</label><input id=nfrom type=time value=23:00 onchange="set('nfrom',this.value)"></div>
<div><label>Окончание</label><input id=nto type=time value=07:00 onchange="set('nto',this.value)"></div>
</div>
<label>Ночная яркость <b id=nbv style=color:#fff>20%</b></label>
<input id=nbr type=range min=0 max=100 value=20
 oninput="nbv.textContent=this.value+'%'"
 onchange="set('nbr',this.value)">
<div class=dim id=nmsg>…</div>
</div>
<div class=grp><div class=gh>Анимация</div>
<label>Цвет фона экрана</label>
<div class=bgsw>
<label class=bgswat id=bgsw0 style="background:#000000" title="Чёрный" onclick="bgSet('#000000')"><i>Чёрный</i></label>
<label class=bgswat id=bgsw1 style="background:#050505" title="Тёмно-серый" onclick="bgSet('#050505')"><i>Тёмно-серый</i></label>
<label class=bgswat id=bgsw2 style="background:#051428" title="Глубокий синий" onclick="bgSet('#051428')"><i>Синий</i></label>
<label class=bgswat id=bgsw3 title="Custom — ваш цвет; клик открывает палитру"
 onclick="document.getElementById('bgc').click()"><i>Custom</i></label>
<label class="bgswpal bglive" id=bgpal title="Палитра — живой предпросмотр цвета">
<input type=color id=bgc value="#000000"
 oninput="colTouched=true;bgpal.style.background=this.value;bgsw3.style.background=this.value"
 onchange="set('bgcolor',this.value)"><i>Палитра</i></label>
</div>
<label>Анимация в правом углу (тема Classic)</label>
<div id=gifpv class=gifs>…</div>
</div>
<div class=grp><div class=gh>Фото</div>
<label>Тема Фото — картинка или анимация</label>
<input type=file id=phf accept="image/jpeg,image/png,image/webp,image/bmp,image/avif,image/gif" style=display:none>
<div class=row>
<div><button onclick="document.getElementById('phf').click()">Выбрать файл…</button></div>
<div><button onclick="phUp()">Загрузить</button></div>
<div><button onclick="set('pdel','1')">Удалить</button></div>
</div>
<div class=bar><i id=phpb></i></div>
<img id=phpv alt="" style="display:none;max-width:240px;max-height:240px;border-radius:8px;border:1px solid #333;margin-top:6px;margin-left:auto;margin-right:auto">
<div class=dim id=phhint>Форматы: <b style=color:#fff>JPEG</b>, <b style=color:#fff>PNG</b>, <b style=color:#fff>WEBP</b>, <b style=color:#fff>BMP</b> и др. — изображение автоматически вписывается в экран 240×240 (обрезка по центру) и загружается как JPEG. Лимит файла 256 КБ. Анимация GIF больше не разрезается на кадры: первый кадр сохранится как обычное фото. Новое изображение заменяет предыдущее.</div>
<div class=dim id=phmsg>…</div>
</div>
<div class=grp><div class=gh>Календарь</div>
<label>Календарь: государственные праздники</label>
<label><input type=checkbox id=holi style="width:auto;height:auto;accent-color:#34c759;margin-right:7px;vertical-align:middle" onchange="set('hol',this.checked?'1':'0')"> Выделять праздничные дни красным</label>
<label>Страна праздников</label>
<select id=holc onchange="set('hctry',this.value)">
<option value=auto>Авто — определить по гео</option>
<option value=BY>Беларусь</option>
<option value=RU>Россия</option>
<option value=UA>Украина</option>
<option value=KZ>Казахстан</option>
<option value=MD>Молдова</option>
<option value=LT>Литва</option>
<option value=LV>Латвия</option>
<option value=EE>Эстония</option>
<option value=PL>Польша</option>
<option value=CZ>Чехия</option>
<option value=SK>Словакия</option>
<option value=RO>Румыния</option>
<option value=BG>Болгария</option>
<option value=RS>Сербия</option>
<option value=HU>Венгрия</option>
<option value=DE>Германия</option>
<option value=AT>Австрия</option>
<option value=FR>Франция</option>
<option value=ES>Испания</option>
<option value=IT>Италия</option>
<option value=PT>Португалия</option>
<option value=GR>Греция</option>
<option value=TR>Турция</option>
<option value=GB>Великобритания</option>
<option value=IE>Ирландия</option>
<option value=NL>Нидерланды</option>
<option value=BE>Бельгия</option>
<option value=CH>Швейцария</option>
<option value=SE>Швеция</option>
<option value=NO>Норвегия</option>
<option value=DK>Дания</option>
<option value=FI>Финляндия</option>
<option value=IS>Исландия</option>
<option value=US>США</option>
<option value=CA>Канада</option>
<option value=MX>Мексика</option>
<option value=BR>Бразилия</option>
<option value=AR>Аргентина</option>
<option value=CL>Чили</option>
<option value=JP>Япония</option>
<option value=CN>Китай</option>
<option value=KR>Корея</option>
<option value=IN>Индия</option>
<option value=IL>Израиль</option>
<option value=AU>Австралия</option>
<option value=ZA>ЮАР</option>
</select>
<div class=dim id=holmsg>…</div>
<div class=dim>Праздники берутся с date.nager.at один раз в год и хранятся на карте; страна «Авто» определяется по внешнему IP.</div>
</div>
</div>
<div class="c sec" id=sec_crypto hidden><h2>Crypto</h2>
<label>Монета: поиск по каталогу (топ-250 CoinGecko)</label>
<input id=q_sym placeholder="bitcoin, doge, solana…" oninput="qSearch('sym')" autocomplete=off>
<div id=r_sym class=sr></div>
<button class=sm onclick="pick('sym',document.getElementById('q_sym').value)">Указать id вручную</button>
<label>График</label><select id=chi onchange="selTouched=true;set('chart',this.value)">
<option value=candle>Свечи</option><option value=line>Линия</option></select>
<label>Обновление данных, сек</label><input id=refi type=number value=60 onchange="set('refresh',this.value)">
<button onclick="doRefresh(this)">Обновить данные сейчас</button>
<div class=dim id=mkmsg style="margin-top:2px"></div>
<label>Вторичный рынок · до 3 монет</label>
<div id=chips class=chips>…</div>
<label>Найти и добавить монету</label>
<input id=q_syms placeholder="ethereum, cardano, tron…" oninput="qSearch('syms')" autocomplete=off>
<div id=r_syms class=sr></div>
<button class=sm onclick="pick('syms',document.getElementById('q_syms').value)">Указать id вручную</button>
<div class=dim>Клик по результату добавляет монету. При трёх монетах самая старая заменяется.</div>
</div>
<div class="c sec" id=sec_wx hidden><h2>Погода</h2>
<label>Город</label>
<div class=row><input id=cityi placeholder="Введите свой город" oninput="citySuggest()" onchange="cityChanged(this)" autocomplete=off>
<button class=sm onclick="geoCity(this)">Моё местоположение</button></div>
<div id=r_city class=sr></div>
<div class=dim id=geomsg></div>
<label>Источник данных</label>
<select id=wprovsel onchange="set('wprov',this.value)">
<option value=owm>OpenWeatherMap</option>
<option value=ometo>Open-Meteo (без ключа, город и с русским названием)</option>
</select>
<button onclick="doRefresh(this)">Обновить погоду сейчас</button>
<div class=dim id=wxmsg style="margin-top:2px"></div>
<div class=dim>Погода обновляется автоматически каждые 10 минут.</div>
</div>
<div class="c sec" id=sec_wifi hidden><h2>WiFi</h2>
<div id=wstat class=dim>…</div>
<button class=sm id=scanbtn onclick="scanWifi()">Сканировать сети</button>
<div id=wlist class=sr></div>
<div id=wform class=sr>
<label>Имя сети (SSID)</label><input id=wssid autocomplete=off oninput="syncWifiForm()">
<div id=wpassw><label>Пароль</label><div class=pwr><input id=wpass type=password placeholder="пусто — открытая сеть" autocomplete=off oninput="syncWifiForm()"><button type=button class=eye onclick="pwTog('wpass',this)" aria-label="Показать пароль"><svg viewBox="0 0 24 24" width="19" height="19"><path d="M12 5.5C5.7 5.5 2.2 12 2.2 12S5.7 18.5 12 18.5 21.8 12 21.8 12 18.3 5.5 12 5.5z" fill="none" stroke="#8ab" stroke-width="1.8"/><circle cx="12" cy="12" r="3.1" fill="none" stroke="#8ab" stroke-width="1.8"/></svg></button></div></div>
<button id=wsave onclick="saveWifi()"><span id=wsav1>Сохранить и подключить</span><span id=wsav2 hidden>Подключено</span></button>
<button class=sm onclick="forgetWifi()">Забыть сохранённую сеть</button>
<div class=dim>Сохранённая сеть подключается автоматически при старте.</div>
</div>
</div>
<div class="c sec" id=sec_fw hidden><h2>Прошивка</h2>
<div class=dim>Загрузка .bin прошивки по воздуху. Конфигурация и данные не трогаются. Принимаются только образы этой сборки — в файле ищется отпечаток (чип, flash, пины дисплея); чужая прошивка отклоняется без записи, старая продолжит работать.</div>
<label>Файл прошивки .bin</label><input type=file id=fw accept=".bin">
<div class=bar><i id=fwpb></i></div>
<div id=fwmsg class=dim>—</div>
<button onclick="fwUp()">Обновить прошивку</button>
<div class=dim style="text-align:center"><b><a href="https://github.com/sam8ac" target="_blank" rel="noopener">BroClock Pro v1.0 + Crypto</a></b></div>
</div>
</main>
</div>
</body></html>
)HTML";

// First-connect setup page: served on "/" while no network is saved, and from
// onNotFound for any host a captive-portal browser probes (Android/iOS/Windows
// connectivity checks). Self-contained, no stock deps, 4 languages.
static const char SETUP_PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>BroClock PRO - Wi-Fi Setup</title>
<style>body{font-family:system-ui;background:#0b0e14;color:#e8e8e8;margin:0;padding:16px}
h1{font-size:19px;margin:0 0 4px;color:#7fa}
label{display:block;color:#8ab;font-size:13px;margin:11px 0 4px}
input{width:100%;padding:10px;border-radius:6px;border:1px solid #333;background:#0f131a;color:#eee;font-size:15px;box-sizing:border-box}
.pwr{display:flex;gap:7px;align-items:stretch}
.pwr input{flex:1;min-width:0}
.eye{flex:0 0 auto;width:40px;padding:0;margin:0;border:1px solid #333;border-radius:6px;background:#0f131a;cursor:pointer;display:flex;align-items:center;justify-content:center;-webkit-tap-highlight-color:transparent}
.eye:active{border-color:#2b6}
button{width:100%;padding:11px;border:0;border-radius:6px;background:#2b6;color:#042;font-weight:700;font-size:15px;margin-top:14px}
button:disabled{opacity:.55;cursor:default}
.qr{display:flex;gap:9px;align-items:center;padding:9px 10px;background:#0f131a;border:1px solid #232a35;border-radius:6px;margin-top:5px;cursor:pointer;font-size:14px}
.qr:hover,.qr.sel{border-color:#2b6;background:#15222e}
.qr b{color:#2b6}.qr span{color:#dde}
#msg{color:#7fa;margin-top:13px;white-space:pre-line;font-size:14px}
#form{display:none;background:#10141c;border:1px solid #2b6;border-radius:8px;padding:11px;margin-top:8px}
#manual{display:inline-block;color:#6cf;font-size:13px;margin-top:9px;cursor:pointer}
.hd{display:flex;align-items:center;justify-content:space-between;gap:9px}
.hd h1{flex:1;min-width:0;margin:0}
#lw{position:relative;flex:0 0 auto;display:flex;align-items:center;gap:7px}
#ln{font-size:14px;color:#dde;white-space:nowrap}
#lb{width:33px;height:33px;display:block;-webkit-tap-highlight-color:transparent;cursor:pointer}
#lm{display:none;position:absolute;right:0;top:39px;z-index:9;background:#151a22;
 border:1px solid #2b6;border-radius:8px;min-width:138px;box-shadow:0 7px 20px rgba(0,0,0,.55)}
#lm b{display:block;padding:10px 14px;font-weight:400;font-size:15px;color:#dde;cursor:pointer}
#lm b:hover,#lm b.sel{background:#15222e;color:#fff}
.dim{color:#678;font-size:13px;margin-top:11px}
</style></head><body>
<div class=hd><h1 id=t>SDPRO - Wi-Fi Setup</h1><div id=lw><span id=ln>English</span><img id=lb src="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAGAAAABgCAYAAADimHc4AAAYaElEQVR42u09a5RdVXnf/s65d15J5pGQJxEhBBKRhySAIAqKaFehqCXUV7uWa5Xiq/hYLpX6CqxqpV20LkXbqtXiUlsX1IVFrdRHQ7GARgJCwitCEgYmLzLJndzHuefs/e2vP2bvYc/OPnfuuTOTDC72WnfNzJ1z9vn29/6+vc/3CZhDg5kRAAQACERUzDzTcyMAMABoIQTPhTXHcwjpLITQ3v+GGo3GqiiKjgeANV1dXQNJkizu6upajojCm0pIKVNm3lUulw83m81n4zge1lrv3Ldv3w4hRAIA2pk7Mr8eU2KIY4R0YbnRRXqSJC8tlUrnMPO5iHgeAJyKiIun8yytNTHzsNZ6GyLex8z3VavVB4eGhsY8YhzBAL93BLCIF0KQg/STSqXSpUKINwHAqxBxQQiPDveKKRAOiMhWlRlC+9c8Q0T/q7W+tdFo3G2JYaXRhe/3YjCzcEQetm3bVpZSXklEPyCiKk8exMySmRUza/PxR+g7bnEtmflU4JpdUsovMPOZHrz4+4L8CcQfPHiwXyn1ISJ6wENCHnI4QBya4pp251HuXESUMvPtzPwG1z69YAnhctH9999fYuZ3EdETASRMhSzLxcr5uxq4RjJz4hGinUH+tUR0Z5Zl54WY6AXH9VLKS4joVx5iyEOsngJBdjzMzBuY+eqAROzNsux8pdSniehQm0SgFrDILMtuSZLkRM9je2Ho+lqttoyIvtUC8dLjOpWHfCLKlFIf27NnT595zoYAAvcxc78x7Ccrpb7fSiUppWQLKVAOXKNZln3YOBBzVxpc7pBSbiCiYc8IhlQJE9EdSqnrlFJVD1EW+QeY+Q/s4s3nbTkEWOoiiIg+71ynXUMtpXzY2KOtAYN9BKxKqZ8x88kOHGLOqZxt27aViehLnl4OcVWNiL6aZdmrzf13eNeQWfSeer2+3lwTM3Nsfn9rDgEWO9ciAECz2fxIQBKImbVS6t0AAGmavt0gmHMIpgzc+5VS73SkXcwF5McAAI1G4wSl1N0tdCszsySif2o2m2vs/VmWfcpDvnU9q1mWrbdG3CV0GwQQ5hMDACilNnoMoZlZE5G2zzCS+4fMfE+OIXcZ6O8clYTHHPnM/GoieqYV1zPzLxqNxgXuvdVq9XRmTh2OmzCESqmr3GcUJYBDiNioo3/3YFJGyn61ffv2Ljv3rbfeGimlriaikRzJsdJwOzMP2HuOGfKllJcx85iPfGtUiaimlPq4g7zI3CuUUneEkEJEN/nI74QArjvMzANE9KjnWUlDhHeba8v2vkajsdIhms9M0sC5uVarLTvqxtk+TCn1VhPA+CrELvA3aZqe7XpIDhLfHNL7RPQIM883SBPTJYB7n5TydUREjsTZn08z86BDrNhRkdcwcyXgqUljzLfW6/UVR40IFrh6vb4h4KtPpA6I6OvM3OMYRVctlJn5/gD3U5Ikb8hbTKcEcO8lon/MUUUbvWdMRMFpmp5JRA8FVKyVoK1HRRLs5M1m8zKlVBowtlZXXhcKyuzvaZr+mUc4i4TbWi1imgQQzIzVanWJUmrUkwCtlDpYr9dX+Hkgy3CVSmWQiH6UZ5yllA9Xq9XFAAAbN27EWUN+mqZnW5F0Fm9/pkqpt+X5ytY7IaL7vMhTM3MtSZKTWyXCpkMAT3V+JkcKrgsxgCsVRPSNPEkgol+OjIz0htTnjARZJrp9KqS7jTfzxyHj6enhSxw97C7klqncuhkggHVPB5l5xJECGxf8ziBQBJgHHTX65RaG+V9nVBVZ42mCrJ+GDC4RZa2Q7yLW8Syk1noiLZGm6SsAQLRy6aZLAI8R/jrESM1mc4OfPg8lGKWUt7SwCR+eMSI4i74hEMxIZuYsy64x15RaIZ+ZlxPR4UCE+R/tBDUzRAArBUuJaL+fmSWiH7WCxfGUkIh+6HlHdk0ZM5837UDNdd8C6WPrNt5oru1ycjX+p8zMqJR6X476usy5LppinoiZ394qF2S8rpbzGGn8psNU1ns7nCTJKnNdKef+2HyGiGhLyB4S0eMHDhxY0LE9cCi9wMnjT3qIlPLbReYkov9zuMTO9UBR2KSUbwqlo5m5r8g89Xr9HKM+tSfRn2h3jmq1uoSIfufFP1ayb55KFbU6FYFCCJJS3hDH8SkAQABgJ0IAAEQcZOZvm+91i31nrbXuQcRzzN+ROR4CWuteY4BL9rtWMAGA1Fqf4sxtuWsQAL7NzHXn+Am0MZ9ynh0BAEdR9H5mXuPCmTMiAKgTUdOe7HDmJUR8PzN/XwhxFzNHob3mXHdNCEFZlq2LouheRIzMpHMnBTv3BwFApLXe/OCDD164bt06MicvuF0CaCK6ExHf4HG/f1qhyJma0BzcQnpanebAnEUXHTMBUx5zEgBERHRNHMdfD0mByON+Zr4MAH7kIb8owlsB2AniZ4MArQgx3fVq8/NZADgDAMYAAFwpCIbsABBrre9FxPUtuP/FUUAVSSmvL5fLN/hSIHK4/3IA+KGhoDVoAgB2AMCv2zBO7vwEAOcCwCrnHqG1HkPEO4usRGuNiKjMXOc6cIHWWgLAnYhYxAgLACCt9XGI+HpPOgUA3A0AI22u1xphBIDLAKDXvUdrvR8R1wghKswsgsch7ckwpdSd3k4WMTPZLcTCLED0P975H1ZK/WAa8ckRbigR7SvqhtpRqVSGiKgSCDA/2Ml85qBXKNf0bt8tdbN+KITQaZqeLoR4vetOmZ/3lEqle9oIctxgBZl5idF/4HHmr9oMvtxPyQDfe4TSRQQAmN9mIDbp09/fXwGAbY7et3O+zu4PFIAP4zj+mpFId70shHiPgV8LIXLz5V8MRatSyg1F8hs2p5Nl2UX+RrfWmhuNxquK5ktmIhXRYp/gH5yo2B4KeHz79u1dReZ0sgf/FtrrllK+7oh128lHRkZ6nWynu7hthrqiACCxIcAHLCBEZMP9fWNjYws7RdZsECBN07f5e8BKqSRJklVFcjp2vizLXukf9DJr/4p7HbqqaPHixRcg4kmOMWGbwRRCSBMdF3LLoig61cKGiNo8/PH+/v7ROXK2hg3HPmai4gmXO4qi7jiOT/PVdUtLPO7EiFKptJmIHnDcUXv/5QcPHuy316GnR9/i+b8RANSjKPqeC2wB9wsAYI3nJQAzP+WkO3iOEOBprfV+5zsL69pOYgohhNZau3hDw4QvGRwcPG8C5cYlIrN/e6mbvzHu071CiKeskW43kSeEYGaej4irAgb/ibnipBs4xcDAwBgA7AxI8OoOmE8DAJRKpdsBoO64sQQArLW+3D7evjcFWZadCgAn+vEBIv60VdoiNK6//noBAHDo0KEhrfWgH3kj4q45FiyhEIK11rt8ZBsvrhABhBDaqNddAPCwQxQbIb/SMKmaSBFEUXQuIsaO6kCtNUsp7y4KgCXA4ODgCu+Nl8gEPk92wFWzKgiGMZ70AjGIougkZo4dpBZRQwwAP/Ofw8xrG43Gikl+udb6fE//CQB4enR09JFOkdVsNuf59xJRRkRjczJnQHTAidSF+W7B2NjY/E5tS5qm93saREdRNC+KojMmcv6G+i8LGMtHly1bVjf6n4tyVHd396pAUuu5er2+f45JgPX2nnxe86JVJ4v6+/sXFVXDds5arfYAEVUcG6jNA86cMIzVanWxEGJVIFrdXMQF80eapkM+ooUQtYULFyZzjPmtFjhAROzZwLhWq3V3YNw1AMDNN9+8x9iCSapNCHGWi9ilML6jNClFQUSPdcipDACglFocEMn9QgiZm5A6hmN0dLTCzE3Pm4m11sd3IAHAzNENN9yghRCPBhhxhfWCYN68eScYmXNfBeVyufzMdBbU19c36APe09OzfzpSNZsSsHLlymocx3X/+wULFiyYzuRa6xEvHgAhxEpmHrTn58txHLsPRa11JU3TERMyi4JnXCJmBq112epSO5RSTTNX1EEpAnsPtvi/hbeQzWJmPnz4sOrr68uiKPKdiQEzb1wQ5piZQUr5lGdfBYzvx8f2iPkJhgDahuKImPX09Bzo8KVlAgCo1+uLe3t7J3FTvV7fOzAwQNDZ7hUZ0a7lcPGhab5kPdpoNGo9PT2T1EW9Xu/p6enpBGYL796AKzp48ODBxbFRC0sCYtOHiDcZnSgK2gEBAExEa33D3tfXd6nxp8tQfEsSASDTWp9uJMvVyf0A8EVDHOwEXq11ZBN64Jy4GBwcvJKZl3cAMwKABIBTfbUbRVHP0NDQQkuNGwHg4yYZFcOL42iNi+M2RGg6nkoU8Bw0dL4Z73IWzgK8swVzHrxTcvtspIsFzN75IjFLc8wazHEblJsNJM3WKQucRcJGs0mAEHVTrfUWRJQdUp+11mcj4nwvChwGgN8Zg8YdICLTWq9ExFPcyNKon/sBIOmQENb9Xo+IPS7MWutdiPhUBzALY4SXAMBpudLWaDQ+5Z8SZub9zLxwOtSt1Wr3ONt8kpm5Wq3+7bSjppxSBZVKZWi6nJ4kyXb/ldRDhw59dJrwXhXY7swqlco6KwFPByQB6/V62biMRd1QBACdpmnNp3apVBpy6rcV9asjN1bxR39/f6lDeAUA8N69e3ujKDriHYc4jhMDc2Q8xULwZlnWVy6XrXsPiAhCiNEsy3bbOKDqE0BrPSCEWC6E2FNkN8xQ3O407ff/19XVNd/m1ovmgnj8Jt0iGmXzXCgytwNvLwD0eVErzJs370AnMJt5tZTSJjrZZgYQUR133HESjQoagckbxxoRoyiKVkxH9KSUTT8JRUTdHSb4ZnMIAIC9e/f2KaWOOHOUpmljOjkmRFzmPMdu9e7ZsmXLGAIA9Pb27tZaNzy/FxDx5dNx77IsezYQYb+kqEQdrdHd3T0Ikw99IQBQs9kc6TQPZ47yvDzwv5H169ePS8Btt9221zmpMMGZiPgKlyBFOaqvr2/M96NLpdI8400Az52SL8KomsE4jl37IQAg6+/vHysqtVZdVavVRaG9Fq31YzZbF5k3YR4BgDPdlCkAnLFz585uIUSzE52dJMkek9gCrbVARFBKLcyybAgAdpu9Y54rBJBSrrRJSXMQGJRSYzt37hzrQG0iAFCpVDoNERc+b8KEfdZWSymbkv5I4IU1qtVqZ5nrsAD17Qm2CwMVDSUzn150Tm/eGTsZ5+Hgr/zjicz8hH37s+CJOzvnR30XlIhks9lc64vEr413YV08QkQsl8sXOoVWCxmfNE33EVHDy9XESqlVs5jq6NhY2opYrgdERM8AgOpgX5xM1vM1/nMAYHtXV9eTE/46AEC9Xt/GzCO+D10qlV5jHqyLLqirq+u5KIoqAb962Ryzv8zMAhFPOEI3CTFi1l+E+9G4tYsA4Hw3NjK4+Y3Zlo3shTg4OFhh5l/D88dS0EjGJfv3719W5FyMPW0GAIdN6gGch4PWeu0cwrwwHlk3Ir7UlwDnsJYoalOyLLsCABbC868qIYzvvv3EXofuDUT0A8djEUYNDQ0MDLzJiezaBkIIoZVSw14uCGD8DZe5EgsIAIBms7kMxg8ngIMsEEI8AgBw1113FVZpcRy/3fmbAQCJ6GCSJHdNqHnXzSyXyz82B1Qn7ShFUfQOV68VyUwS0dZAqvc0Zu7t4LTZrBFACLHGRMFW1SIRcbPZfAIA4OKLL273XCwalbYaAC7w1Y8Q4r/nz5+/z6opdFQGCiEOAsAvHGRHJny+IMuyC811UREuIKKHHCDsEcAVWZatmeUUciECAMB6X1UCwLO7d+/eWVBahRCClVLXIGKvweOEVjHlkSfiIvQB0Vp/DY58NTWKosi+L+XXSwh+AACZOULErQBQc55FABCXy+VzzXXxVHM5c07MnZf86mBOwcxYKpXO9/U/ADyxevXqmnFDozbms+7qojiO3+WlH1Br/fTo6OhPcp0aWx+CiDYHCioljptWaJgCTZPS0sz8lWkYzssDccAeZu7q1BAT0bOBl/Q+0cl8SqkPhl7Sk1Le4MYzAEfuiKF5V+BfAOAcj4LdWuuvMvMPAaCrTbdUAEAGkzd+rHd1GTPvLOhdIACkAPCKgGeyQGv9SWYeK6DW7IbJckRc6qWRoVQqncrMHyqwXnvvBxynw2701OI4/oavzo4oKeYsZisiHu+lJl4cHQgEjB9v/EYURVf7icjcUgVKqWujKPpSwB5oKL7hETppwFB8Q8Z9GXomT0Xk7flOd73W/awZqd1p3fNWBLDfdQPAFgBY6+0V+ABCQRUS8pY62RsWMwDPTMGUB4+tmPI3URR9sq1iHa4USCnfHMfx7fBivYiO9gJgfEN/d71eP2v+/PmjPve3DK+trlJK/WcURVfkEOExP8/dIiYomQhYuJGx1noUEfdAewWbrNFcCADLAgveYf7fjmG3qYaTEbHkResNoy6iNuBJtdaLEHE5TK5dQabO0ruEEN/KK9jUMqJjZpEkyYnMfNBz+fwaz72mZlyXKT3gfux384lom3/iQCl1LzP3MHN3zv3up9fEIO8M1Ip4jpmPNy+Ud7eYo8s8r5xl2Toiavgly4joJuPX97aYp9t8+pVSvwkVomXmHzslmzuqG2fz71cHKiYSM3Oapu8EaF1B3ClZeX3g9X1m5le2k2936nb+Sc5+wKIC6QKQUt7o7YHocfc/Wz8VPE4lyFsC5ZvJtFE5adodmZzaB9/LqfOfZFl2gbm25JSFdD8RM4tms7nGFHidFPBIKb9sNzFy7p/oB2B+5nXQWOIUG8ybA5kZ9+3bN88U2ztCIl0NkPMpGYb6XF4BV6XUO/ygq1MCWEAWEdGTOSWLR5j5tFYPZGbcuHEjEtHPvR0iTUSHK5XKlNwyQ3VDbQ2La0LRapZl75tiHXan672B/gKWoWa8em5kNm3OMY3XyK8fakrAv9wFMgR4mqbvyOkZcONUQM9g6eISM/82gMA9hw4dGgjN4TaDYOZrAq1O7Druma360fbhfxo4xmiR+SwznxEigrP4blOHdFL1cpOLWdiqIstMFe+WUr4l1MiBmb8QYgJXMpn5vQHk23meYeaVro2Y6d2j3I1mhwP2SSkvsQvxullYBF4b8hqI6AtTiP+0y9cPDw/3KKVCDNAwtULBK1/v9kT7bB7ylVJjWZatm1HVk7MIy0XXt5CEpi1g7SHOSsF8Zt7hIYGIqM7ML8tbxEw0cMiy7KMhFcjM/xxAvpX6eUT0nTzkE1EtSZKLZxX5IV2YZdnGFuLIpjbzAs/DsUj8y5y6aj+baQI46uMkpVTFaxpEhvtXu16S4/Ke7fS+PKJxAxGNSSnfmGf7Zl0Sms3mZ6ZogvYwM1/kcpVRTd3M/EAON16bY0c6beJjy5L9V44D8FnHjZ5o1mDOSlVblKk/0Gg0LjqqyA8tjJnfT0SyRe8taSoJLnT1sZTyUo9Y9vd6SJ92QgDbg0wp9clALTxNRDsrlcqgW3qfmc8iol/k9Mix0fJTzHzWMUF+jnf0RiLaE7ALrkraoZR6z/DwcI+zW/bdnG5KT9br9eUe4ov2EbNbhBsCLXOtynurhcW0rrqJmRs5PcSswf35MWlf1YZ/fjIR3dVGX8atSqk/Z+Z5Rh1tzWnqs9kp7hcXIABa5CdJ8loiGvOaDNl7vmhik9NNd7zRVn3DDGP8/aZNm+Kp0i/HjAibNm2KieizTlcJGepEZ8Z20+TtfaYxEAU4dDMzv8Q8o2yQm0sA1/WVUl5hmoT6aoSJ6CEp5R9JKb9reghwK64nomEp5Vv8uGBODc+Fu4iIHpxqYbZBs3FB2fSV8Rvj7JRSup2uQ7mg/VYCNm3aFCulrgukTNwurs+16MRNXu/IbzHz0lBsMxeJ4IbsfVmWfcy4flMutEWLcouI7zDzycx8ZU4HjSVSyteaUx3coid9K8S76uYhKeUVoaBszg8X2GazeQozf91pexhq8DxV52urOqpea3Q7EtMGsd3e8n67WpfQu5j52pGRkV43IfmC25Pz20FlWbaOmb/pNP90pYDa6P9epE98PubH1RwFunrvUEp9ulqtLnlBcv0UKe0J+5AkySop5eecfDx7KkC1UB+6BYKpBbeTz+mWGET0yzRN/8K2RD+aul4cAyMt7N4oM89TSl0MAFci4qWIuKLFURMxxQkEgPG38wER2dvj9bOboLV+VAhxOyLeIYTY7HG8Plrl1MSxkgiXEADjNfz7+vouQMSLAGA9M58WRdFxM/VMInoaAB6Moug+ANi0ZcuW365fv14eK8QfUwJ4e7z2hPakEwPDw8NDS5cuXYuIZwohTgSA1YjYJ6VcVCqVFgZgF1rrjJl3R1GUKKX2xXG8I0mSx5l5++HDhx9btmxZPeAo8Fx8ZfaYGWy393BoXHXVVREzL2Lm47zPYrubNYUtiueSL///+6l+nXc9J1kAAAAASUVORK5CYII=" alt=lang>
<div id=lm><b data-l=en>English</b><b data-l=ru>Русский</b><b data-l=es>Español</b><b data-l=zh>中文</b></div>
</div></div>
<div class=dim id=hint></div>
<button id=scan>Scan networks</button>
<a id=manual>Enter manually</a>
<div id=wl></div>
<div id=form>
<label id=lssid>Network name (SSID)</label><input id=ssid autocomplete=off>
<label id=lpass>Password</label><div class=pwr><input id=pass type=password autocomplete=off><button type=button class=eye onclick="pwTog('pass',this)" aria-label="Show password"><svg viewBox="0 0 24 24" width="19" height="19"><path d="M12 5.5C5.7 5.5 2.2 12 2.2 12S5.7 18.5 12 18.5 21.8 12 21.8 12 18.3 5.5 12 5.5z" fill="none" stroke="#8ab" stroke-width="1.8"/><circle cx="12" cy="12" r="3.1" fill="none" stroke="#8ab" stroke-width="1.8"/></svg></button></div>
<button id=go>Connect</button>
<div id=msg></div>
</div>
<script>
var L={
ru:['Настройка Wi-Fi','Сканируйте, выберите сеть — поля появятся под ней. Точка доступа: BroClock-PRO (без пароля)','Сканировать сети','Имя сети (SSID)','Пароль','Подключить','<b style="color:#f44">Wait!</b> устройство переключится на домашнюю сеть и сменит адрес.','Устройство в сети! Новый адрес: ','Ошибка: ','Ввести сеть вручную','Подключаемся…','Подключено'],
en:['Wi-Fi Setup','Scan and tap a network — the fields appear right under it. Hotspot: BroClock-PRO (no password)','Scan networks','Network name (SSID)','Password','Connect','<b style="color:#f44">Wait!</b> The device will join your home network and change its address.','Device online! New address: ','Error: ','Enter network manually','Connecting...','Connected'],
es:['Configuracion de Wi-Fi','Escanea y toca una red — los campos aparecen debajo. Punto de acceso: BroClock-PRO (sin contraseña)','Escanear redes','Nombre de la red (SSID)','Contrasena','Conectar','<b style="color:#f44">Wait!</b> el dispositivo se unira a tu red y cambiara de direccion.','Dispositivo en la red! Nueva direccion: ','Error: ','Introducir red manualmente','Conectando...','Conectado'],
zh:['Wi-Fi 设置','扫描并点按网络 — 输入框会出现在它下方。热点：BroClock-PRO（无密码）','扫描网络','网络名称 (SSID)','密码','连接','<b style="color:#f44">Wait!</b> 设备将加入家庭网络并更换地址。','设备已联网！新地址：','错误： ','手动输入网络','连接中…','已连接']};
var cur=null;
try{ cur=localStorage.getItem('sdp-lang2'); }catch(e){}
if(!L[cur]) cur='en';                       // default: English (key bumped so old ru is ignored)
function T(){ return L[cur]; }
var EYE='<svg viewBox="0 0 24 24" width="19" height="19"><path d="M12 5.5C5.7 5.5 2.2 12 2.2 12S5.7 18.5 12 18.5 21.8 12 21.8 12 18.3 5.5 12 5.5z" fill="none" stroke="#8ab" stroke-width="1.8"/><circle cx="12" cy="12" r="3.1" fill="none" stroke="#8ab" stroke-width="1.8"/></svg>';
var EYEX='<svg viewBox="0 0 24 24" width="19" height="19"><path d="M12 5.5C5.7 5.5 2.2 12 2.2 12S5.7 18.5 12 18.5 21.8 12 21.8 12 18.3 5.5 12 5.5z" fill="none" stroke="#567" stroke-width="1.8"/><circle cx="12" cy="12" r="3.1" fill="none" stroke="#567" stroke-width="1.8"/><line x1="4.5" y1="19.5" x2="19.5" y2="4.5" stroke="#f66" stroke-width="2"/></svg>';
function pwTog(id,b){
  var i=document.getElementById(id);
  var on=i.type=='password';
  i.type=on?'text':'password';
  b.innerHTML=on?EYEX:EYE;
  i.focus();
}
function applyLang(){
  var t=T();
  document.title='BroClock PRO - '+t[0];
  document.getElementById('t').textContent=t[0];
  document.getElementById('hint').textContent=t[1];
  document.getElementById('scan').textContent=T()[2];
  document.getElementById('lssid').textContent=t[3];
  document.getElementById('lpass').textContent=t[4];
  var gob=document.getElementById('go');
  if(!gob.disabled) gob.textContent=t[5];
  document.getElementById('manual').textContent=t[9];
  var ms=document.querySelectorAll('#lm b');
  for(var i=0;i<ms.length;i++){
    var isCur=(ms[i].getAttribute('data-l')===cur);
    ms[i].className=isCur?'sel':'';
    if(isCur) document.getElementById('ln').textContent=ms[i].textContent;
  }
}
document.getElementById('lb').onclick=function(ev){
  var m=document.getElementById('lm');
  m.style.display=(m.style.display==='block')?'none':'block';
  ev.stopPropagation();
};
document.addEventListener('click',function(){ document.getElementById('lm').style.display='none'; });
var _ms=document.querySelectorAll('#lm b');
for(var _i=0;_i<_ms.length;_i++){ _ms[_i].onclick=function(){
  cur=this.getAttribute('data-l');
  try{ localStorage.setItem('sdp-lang2',cur); }catch(e){}
  applyLang();
  document.getElementById('lm').style.display='none';
};}
var busy=false;
function showForm(el){
  var f=document.getElementById('form');
  if(el) el.after(f); else document.getElementById('wl').after(f);
  f.style.display='block';
}
function showNets(x){
  var f=document.getElementById('form');
  document.getElementById('wl').after(f);   // keep it out of the list wipe
  f.style.display='none';
  var ns=(x.networks||[]); ns.sort(function(a,b){return b.rssi-a.rssi;});
  var box=document.getElementById('wl'); box.innerHTML='';
  ns.forEach(function(n){
    var e=document.createElement('div'); e.className='qr';
    e.innerHTML='<b>'+(n.secure?String.fromCharCode(128274):'&#9675;')+'</b> <span></span>';
    e.querySelector('span').textContent=n.ssid;
    e.onclick=function(){
      document.getElementById('ssid').value=n.ssid;
      var all=box.querySelectorAll('.qr'); for(var i=0;i<all.length;i++) all[i].className='qr';
      e.className='qr sel';
      showForm(e);                       // fields right under the picked network
    };
    box.appendChild(e);
  });
}
function scan(){
  if(busy) return; busy=true;
  document.getElementById('scan').textContent='...';
  fetch('/api/wifiscan').then(function(r){return r.json();}).then(function p(x){
    if(x.scanning) return fetch('/api/wifiscan').then(function(r){return r.json();}).then(p);
    busy=false; document.getElementById('scan').textContent=T()[2]; showNets(x);
  }).catch(function(){busy=false; document.getElementById('scan').textContent=T()[2];});
}
document.getElementById('scan').onclick=scan;
document.getElementById('manual').onclick=function(){ showForm(null); document.getElementById('ssid').focus(); };
document.getElementById('go').onclick=function(){
  var s=document.getElementById('ssid').value.trim();
  var p=document.getElementById('pass').value;
  var m=document.getElementById('msg');
  if(!s){ m.textContent=T()[8]+'ssid'; return; }
  m.style.color=''; m.style.fontWeight=''; m.innerHTML=T()[6];
  var go=document.getElementById('go');
  go.disabled=true; go.textContent=T()[10];
  var tries=0;
  function sendWifi(){
    fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},
      body:JSON.stringify({ssid:s,pass:p})})
    .then(function(r){return r.json();})
    .then(function(x){
      if(!x.ok){
        m.textContent=T()[8]+JSON.stringify(x);
        go.disabled=false; go.textContent=T()[5];
        return;
      }
      var iv=setInterval(function(){
        fetch('/api/status').then(function(r){return r.json();}).then(function(st){
          if(st.wifi && st.wifi.connected){
            clearInterval(iv);
            m.style.color='#f44'; m.style.fontWeight='700';
            m.innerHTML=T()[7]+'<a href="http://'+st.wifi.ip+'" style="color:#6cf;text-decoration:underline">'+st.wifi.ip+'</a>';
            go.textContent=T()[11];
            setTimeout(function(){ try{ window.close(); }catch(e){} },2500);
          }
        }).catch(function(){});
      },2000);
    }).catch(function(){
      if(++tries<2){ setTimeout(function(){ m.innerHTML=T()[6]; sendWifi(); },1500); return; }
      m.textContent=T()[8]+'network';
      go.disabled=false; go.textContent=T()[5];
    });
  }
  sendWifi();
};
applyLang();
scan();
</script></body></html>
)HTML";

// icon cache upload (multipart POST /api/iconup, curl -F "file=@icons.bin"):
// streams straight into LittleFS, replacing the previous cache atomically
// enough for our purpose - a torn file fails the SDPI header check.
static File icUp;
static bool icUpOk = false;
static void iconUploadDone(){
  server.send(icUpOk ? 200 : 500, "application/json",
              icUpOk ? "{\"ok\":1}" : "{\"error\":\"write\"}");
}
static void iconUpload(){
  HTTPUpload& u = server.upload();
  if(u.status == UPLOAD_FILE_START){
    icUpOk = true;
    icUp = LittleFS.open(ICO_CACHE, "w");       // truncates the old cache
    if(!icUp) icUpOk = false;
  } else if(u.status == UPLOAD_FILE_WRITE){
    if(icUp && icUp.write(u.buf, u.currentSize) != u.currentSize) icUpOk = false;
  } else if(u.status == UPLOAD_FILE_END){
    if(icUp) icUp.close();
    icF.close(); icFIdx = -1; icCnt = -1;       // force header re-probe
  } else if(u.status == UPLOAD_FILE_ABORTED){
    if(icUp) icUp.close();
    LittleFS.remove(ICO_CACHE);
    icF.close(); icFIdx = -1; icCnt = -1;
    icUpOk = false;
  }
}

static void handleRoot(){
  // the JS here fixes GIF handling bugs constantly - never serve a stale copy
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  if(!cfg.wifiSsid.length()){ server.send_P(200, "text/html", SETUP_PAGE); return; }
  server.send_P(200, "text/html", PAGE);
}

// photo upload (multipart POST /api/pupload?kind=jpg): one JPEG validated and
// swapped in place.  kind=anim (GIF cut into frames) was removed: the browser
// normalizes every image to a single JPEG before sending.
static File phUp;
static bool phUpOk = true;
static bool phUpActive = false;
static String phErr;
static uint32_t phUpSz = 0;       // total bytes written this request
static uint32_t phUpFree = 0;     // fs free before the first part

static void phResetState(){
  phUpActive = false;
  phUpSz = 0;
  phUpOk = true;
  phErr = "";
}

static void phFail(const char* code){
  String r = String("{\"error\":\"") + code + "\"}";  // build BEFORE phResetState:
  if(phUp) phUp.close();                             // it clears phErr, and code
  LittleFS.remove(PH_TMP);                           // may point into it
  phResetState();
  server.send(400, "application/json", r);
}

static void phUploadDone(){
  if(!phUpActive){ phFail("empty"); return; }
  if(!phUpOk){ phFail(phErr.length() ? phErr.c_str() : "write"); return; }
  if(phUp) phUp.close();

  File f = LittleFS.open(PH_TMP, "r");
  int w = 0, h = 0;
  bool prog = false;
  if(!f || !jpgDims(f, w, h, prog)){ if(f) f.close(); phFail("format"); return; }
  f.close();
  if(prog){ phFail("prog"); return; }
  if(w > 4096 || h > 4096){ phFail("dims"); return; }
  LittleFS.remove(PH_JPG);      // rename target must not exist
  LittleFS.remove(PH_META);
  if(!LittleFS.rename(PH_TMP, PH_JPG)){ phFail("rename"); return; }
  File m = LittleFS.open(PH_META, "w");
  if(!m){ phFail("write"); return; }
  m.print("1 ");
  m.print(w);
  m.print(' ');
  m.println(h);
  m.close();
  phKind = 1; phW = w; phH = h;
  File jf = LittleFS.open(PH_JPG, "r");
  phSz = jf ? jf.size() : 0;
  if(jf) jf.close();
  phResetState();
  server.send(200, "application/json", "{\"ok\":1}");
  mark(RM_ALL);
}

static void phUpload(){
  HTTPUpload& u = server.upload();
  if(u.status == UPLOAD_FILE_START){
    if(!phUpActive){
      phUpActive = true;
      if(server.arg("kind") == "anim"){ phUpOk = false; phErr = "frames"; return; }
      phUpOk = true;
      phErr = "";
      phUpSz = 0;
      FSInfo fi;
      LittleFS.info(fi);
      phUpFree = (fi.totalBytes > fi.usedBytes) ? (fi.totalBytes - fi.usedBytes) : 0;
      LittleFS.remove(PH_TMP);
    }
    if(!phUpOk) return;
    phUp = LittleFS.open(PH_TMP, "w");
    if(!phUp){ phUpOk = false; phErr = "write"; return; }
  } else if(u.status == UPLOAD_FILE_WRITE){
    if(!phUpOk || !phUp) return;
    uint32_t cap = 262144UL;
    if(phUpSz + u.currentSize > cap){ phUpOk = false; phErr = "big"; return; }
    if(phUpSz + u.currentSize + 40960UL > phUpFree){ phUpOk = false; phErr = "fs"; return; }
    if(phUp.write(u.buf, u.currentSize) != u.currentSize){ phUpOk = false; phErr = "write"; return; }
    phUpSz += u.currentSize;
  } else if(u.status == UPLOAD_FILE_END){
    if(phUp) phUp.close();
  } else if(u.status == UPLOAD_FILE_ABORTED){
    if(phUp) phUp.close();
    phUpOk = false;
    phErr = "abort";
  }
}

// JSON string escaping into the pre-reserved output buffer
static void jsonEsc(String& out, const String& s){
  out += '"';
  for(uint16_t i = 0; i < s.length(); i++){
    char c = s[i];
    if(c == '"' || c == '\\'){ out += '\\'; out += c; }
    else if(c < 32) out += ' ';
    else out += c;
  }
  out += '"';
}

// /api/status used to build the whole payload in an ArduinoJson document:
// every 5s browser poll allocated ~15 pool chunks + a 4.5k String, freed in
// a different order - the heap never healed, mfb sagged to ~10k, and the
// TLS fetch starved lwip (bodies truncated at ~1.5k, candles dying mid-
// transfer).  Now serialized by hand into ONE reserved String: a single
// same-size malloc per poll lands in the same slot every time.
// rescue arena in cores/esp8266/heap.cpp (serves small SDK pvPort* allocs
// when umm has no contiguous block - the Exception(29) crash trigger)
extern "C" uint32_t sdpro_rescue_hits;
extern "C" uint32_t sdpro_rescue_calls;
extern "C" uint32_t sdpro_rescue_oom;

static void handleStatus(){
  String out;
  out.reserve(2200);
  out += "{\"theme\":\""; out += cfg.theme;
  out += "\",\"themes\":[";
  for(int i = 0; i < THEME_N; i++){
    if(i) out += ',';
    out += "{\"key\":\""; out += THEMES[i].key;
    out += "\",\"name\":\""; out += themeName(i);
    out += "\",\"on\":"; out += themeEnabled(THEMES[i].key) ? "true" : "false";
    out += '}';
  }
  out += "],\"label\":\""; out += labelOf(cfg.sym);
  out += "\",\"sym\":\""; out += cfg.sym;
  out += "\",\"syms\":\""; out += cfg.syms;
  // q3/price/chg deliberately NOT serialized: they only mirror what the
  // device screen shows, the web UI never rendered them and every status
  // poll paid String() cost for the dead payload
  out += "\",\"mkok\":";    out += mkOk ? "true" : "false";
  out += ",\"wxok\":";    out += wxOk ? "true" : "false";
  out += ",\"wxerr\":";      jsonEsc(out, errLoc(wxErr));
  out += ",\"q3age\":\""; out += q3At ? String((millis() - q3At) / 1000) +
                                LG(" с назад", "s ago", "s atrás", "秒前") : String("-");
  out += "\"";
  out += ",\"wxage\":\""; out += wxOk ? String((millis() - wxAt) / 1000) + LG(" с назад", "s ago", "s atrás", "秒前") : String("-");
  out += "\"";
  // price panel diagnostics: string + measured widths for the font rule
  {
    uint8_t pf = 7;
    String ps = mkOk ? priceBig(mkPrice, pf) : String("--");
    out += ",\"plen\":";  out += String(ps.length());
    out += ",\"pfont\":";   out += String(pf);
    out += ",\"pw7\":";     out += String((int)tft.textWidth(ps, 7));
    out += ",\"pw6\":";     out += String((int)tft.textWidth(ps, 6));
    out += ",\"pw4\":";     out += String((int)tft.textWidth(ps, 4));
  }
  { // SDK zalloc rescue arena diagnostics (heap.cpp patch)
    out += ",\"resc\":";  out += String((unsigned long)sdpro_rescue_hits);
    out += ",\"rescC\":"; out += String((unsigned long)sdpro_rescue_calls);
    out += ",\"rescO\":"; out += String((unsigned long)sdpro_rescue_oom);
  }
  out += ",\"range\":\""; out += cfg.range;
  out += "\",\"chart\":\""; out += cfg.chart;
  out += "\",\"cd\":";  out += String(cdN);
  out += ",\"ln\":";    out += String(lnN);
  { // icon cache size (0 = not uploaded yet) + lookup diagnostics
    int sz = 0;
    if(LittleFS.exists(ICO_CACHE)){ File f = LittleFS.open(ICO_CACHE, "r"); if(f){ sz = f.size(); f.close(); } }
    out += ",\"icn\":"; out += String(sz);
    int ci = catalogIdx(cfg.sym);
    out += ",\"icidx\":";  out += String(ci);
    out += ",\"icopen\":"; out += (ci >= 0 && icF && icFIdx == ci) ? "1" : "0";
  }
  out += ",\"interval\":"; out += String(cfg.interval);
  out += ",\"gifnum\":";   out += String(cfg.gifnum);
  out += ",\"hol\":";      out += cfg.hol ? "true" : "false";
  out += ",\"hctry\":";    jsonEsc(out, cfg.hctry);
  out += ",\"hctryG\":";   jsonEsc(out, hctryGeo);
  out += ",\"holN\":";     out += String(holN);
  out += ",\"holY\":";     out += String(holYear);
  {
    time_t hn = time(nullptr);
    struct tm ht;
    localtime_r(&hn, &ht);
    String nx = (hn > 1600000000UL && holOk && holYear == (uint16_t)(ht.tm_year + 1900))
                ? holNextStr(ht.tm_mon + 1, ht.tm_mday) : "";
    out += ",\"holNext\":"; jsonEsc(out, nx);
    out += ",\"mon\":"; out += String(ht.tm_mon + 1);
  }
  out += ",\"bgcolor\":\""; out += rgb565ToHex(cfg.bgcolor);
  out += "\",\"carousel\":"; out += cfg.carousel ? "true" : "false";
  out += ",\"refresh\":";    out += String(cfg.refresh);
  out += ",\"brightness\":"; out += String(cfg.brightness);
  out += ",\"nmode\":";      out += cfg.nmode ? "true" : "false";
  out += ",\"nfrom\":";      jsonEsc(out, cfg.nfrom);
  out += ",\"nto\":";        jsonEsc(out, cfg.nto);
  out += ",\"nbr\":";        out += String(cfg.nbr);
  out += ",\"nact\":";       out += nightActive() ? "true" : "false";
  out += ",\"tfmt\":";       jsonEsc(out, cfg.tfmt);
  out += ",\"lang\":\"";     out += cfg.lang;
  out += "\",\"wprov\":\"";  out += cfg.wprov;
  out += "\",\"pf\":";
  if(phKind == 1){
    out += "{\"k\":\"jpg\",\"w\":"; out += String(phW);
    out += ",\"h\":"; out += String(phH);
    out += ",\"s\":"; out += String(phSz);
    out += "}";
  } else out += "0";
  out += ",\"tz\":"; jsonEsc(out, cfg.tz);
  out += ",\"city\":"; jsonEsc(out, cfg.city);
  out += ",\"ip\":\"";
  out += WiFi.isConnected() ? WiFi.localIP().toString() : String("ap");
  out += "\",\"wifi\":{\"saved\":"; jsonEsc(out, cfg.wifiSsid);
  out += ",\"ssid\":"; jsonEsc(out, WiFi.isConnected() ? WiFi.SSID() : cfg.wifiSsid);
  out += ",\"connected\":"; out += WiFi.isConnected() ? "true" : "false";
  out += ",\"ip\":\"";
  out += WiFi.isConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  out += "\",\"rssi\":"; out += String(WiFi.isConnected() ? WiFi.RSSI() : 0);
  out += ",\"status\":"; out += String((int)WiFi.status());
  out += ",\"ap\":";     out += ((WiFi.getMode() & WIFI_AP) != 0) ? "true" : "false";
  out += "},\"heap\":"; out += String(ESP.getFreeHeap());
  out += ",\"mfb\":";   out += String(ESP.getMaxFreeBlockSize());   // fragmentation
  out += ",\"rst\":"; jsonEsc(out, ESP.getResetReason());
  out += ",\"rstinfo\":"; jsonEsc(out, ESP.getResetInfo());   // full exception text + EPC, survives reboot
  out += ",\"httpMs\":"; out += String(lastHttpMs);
  out += ",\"phase\":\""; out += String(lastPhase) + ":" + lastPhaseMs + "ms";
  out += "\",\"cdi\":\""; out += String(cdLen) + "B " + (cdChunk ? "chunked" : "plain");
  {
    time_t now = time(nullptr);
    struct tm lt;
    localtime_r(&now, &lt);
    out += "\",\"time\":\"";
    if(now > 1577836800L){   // NTP not synced yet -> epoch 1970
      if(is12())
        out += String(hour12(lt.tm_hour)) + ":" + two(lt.tm_min) + ":" + two(lt.tm_sec) + " " + ampm(lt.tm_hour);
      else
        out += two(lt.tm_hour) + ":" + two(lt.tm_min) + ":" + two(lt.tm_sec);
    } else out += "--:--:--";
  }
  out += "\",\"uptime\":\""; out += String((millis() - bootMs) / 1000) + "s";
  out += "\",\"age\":\""; out += mkOk ? String((millis() - mkAt) / 1000) +
                                LG(" с назад", "s ago", "s atrás", "秒前")
                                     : LG("нет данных", "no data", "sin datos", "无数据");
  // only REAL market-path failures: mkErr is shared with weather/geocode
  out += "\",\"err\":";
  jsonEsc(out, errLoc(mkErrReal));
  out += '}';
  server.send(200, "application/json", out);
}

static void handleSet(){
  if(!server.hasArg("key")){ server.send(400, "application/json", "{\"error\":\"key\"}"); return; }
  String k = server.arg("key"), v = server.arg("value");
  bool needReload = false;

  if(k == "theme"){
    if(themeExists(v)){
      cfg.theme = v;
      if(!themeEnabled(v)){                    // keep the current theme checked
        if(cfg.enabled.length()) cfg.enabled += ",";
        cfg.enabled += v;
      }
      themeSince = millis();
      mark(RM_FULL);
      bumpWx();                             // keep cache, top up only what's missing
    }
  } else if(k == "themes"){
    String csv = "";
    String item = "";
    for(uint8_t c = 0; c <= v.length(); c++){
      char ch = (c < v.length()) ? v[c] : ',';
      if(ch == ','){
        item.trim();
        if(item.length() && themeExists(item)){
          if(csv.length()) csv += ",";
          csv += item;
        }
        item = "";
      } else item += ch;
    }
    if(csv.length() == 0){ server.send(400, "application/json", "{\"error\":\"empty\"}"); return; }
    cfg.enabled = csv;
    if(!themeEnabled(cfg.theme)){
      cfg.theme = ""; 
      for(int i = 0; i < THEME_N; i++) if(themeEnabled(THEMES[i].key)){ cfg.theme = THEMES[i].key; break; }
      themeSince = millis();
      mark(RM_FULL);
    }
  } else if(k == "carousel"){
    cfg.carousel = (v == "1" || v == "true");
  } else if(k == "interval"){
    int n = v.toInt(); if(n >= 5 && n <= 900) cfg.interval = n;
  } else if(k == "sym"){
    String s = v; s.trim(); s.toLowerCase();
    if(!validId(s)){ server.send(400, "application/json", "{\"error\":\"id\"}"); return; }
    cfg.sym = s; cdN = 0; lnN = 0; mkOk = false; needReload = true;
    mark(RM_HEAD | RM_PRICE | RM_CHART | RM_FOOT);
  } else if(k == "syms"){
    String s = sanitizeSyms(v);
    if(!s.length()){ server.send(400, "application/json", "{\"error\":\"ids\"}"); return; }
    cfg.syms = s;
    q3Ok = false;
    for(int i = 0; i < NQ; i++) q3[i] = Quote();
    pokeWx();
    needReload = true;
    mark(RM_PRICE);
  } else if(k == "gifnum"){
    int n = v.toInt();
    if(n >= 0 && n < ANIM_N){
      cfg.gifnum = n;
      animFrame = 0;
      animNext = 0;
      mark(RM_CHART | RM_FOOT);
    } else { server.send(400, "application/json", "{\"error\":\"gifnum\"}"); return; }
  } else if(k == "pdel"){
    phWipe();
    phKind = 0; phW = phH = 0; phSz = 0;
    mark(RM_ALL);
    server.send(200, "application/json", "{\"ok\":1}");   // nothing to persist
    return;
  } else if(k == "tfmt"){
    if(v != "12" && v != "24"){
      server.send(400, "application/json", "{\"error\":\"bad format\"}");
      return;
    }
    cfg.tfmt = v;
    mark(RM_ALL);                    // repaint every clock face immediately
  } else if(k == "lang"){
    if(v != "ru" && v != "en" && v != "es" && v != "zh"){
      server.send(400, "application/json", "{\"error\":\"lang\"}");
      return;
    }
    cfg.lang = v;
    mark(RM_ALL);                    // repaint the screen in the new language
    // The city name comes back localized from the geocoder, so re-geocode and
    // refetch weather+forecast right away - but KEEP the cached data on screen
    // while they run. The old wxOk=false/fcn=0 discard blanked the weather
    // until the refetch finished ("data hangs"), and the TLS handshake racing
    // the still-open browser tab failed/crashed: coolUntil now holds every
    // fetch back for 5s so the browser finishes its traffic first.
    omGeoInvalidate();
    uint32_t lt = millis();
    wxAt  = lt - 600000UL;           // age the stamps: gates pass, data stays
    fcAt  = lt - 1800000UL;
    wxTry = fcTry = lt - 60000UL;    // probe gates satisfied -> immediate fetch
    coolUntil = lt + 5000UL;
  } else if(k == "wprov"){
    if(v != "owm" && v != "ometo"){
      server.send(400, "application/json", "{\"error\":\"wprov\"}");
      return;
    }
    cfg.wprov = v;
    uint32_t t = millis();          // make the age gates pass right now so both
    wxAt = t - 600000UL;            // current weather and forecast refetch from
    fcAt = t - 1800000UL;           // the new source without blanking the data
    wxTry = fcTry = t - 61000UL;
    omGeoInvalidate();              // stored coordinates may belong elsewhere
  } else if(k == "chart"){
    // data is already shared: line is built from the same candles -> no network wait
    cfg.chart = (v == "line") ? "line" : "candle";
    if(cfg.chart == "line" && lnN < 2) buildLineFromCandles();
    mark(RM_CHART | RM_FOOT);
  } else if(k == "brightness"){
    int n = v.toInt(); if(n >= 0 && n <= 100){ cfg.brightness = n; applyBrightness(); }
  } else if(k == "bgcolor"){
    // accepts #RRGGBB / RRGGBB / 0xRRGGBB
    String s = v; s.trim();
    if(s.startsWith("#")) s = s.substring(1);
    if(s.startsWith("0x") || s.startsWith("0X")) s = s.substring(2);
    bool hexOk = (s.length() == 6);
    for(uint8_t i = 0; hexOk && i < 6; i++){
      char ch = s[i];
      hexOk = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
    }
    if(!hexOk){ server.send(400, "application/json", "{\"error\":\"color\"}"); return; }
    uint32_t rgb = strtoul(s.c_str(), nullptr, 16);
    uint16_t c = (uint16_t)((((rgb >> 19) & 0x1F) << 11) |
                            (((rgb >> 10) & 0x3F) << 5)  |
                            ((rgb >> 3) & 0x1F));
    if(c != cfg.bgcolor){
      cfg.bgcolor = c; C_BG = c;
      mark(RM_ALL);                   // repaint everything on the new background
    }
  } else if(k == "nmode"){
    cfg.nmode = (v == "1" || v == "true");
    applyBrightness();
  } else if(k == "nfrom" || k == "nto"){
    String s = v; s.trim();
    if(parseHM(s) < 0){
      server.send(400, "application/json", "{\"error\":\"bad time\"}");
      return;
    }
    if(k == "nfrom") cfg.nfrom = s; else cfg.nto = s;
    applyBrightness();
  } else if(k == "nbr"){
    int n = v.toInt();
    if(n < 0 || n > 100){
      server.send(400, "application/json", "{\"error\":\"bad brightness\"}");
      return;
    }
    cfg.nbr = n;
    applyBrightness();
  } else if(k == "hol"){
    cfg.hol = (v == "1" || v == "true");
    holTry = 0;                       // allow the fetch right on enable
    holDelayUntil = millis() + 4000UL;
  } else if(k == "hctry"){
    String s = v; s.trim(); s.toUpperCase();
    if(s == "AUTO") s = "";
    if(s.length() && !(s.length() == 2 && isAlpha(s[0]) && isAlpha(s[1]))){
      server.send(400, "application/json", "{\"error\":\"bad country code\"}");
      return;
    }
    cfg.hctry = s;
    holOk = false;                    // the stored bitmask belongs to the old country
    holYear = 0; holN = 0; holVer++;
    holTry = 0;
    holDelayUntil = millis() + 4000UL;
  } else if(k == "refresh"){
    int n = v.toInt(); if(n >= 10 && n <= 3600) cfg.refresh = n;
  } else if(k == "city"){
    String c = v; c.trim();
    if(c.length() <= 40){            // "" clears the city, weather waits
      cfg.city = c;
      wxOk = false; fcn = 0;
      if(c.length()) wxErr = ""; else wxErr = "no city";
      omGeoInvalidate();             // the stored coords belong to the old city
      pokeWx();
    }
  } else if(k == "tz"){
    cfg.tz = v.length() ? v : "MSK-3";
    configTime(cfg.tz.c_str(), "pool.ntp.org", "time.google.com", "time.cloudflare.com");
    mark(RM_PRICE | RM_CHART);
  } else {
    server.send(404, "application/json", "{\"error\":\"unknown key\"}");
    return;
  }

  saveCfg();
  // fetches must NOT run inside the request handler: the live browser
  // connection + LittleFS buffers leave no room for BearSSL (it failed with
  // "connection failed (-1) ... unable to allocate memory").  Just mark the
  // data stale - loop()'s next poll slot does the actual HTTP.  coolUntil
  // holds every fetch back for 5s so the browser can finish its traffic
  // before the handshake grabs the largest free block.
  if(k == "sym"){
    cdN = 0; lnN = 0; mkAt = 0; lastFetchTry = 0; candlesForce = true; coolUntil = millis() + 5000UL;
    // instant last-known data for the new sym+range, even when the heap is too
    // fragmented for the TLS handshake right after the switch: the panel and
    // chart show cached values, not "--"/emptiness
    float p, ch;
    uint32_t ts;
    if(priceCacheLookup(cfg.sym, p, ch, ts)){
      mkPrice = p; mkChg = ch; mkOk = true;
      mkAt = stampFor(cacheAgeSec(ts));
      mark(RM_HEAD | RM_PRICE | RM_FOOT);
    }
    if(chartCacheLookup(cfg.sym, cfg.range)) mark(RM_CHART);   // instant candles for a seen coin
  }
  if(k == "syms" && cfg.theme == "crypto3"){ q3At = 0; q3Try = millis() - 21000UL; coolUntil = millis() + 3000UL; }
  server.send(200, "application/json",
              needReload ? "{\"ok\":1,\"need\":1}" : "{\"ok\":1}");
}

static void handleRefresh(){
  // same rule as handleSet: only invalidate the caches here, the requests
  // happen in loop() where there are no competing socket/FS buffers
  // backdate, never zero: the gates compare absolute millis(), so mkAt=0 read
  // as "fetched at boot" - dead button until 60s uptime and an age readout
  // counting the whole uptime.  A backdated stamp opens every age gate now.
  mkAt = millis() - (uint32_t)cfg.refresh * 1000UL - 1000UL;
  lastFetchTry = 0;
  mkErrReal = "";
  candlesForce = true;
  coolUntil = millis() + 2000UL;   // let any in-flight fetch finish first
  pokeWx();
  if(cfg.theme == "crypto3"){ q3At = millis() - (uint32_t)cfg.refresh * 1000UL - 1000UL; q3Try = millis() - 21000UL; }
  server.send(200, "application/json", "{\"ok\":1}");
}

// ---------------------------------------------------------------- wifi management
static uint32_t wifiTryMs = 0;    // last connect attempt
static uint32_t setupDoneAt = 0;   // setup page saved: AP can drop early
static uint32_t wifiApOffAt = 0;  // drop the fallback AP this long after STA is up

static bool     scanRunning = false;
static uint32_t scanStarted = 0;

static void handleWifiScan(){
  // Async scan: a blocking WiFi.scanNetworks() inside a request handler makes
  // the STA leave/return to the AP in the middle of the SDK's beacon handling
  // and crashed it (Exception 29 in ieee80211_setup_ratetable /
  // cnx_update_bss_more).  Start it and let the page poll; the SDK services
  // beacons between calls and the loop() stays responsive.
  if(scanRunning){
    int n = WiFi.scanComplete();
    if(n == WIFI_SCAN_RUNNING && millis() - scanStarted < 15000UL){
      server.send(200, "application/json", "{\"scanning\":1}");
      return;
    }
    scanRunning = false;
    if(n < 0) n = 0;                          // failed / timed out -> empty
    JsonDocument d;
    JsonArray arr = d["networks"].to<JsonArray>();
    for(int i = 0; i < n && i < 20; i++){
      JsonObject o = arr.add<JsonObject>();
      o["ssid"] = WiFi.SSID(i);
      o["rssi"] = WiFi.RSSI(i);
      o["secure"] = WiFi.encryptionType(i) != ENC_TYPE_NONE;
    }
    WiFi.scanDelete();
    String out;
    serializeJson(d, out);
    server.send(200, "application/json", out);
    return;
  }
  if(ESP.getMaxFreeBlockSize() < 8000UL){
    server.send(503, "application/json", "{\"error\":\"busy\"}");
    return;
  }
  if(WiFi.getMode() == WIFI_AP) WiFi.mode(WIFI_AP_STA);   // scan needs STA
  if(WiFi.scanNetworks(true) == WIFI_SCAN_FAILED){
    server.send(500, "application/json", "{\"error\":\"scan\"}");
    return;
  }
  scanRunning = true;
  scanStarted = millis();
  server.send(200, "application/json", "{\"scanning\":1}");
}

static void handleWifi(){
  JsonDocument d;
  if(deserializeJson(d, server.arg("plain"))){
    server.send(400, "application/json", "{\"error\":\"json\"}");
    return;
  }
  if(!d["forget"].isNull()){
    cfg.wifiSsid = "";
    cfg.wifiPass = "";
    saveCfg();
    // reply first: mode(WIFI_AP) below tears the STA TCP connection down, and
    // ArduinoJson 7 `d["forget"] | false` is false for integer 1 (what the page
    // sends), so a presence check is used - otherwise the button did nothing.
    server.send(200, "application/json", "{\"ok\":1,\"forget\":1}");
    delay(250);
    WiFi.setAutoReconnect(false);
    WiFi.persistent(true);
    WiFi.disconnect();
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP);
    WiFi.softAP("BroClock-PRO");
    return;
  }
  String ssid = d["ssid"] | "";
  String pass = d["pass"] | "";
  ssid.trim();
  if(!ssid.length() || ssid.length() > 32 || pass.length() > 64){
    server.send(400, "application/json", "{\"error\":\"ssid\"}");
    return;
  }
  // blank password for the network that is already saved: keep the stored one -
  // an empty string would be saved as an open network and lock the user out
  if(!pass.length() && ssid == cfg.wifiSsid && cfg.wifiPass.length())
    pass = cfg.wifiPass;
  cfg.wifiSsid = ssid;
  cfg.wifiPass = pass;
  saveCfg();
  // reply first, then move the radio - same order as the forget branch:
  // mode(AP->AP_STA) / WiFi.begin() before the response leaves the AP kills
  // the TCP connection and the page showed "Error" on the first attempt
  // (the second one worked: the mode was already AP_STA by then).
  server.send(200, "application/json", "{\"ok\":1,\"connecting\":1}");
  delay(250);
  WiFi.setAutoReconnect(true);
  wifiTryMs = millis();
  wifiApOffAt = 0;
  setupDoneAt = millis();   // phone is on the setup page now; bound the AP
  if(WiFi.getMode() == WIFI_AP) WiFi.mode(WIFI_AP_STA);   // keep AP alive while connecting
  WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
}

// local coin catalog (top-250) - the CoinGecko /search endpoint is too heavy
// for the 8266; catalog itself is included at the top of the file.
// All comparisons run directly on PROGMEM against the lowercased query - the
// old version built three Strings per coin per pass (~4500 heap allocs per
// keystroke) which shredded the heap and later starved BearSSL (-1000 OOM).
static inline char lowerA(char ch){ return (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch; }

static bool pgmEqCI(const char* pgm, const char* q){
  for(;;){
    char a = lowerA(pgm_read_byte(pgm));
    char b = *q;
    if(a == 0 || b == 0) return a == b;
    if(a != b) return false;
    pgm++; q++;
  }
}

static bool pgmStartsCI(const char* pgm, const char* q){
  while(*q){
    if(lowerA(pgm_read_byte(pgm)) != *q) return false;
    pgm++; q++;
  }
  return true;
}

static bool pgmContainsCI(const char* pgm, const char* q, size_t ql){
  size_t hl = 0;
  while(pgm_read_byte(&pgm[hl])) hl++;
  if(ql == 0 || hl < ql) return ql == 0;
  for(size_t start = 0; start + ql <= hl; start++){
    size_t i = 0;
    while(i < ql && lowerA(pgm_read_byte(&pgm[start + i])) == q[i]) i++;
    if(i == ql) return true;
  }
  return false;
}

// scoring: 0 = symbol/id exact, 1 = symbol prefix, 2 = id prefix,
// 3 = substring, 4 = name prefix, 5 = name substring
static int coinMatch(uint16_t idx, const char* q, size_t ql){
  CoinRef c;
  memcpy_P(&c, &COINS_LOCAL[idx], sizeof(c));
  if(!c.sym || !c.id) return -1;
  if(pgmEqCI(c.sym, q) || pgmEqCI(c.id, q)) return 0;
  if(pgmStartsCI(c.sym, q)) return 1;
  if(pgmStartsCI(c.id, q))  return 2;
  if(pgmContainsCI(c.sym, q, ql) || pgmContainsCI(c.id, q, ql)) return 3;
  if(c.name && pgmStartsCI(c.name, q)) return 4;
  if(c.name && pgmContainsCI(c.name, q, ql)) return 5;
  return -1;
}

static void handleSearch(){
  String q = server.arg("q");
  q.trim();
  q.toLowerCase();
  String out = "{\"coins\":[]}";
  if(q.length() >= 1 && q.length() <= 32){
    JsonDocument r;
    JsonArray ra = r["coins"].to<JsonArray>();
    // pass 1..: collect matches in quality order so exact hits come first
    for(int pass = 0; pass < 6; pass++){
      for(uint16_t i = 0; i < COINS_LOCAL_N && ra.size() < 12; i++){
        int m = coinMatch(i, q.c_str(), q.length());
        if(m != pass) continue;
        CoinRef c;
        memcpy_P(&c, &COINS_LOCAL[i], sizeof(c));
        JsonObject o = ra.add<JsonObject>();
        o["id"] = c.id;
        o["name"] = c.name;
        o["symbol"] = c.sym;
        o["rank"] = i + 1;
      }
      if(ra.size() >= 12) break;
    }
    out = ""; serializeJson(r, out);
  }
  server.send(200, "application/json", out);
}

// browser geolocation -> city name (OpenWeatherMap reverse geocoding)
static void handleGeocode(){
  float la = server.arg("lat").toFloat();
  float lo = server.arg("lon").toFloat();
  if(la < -90.0f || la > 90.0f || lo < -180.0f || lo > 180.0f){
    server.send(400, "application/json", "{\"error\":\"bad coords\"}");
    return;
  }
  // TLS inside a request handler competes with the live browser socket for the
  // largest free block (same trap the fetch comments warn about): refuse when
  // the heap is fragmented instead of failing with a half handshake.
  if(ESP.getMaxFreeBlockSize() < 9500UL){
    server.send(503, "application/json", "{\"error\":\"busy\"}");
    return;
  }
  // OWM reverse geocoding needs a PAID Geocoding subscription - free keys get
  // 401, which is why "My location" always failed with "geocoder failed".
  // The keyless BigDataCloud client geocoder answers first; OWM stays as the
  // fallback for the owners of a paid key.
  String nm = "", cc = "";
  {
    String url = String("https://api.bigdatacloud.net/data/reverse-geocode-client?latitude=") +
                 String(la, 4) + "&longitude=" + String(lo, 4) +
                 "&localityLanguage=" + geoLocalityLang();
    static const char* F =
      "{\"city\":true,\"locality\":true,\"principalSubdivision\":true,\"countryCode\":true}";
    JsonDocument d;
    srvSkip = true;                     // keep the listener: closing it would
                                        // drop the client waiting for this reply
    bool gok = httpJson(url, d, F);
    srvSkip = false;
    if(gok){
      const char* cn = d["city"] | "";
      if(!cn || !*cn) cn = d["locality"] | "";
      if(!cn || !*cn) cn = d["principalSubdivision"] | "";
      if(cn && *cn) nm = String(cn);
      cc = String((const char*)(d["countryCode"] | ""));
    }
  }
  if(!nm.length() && OWM_KEY && OWM_KEY[0]){
    String url = String("https://api.openweathermap.org/geo/1.0/reverse?lat=") +
                 String(la, 4) + "&lon=" + String(lo, 4) +
                 "&limit=1&appid=" + OWM_KEY + langQ();
    JsonDocument d;
    srvSkip = true;
    bool gok = httpJson(url, d);
    srvSkip = false;
    if(gok){
      JsonArray a = d.as<JsonArray>();
      if(a.size()){
        const char* an = a[0]["name"] | "";
        if(an && *an){ nm = String(an); cc = String((const char*)(a[0]["country"] | "")); }
      }
    }
  }
  if(!nm.length()){
    server.send(502, "application/json", "{\"error\":\"geocoder failed\"}");
    return;
  }
  JsonDocument r;
  r["city"] = nm;
  r["country"] = cc;
  String out;
  serializeJson(r, out);
  server.send(200, "application/json", out);
}

// city autocomplete for the setup page (Open-Meteo geocoder: free, keyless).
// The stored value is "Name,CC": OWM's geocoder - the first hop of omGeocode
// and the owm source's q= parameter - resolves it for BOTH weather sources.
static void handleCitySuggest(){
  String q = server.arg("q");
  q.trim();
  if(q.length() < 2){
    server.send(200, "application/json", "{\"cities\":[]}");
    return;
  }
  if(q.length() > 40) q = q.substring(0, 40);
  // same in-handler TLS pattern as /api/geocode: refuse while the heap is
  // fragmented instead of dying mid-handshake (Exception 29)
  if(ESP.getMaxFreeBlockSize() < 9500UL){
    server.send(503, "application/json", "{\"error\":\"busy\"}");
    return;
  }
  // a Cyrillic query must search with language=ru: with language=en
  // Open-Meteo matches transliterated names and answers 0 results
  // ("Витебск" -> nothing), while ru finds it as the first hit
  const char* lang = geoLang();
  for(unsigned i = 0; i < q.length(); i++){
    uint8_t c = (uint8_t)q[i];
    if(c >= 0xD0 && c <= 0xD1){ lang = "ru"; break; }
  }
  String url = String("https://geocoding-api.open-meteo.com/v1/search?name=") +
               urlencode(q) + "&count=5&language=" + lang + "&format=json";
  static const char* F =
    "{\"results\":[{\"name\":true,\"admin1\":true,\"country\":true,\"country_code\":true}]}";
  JsonDocument d;
  srvSkip = true;                     // keep the listener while TLS runs here
  bool ok = httpJson(url, d, F);
  srvSkip = false;
  if(!ok){
    server.send(502, "application/json", "{\"error\":\"geocoder failed\"}");
    return;
  }
  JsonDocument r;
  JsonArray ra = r["cities"].to<JsonArray>();
  JsonArray res = d["results"].as<JsonArray>();
  for(JsonVariant v : res){
    String nm = String((const char*)(v["name"] | ""));
    if(!nm.length()) continue;
    String cc = String((const char*)(v["country_code"] | ""));
    String det = String((const char*)(v["admin1"] | ""));
    String ctry = String((const char*)(v["country"] | ""));
    if(det.length() && ctry.length()) det += ", " + ctry;
    else if(!det.length()) det = ctry;
    JsonObject c = ra.add<JsonObject>();
    c["v"] = cc.length() ? nm + "," + cc : nm;
    c["n"] = nm;
    c["d"] = det;
    if(ra.size() >= 5) break;
  }
  String out;
  serializeJson(r, out);
  server.send(200, "application/json", out);
}

// Plain-http pages can't use navigator.geolocation - this endpoint answers
// with the caller's public coordinates (free, keyless ip-api over http).
// Plain WiFiClient on purpose: an http:// URL through httpJson() would try
// a TLS handshake on port 80.
static void handleGeoIp(){
  WiFiClient c;
  c.setTimeout(6000);
  if(!c.connect("ip-api.com", 80)){
    server.send(502, "application/json", "{\"error\":\"ip connect\"}");
    return;
  }
  c.print("GET /json/?fields=status,lat,lon,city,countryCode,timezone,offset HTTP/1.1\r\n"
          "Host: ip-api.com\r\nConnection: close\r\n\r\n");
  String raw;
  uint32_t t0 = millis();
  while(millis() - t0 < 6000){
    while(c.available()) raw += (char)c.read();
    if(raw.length() > 4096) break;        // bound the buffer (ip-api replies are tiny)
    if(!c.connected() && !c.available()) break;
    delay(1);
  }
  c.stop();
  int h = raw.indexOf("\r\n\r\n");
  JsonDocument d;
  if(h < 0 || deserializeJson(d, raw.substring(h + 4)) ||
     strcmp(d["status"] | "", "success") != 0){
    server.send(502, "application/json", "{\"error\":\"ip lookup failed\"}");
    return;
  }
  JsonDocument r;
  r["lat"] = d["lat"];
  r["lon"] = d["lon"];
  const char* gc = d["city"] | "";        // city straight from the IP lookup
  if(gc && *gc){
    r["city"] = gc;
    r["countryCode"] = d["countryCode"] | "";
  }
  const char* tz = d["timezone"] | "";    // IANA zone + raw offset (seconds)
  if(tz && *tz){
    r["timezone"] = tz;
    r["offset"] = d["offset"] | 0;
  }
  String out;
  serializeJson(r, out);
  server.send(200, "application/json", out);
}

// file system / firmware footprint - "what from the original still occupies space"
static void handleFs(){
  JsonDocument d;
  FSInfo fi;
  if(LittleFS.info(fi)){
    d["fsTotal"] = fi.totalBytes;
    d["fsUsed"]  = fi.usedBytes;
    d["fsFree"]  = fi.totalBytes - fi.usedBytes;
  }
  d["sketch"]  = ESP.getSketchSize();
  d["freeHeap"] = ESP.getFreeHeap();
  JsonArray fa = d["files"].to<JsonArray>();
  File dir = LittleFS.open("/", "r");
  uint32_t fsShown = 0;
  if(dir){
    File f = dir.openNextFile();
    while(f){
      JsonObject o = fa.add<JsonObject>();
      o["name"] = f.name();
      o["size"] = f.size();
      fsShown += f.size();
      f.close();
      f = dir.openNextFile();
    }
    dir.close();
  }
  d["filesSum"] = fsShown;
  String out;
  serializeJson(d, out);
  server.send(200, "application/json", out);
}

// debug: raw RGB565 of one animation frame (80*80*2 bytes)
static void handleAnim(){
  int f = server.hasArg("frame") ? server.arg("frame").toInt() : 0;
  int g = server.hasArg("gif")  ? server.arg("gif").toInt()  : cfg.gifnum;
  if(g < 0 || g >= ANIM_N) g = cfg.gifnum < ANIM_N ? cfg.gifnum : 0;
  const AnimTab& a = ANIMS[g];
  if(f < 0 || f >= pgm_read_byte(&a.n)) f = 0;
  const int n = ANIM_W * ANIM_H;   // 6400 px, 12800 bytes on the wire
  const int CH = 1600;             // 3.2k chunk: a full-frame 12.8k malloc
  uint16_t* buf = (uint16_t*)malloc(CH * 2);  // fails while browser/TLS hold the heap
  if(!buf){ server.send(500, "text/plain", "oom"); return; }
  uint8_t* ab = (uint8_t*)malloc(ANIM_MAX_FRAME + 4);
  if(!ab){ free(buf); server.send(500, "text/plain", "oom"); return; }
  const uint8_t* p = animStage(ab, a, f);
  const uint8_t* e = p + ANIM_MAX_FRAME;
  server.setContentLength((size_t)n * 2);
  server.send(200, "application/octet-stream", "");
  int i = 0, o = 0;
  while(i < n && p < e){
    uint8_t b = *p++;
    int c = b >> 4, v = b & 15;
    if(c == 0){ c = *p++; v = *p++ & 15; }
    uint16_t col = pgm_read_word(&a.pal[v]);
    while(c-- && i < n){
      buf[o++] = col; i++;
      if(o == CH){ server.sendContent((const char*)buf, CH * 2); o = 0; }
    }
  }
  while(i < n){                    // pad a short source so Content-Length holds
    buf[o++] = 0; i++;
    if(o == CH){ server.sendContent((const char*)buf, CH * 2); o = 0; }
  }
  if(o) server.sendContent((const char*)buf, o * 2);
  free(ab);
  free(buf);
}

// ---------------------------------------------------------------- OTA upload
// Anti-brick guard for /update: an image is committed only if it carries OUR
// build fingerprint. FW_MARKER is a plain .rodata array built from the string-
//ified build flags below (chip/flash/ld/display pins), so an image built for
// a different board (other pins, flash layout, driver) never passes the
// sliding scan and is discarded before Update.end -> old firmware keeps booting.
#define FW_STR2(x) #x
#define FW_STR(x)  FW_STR2(x)
// OTA scratch region: [0x1E4000, FS_start 0x300000). Below it live the assets:
// FF 0x140000, ANIM 0x17C000, boot splash 0x1B8000..0x1E3445 (boot.bin, 18
// frames). Above it is LittleFS. Declaring exactly this window keeps ANY
// uploaded stream (even a rejected one) from ever touching assets or config.
#define FW_OTA_MAX 0x0011C000UL
static const char FW_MARKER[] =
  "SDPRO_FW|ESP8266|flash=4M|ld=4m1m-big|"
  "driver=" FW_STR(ST7789_DRIVER) "|"
  "cs=" FW_STR(TFT_CS) "|dc=" FW_STR(TFT_DC) "|rst=" FW_STR(TFT_RST) "|bl=" FW_STR(TFT_BL)
  "|blon=" FW_STR(TFT_BACKLIGHT_ON) "|mosi=" FW_STR(TFT_MOSI) "|sclk=" FW_STR(TFT_SCLK) "|";
#define FW_MLEN (sizeof(FW_MARKER) - 1)

enum FwState { FW_IDLE, FW_RUN, FW_REJECT };
static FwState fwState = FW_IDLE;
static bool    fwDone  = false;
static bool    fwMagic = false;
static bool    fwFound = false;
static uint8_t fwWinN  = 0;
static String  fwErr;

static void fwReject(const String& why){
  if(fwState == FW_REJECT) return;
  fwState = FW_REJECT;
  fwErr = why;
}

static void fwHandleDone(){
  if(fwDone){
    server.send(200, "text/plain", "OK");
    delay(300);
    ESP.restart();
    return;
  }
  if(fwState == FW_RUN)
    fwReject("update incomplete (marker missing or stream ended early)");
  if(Update.isRunning()) Update.end(false);   // discard -> old firmware stays
  fwState = FW_IDLE;
  server.send(400, "text/plain", fwErr.length() ? fwErr : "update failed");
}

static void fwUpload(){
  HTTPUpload& u = server.upload();
  if(u.status == UPLOAD_FILE_START){
    fwDone = false; fwMagic = false; fwFound = false; fwWinN = 0;
    fwErr = "";
    String fn = u.filename;
    int sl = fn.lastIndexOf('/');
    int bs = fn.lastIndexOf('\\');
    if(bs > sl) sl = bs;
    if(sl >= 0) fn = fn.substring(sl + 1);
    if(!fn.startsWith("SDP")){
      fwReject("filename must start with SDP (same rule as stock updater)");
      return;
    }
    if(!Update.begin(FW_OTA_MAX)){
      fwReject(String("no space: ") + Update.getErrorString());
      return;
    }
    fwState = FW_RUN;
  }
  else if(u.status == UPLOAD_FILE_WRITE){
    if(fwState != FW_RUN) return;
    if(!fwMagic){
      if(u.currentSize == 0) return;
      if(u.buf[0] != 0xE9){
        fwReject("bad magic byte 0xE9 expected - not an ESP image");
        return;
      }
      fwMagic = true;
    }
    // sliding scan for FW_MARKER across chunk boundaries; naive reset is
    // correct here because the marker has no self-overlapping prefixes
    for(uint16_t i = 0; i < u.currentSize && !fwFound; i++){
      uint8_t c = u.buf[i];
      if(c == (uint8_t)FW_MARKER[fwWinN]){
        if(++fwWinN == FW_MLEN) fwFound = true;
      } else {
        fwWinN = (c == (uint8_t)FW_MARKER[0]) ? 1 : 0;
      }
    }
    if(Update.write(u.buf, u.currentSize) != u.currentSize)
      fwReject(String("write failed: ") + Update.getErrorString());
  }
  else if(u.status == UPLOAD_FILE_END){
    if(fwState != FW_RUN) return;
    if(!fwMagic){ fwReject("empty file"); return; }
    if(!fwFound){ fwReject("no SDPRO fingerprint - image is not built for this device"); return; }
    if(!Update.end(true)){
      fwReject(String("commit failed: ") + Update.getErrorString());
      return;
    }
    fwDone = true;
  }
  else if(u.status == UPLOAD_FILE_ABORTED){
    fwReject("upload aborted");
  }
}

// ---------------------------------------------------------------- setup
void setup(){
  Serial.begin(115200);
  Serial.printf("[boot] rst=%s | %s\n", ESP.getResetReason().c_str(),
                ESP.getResetInfo().c_str());
  bootMs = millis();
  Serial.println("\n[boot] BroClock Pro crypto-ticker");

  pinMode(TFT_BL, OUTPUT);
#ifdef TFT_BACKLIGHT_ON
  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);   // LOW = on (active-low)
#else
  digitalWrite(TFT_BL, HIGH);
#endif
  tft.init();
  tft.setRotation(0);
  bootShow(0);                          // fullscreen winXP boot animation
  bootNext = millis();

  if(!LittleFS.begin()){
    Serial.println("[fs] mount failed, formatting");
    LittleFS.format();
    LittleFS.begin();
  }
  loadCfg();
  applyBrightness();

  themeSince = millis();

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  // The SDK crashes intermittently in ieee80211_setup_ratetable <- cnx_update_bss_more
  // while applying the AP's rate set (Exception 29, excvaddr 0).  Forcing 11G
  // keeps the association off the 802.11b rate table that trips it.
  WiFi.setPhyMode(WIFI_PHY_MODE_11G);
  // modem sleep buffers the router's first replies (DTIM) right after link-up
  // -> the first TCP SYN after a reboot gets no ACK for 15s and the initial
  // candle/price fetches die as "connection failed (-1)", after which the
  // failed TLS sessions drop mfb below the 12k gate and the boot runs stale
  // until the breaker reboots it.  Mains-powered: keep the radio awake.
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  if(cfg.wifiSsid.length()){
    WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
    Serial.printf("[wifi] connecting to %s\n", cfg.wifiSsid.c_str());
    uint32_t t0 = millis();
    while(WiFi.status() != WL_CONNECTED && millis() - t0 < 20000){
      delay(80); Serial.print(".");
      bootTick(millis());               // play the boot animation while connecting
    }
    Serial.println();
    if(WiFi.status() == WL_CONNECTED){
      Serial.printf("[wifi] ok %s\n", WiFi.localIP().toString().c_str());
    } else {
      WiFi.mode(WIFI_AP);
      WiFi.softAP("BroClock-PRO");
      Serial.printf("[wifi] AP BroClock-PRO %s\n", WiFi.softAPIP().toString().c_str());
    }
  } else {
    // first boot / after Forget: straight into the provisioning portal,
    // no 20s dead wait while a bogus begin() times out
    WiFi.mode(WIFI_AP);
    WiFi.softAP("BroClock-PRO");
    portalStart();
    Serial.printf("[wifi] setup portal BroClock-PRO %s\n",
                  WiFi.softAPIP().toString().c_str());
  }

  fetchHoldUntil = millis() + 12000UL;   // first fetches start on a settled link
  applyDns();
  Serial.printf("[net] dns1=%s dns2=%s\n", WiFi.dnsIP(0).toString().c_str(),
                WiFi.dnsIP(1).toString().c_str());

  // three servers: the first DNS resolution may fail right after link-up
  configTime(cfg.tz.c_str(), "pool.ntp.org", "time.google.com", "time.cloudflare.com");

  server.on("/", handleRoot);
  server.on("/api/status", handleStatus);
  server.on("/api/set", handleSet);
  server.on("/api/refresh", handleRefresh);
  server.on("/api/search", handleSearch);
  server.on("/api/geocode", handleGeocode);
  server.on("/api/city", handleCitySuggest);
  server.on("/api/geoip", handleGeoIp);
  server.on("/api/fs", handleFs);
  server.on("/api/anim", handleAnim);
  server.on("/api/wifiscan", handleWifiScan);
  server.on("/api/wifi", HTTP_POST, handleWifi);
  server.on("/update", HTTP_POST, fwHandleDone, fwUpload);
  server.on("/api/iconup", HTTP_POST, iconUploadDone, iconUpload);
  server.on("/api/pupload", HTTP_POST, phUploadDone, phUpload);

server.on("/api/phjpg", handlePhJpg);
  server.on("/api/cdbg", [](){
    if(LittleFS.exists("/cdbg.bin")){
      File f = LittleFS.open("/cdbg.bin", "r");
      if(f){ server.streamFile(f, "text/plain"); f.close(); return; }
    }
    server.send(404, "text/plain", "no capture");
  });
  // captive portal: while no network is saved every unknown host/probe gets a
  // bare 302 to the setup page (no body -> nothing can be "downloaded" from
  // random urls the phone probes with)
  server.onNotFound([](){
    if(!cfg.wifiSsid.length()){
      server.sendHeader("Location",
                        "http://" + WiFi.softAPIP().toString() + "/", true);
      server.send(302, "text/plain", "");
      return;
    }
    server.send(404, "text/plain", "not found");
  });
  server.begin();

  TJpgDec.setCallback(wxJpgOut);
  TJpgDec.setJpgScale(1);
  wifiTryMs = millis();

  // restore the last payload from flash; boot only hits the network when the
  // cache is missing or taken under settings that no longer apply
  bool cached = cacheLoadAll();
  loadPhotoMeta();
  // NO fetch inside setup: the browser's UI burst races the TLS handshake and
  // collapses the heap to ~2k mid-handshake (ssl=6 "record too large").  All
  // fetches start 15s in, when the browser has finished loading.
  coolUntil = millis() + 15000UL;
  rm = RM_ALL;      // force a full first paint after boot
  dirty = true;
  Serial.printf("[boot] cache=%d mk=%d wx=%d fc=%d q3=%d\n",
                (int)cached, (int)mkOk, (int)wxOk, fcn, (int)q3Ok);
}

void loop(){
  uint32_t now = millis();

  static uint32_t lastHb = 0;
  static uint32_t ntpKick = 0;
  if(now - lastHb > 5000UL){
    lastHb = now;
    Serial.printf("[hb] up=%lus heap=%u wifi=%d theme=%s err=%s\n",
                  (unsigned long)((now - bootMs) / 1000), ESP.getFreeHeap(),
                  (int)WiFi.isConnected(), cfg.theme.c_str(), mkErr.c_str());
    // NTP sometimes loses the race with the first TLS sessions after boot;
    // re-issue configTime periodically until the clock is valid
    struct tm ti;
    if(!timeOk(ti) && WiFi.isConnected() && now > 20000UL &&
       now - ntpKick > 30000UL){
      ntpKick = now;
      configTime(cfg.tz.c_str(), "pool.ntp.org", "time.google.com",
                 "time.cloudflare.com");
      Serial.println("[ntp] re-kick");
    }
  }

  server.handleClient();
  now = millis();      // handlers above may have just bumped themeSince / stamps
  if(dnsPortalOn) dnsPoll();

  // first-connect setup mode: no network saved -> the static setup screen owns
  // the panel; carousel and the animated themes must not repaint over it
  bool setupMode = !cfg.wifiSsid.length();
  static bool setupShown = false;
  if(setupMode && !setupShown){ setupShown = true; drawSetupScreen(); }
  if(!setupMode && setupShown){ setupShown = false; mark(RM_FULL); }

  if(!setupMode && cfg.carousel && enabledCount() > 1 &&
     now - themeSince > (uint32_t)cfg.interval * 1000UL){
    nextEnabledTheme();
    themeSince = now;
    mark(RM_FULL);
    Serial.printf("[theme] -> %s\n", cfg.theme.c_str());
  }

  bool secTick = false;
  static uint32_t lastSec = 0;
  if(now / 1000UL != lastSec){ lastSec = now / 1000UL; secTick = true; }

  bool isTicker = (cfg.theme == "ticker");
  static int lastMin = -1;
  static bool lastTok = false, lastConn = false;

  struct tm ti;
  bool tok = timeOk(ti);
  bool conn = WiFi.isConnected();
  if(conn != lastConn){
    lastConn = conn;
    mark(RM_HEAD);
    if(conn) applyDns();     // DHCP renew may have reset the servers on (re)connect
  }
  if(tok != lastTok){
    lastTok = tok;
    mark(isTicker ? (uint8_t)RM_HEAD : (uint8_t)(RM_PRICE | RM_CHART));
  }

  if(secTick){
    applyBrightness();                  // night window open/close transitions
    if(cfg.theme == "classic"){
      if((now / 1000UL) % 5 == 0) mark(RM_HEAD);      // rotate the metric line
      if(tok && ti.tm_min != lastMin){ lastMin = ti.tm_min; mark(RM_PRICE | RM_CHART); }
      if(!tok) mark(RM_PRICE | RM_CHART);
    } else if(isTicker){
      if(tok && ti.tm_min != lastMin){ lastMin = ti.tm_min; mark(RM_HEAD); }
      if((now / 1000UL) % 5 == 0) mark(RM_FOOT);
    } else {
      if(tok && ti.tm_min != lastMin){ lastMin = ti.tm_min; mark(RM_PRICE | RM_CHART); }
      if(!tok) mark(RM_PRICE | RM_CHART);
    }
  }

  if(dirty) render();

  if(!setupMode){
    animTick(now);
    if(cfg.theme == "flip") flipTick(now);
  }

  // access point fallback: raise it when STA is lost, drop it after STA returns
  static bool wasStaUp = false;
  if(WiFi.isConnected()){
    if(!wasStaUp){
      wasStaUp = true;
      fetchHoldUntil = millis() + 12000UL;
      Serial.println("[net] sta up -> quiet 12s");
    }
    // setup flow finished: drop the AP 25s after the save even if the
    // phone still sits on it - the fetchers wait for it (fetchHold)
    if(setupDoneAt && now - setupDoneAt > 25000UL){
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
      setupDoneAt = 0;
      Serial.println("[wifi] AP off after setup page");
    }
    portalStop();
    if(WiFi.getMode() == WIFI_AP_STA){
      if(!wifiApOffAt) wifiApOffAt = now + 90000UL;
      else if(now > wifiApOffAt){
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        wifiApOffAt = 0;
        Serial.println("[wifi] AP off, STA is back");
      }
    }
  } else {
    wifiApOffAt = 0;
    if(WiFi.getMode() == WIFI_STA && now - wifiTryMs > 40000UL){
      WiFi.mode(WIFI_AP_STA);
      WiFi.softAP("BroClock-PRO");
      wifiTryMs = now;
      Serial.println("[wifi] STA lost -> AP BroClock-PRO");
    }
    if(setupMode) portalStart();   // wildcard DNS so captive browsers pop the setup page
  }

  // Deadlock breaker: every fetcher skips TLS while the heap is fragmented
  // (< 12k contiguous - the same floor the fetchers gate on) and nothing else
  // frees memory, so a deeply fragmented device would show "loading" forever.
  // Count the blocked time at any uptime; reboot as soon as the data is 3min
  // stale AND blocked for 2min past a 4min boot - a stalled boot should cost
  // the user ~4min of stale cache, not the old ~10 (observed: first fetch
  // fail -> mfb 8664 -> 500s of frozen price before the breaker fired).
  static uint32_t blkSince = 0;
  // Only the PRICE may arm the breaker: a single failed weather pull flips
  // wxOk=false, and with the fetchers skipping (lastTlsTry zeroed) that armed
  // a reboot inside ~4 min while price+chart were both perfectly fresh.  wx
  // retries on its own cadence and a stale weather widget is not worth a
  // restart.
  bool dueNow = !mkOk || (now - mkAt > 180000UL);
  // two stuck shapes: the classic mfb shredding, and the httpGetStart skip
  // state (loop free < 16.6k / mfb < 8.1k -> no attempt is ever made, so
  // lastTlsTry goes stale)
  // ...and never while the provisioning portal owns the device: with no saved
  // network nothing can un-stick the fetchers, and a reboot mid-setup would
  // drop the user's phone session for no reason.
  if((ESP.getMaxFreeBlockSize() < 12000UL || ESP.getFreeHeap() < 16700UL) && dueNow
     && cfg.wifiSsid.length()){
    // fetchers still ATTEMPTING (a TLS try in the last 2 min) = they are alive
    // and failing from outside (handshake wave / rate limit) - a reboot cannot
    // help that and used to produce a restart cycle every ~5 min during a
    // 30+ min outage.  The breaker is for the fetchers being STUCK (no try).
    if(now - lastTlsTry < 120000UL) blkSince = 0;
    else if(!blkSince) blkSince = now;
    else if(now - blkSince > 120000UL && (now - bootMs) > 240000UL){
      Serial.println("[heap] fragmented too long with data due -> restart");
      delay(100); Serial.flush(); ESP.restart();
    }
  } else blkSince = 0;

  // Data polling is spread across 4 phase slots so a single pass of loop() never
  // fires several HTTP requests back-to-back.  That keeps CoinGecko/OWM under the
  // free-tier rate limit and stops RAM / watchdog spikes (each request allocates
  // a TLS buffer + JsonDocument).  Individual fetchers still honour their own
  // cache age / cooldown gates.
  static uint8_t pollPhase = 0;
  static uint32_t lastPoll = 0;
  if(now - lastPoll >= 1000UL){
    lastPoll = now;
    // boot settle: the first pass used to fire at up=1s - right while WiFi
    // is still connecting/scan-parse runs - and with a stale cache that meant
    // a full TLS session inside the crash window (SDK beacon path does
    // pvPortZalloc(212)+ets_memcpy with no NULL check).  The caches cover
    // the first minutes anyway.
    if(now >= 6000UL){
      switch(pollPhase){
        // weather FIRST (city auto-detect + current + forecast), then price,
        // then the heavy candles, then quotes+holidays: strictly one fetch
        // per slot, so a clean install downloads everything within ~25s
        // and the user sees weather before crypto, as asked
        case 0: maybeFetchWx();      break;   // weather / auto city / forecast
        case 1: fetchData(false);    break;   // price
        case 2: fetchCandlesData();  break;   // candles: the big request
        case 3: maybeFetchQuotes(); maybeFetchHolidays(); break;   // crypto3 markets + calendar holidays
      }
      srvOpen();                              // fetch window closed -> accept again
      pollPhase = (pollPhase + 1) & 3;
    }
  }
}
