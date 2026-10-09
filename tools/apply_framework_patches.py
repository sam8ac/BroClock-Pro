# PlatformIO pre-script (extra_scripts = pre:...): keep the SDPRO framework
# patches applied before every build.  Without them the stock core breaks the
# device: BearSSL's 15000ms handshake timeout stalls loop() on dead links,
# the missing pvPort rescue arena turns wifi-task OOM into Exception(29) and
# HTTP keep-alive parks ~1.5k of lwip heap on idle browser sockets, so TLS
# fetches then fail the heap gate and starve.
#
# The patched files live in tools/framework-patches and are byte-copied over
# the framework package (version-guarded: a framework update needs a re-export
# of the patched files first).
Import("env")

import json
import os
import shutil

FRAMEWORK_ID = "framework-arduinoespressif8266"
PATCHED_VERSION = "3.30102.0"

# PlatformIO exec()s pre-scripts without __file__; PROJECT_DIR is always set.
HERE = env.subst("$PROJECT_DIR")
PATCH_DIR = os.path.join(HERE, "tools", "framework-patches")

TARGETS = (
    os.path.join("cores", "esp8266", "heap.cpp"),
    os.path.join("libraries", "ESP8266WiFi", "src", "WiFiClientSecureBearSSL.cpp"),
    os.path.join("libraries", "ESP8266WebServer", "src", "Parsing-impl.h"),
)


def apply_patches():
    fw_dir = env.PioPlatform().get_package_dir(FRAMEWORK_ID)
    if not fw_dir:
        print("*** apply_framework_patches: %s package dir not found" % FRAMEWORK_ID)
        return
    try:
        with open(os.path.join(fw_dir, "package.json")) as f:
            ver = json.load(f).get("version")
    except Exception as exc:
        print("*** apply_framework_patches: cannot read package.json: %s" % exc)
        return
    if ver != PATCHED_VERSION:
        print("*** apply_framework_patches: WARNING framework %s != patched %s -"
              " export fresh tools/framework-patches/* before trusting this build"
              % (ver, PATCHED_VERSION))
        return
    import filecmp
    for rel in TARGETS:
        src = os.path.join(PATCH_DIR, os.path.basename(rel))
        dst = os.path.join(fw_dir, rel)
        if not os.path.isfile(src):
            print("*** apply_framework_patches: MISSING %s" % src)
        elif not os.path.isfile(dst) or not filecmp.cmp(src, dst, shallow=False):
            shutil.copyfile(src, dst)
            print("*** apply_framework_patches: applied %s" % rel)


apply_patches()
