#!/usr/bin/env python3
import argparse
import struct
import zlib
from pathlib import Path


HEADER_SIZE = 0x62
DATA_OFFSET = 0x62

# Classic Workbench WBTOOL DiskObject with one Image. Width, height, depth and
# PlanePick are patched before writing.
WORKBENCH_TOOL_HEADER = bytes.fromhex(
    "e3100001"
    "00000000"
    "00000000"
    "00500028"
    "00050003"
    "0001"
    "00000001"
    "00000000"
    "00000000"
    "00000000"
    "00000000"
    "0000"
    "00000001"
    "0300"
    "00000000"
    "00000000"
    "80000000"
    "80000000"
    "00000000"
    "00000000"
    "00002000"
    "0000"
    "0000"
    "00500028"
    "0003"
    "00000001"
    "0700"
    "00000000"
)


def read_png(path):
    data = Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG file")

    pos = 8
    width = height = bit_depth = color_type = interlace = None
    idat = bytearray()

    while pos < len(data):
        if pos + 8 > len(data):
            raise ValueError("truncated PNG chunk")
        length = struct.unpack(">I", data[pos : pos + 4])[0]
        chunk_type = data[pos + 4 : pos + 8]
        chunk_data = data[pos + 8 : pos + 8 + length]
        pos += 12 + length

        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type, _comp, _filter, interlace = struct.unpack(
                ">IIBBBBB", chunk_data
            )
        elif chunk_type == b"IDAT":
            idat.extend(chunk_data)
        elif chunk_type == b"IEND":
            break

    if bit_depth != 8 or color_type not in (2, 6) or interlace != 0:
        raise ValueError("expected 8-bit non-interlaced RGB/RGBA PNG")

    channels = 4 if color_type == 6 else 3
    stride = width * channels
    raw = zlib.decompress(bytes(idat))
    rows = []
    src = 0
    prev = [0] * stride

    for _y in range(height):
        filter_type = raw[src]
        src += 1
        cur = list(raw[src : src + stride])
        src += stride

        for i, value in enumerate(cur):
            left = cur[i - channels] if i >= channels else 0
            up = prev[i]
            up_left = prev[i - channels] if i >= channels else 0

            if filter_type == 1:
                cur[i] = (value + left) & 0xFF
            elif filter_type == 2:
                cur[i] = (value + up) & 0xFF
            elif filter_type == 3:
                cur[i] = (value + ((left + up) >> 1)) & 0xFF
            elif filter_type == 4:
                cur[i] = (value + paeth(left, up, up_left)) & 0xFF
            elif filter_type != 0:
                raise ValueError("unknown PNG filter")

        rows.append(cur)
        prev = cur

    pixels = []
    for row in rows:
        out = []
        for x in range(0, len(row), channels):
            if channels == 4:
                out.append((row[x], row[x + 1], row[x + 2], row[x + 3]))
            else:
                out.append((row[x], row[x + 1], row[x + 2], 255))
        pixels.append(out)

    return width, height, pixels


def paeth(a, b, c):
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def is_logo_pixel(pixel):
    r, g, b, a = pixel
    if a < 32:
        return False
    if r > 235 and g > 235 and b > 235:
        return False
    hi = max(r, g, b)
    lo = min(r, g, b)
    if hi < 55:
        return False
    sat = (hi - lo) / float(hi or 1)
    if sat > 0.18 and hi > 80:
        return True
    return 70 <= hi <= 210 and (hi - lo) < 55


def find_content_bounds(width, height, pixels):
    left = width
    top = height
    right = -1
    bottom = -1
    for y, row in enumerate(pixels):
        for x, pixel in enumerate(row):
            if is_logo_pixel(pixel):
                left = min(left, x)
                top = min(top, y)
                right = max(right, x)
                bottom = max(bottom, y)

    if right < left or bottom < top:
        return 0, 0, width - 1, height - 1

    pad_x = max(1, (right - left + 1) // 24)
    pad_y = max(1, (bottom - top + 1) // 24)
    return (
        max(0, left - pad_x),
        max(0, top - pad_y),
        min(width - 1, right + pad_x),
        min(height - 1, bottom + pad_y),
    )


def classify_pixel(pixel):
    if not is_logo_pixel(pixel):
        return 0

    r, g, b, _a = pixel
    hi = max(r, g, b)
    lo = min(r, g, b)
    sat = (hi - lo) / float(hi or 1)

    if hi < 95 or sat < 0.12:
        return 1
    if b >= r and b >= g:
        return 3
    if hi > 205:
        return 2
    return 3 if (g > r and g > b) else 2


def render_icon(src_width, src_height, pixels, out_width, out_height):
    left, top, right, bottom = find_content_bounds(src_width, src_height, pixels)
    content_width = right - left + 1
    content_height = bottom - top + 1

    margin_x = 3
    margin_y = 2
    scale = min(
        (out_width - margin_x * 2) / float(content_width),
        (out_height - margin_y * 2) / float(content_height),
    )
    draw_width = max(1, int(content_width * scale))
    draw_height = max(1, int(content_height * scale))
    origin_x = (out_width - draw_width) // 2
    origin_y = (out_height - draw_height) // 2

    icon = [[0 for _x in range(out_width)] for _y in range(out_height)]
    for y in range(draw_height):
        src_y = top + min(content_height - 1, int((y + 0.5) / scale))
        for x in range(draw_width):
            src_x = left + min(content_width - 1, int((x + 0.5) / scale))
            icon[origin_y + y][origin_x + x] = classify_pixel(pixels[src_y][src_x])

    shadow = [row[:] for row in icon]
    for y, row in enumerate(icon):
        for x, value in enumerate(row):
            if value and x + 1 < out_width and y + 1 < out_height and shadow[y + 1][x + 1] == 0:
                shadow[y + 1][x + 1] = 1
    for y, row in enumerate(icon):
        for x, value in enumerate(row):
            if value:
                shadow[y][x] = value

    return shadow


def pack_bitplanes(icon, width, height, depth):
    words_per_row = (width + 15) // 16
    out = bytearray()
    for plane in range(depth):
        for y in range(height):
            for word_x in range(words_per_row):
                word = 0
                for bit in range(16):
                    x = word_x * 16 + bit
                    if x < width and ((icon[y][x] >> plane) & 1):
                        word |= 1 << (15 - bit)
                out.extend(struct.pack(">H", word))
    return bytes(out)


def build_info(icon, width, height, depth):
    if len(WORKBENCH_TOOL_HEADER) != HEADER_SIZE:
        raise ValueError("internal header has wrong size")
    header = bytearray(WORKBENCH_TOOL_HEADER)
    patch_word(header, 0x0C, width)
    patch_word(header, 0x0E, height)
    patch_word(header, 0x52, width)
    patch_word(header, 0x54, height)
    patch_word(header, 0x56, depth)
    header[0x5C] = (1 << depth) - 1
    header[0x5D] = 0
    return bytes(header) + pack_bitplanes(icon, width, height, depth)


def patch_word(buf, offset, value):
    buf[offset : offset + 2] = struct.pack(">H", value)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--width", type=int, default=80)
    parser.add_argument("--height", type=int, default=56)
    parser.add_argument("--depth", type=int, default=3)
    parser.add_argument("png")
    parser.add_argument("info")
    args = parser.parse_args()

    if args.depth < 2 or args.depth > 3:
        raise SystemExit("only 2 or 3 bitplanes are supported")

    src_width, src_height, pixels = read_png(args.png)
    icon = render_icon(src_width, src_height, pixels, args.width, args.height)
    Path(args.info).write_bytes(build_info(icon, args.width, args.height, args.depth))


if __name__ == "__main__":
    main()
