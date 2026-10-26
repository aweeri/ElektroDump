#!/usr/bin/env python3
"""Generate the M6 calibration table and palette PNGs (clean-room data)."""
import json
import os
import struct
import zlib


def png_rgb(path, width, height, rows):
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter type 0
        for x in range(width):
            raw.extend(rows[y][x])

    def chunk(typ, data):
        out = struct.pack('>I', len(data)) + typ + data
        out += struct.pack('>I', zlib.crc32(typ + data) & 0xFFFFFFFF)
        return out

    ihdr = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)
    idat = zlib.compress(bytes(raw), 9)
    with open(path, 'wb') as handle:
        handle.write(b'\x89PNG\r\n\x1a\n')
        handle.write(chunk(b'IHDR', ihdr))
        handle.write(chunk(b'IDAT', idat))
        handle.write(chunk(b'IEND', b''))


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    cal_dir = os.path.join(root, 'resources', 'calibration')
    lut_dir = os.path.join(root, 'resources', 'luts')
    os.makedirs(cal_dir, exist_ok=True)
    os.makedirs(lut_dir, exist_ok=True)

    channels = []
    for channel, wavelength in ((0, 0.5), (1, 0.6), (2, 0.7)):
        lut = [round(i / 1023.0, 6) for i in range(1024)]
        channels.append({
            'id': channel,
            'wavelength_um': wavelength,
            'unit': 'reflectance',
            'lut': lut,
        })
    ir_lut = [round(190.0 + i / 1023.0 * 130.0, 6) for i in range(1024)]
    channels.append({
        'id': 3,
        'wavelength_um': 10.7,
        'unit': 'brightness_temperature',
        'lut': ir_lut,
    })
    table = {'version': 1, 'channels': channels}
    with open(os.path.join(cal_dir, 'msugs_table.json'), 'w') as handle:
        json.dump(table, handle, indent=1)
        handle.write('\n')

    gray = [[(i, i, i) for i in range(256)]]
    png_rgb(os.path.join(lut_dir, 'ir_gray.png'), 256, 1, gray)

    cloud = []
    for i in range(256):
        t = i / 255.0
        r = int(20 + t * 235)
        g = int(30 + t * 225)
        b = int(80 + t * 175)
        cloud.append((r, g, b))
    png_rgb(os.path.join(lut_dir, 'ir_cloud.png'), 256, 1, [cloud])

    print('wrote calibration table and LUT PNGs')


if __name__ == '__main__':
    main()
