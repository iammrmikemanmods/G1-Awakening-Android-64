#!/usr/bin/env python3
"""Host-only true text rasterizer for the Android UI fallback contract probe.

The application uses Android Canvas, not this tool or this font. No generated
glyphs replace the source bitmap fonts; this tests explicit fallback handling.
"""
import json
import math
import sys
import struct
from functools import lru_cache
from PIL import Image, ImageDraw, ImageFont

@lru_cache(maxsize=64)
def face(size):
    return ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", size)


def render(request):
    font = face(max(1, math.ceil(request["size"])))
    text = request["text"]
    ascent, descent = font.getmetrics()
    left, top, right, bottom = font.getbbox(text, anchor="ls")
    width = max(1, math.ceil(max(font.getlength(text), right) - min(0, left)) + 2)
    height = max(1, ascent + descent + 2)
    if width > 8192 or height > 8192 or width * height > 16 * 1024 * 1024:
        raise ValueError("Fallback text exceeds bounded raster dimensions")
    image = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    ImageDraw.Draw(image).text((1 - min(0, left), 1 + ascent), text, font=font, fill=(255,255,255,255), anchor="ls")
    return image


if sys.argv[1] == "--server":
    for request_line in sys.stdin.buffer:
        image = render(json.loads(request_line))
        pixels = image.tobytes()
        sys.stdout.buffer.write(struct.pack("<III", image.width, image.height, len(pixels)))
        sys.stdout.buffer.write(pixels)
        sys.stdout.buffer.flush()
else:
    render(json.load(open(sys.argv[1], encoding="utf-8"))).save(sys.argv[2])
