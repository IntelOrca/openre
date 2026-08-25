# Generates games/core/texture/ui_arrow.bmp: two solid white triangles
# (left / right) on a transparent background, packed side by side.
# Written as a bottom-up 32bpp BMP with a BITMAPV4HEADER so the engine's
# BMP decoder picks up the alpha mask (see texture_loader.cpp).

import struct

W = 10   # two 5px-wide glyphs
H = 9    # glyph height (odd -> centred tip row)
CELL_W = 5

def right_triangle(w, h):
    # Solid triangle pointing left-to-right: column x spans the central
    # (h - x*2) rows, widest at x=0, single-pixel tip at x=w-1.
    cx = h // 2
    return {(x, y)
            for x in range(w)
            for y in range(cx - (h - 1 - x * 2) // 2, cx + (h - 1 - x * 2) // 2 + 1)}

# Left cell mirrors it so the tips point outwards from the control.
left = {(CELL_W - 1 - x, y) for x, y in right_triangle(CELL_W, H)}
right = {(W - 1 - x, y) for x, y in left}

rows = []
for y in range(H - 1, -1, -1):  # bottom-up
    row = b''
    for x in range(W):
        on = (x, y) in left or (x, y) in right
        row += struct.pack('<BBBB', 255, 255, 255, 255 if on else 0)
    rows.append(row)

header_size = 108  # BITMAPV4HEADER
data_offset = 14 + header_size
bmp = b'BM' + struct.pack('<IHHI', data_offset + len(rows) * W * 4, 0, 0, data_offset)
bmp += struct.pack('<IiiHHIIiiII', header_size, W, H, 1, 32, 3, 0, 0, 0, 0, 0)
bmp += struct.pack('<III', 0x00FF0000, 0x0000FF00, 0x000000FF)  # R G B
bmp += struct.pack('<III', 0xFF000000, 0, 0)                    # A + CIEXYZ
bmp += bytes(header_size + 14 - len(bmp))                       # pad to V4 size
bmp += b''.join(rows)

with open('games/core/texture/ui_arrow.bmp', 'wb') as f:
    f.write(bmp)
print(f'wrote ui_arrow.bmp ({len(bmp)} bytes)')
