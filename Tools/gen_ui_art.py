"""gen_ui_art.py - generate the Wrecktide menu art with Pillow (2026-09-06).

Everything here is original, drawn from primitives and noise: no scanned paper, no traced logo,
no third party image. Output lands in Content\\UI\\Textures\\*.png and is imported as UTexture2D by
Tools\\import_ui_textures.py.

  parchment.png    1024x1536  warm paper, fibre noise, blotches, burnt edges, vignette (Image brush)
  frame_rope.png    256x256    nine-slice rope-and-wood border, 48 px margin, hollow centre (Box brush)
  compass_rose.png  256x256    selector glyph and parchment watermark (Image brush)
  wax_seal.png      128x128    red wax seal for the panel foot (Image brush)

Run: python tools\\gen_ui_art.py [outdir]
"""
import math
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFilter

M = "WRECKTIDE-UIART"

# --- tunables (all in one place so the look can be dialled without hunting through the drawing) ---
PARCHMENT_SIZE = (1024, 1536)
PAPER_LIGHT = (236, 214, 170)      # centre of the sheet
PAPER_DARK = (150, 116, 68)        # burnt edge
FIBRE_STRENGTH = 0.16              # 0..1 how much noise shows through
EDGE_BURN_PX = 130                 # how far the edge darkening reaches in
BLOTCH_COUNT = 46
ROPE_SIZE = 256
ROPE_MARGIN = 48                   # nine-slice margin in pixels (48/256 = 0.1875)
WOOD_DARK = (58, 38, 22)
WOOD_MID = (96, 64, 36)
ROPE_LIGHT = (198, 164, 108)
ROPE_DARK = (120, 92, 54)
GOLD = (214, 172, 88)
GOLD_DARK = (120, 88, 34)
INK = (52, 34, 18)


def log(msg):
    print("%s %s" % (M, msg))


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(len(a)))


def make_parchment(path):
    w, h = PARCHMENT_SIZE
    rng = random.Random(20260906)

    # base gradient: light in the middle, warmer and darker toward every edge
    base = Image.new("RGB", (w, h), PAPER_LIGHT)
    px = base.load()
    for y in range(h):
        ny = min(y, h - 1 - y)
        for x in range(w):
            nx = min(x, w - 1 - x)
            edge = min(nx, ny)
            # 0 at the very edge, 1 once we are EDGE_BURN_PX inside
            t = min(1.0, edge / float(EDGE_BURN_PX))
            t = t * t * (3.0 - 2.0 * t)  # smoothstep, no hard ring
            # a gentle bowl vignette on top so the centre of the sheet reads as lit
            cx = (x - w * 0.5) / (w * 0.5)
            cy = (y - h * 0.5) / (h * 0.5)
            bowl = 1.0 - 0.20 * min(1.0, (cx * cx + cy * cy) * 0.75)
            col = lerp(PAPER_DARK, PAPER_LIGHT, t)
            px[x, y] = (int(col[0] * bowl), int(col[1] * bowl), int(col[2] * bowl))

    # paper fibre: two octaves of gaussian noise, blurred so it reads as grain and not as static
    fine = Image.effect_noise((w, h), 26).filter(ImageFilter.GaussianBlur(0.7))
    coarse = Image.effect_noise((w, h), 34).filter(ImageFilter.GaussianBlur(4.5))
    grain = Image.blend(fine, coarse, 0.55)
    base = Image.blend(base, Image.merge("RGB", (grain, grain, grain)), FIBRE_STRENGTH)

    # tea blotches: soft dark ellipses, mostly near the edges where a real sheet stains first
    stain = Image.new("L", (w, h), 0)
    sd = ImageDraw.Draw(stain)
    for _ in range(BLOTCH_COUNT):
        r = rng.randint(30, 170)
        bx = rng.randint(-r, w + r)
        by = rng.randint(-r, h + r)
        sd.ellipse([bx - r, by - int(r * 0.7), bx + r, by + int(r * 0.7)], fill=rng.randint(28, 78))
    stain = stain.filter(ImageFilter.GaussianBlur(26))
    dark = Image.new("RGB", (w, h), (108, 76, 40))
    base = Image.composite(dark, base, stain.point(lambda v: min(255, int(v * 0.85))))

    # a faint compass watermark pressed into the sheet, drawn as ink at very low alpha
    mark = Image.new("L", (w, h), 0)
    md = ImageDraw.Draw(mark)
    ccx, ccy, cr = w * 0.5, h * 0.62, w * 0.34
    md.ellipse([ccx - cr, ccy - cr, ccx + cr, ccy + cr], outline=255, width=5)
    md.ellipse([ccx - cr * 0.86, ccy - cr * 0.86, ccx + cr * 0.86, ccy + cr * 0.86], outline=255, width=2)
    for i in range(8):
        a = math.pi * 2.0 * i / 8.0
        long_r = cr * (0.92 if i % 2 == 0 else 0.55)
        md.polygon([
            (ccx + math.cos(a) * long_r, ccy + math.sin(a) * long_r),
            (ccx + math.cos(a + 0.22) * cr * 0.14, ccy + math.sin(a + 0.22) * cr * 0.14),
            (ccx + math.cos(a - 0.22) * cr * 0.14, ccy + math.sin(a - 0.22) * cr * 0.14),
        ], outline=255)
    mark = mark.filter(ImageFilter.GaussianBlur(1.6)).point(lambda v: int(v * 0.13))
    base = Image.composite(Image.new("RGB", (w, h), (120, 92, 54)), base, mark)

    out = base.convert("RGBA")
    out.save(path)
    log("wrote %s (%dx%d)" % (path, w, h))


def make_rope_frame(path):
    s = ROPE_SIZE
    m = ROPE_MARGIN
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # wooden batten: a filled border ring, then the hollow centre punched back out
    d.rectangle([0, 0, s - 1, s - 1], fill=WOOD_MID + (255,))
    d.rectangle([m, m, s - 1 - m, s - 1 - m], fill=(0, 0, 0, 0))
    # dark outline top and bottom of the batten so it reads as a plank edge
    d.rectangle([0, 0, s - 1, s - 1], outline=WOOD_DARK + (255,), width=5)
    d.rectangle([m - 5, m - 5, s - m + 4, s - m + 4], outline=WOOD_DARK + (255,), width=5)

    # wood grain: short darker strokes running along each side
    rng = random.Random(4242)
    for _ in range(150):
        if rng.random() < 0.5:
            gx = rng.randint(6, s - 7)
            gy = rng.choice([rng.randint(7, m - 7), rng.randint(s - m + 7, s - 8)])
            d.line([gx, gy, gx + rng.randint(8, 26), gy], fill=(70, 46, 26, 90), width=1)
        else:
            gy = rng.randint(6, s - 7)
            gx = rng.choice([rng.randint(7, m - 7), rng.randint(s - m + 7, s - 8)])
            d.line([gx, gy, gx, gy + rng.randint(8, 26)], fill=(70, 46, 26, 90), width=1)

    # rope twist laid along the middle of each batten: alternating light and dark diagonal strands
    half = m // 2
    step = 12
    for i in range(0, s, step):
        for (x0, y0, horiz) in ((i, half, True), (i, s - half, True), (half, i, False), (s - half, i, False)):
            shade = ROPE_LIGHT if (i // step) % 2 == 0 else ROPE_DARK
            if horiz:
                d.line([x0, y0 - 9, x0 + step - 2, y0 + 9], fill=shade + (235,), width=7)
            else:
                d.line([x0 - 9, y0, x0 + 9, y0 + step - 2], fill=shade + (235,), width=7)

    # corner blocks with a brass nail, drawn last so the rope tucks under them
    for (cx, cy) in ((m // 2, m // 2), (s - m // 2, m // 2), (m // 2, s - m // 2), (s - m // 2, s - m // 2)):
        d.rectangle([cx - m // 2 + 3, cy - m // 2 + 3, cx + m // 2 - 3, cy + m // 2 - 3],
                    fill=WOOD_MID + (255,), outline=WOOD_DARK + (255,), width=4)
        d.ellipse([cx - 6, cy - 6, cx + 6, cy + 6], fill=GOLD + (255,), outline=GOLD_DARK + (255,), width=2)

    # keep the nine-slice centre perfectly empty: any stray pixel there would tile across the panel
    d.rectangle([m, m, s - 1 - m, s - 1 - m], fill=(0, 0, 0, 0))
    img.save(path)
    log("wrote %s (%dx%d, margin %d px = %.4f)" % (path, s, s, m, m / float(s)))


def make_compass(path):
    s = 256
    big = s * 4  # draw at 4x and downsample: cheap antialiasing, no extra dependency
    img = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    c = big / 2.0
    r = big * 0.46

    d.ellipse([c - r, c - r, c + r, c + r], outline=GOLD_DARK + (255,), width=int(big * 0.020))
    d.ellipse([c - r * 0.88, c - r * 0.88, c + r * 0.88, c + r * 0.88], outline=GOLD + (200,), width=int(big * 0.008))
    for i in range(32):
        a = math.pi * 2.0 * i / 32.0
        inner = r * (0.78 if i % 4 else 0.70)
        d.line([c + math.cos(a) * inner, c + math.sin(a) * inner,
                c + math.cos(a) * r * 0.86, c + math.sin(a) * r * 0.86],
               fill=GOLD_DARK + (190,), width=int(big * 0.006))

    # eight points: the four cardinals long and light, the diagonals short and dark
    for i in range(8):
        a = math.pi * 2.0 * i / 8.0 - math.pi / 2.0
        tip = r * (0.72 if i % 2 == 0 else 0.44)
        wide = 0.19 if i % 2 == 0 else 0.13
        fill = GOLD if i % 2 == 0 else GOLD_DARK
        d.polygon([
            (c + math.cos(a) * tip, c + math.sin(a) * tip),
            (c + math.cos(a + math.pi / 2) * r * wide * 0.5, c + math.sin(a + math.pi / 2) * r * wide * 0.5),
            (c + math.cos(a + math.pi) * r * 0.06, c + math.sin(a + math.pi) * r * 0.06),
            (c + math.cos(a - math.pi / 2) * r * wide * 0.5, c + math.sin(a - math.pi / 2) * r * wide * 0.5),
        ], fill=fill + (255,), outline=INK + (170,))
    d.ellipse([c - r * 0.10, c - r * 0.10, c + r * 0.10, c + r * 0.10], fill=INK + (255,))

    img = img.resize((s, s), Image.LANCZOS)
    img.save(path)
    log("wrote %s (%dx%d)" % (path, s, s))


def make_seal(path):
    s = 128
    big = s * 4
    img = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    c = big / 2.0
    r = big * 0.42
    rng = random.Random(77)

    # a wax blob: a circle with a wobbly rim so it does not read as a perfect vector disc
    pts = []
    for i in range(72):
        a = math.pi * 2.0 * i / 72.0
        rr = r * (1.0 + rng.uniform(-0.055, 0.075))
        pts.append((c + math.cos(a) * rr, c + math.sin(a) * rr))
    d.polygon(pts, fill=(132, 26, 24, 255))
    d.polygon([(x, y - big * 0.012) for (x, y) in pts], fill=(168, 38, 32, 255))
    d.ellipse([c - r * 0.72, c - r * 0.72, c + r * 0.72, c + r * 0.72], outline=(96, 16, 16, 255), width=int(big * 0.018))

    # pressed anchor
    aw = r * 0.40
    d.line([c, c - r * 0.46, c, c + r * 0.40], fill=(96, 16, 16, 255), width=int(big * 0.030))
    d.line([c - aw, c - r * 0.28, c + aw, c - r * 0.28], fill=(96, 16, 16, 255), width=int(big * 0.024))
    d.arc([c - aw * 1.5, c - r * 0.10, c + aw * 1.5, c + r * 0.66], start=15, end=165,
          fill=(96, 16, 16, 255), width=int(big * 0.030))
    d.ellipse([c - r * 0.13, c - r * 0.62, c + r * 0.13, c - r * 0.36], outline=(96, 16, 16, 255), width=int(big * 0.024))

    img = img.resize((s, s), Image.LANCZOS)
    img.save(path)
    log("wrote %s (%dx%d)" % (path, s, s))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Content", "UI", "Textures")
    os.makedirs(out, exist_ok=True)
    make_parchment(os.path.join(out, "parchment.png"))
    make_rope_frame(os.path.join(out, "frame_rope.png"))
    make_compass(os.path.join(out, "compass_rose.png"))
    make_seal(os.path.join(out, "wax_seal.png"))
    log("RESULT=OK 4 textures in %s" % out)


if __name__ == "__main__":
    main()
