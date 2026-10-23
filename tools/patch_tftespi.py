# Pre-build: let main.cpp suppress the TFT panel reset on a CLEAN warm restart.
# Skipping the reset keeps the ST7789 GRAM alive, so the old clock stays on the
# glass until the first repaint - no white flash, no black frame.  After a
# crash the SPI bus may have been interrupted mid-command, so main.cpp only
# sets the flag for clean restarts.
# The patch is applied to every env's libdeps copy and is idempotent.
import io, os, glob

try:
    Import("env")
    ROOT = env.Dir("#").abspath
except NameError:
    ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

MARK = "sdpro_skip_tft_reset"

SIG_ANCHOR = "void TFT_eSPI::initBus(void) {"
SIG_NEW = ("extern bool sdpro_skip_tft_reset;  // main.cpp: keep the panel state on a clean warm restart\n\n"
           "void TFT_eSPI::initBus(void) {")

OLD_RESET = """  if (TFT_RST >= 0) {
    writecommand(0x00); // Put SPI bus in known state for TFT with CS tied low
    digitalWrite(TFT_RST, HIGH);
    delay(5);
    digitalWrite(TFT_RST, LOW);
    delay(20);
    digitalWrite(TFT_RST, HIGH);
  }
  else writecommand(TFT_SWRST); // Software reset
#else
  writecommand(TFT_SWRST); // Software reset
#endif

  delay(150); // Wait for reset to complete
"""

NEW_RESET = """  if (!sdpro_skip_tft_reset) {
  if (TFT_RST >= 0) {
    writecommand(0x00); // Put SPI bus in known state for TFT with CS tied low
    digitalWrite(TFT_RST, HIGH);
    delay(5);
    digitalWrite(TFT_RST, LOW);
    delay(20);
    digitalWrite(TFT_RST, HIGH);
  }
  else writecommand(TFT_SWRST); // Software reset
  }
#else
  if (!sdpro_skip_tft_reset) writecommand(TFT_SWRST); // Software reset
#endif

  if (!sdpro_skip_tft_reset) delay(150); // Wait for reset to complete
"""

patched = 0
for path in glob.glob(os.path.join(ROOT, ".pio", "libdeps", "*", "TFT_eSPI", "TFT_eSPI.cpp")):
    with io.open(path, "r", encoding="utf-8", newline="") as f:
        src = f.read().replace("\r\n", "\n")
    if MARK in src:
        continue                       # already patched
    if OLD_RESET not in src:
        raise SystemExit("patch_tftespi: reset block not found in %s" % path)
    if SIG_ANCHOR not in src:
        raise SystemExit("patch_tftespi: initBus anchor not found in %s" % path)
    src = src.replace(SIG_ANCHOR, SIG_NEW, 1).replace(OLD_RESET, NEW_RESET, 1)
    with io.open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(src)
    patched += 1

if patched:
    print("patch_tftespi: patched %d copy(ies)" % patched)
