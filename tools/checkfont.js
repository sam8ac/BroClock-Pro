const fs = require('fs');
const src = fs.readFileSync(__dirname + '/../src/font_ru.h', 'utf8');
function pf(n) {
  const gl = src.match(new RegExp(n + 'Glyphs\\[\\] PROGMEM = \\{([\\s\\S]*?)\\};'))[1];
  return [...gl.matchAll(/\{(\d+),(\d+),(\d+),(\d+),(-?\d+),(-?\d+)\}/g)].map(m => ({ off:+m[1], w:+m[2], h:+m[3], xa:+m[4], xo:+m[5], yo:+m[6] }));
}
const g = pf('FontRu16'); const F = 0x20;
for (const [ch, cp] of [['space',0x20],['A',0x41],['0',0x30],['А',0x410],['Н',0x41D],['я',0x44F],['ё',0x451],['.',0x2E],['C',0x43],['°',0xB0]]) {
  const x = g[cp - F];
  console.log(ch, 'U+' + cp.toString(16), 'w=' + x.w, 'h=' + x.h, 'xa=' + x.xa, 'xo=' + x.xo, 'yo=' + x.yo);
}
// sanity: no glyph wider than 26, advance > 0 for letters
let bad = 0;
for (let i = 0; i < g.length; i++) { const cp = i + F; if (cp>=0x410 && cp<=0x44F && g[i].xa <= 0) bad++; }
console.log('cyrillic glyphs with zero advance:', bad);
