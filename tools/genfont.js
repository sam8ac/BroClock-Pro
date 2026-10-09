// Generates src/font_ru.h:
//  - GFXfont FontRu16/26 (Segoe UI Bold): ASCII + Latin-1 (es) + Cyrillic (ru)
//  - sparse CJK side tables FontZh16/26 (Microsoft YaHei Bold) for zh,
//    codepoints listed one-hex-per-line in zh_chars.txt (sorted, unique)
// Run: node genfont.js
const fs = require('fs');
const { createCanvas, GlobalFonts } = require('@napi-rs/canvas');

GlobalFonts.registerFromPath('C:/Windows/Fonts/segoeuib.ttf', 'SegoeUIBold');
let zhFamily = null;
for (const cand of [
  ['C:/Windows/Fonts/msyhbd.ttc', 'YaHeiBold'],
  ['C:/Windows/Fonts/msyh.ttc', 'YaHei'],
]) {
  if (fs.existsSync(cand[0])) {
    try { GlobalFonts.registerFromPath(cand[0], cand[1]); zhFamily = cand[1]; break; }
    catch (e) { console.warn('register failed', cand[0], e.message); }
  }
}
if (!zhFamily) console.warn('WARNING: no YaHei font found - CJK glyphs will be missing');

const FIRST = 0x20, LAST = 0x451;   // space .. ё (Latin-1 0xA0..0xFF sits inside
const OUT = __dirname + '/../src/font_ru.h';   // this range: es accents/punct)

function isRenderable(cp) {
  if (cp >= 0x20 && cp <= 0x7E) return true;      // ASCII
  if (cp >= 0xA0 && cp <= 0xFF) return true;      // Latin-1: áéíóúñü ¿¡ «» · °
  if (cp >= 0x401 && cp <= 0x451) return true;    // Cyrillic + Ё/ё
  return false;
}

// draw one char, return glyph metrics + packed bits (continuous bit stream,
// rows NOT byte-aligned - matches the renderer's bit walking in ruDraw)
function raster(ctx, cp, W, H, PX, BY) {
  const ch = String.fromCodePoint(cp);
  const adv = Math.round(ctx.measureText(ch).width);
  ctx.clearRect(0, 0, W, H);
  ctx.fillText(ch, PX, BY);
  const img = ctx.getImageData(0, 0, W, H).data;
  let minx = W, maxx = -1, miny = H, maxy = -1;
  for (let y = 0; y < H; y++) {
    for (let x = 0; x < W; x++) {
      const a = img[(y * W + x) * 4 + 3];
      if (a > 110) {
        if (x < minx) minx = x; if (x > maxx) maxx = x;
        if (y < miny) miny = y; if (y > maxy) maxy = y;
      }
    }
  }
  if (maxx < 0) return { w: 0, h: 0, xa: adv, xo: 0, yo: 0, bytes: [] };
  const gw = maxx - minx + 1, gh = maxy - miny + 1;
  const xo = minx - PX, yo = miny - BY;   // relative to pen / baseline
  const bytes = [];
  let acc = 0, bits = 0;
  for (let y = 0; y < gh; y++) {
    for (let col = 0; col < gw; col++) {
      const on = img[((miny + y) * W + (minx + col)) * 4 + 3] > 110;
      acc = (acc << 1) | (on ? 1 : 0);
      if (++bits === 8) { bytes.push(acc); acc = 0; bits = 0; }
    }
  }
  if (bits) bytes.push(acc << (8 - bits));   // flush at glyph end
  return { w: gw, h: gh, xa: adv, xo, yo, bytes };
}

function makeCtx(emPx, family) {
  const W = emPx * 3, H = emPx * 3;
  const cv = createCanvas(W, H);
  const ctx = cv.getContext('2d');
  ctx.font = `${emPx}px "${family}"`;
  ctx.textBaseline = 'alphabetic';
  ctx.fillStyle = '#ffffff';
  return { ctx, W, H, PX: emPx, BY: emPx * 2 };
}

function buildFont(emPx, yAdvance, name) {
  const { ctx, W, H, PX, BY } = makeCtx(emPx, 'SegoeUIBold');
  const spaceAdv = Math.round(ctx.measureText(' ').width);
  const glyphs = [], bitmap = [];
  let offset = 0;
  for (let cp = FIRST; cp <= LAST; cp++) {
    if (!isRenderable(cp)) {                     // gap: invisible, zero advance
      glyphs.push({ off: offset, w: 0, h: 0, xa: cp === 0x20 ? spaceAdv : 0, xo: 0, yo: 0 });
      continue;
    }
    const g = raster(ctx, cp, W, H, PX, BY);
    g.off = offset;
    glyphs.push(g);
    for (const b of g.bytes) { bitmap.push(b); offset++; }
  }
  return { name, glyphs, bitmap, yAdvance };
}

function buildCJK(emPx, name) {
  const listFile = __dirname + '/zh_chars.txt';
  const cps = fs.existsSync(listFile)
    ? fs.readFileSync(listFile, 'utf8').split(/[\s,]+/).filter(Boolean)
        .map(s => parseInt(s, 16)).filter(n => isFinite(n)).sort((a, b) => a - b)
    : [];
  const seen = new Set();
  const uniq = cps.filter(c => (seen.has(c) ? false : (seen.add(c), true)));
  if (!zhFamily) return { name, cps: [], glyphs: [], bitmap: [], n: 0 };
  const { ctx, W, H, PX, BY } = makeCtx(emPx, zhFamily);
  const glyphs = [], bitmap = [], outCps = [];
  let offset = 0;
  for (const cp of uniq) {
    const g = raster(ctx, cp, W, H, PX, BY);
    if (g.w === 0)
      throw new Error(`no ink for U+${cp.toString(16)} (${String.fromCodePoint(cp)}) - bad font family?`);
    g.off = offset;
    outCps.push(cp); glyphs.push(g);
    for (const b of g.bytes) { bitmap.push(b); offset++; }
  }
  return { name, cps: outCps, glyphs, bitmap, n: outCps.length };
}

function emitGfx(f, sizeLabel) {
  let s = '';
  s += `// ${sizeLabel}px\n`;
  s += `const uint8_t ${f.name}Bitmaps[] PROGMEM = {\n`;
  for (let i = 0; i < f.bitmap.length; i += 24)
    s += f.bitmap.slice(i, i + 24).map(b => '0x' + b.toString(16).padStart(2, '0')).join(',') + ',\n';
  s += '};\n';
  s += `const GFXglyph ${f.name}Glyphs[] PROGMEM = {\n`;
  for (const g of f.glyphs)
    s += `{${g.off},${g.w},${g.h},${g.xa},${g.xo},${g.yo}},\n`;
  s += '};\n';
  s += `const GFXfont ${f.name} PROGMEM = { (uint8_t*)${f.name}Bitmaps, (GFXglyph*)${f.name}Glyphs, 0x${FIRST.toString(16)}, 0x${LAST.toString(16)}, ${f.yAdvance} };\n\n`;
  return s;
}

function emitZh(f) {
  let s = `// CJK sparse tables for ${f.name} (sorted cp -> glyph, binary search)\n`;
  if (!f.n) {   // zero-length arrays are illegal in C - keep placeholders, N=0
    s += `const uint16_t ${f.name}Cps[] PROGMEM = {0};\n`;
    s += `const uint8_t ${f.name}Bitmaps[] PROGMEM = {0};\n`;
    s += `const GFXglyph ${f.name}Glyphs[] PROGMEM = {{0,0,0,0,0,0}};\n`;
    s += `const uint16_t ${f.name}N = 0;\n\n`;
    return s;
  }
  s += `const uint16_t ${f.name}Cps[] PROGMEM = {\n`;
  for (let i = 0; i < f.cps.length; i += 16)
    s += f.cps.slice(i, i + 16).map(c => '0x' + c.toString(16)).join(',') + ',\n';
  s += '};\n';
  s += `const uint8_t ${f.name}Bitmaps[] PROGMEM = {\n`;
  for (let i = 0; i < f.bitmap.length; i += 24)
    s += f.bitmap.slice(i, i + 24).map(b => '0x' + b.toString(16).padStart(2, '0')).join(',') + ',\n';
  s += '};\n';
  s += `const GFXglyph ${f.name}Glyphs[] PROGMEM = {\n`;
  for (const g of f.glyphs)
    s += `{${g.off},${g.w},${g.h},${g.xa},${g.xo},${g.yo}},\n`;
  s += '};\n';
  s += `const uint16_t ${f.name}N = ${f.n};\n\n`;
  return s;
}

const f16 = buildFont(16, 16, 'FontRu16');
const f26 = buildFont(26, 26, 'FontRu26');
const z16 = buildCJK(16, 'FontZh16');
const z26 = buildCJK(26, 'FontZh26');
let out = `#pragma once
#include <pgmspace.h>
#include <Fonts/GFXFF/gfxfont.h>
#ifndef LOAD_GFXFF
// gfxfont.h ships the GFXglyph/GFXfont typedefs only under LOAD_GFXFF; the
// manual Cyrillic/CJK renderer needs the two types (not the Adafruit free
// fonts), so define them here with the identical layout while GFXFF stays off.
typedef struct {
  uint32_t bitmapOffset;
  uint8_t  width, height;
  uint8_t  xAdvance;
  int8_t   xOffset, yOffset;
} GFXglyph;
typedef struct {
  uint8_t  *bitmap;
  GFXglyph *glyph;
  uint16_t first, last;
  uint8_t  yAdvance;
} GFXfont;
#endif
// generated by genfont.js - ASCII + Latin-1 (es) + Cyrillic (ru), Segoe UI
// Bold; CJK sparse tables, Microsoft YaHei Bold

`;
out += emitGfx(f16, 16) + emitGfx(f26, 26) + emitZh(z16) + emitZh(z26);
fs.writeFileSync(OUT, out);
console.log('written', OUT);
console.log('FontRu16 bitmaps=', f16.bitmap.length, 'glyphs=', f16.glyphs.length);
console.log('FontRu26 bitmaps=', f26.bitmap.length, 'glyphs=', f26.glyphs.length);
console.log('FontZh16 N=', z16.n, 'bitmaps=', z16.bitmap.length);
console.log('FontZh26 N=', z26.n, 'bitmaps=', z26.bitmap.length);
console.log('file bytes=', Buffer.byteLength(out));
