"""Reads exported meshes for the regression tests, expanding the indexed '2'
variants (TCM2, TCV2, TCA2, TCF2) to the '1' triangle-list layout they parse."""
import struct


def read(path):
    data = path.read_bytes()
    if data[3:4] != b'2':
        return data
    vertices, indices = struct.unpack_from('<II', data, 4)
    stride = 40 if data[:3] == b'TCM' else 72
    table = data[12:12 + vertices * stride]
    order = struct.unpack_from('<%dI' % indices, data, 12 + vertices * stride)
    rows = [table[i * stride:(i + 1) * stride] for i in range(vertices)]
    return data[:3] + b'1' + struct.pack('<I', indices) + b''.join(rows[i] for i in order)
