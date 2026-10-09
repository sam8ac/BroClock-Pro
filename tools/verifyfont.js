// Renders sample text from src/font_ru.h to a PNG to verify the GFXfont.
const fs = require('fs');
const { createCanvas } = require('@napi-rs/canvas');
const src = fs.readFileSync(__dirname + '/../src/font_ru.h', 'utf8');

function parseFont(name) {
  const bm = src.match(new RegExp(name + 'Bitmaps\\[\\] PROGMEM = \\{([\\s\\S]*?)\\};'));
  const gl = src.match(new RegExp(name + 'Glyphs\\[\\] PROGMEM = \\{([\\s\\S]*?)\\};'));
  const fd = src.match(new RegExp('const GFXfont ' + name + ' PROGMEM = \\{ \\(uint8_t\\*\\)' + name + 'Bitmaps, \\(GFXglyph\\*\\)' + name + 'Glyphs, (0x[0-9a-f]+), (0x[0-9a-f]+), (\\d+) \\};'));
  const bitmaps = bm[1].split(',').map(s => s.trim()).filter(Boolean).map(s => parseInt(s, 16));
  const glyphs = [...gl[1].matchAll(/\{(\d+),(\d+),(\d+),(\d+),(-?\d+),(-?\d+)\}/g)].map(m => ({
    off: +m[1], w: +m[2], h: +m[3], xa: +m[4], xo: +m[5], yo: +m[6]
  }));
  return { bitmaps, glyphs, first: parseInt(fd[1], 16), last: parseInt(fd[2], 16), yAdv: +fd[3] };
}

function draw(font, text, x, y) {
  const cv = ctx.canvas;
  let cx = x;
  for (const ch of text) {
    const cp = ch.codePointAt(0);
    if (cp < font.first || cp > font.last) continue;
    const g = font.glyphs[cp - font.first];
    let bo = g.off;
    for (let yy = 0; yy < g.h; yy++) {
      let bit = 0, bits = 0;
      for (let xx = 0; xx < g.w; xx++) {
        if (bit === 0) { bits = font.bitmaps[bo++]; bit = 0x80; }
        if (bits & bit) ctx.fillRect(cx + g.xo + xx, y + g.yo + yy, 1, 1);
        bit >>= 1;
      }
    }
    cx += g.xa;
  }
  return cx - x;
}

const F16 = parseFont('FontRu16');
const F26 = parseFont('FontRu26');
console.log('16: first', F16.first.toString(16), 'last', F16.last.toString(16), 'glyphs', F16.glyphs.length, 'yAdv', F16.yAdv);

const canvas = createCanvas(700, 260);
const ctx = canvas.getContext('2d');
ctx.fillStyle = '#101418'; ctx.fillRect(0, 0, 700, 260);
ctx.fillStyle = '#ffffff';
let w1 = draw(F16, 'НЕТ ФОТО  загрузите картинку', 10, 40);
let w2 = draw(F16, 'Погода: Витебск +11°C, облачно', 10, 80);
let w3 = draw(F26, 'Загрузка… 5 дней', 10, 140);
let w4 = draw(F26, 'Рынок ABCDEF 0123456789', 10, 210);
console.log('widths', w1, w2, w3, w4);
fs.writeFileSync(__dirname + '/fonttest.png', canvas.toBuffer('image/png'));
console.log('png written');
