# -*- coding: utf-8 -*-
# Готовит значки приложения из рисунка res/icon_исходник.* .
#
# Что здесь делается и почему:
#  * фон рисунка — не ровный чёрный, а шумный (следы JPEG). Всё, что
#    темнее порога, заменяем ровным фоном игры, иначе на значке видна
#    грязь вокруг огня.
#  * рисунок обрезается по содержимому и ставится в квадрат с полями:
#    иначе на рабочем столе он выглядит мелким.
#  * размер сводим сначала к логическим 64x64 усреднением, а уже оттуда
#    растим NEAREST в 512 и 192 (ровно x8 и x3). Прямое уменьшение
#    большого рисунка даёт пиксели разной ширины — сетка «плывёт».
from PIL import Image
import os

BG   = (26, 28, 34)      # тот же тёмный фон, что и в самой игре
DARK = 46                # ниже этой яркости считаем фоном
LOGIC = 64               # логический размер пиксельной картинки

src = None
for name in ("res/icon_исходник.png", "res/icon_исходник.jpg"):
    if os.path.exists(name):
        src = name
        break
assert src, "нет файла res/icon_исходник.*"

im = Image.open(src)
if im.mode == "RGBA":
    # прозрачный фон: содержимое там, где непрозрачно
    mask = im.getchannel("A").point(lambda v: 255 if v > 40 else 0)
    flat = Image.new("RGB", im.size, BG)
    flat.paste(im.convert("RGB"), (0, 0), mask)
else:
    im = im.convert("RGB")
    gray = im.convert("L")
    mask = gray.point(lambda v: 255 if v > DARK else 0)
    flat = Image.new("RGB", im.size, BG)
    flat.paste(im, (0, 0), mask)

# Сетку исходника сохраняем: сперва сводим ВЕСЬ холст к логическим
# 64x64 (1024 делится на 64 ровно, пиксели не разъезжаются), и только
# потом обрезаем и центруем — уже в логических пикселях.
small = flat.resize((LOGIC, LOGIC), Image.BOX)
mlow  = mask.resize((LOGIC, LOGIC), Image.BOX).point(lambda v: 255 if v > 24 else 0)
box = mlow.getbbox()
assert box, "на картинке не нашлось содержимого"

art = small.crop(box)
aw, ah = art.size
canvas = Image.new("RGB", (LOGIC, LOGIC), BG)
canvas.paste(art, ((LOGIC - aw) // 2, (LOGIC - ah) // 2))

for size, name in ((512, "res/icon.png"), (192, "res/icon_192.png")):
    canvas.resize((size, size), Image.NEAREST).save(name)
    print("готов", name, size)
print("источник", src, "| содержимое в логических точках", aw, "x", ah)
