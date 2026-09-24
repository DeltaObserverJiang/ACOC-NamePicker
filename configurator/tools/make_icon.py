# -*- coding: utf-8 -*-
"""由 A-color.png 生成程序图标 res/icon.ico。

原图是透明底的四色斜体 A。直接缩小到 16px 会糊，所以给一层深色圆角底，
再把字母按比例内缩，保证任务栏和资源管理器里都看得清。
"""
import io
import os

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "A-color.png")
DST = os.path.join(ROOT, "configurator", "res", "icon.ico")
PREVIEW = os.path.join(ROOT, "configurator", "res", "icon_preview.png")

SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
BG_TOP = (16, 18, 28)
BG_BOTTOM = (7, 8, 13)


def rounded_mask(size, radius_ratio=0.21):
    m = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(m)
    r = max(1, int(size * radius_ratio))
    d.rounded_rectangle([0, 0, size - 1, size - 1], radius=r, fill=255)
    return m


def background(size):
    g = Image.new("RGBA", (1, size))
    for y in range(size):
        t = y / max(1, size - 1)
        g.putpixel((0, y), tuple(
            int(BG_TOP[i] + (BG_BOTTOM[i] - BG_TOP[i]) * t) for i in range(3)) + (255,))
    bg = g.resize((size, size), Image.NEAREST)
    bg.putalpha(rounded_mask(size))
    return bg


def main():
    src = Image.open(SRC).convert("RGBA")
    src = src.crop(src.getbbox())

    side = max(src.size)
    sq = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    sq.paste(src, ((side - src.width) // 2, (side - src.height) // 2), src)

    frames = []
    for s in SIZES:
        canvas = background(s)
        inner = max(6, int(s * 0.76))
        art = sq.resize((inner, inner), Image.LANCZOS)
        off = (s - inner) // 2
        canvas.paste(art, (off, off), art)
        frames.append(canvas)

    frames[-1].save(DST, format="ICO",
                    sizes=[(s, s) for s in SIZES],
                    append_images=frames[:-1])
    frames[5].save(PREVIEW)

    # 另存一份 256 的 PNG，安装程序和"关于"框用得上
    frames[-1].save(os.path.join(ROOT, "configurator", "res", "icon256.png"))
    print("icon.ico", os.path.getsize(DST), "bytes")


if __name__ == "__main__":
    main()
