# Draws the plain version of the repository's banner, and the social preview:
# an original graphic in the colours of the project's launcher (charcoal,
# honeycomb, lime bands). No game artwork is used here.
#
# banner.png, the picture at the top of the README, is not written by this
# script: it is this drawing with a car added by the repository owner in an AI
# image generator (the file carries a C2PA record that says so).
#
# Needs Pillow, the Inter typeface installed, and Michroma-Regular.ttf (SIL Open
# Font License, https://github.com/googlefonts/Michroma-font) next to this file.
# Writes banner-plain.png and social-preview.png into the current folder.
import math, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

LIME = (0xBF, 0xF5, 0x2E)
INK_TOP, INK_BOTTOM = (0x2A, 0x31, 0x37), (0x12, 0x15, 0x19)
SS = 2  # supersampling

def hexagon(cx, cy, r):
    return [(cx + r * math.cos(math.radians(a)), cy + r * math.sin(math.radians(a))) for a in range(0, 360, 60)]

def draw(width, height, title_px, sub_px, out, taglines, lit_from):
    W, H = width * SS, height * SS
    img = Image.new('RGB', (W, H))
    px = img.load()
    for y in range(H):
        t = y / (H - 1)
        c = tuple(round(INK_TOP[i] + (INK_BOTTOM[i] - INK_TOP[i]) * t) for i in range(3))
        for x in range(W):
            px[x, y] = c

    # Honeycomb, a few cells lit, denser towards the right.
    layer = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    r = 30 * SS
    dx, dy = r * 1.5, r * math.sqrt(3)
    col = -1
    while col * dx < W + r:
        row = -1
        while row * dy < H + dy:
            cx, cy = col * dx, row * dy + (dy / 2 if col & 1 else 0)
            pts = hexagon(cx, cy, r)
            pick = (col * 7 + row * 13 + 40) % 9
            if cx > W * lit_from and pick == 0:
                a = 34 + ((col * 5 + row * 3 + 40) % 4) * 26
                d.polygon(pts, fill=LIME + (a,))
            d.line(pts + [pts[0]], fill=(255, 255, 255, 22), width=SS)
            row += 1
        col += 1
    img = Image.alpha_composite(img.convert('RGBA'), layer)

    # Calm ground on the left for the lettering.
    calm = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    cp = calm.load()
    edge = int(W * 0.60)
    for x in range(edge):
        a = int(255 * (1 - x / edge) ** 1.2)
        for y in range(H):
            cp[x, y] = (0x1C, 0x21, 0x26, a)
    img = Image.alpha_composite(img, calm)

    # Slanted lime bands with a soft glow.
    bands = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    bd = ImageDraw.Draw(bands)
    slant = H * 0.62
    left = W * 0.80
    for off, wide, alpha in ((0, 78 * SS, 255), (96 * SS, 22 * SS, 150), (-36 * SS, 9 * SS, 110), (140 * SS, 6 * SS, 70)):
        x0 = left + off
        bd.polygon([(x0, 0), (x0 + wide, 0), (x0 + wide - slant, H), (x0 - slant, H)], fill=LIME + (alpha,))
    glow = bands.filter(ImageFilter.GaussianBlur(26 * SS))
    glow.putalpha(glow.getchannel('A').point(lambda v: int(v * 0.45)))
    img = Image.alpha_composite(img, glow)
    img = Image.alpha_composite(img, bands)

    # Lettering.
    d = ImageDraw.Draw(img)
    title_font = ImageFont.truetype('Michroma-Regular.ttf', title_px * SS)
    sub_font = ImageFont.truetype('/usr/share/fonts/opentype/inter/Inter-Medium.otf', sub_px * SS)
    margin = int(width * 0.055) * SS
    lines = ['Ridge Racer 6', 'Recomp']
    heights = [title_font.getbbox(t)[3] for t in lines]
    gap = int(title_px * 0.06) * SS
    line_step = int(sub_px * 1.45) * SS
    block = sum(heights) + gap + int(title_px * 0.62) * SS + line_step * len(taglines) + int(title_px * 0.35) * SS
    y = (H - block) // 2
    for t, h in zip(lines, heights):
        d.text((margin, y), t, font=title_font, fill=(255, 255, 255))
        y += h + gap
    y += int(title_px * 0.10) * SS
    bar_w = int(title_font.getlength('Recomp') * 0.55)
    d.rectangle([margin + 2 * SS, y, margin + 2 * SS + bar_w, y + 5 * SS], fill=LIME)
    y += int(title_px * 0.48) * SS
    for line in taglines:
        d.text((margin + 2 * SS, y), line, font=sub_font, fill=(0xB4, 0xBC, 0xC1))
        y += line_step
    img.convert('RGB').resize((width, height), Image.LANCZOS).save(out, optimize=True)

draw(1280, 400, 58, 21, 'banner-plain.png', ['Unofficial native Windows version. Bring your own disc image.'], 0.50)
draw(1280, 640, 76, 26, 'social-preview.png', ['Unofficial native Windows version.', 'Bring your own disc image.'], 0.62)
