#!/usr/bin/env python3
"""生成设备查看器的应用图标（多尺寸 .ico 与预览图）。

设计：深色圆角底 + 三个等轴测零件方块（对应界面默认蓝/橙/青稳定配色）+ 受限地面网格。
每个尺寸独立以 4 倍超采样绘制后 LANCZOS 缩放，16/24 像素下只保留一个主方块，
避免细节糊成一团；.ico 用 32 位 BMP 条目手工封装，兼容 Windows 资源管理器与旧外壳。

用法：python make_icon.py [输出目录]
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw

BACKGROUND_TOP = (28, 40, 56)
BACKGROUND_BOTTOM = (10, 15, 22)
BORDER = (60, 84, 110)
GRID = (86, 128, 168, 120)

# 与三维视图一致的零件配色：顶面亮、右侧中、左侧暗。
BLUE = ((92, 168, 246), (57, 136, 232), (28, 84, 156))
ORANGE = ((250, 172, 108), (242, 141, 56), (172, 92, 24))
CYAN = ((86, 216, 232), (39, 191, 208), (16, 122, 138))
# 小尺寸专用：提高顶面亮度与侧面明暗差，缩到 16 像素仍能分辨立体。
SMALL_BLUE = ((150, 202, 255), (60, 140, 236), (26, 84, 166))
OUTLINE = (12, 20, 30)

SUPERSAMPLE = 4


def _lerp(first: tuple[int, int, int], second: tuple[int, int, int], t: float) -> tuple[int, int, int]:
    return tuple(int(round(first[i] + (second[i] - first[i]) * t)) for i in range(3))


def _iso_box(draw: ImageDraw.ImageDraw, cx: float, cy: float, half_width: float,
             depth: float, colors: tuple[tuple[int, int, int], ...], outline_width: int) -> None:
    """画一个等轴测方块：half_width 为水平半宽，depth 为竖直棱长。"""
    half_height = half_width * 0.5
    north = (cx, cy - half_height)
    east = (cx + half_width, cy)
    south = (cx, cy + half_height)
    west = (cx - half_width, cy)
    left = [west, south, (south[0], south[1] + depth), (west[0], west[1] + depth)]
    right = [south, east, (east[0], east[1] + depth), (south[0], south[1] + depth)]
    top = [north, east, south, west]
    for polygon, color in ((left, colors[2]), (right, colors[1]), (top, colors[0])):
        draw.polygon(polygon, fill=color, outline=OUTLINE, width=outline_width)


def draw_icon(size: int) -> Image.Image:
    scale = size * SUPERSAMPLE
    image = Image.new("RGBA", (scale, scale), (0, 0, 0, 0))

    # 深色渐变圆角底：先做单列渐变再拉伸，等价逐行插值。
    column = Image.new("RGB", (1, scale))
    for y in range(scale):
        column.putpixel((0, y), _lerp(BACKGROUND_TOP, BACKGROUND_BOTTOM, y / max(1, scale - 1)))
    mask = Image.new("L", (scale, scale), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, scale - 1, scale - 1], radius=size * 0.22 * SUPERSAMPLE, fill=255)
    image.paste(column.resize((scale, scale)), (0, 0), mask)

    draw = ImageDraw.Draw(image)
    hairline = max(1, SUPERSAMPLE)
    if size >= 32:
        draw.rounded_rectangle([hairline, hairline, scale - 1 - hairline, scale - 1 - hairline],
                               radius=size * 0.22 * SUPERSAMPLE, outline=BORDER, width=hairline)

    # 地面参考网格：只在较大尺寸出现，并按透明度绘制，避免与方块糊在一起。
    if size >= 64:
        blended = ImageDraw.Draw(image, "RGBA")
        grid_width = max(1, int(SUPERSAMPLE * (1.5 if size >= 128 else 1.1)))
        for index in range(-2, 3):
            offset_x = index * size * 0.12 * SUPERSAMPLE
            start_y = size * 0.76 * SUPERSAMPLE
            blended.line([(size * 0.5 * SUPERSAMPLE + offset_x - size * 0.19 * SUPERSAMPLE,
                           start_y + size * 0.095 * SUPERSAMPLE),
                          (size * 0.5 * SUPERSAMPLE + offset_x + size * 0.19 * SUPERSAMPLE,
                           start_y - size * 0.095 * SUPERSAMPLE)], fill=GRID, width=grid_width)

    if size <= 24:
        # 极小尺寸只画一个高对比方块：三点明暗差保证 16 像素下仍能读出立体轮廓。
        _iso_box(draw, size * 0.50 * SUPERSAMPLE, size * 0.40 * SUPERSAMPLE,
                 size * 0.31 * SUPERSAMPLE, size * 0.28 * SUPERSAMPLE, SMALL_BLUE,
                 max(1, int(SUPERSAMPLE * 1.0)))
    elif size < 48:
        # 小型尺寸保留主方块加一个强调方块，不画网格。
        _iso_box(draw, size * 0.44 * SUPERSAMPLE, size * 0.40 * SUPERSAMPLE,
                 size * 0.29 * SUPERSAMPLE, size * 0.25 * SUPERSAMPLE, SMALL_BLUE,
                 max(1, int(SUPERSAMPLE * 1.0)))
        _iso_box(draw, size * 0.70 * SUPERSAMPLE, size * 0.58 * SUPERSAMPLE,
                 size * 0.17 * SUPERSAMPLE, size * 0.14 * SUPERSAMPLE, ORANGE,
                 max(1, int(SUPERSAMPLE * 1.0)))
    else:
        outline_width = max(1, int(SUPERSAMPLE * 1.1))
        _iso_box(draw, size * 0.36 * SUPERSAMPLE, size * 0.46 * SUPERSAMPLE,
                 size * 0.22 * SUPERSAMPLE, size * 0.19 * SUPERSAMPLE, BLUE, outline_width)
        _iso_box(draw, size * 0.60 * SUPERSAMPLE, size * 0.55 * SUPERSAMPLE,
                 size * 0.155 * SUPERSAMPLE, size * 0.135 * SUPERSAMPLE, ORANGE, outline_width)
        _iso_box(draw, size * 0.755 * SUPERSAMPLE, size * 0.42 * SUPERSAMPLE,
                 size * 0.11 * SUPERSAMPLE, size * 0.095 * SUPERSAMPLE, CYAN, outline_width)

    return image.resize((size, size), Image.LANCZOS)


def write_ico(path: Path, images: list[Image.Image]) -> None:
    """写多尺寸 .ico：每个尺寸一个 32 位 BMP 条目加 1 位 AND 掩码。"""
    entries = []
    payloads = []
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = len(header) + 16 * len(images)
    for image in images:
        width, height = image.size
        pixels = image.load()
        color_rows = bytearray()
        mask_rows = bytearray()
        mask_stride = ((width + 31) // 32) * 4
        for y in range(height - 1, -1, -1):
            mask_bits = bytearray(mask_stride)
            for x in range(width):
                red, green, blue, alpha = pixels[x, y]
                color_rows += bytes((blue, green, red, alpha))
                if alpha < 128:
                    mask_bits[x // 8] |= 0x80 >> (x % 8)
            mask_rows += mask_bits
        image_header = struct.pack("<IiiHHIIiiII", 40, width, height * 2, 1, 32, 0,
                                   len(color_rows) + len(mask_rows), 0, 0, 0, 0)
        payload = image_header + bytes(color_rows) + bytes(mask_rows)
        payloads.append(payload)
        entries.append(struct.pack("<BBBBHHII", width if width < 256 else 0, height if height < 256 else 0,
                                   0, 0, 1, 32, len(payload), offset))
        offset += len(payload)
    path.write_bytes(header + b"".join(entries) + b"".join(payloads))


def main() -> int:
    destination = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "resources"
    destination.mkdir(parents=True, exist_ok=True)
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    images = [draw_icon(size) for size in sizes]
    icon_path = destination / "app.ico"
    write_ico(icon_path, images)

    # 预览：左为 256 原图，右为 Windows 常用的 16/24/32/48 放大 8 倍，用于核对小尺寸可读性。
    wanted = {16: None, 24: None, 32: None, 48: None}
    for image in images:
        if image.width in wanted:
            wanted[image.width] = image.resize((image.width * 8, image.height * 8), Image.NEAREST)
    preview_width = 256 + 12 + sum(image.width + 12 for image in wanted.values())
    preview = Image.new("RGB", (preview_width, 256), (245, 247, 250))
    preview.paste(images[-1].convert("RGB"), (0, 0))
    x = 256 + 12
    for image in wanted.values():
        preview.paste(image.convert("RGB"), (x, 256 - image.height))
        x += image.width + 12
    preview.save(destination / "app-icon-preview.png")
    print(f"ICON: {icon_path} ({icon_path.stat().st_size} bytes, {len(sizes)} sizes)")
    print(f"PREVIEW: {destination / 'app-icon-preview.png'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
