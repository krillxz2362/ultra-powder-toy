# -*- coding: utf-8 -*-
# Переводит пиксельный шрифт из game/ui/font.lua в заголовок для C++.
# Источник один — Lua; править надо там, здесь только перевод.
import re, sys

src = open('game/ui/font.lua', encoding='utf-8').read()
pat = re.compile(r'G\[("?)(.+?)\1\]\s*=\s*\{(.*?)\}', re.S)
glyphs = {}
for m in pat.finditer(src):
    ch = m.group(2)
    rows = re.findall(r'"([.#]*)"', m.group(3))
    if len(ch) != 1 or not rows:
        continue
    assert len(rows) == 7, (ch, len(rows))
    bits = []
    for r in rows:
        r = (r + '.....')[:5]
        bits.append(sum((1 << (4 - i)) for i, c in enumerate(r) if c == '#'))
    glyphs[ord(ch)] = bits

assert len(glyphs) > 60, len(glyphs)
out = ['// Пиксельный шрифт 5x7. Создан tools/gen_font.py из game/ui/font.lua.',
       '// Править здесь бессмысленно — правь Lua и запусти генератор заново.',
       '#pragma once', '#include <cstdint>', '', 'namespace upt {', '',
       'struct Glyph { uint32_t cp; uint8_t rows[7]; };', '',
       f'inline constexpr int GLYPH_COUNT = {len(glyphs)};',
       'inline constexpr Glyph GLYPHS[GLYPH_COUNT] = {']
for cp in sorted(glyphs):
    b = ', '.join(f'0x{v:02X}' for v in glyphs[cp])
    try:
        name = chr(cp)
    except Exception:
        name = '?'
    out.append(f'    {{ {cp}, {{ {b} }} }},   // {name}')
out += ['};', '', '}  // namespace upt', '']
open('cpp/include/upt_font.h', 'w', encoding='utf-8').write('\n'.join(out))
print('букв в шрифте:', len(glyphs))
