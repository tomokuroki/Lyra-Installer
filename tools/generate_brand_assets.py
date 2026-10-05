"""Generate Windows raster assets from Lyra's official SVG logo geometry."""

from pathlib import Path
from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets"
SCALE = 8
SIZE = 128 * SCALE


def cubic(p0, p1, p2, p3, steps=24):
    points = []
    for index in range(1, steps + 1):
        t = index / steps
        u = 1 - t
        points.append((
            u**3 * p0[0] + 3 * u**2 * t * p1[0] + 3 * u * t**2 * p2[0] + t**3 * p3[0],
            u**3 * p0[1] + 3 * u**2 * t * p1[1] + 3 * u * t**2 * p2[1] + t**3 * p3[1],
        ))
    return points


def scaled(points):
    return [(round(x * SCALE), round(y * SCALE)) for x, y in points]


def gradient(size, start, end, vertical=True):
    image = Image.new("RGBA", size)
    pixels = image.load()
    span = size[1] if vertical else size[0]
    for offset in range(span):
        t = offset / max(span - 1, 1)
        color = tuple(round(start[i] * (1 - t) + end[i] * t) for i in range(4))
        if vertical:
            ImageDraw.Draw(image).line((0, offset, size[0], offset), fill=color)
        else:
            ImageDraw.Draw(image).line((offset, 0, offset, size[1]), fill=color)
    return image


def render_logo():
    image = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    gold = gradient((SIZE, SIZE), (255, 230, 154, 255), (248, 200, 90, 255))
    blue = gradient((SIZE, SIZE), (95, 215, 255, 255), (51, 119, 217, 255))

    first = [(29, 96.5)]
    segments = [
        ((29, 96.5), (39, 86.1), (51.3, 82), (63.5, 82)),
        ((63.5, 82), (76, 82), (85.5, 86.3), (95.7, 87.5)),
        ((95.7, 87.5), (103.7, 88.4), (110.4, 85.8), (114, 78.8)),
        ((114, 78.8), (111.1, 71.4), (104.3, 67.4), (96, 67.4)),
        ((96, 67.4), (87.2, 67.4), (80.1, 72.2), (73.2, 76.1)),
        ((73.2, 76.1), (68, 79), (62.9, 80.5), (56.8, 80.5)),
        ((56.8, 80.5), (49.8, 80.5), (43, 78.8), (38.5, 76.2)),
    ]
    for segment in segments:
        first += cubic(*segment)
    first += [(38.5, 38.5)]
    first += cubic((38.5, 38.5), (44.7, 36), (52.9, 31.8), (58.7, 23.2))
    first += cubic((58.7, 23.2), (59.8, 21.6), (60.6, 19.7), (61, 17.5))
    first += cubic((61, 17.5), (49.7, 19.2), (39, 22.3), (30.4, 28.8))
    first += cubic((30.4, 28.8), (23.7, 33.8), (18.5, 41.1), (18.5, 50.1))
    first += [(18.5, 81)]
    first += cubic((18.5, 81), (18.5, 87.8), (15.4, 93.7), (11, 99.3))
    first += [(29, 96.5)]

    second = [(54, 109)]
    for segment in [
        ((54, 109), (64.7, 110.4), (77.9, 114), (91.2, 114)),
        ((91.2, 114), (102.4, 114), (111.9, 111.2), (118, 103.1)),
        ((118, 103.1), (114.7, 101.4), (111, 100.6), (107.3, 100.6)),
        ((107.3, 100.6), (99.1, 100.6), (91.3, 104.9), (82.8, 104.9)),
        ((82.8, 104.9), (73.8, 104.9), (67.8, 99.8), (59.1, 97.3)),
        ((59.1, 97.3), (48.7, 94.3), (37.3, 97), (28, 101.9)),
        ((28, 101.9), (35.6, 105.7), (44.8, 107.8), (54, 109)),
    ]:
        second += cubic(*segment)

    gold_mask = Image.new("L", (SIZE, SIZE), 0)
    mask_draw = ImageDraw.Draw(gold_mask)
    mask_draw.polygon(scaled(first), fill=255)
    mask_draw.polygon(scaled(second), fill=255)
    image.alpha_composite(Image.composite(gold, Image.new("RGBA", image.size), gold_mask))

    blue_mask = Image.new("L", (SIZE, SIZE), 0)
    draw = ImageDraw.Draw(blue_mask)
    for box, radius in [((64, 54, 74, 82), 5), ((78, 42, 90, 88), 6),
                        ((94, 58, 104, 86), 5), ((54, 62, 62, 82), 4)]:
        draw.rounded_rectangle(tuple(round(v * SCALE) for v in box), radius=radius * SCALE, fill=255)
    image.alpha_composite(Image.composite(blue, Image.new("RGBA", image.size), blue_mask))
    return image.resize((256, 256), Image.Resampling.LANCZOS)


def main():
    ASSETS.mkdir(exist_ok=True)
    logo = render_logo()
    logo.save(ASSETS / "lyra-logo.png")
    logo.save(ASSETS / "lyra.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])


if __name__ == "__main__":
    main()
