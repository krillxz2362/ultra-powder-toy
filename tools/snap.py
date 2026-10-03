# Собирает снимки ядра (.ppm) в картинки и ленту кадров.
from PIL import Image
import os
shots = [0, 60, 150, 300, 600, 1000]
imgs = []
for i, step in enumerate(shots):
    im = Image.open(f"/tmp/snap_{i}.ppm").convert("RGB")
    im = im.resize((im.width * 2, im.height * 2), Image.NEAREST)
    im.save(f"snapshots/шаг_{step:04d}.png")
    imgs.append(im)
# лента: все кадры в столбик, чтобы видеть развитие сцены сразу
pad = 6
W = imgs[0].width
Hs = sum(i.height for i in imgs) + pad * (len(imgs) - 1)
strip = Image.new("RGB", (W, Hs), (10, 10, 12))
y = 0
for im in imgs:
    strip.paste(im, (0, y)); y += im.height + pad
strip.save("snapshots/лента.png")
print("готово:", ", ".join(sorted(os.listdir("snapshots"))))
