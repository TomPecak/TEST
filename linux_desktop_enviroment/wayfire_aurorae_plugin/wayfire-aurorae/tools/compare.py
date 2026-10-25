#!/usr/bin/env python3
"""Compare two screenshots of a decorated window (e.g. KWin vs aurorae-preview).

usage: tools/compare.py REFERENCE.png OURS.png [OUT.png]

Both images are flattened onto white and cropped to their content, then
compared pixel by pixel. Prints the number of differing pixels; OUT shows
reference | ours | 4x amplified difference. Render ours with a white
backdrop: aurorae-preview --backdrop 255,255,255 --hide S ...
"""
import sys
from PIL import Image, ImageChops


def load(path):
    im = Image.open(path).convert('RGBA')
    flat = Image.alpha_composite(Image.new('RGBA', im.size, (255, 255, 255, 255)), im).convert('RGB')
    bg = Image.new('RGB', flat.size, flat.getpixel((0, 0)))
    mask = ImageChops.difference(flat, bg).convert('L').point(lambda v: 255 if v > 6 else 0)
    return flat.crop(mask.getbbox())


def main():
    ref, ours = load(sys.argv[1]), load(sys.argv[2])
    w, h = max(ref.width, ours.width), max(ref.height, ours.height)
    a = Image.new('RGB', (w, h), 'white'); a.paste(ref, (0, 0))
    b = Image.new('RGB', (w, h), 'white'); b.paste(ours, (0, 0))
    diff = ImageChops.difference(a, b)
    hist = diff.convert('L').histogram()
    print(f'reference {ref.size}, ours {ours.size}: {sum(hist[9:])} of {w * h} pixels differ by more than 8')
    if len(sys.argv) > 3:
        out = Image.new('RGB', (3 * w + 20, h), (255, 0, 255))
        out.paste(a, (0, 0)); out.paste(b, (w + 10, 0))
        out.paste(diff.point(lambda v: min(255, v * 4)), (2 * w + 20, 0))
        out.save(sys.argv[3])
    return 0 if ref.size == ours.size else 1


if __name__ == '__main__':
    sys.exit(main())
