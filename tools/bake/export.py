#!/usr/bin/env python3
"""Turn a track of the rip (Car Models/Off-Road Redneck Racing/models/tracks) into what bake.c casts rays at.

    export.py SLUG OUT.geo

Game units (x back from glTF's -x, 100 to the metre). What blocks light: the terrain and every opaque or cut-out part of
the scenery, not the water, sky, puddles, racing line, or the rain-only and see-through parts.
OUT.geo, little endian: 'OGEO', W, H, nmat, ntri (u32); W * H f32 heights; nmat {u32 tw, th; f32 albedo[3]; tw * th u8 alpha};
ntri {f32 p[9]; f32 uv[6]; u32 mat (bit 31: the terrain)}."""
import io, json, os, struct, sys
import numpy as np
from PIL import Image

T = os.path.expanduser('~/DevWork/Car Models/Off-Road Redneck Racing/models/tracks')
S, CELL = 0.01, 512.0
MASK = 128


def read_glb(path):
    d = open(path, 'rb').read()
    n = struct.unpack_from('<I', d, 12)[0]
    g = json.loads(d[20:20 + n])
    m = struct.unpack_from('<I', d, 20 + n)[0]
    return g, memoryview(d)[28 + n:28 + n + m]


def main(slug, out):
    g, b = read_glb(f'{T}/glb/{slug}.glb')
    WIDTHS = {5120: 'i1', 5121: 'u1', 5122: 'i2', 5123: 'u2', 5125: 'u4', 5126: 'f4'}
    COUNT = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}

    def acc(i):
        a = g['accessors'][i]
        v = g['bufferViews'][a['bufferView']]
        t = np.dtype(WIDTHS[a['componentType']]).newbyteorder('<')
        k = COUNT[a['type']]
        off = v.get('byteOffset', 0) + a.get('byteOffset', 0)
        st = v.get('byteStride', 0) or t.itemsize * k
        raw = np.frombuffer(b, np.uint8, a['count'] * st if st != t.itemsize * k else a['count'] * st, off)
        if st == t.itemsize * k:
            return raw.view(t).reshape(-1, k)
        return np.stack([raw[i * st:i * st + t.itemsize * k].view(t) for i in range(a['count'])])

    mats = []
    for m in g['materials']:
        ex = m.get('extras') or {}
        solid = m.get('alphaMode') != 'BLEND' and ex.get('blend') in ('opaque', None) and not (ex.get('zwrite') is False)
        tw = th = 0
        alpha = b''
        col = (0.5, 0.5, 0.5)
        bt = (m.get('pbrMetallicRoughness') or {}).get('baseColorTexture')
        if bt is not None and solid:
            im = g['images'][g['textures'][bt['index']]['source']]
            v = g['bufferViews'][im['bufferView']]
            pic = Image.open(io.BytesIO(bytes(b[v.get('byteOffset', 0):v.get('byteOffset', 0) + v['byteLength']]))).convert('RGBA')
            s = pic.resize((1, 1), Image.BOX).getpixel((0, 0))
            col = (s[0] / 255, s[1] / 255, s[2] / 255)
            if m.get('alphaMode') == 'MASK':
                q = pic.resize((min(MASK, pic.width), min(MASK, pic.height)), Image.BOX).getchannel('A')
                tw, th = q.size
                alpha = q.tobytes()
        mats.append((solid, tw, th, col, alpha))

    tris = []
    height = None

    def node_mat(n):
        if 'matrix' in n:
            return np.array(n['matrix'], float).reshape(4, 4).T
        M = np.eye(4)
        if 'translation' in n: M[:3, 3] = n['translation']
        assert 'rotation' not in n and 'scale' not in n, n.get('name')
        return M

    def walk(i, P, skip):
        nonlocal height
        n = g['nodes'][i]
        name = n.get('name', '')
        M = P @ node_mat(n)
        if name in ('water', 'puddles', 'rain_only', 'racing_line', 'start_finish') or name.startswith('sky_') or 'heli' in name.lower():
            return
        if 'mesh' in n:
            is_terrain = name == 'terrain'
            for p in g['meshes'][n['mesh']]['primitives']:
                mid = p.get('material', 0)
                if not mats[mid][0]:
                    continue
                pos = acc(p['attributes']['POSITION']).astype(float)
                uv = acc(p['attributes']['TEXCOORD_0']) if 'TEXCOORD_0' in p['attributes'] else np.zeros((len(pos), 2), 'f4')
                w = (M[:3, :3] @ pos.T).T + M[:3, 3]
                w = w / S
                w[:, 0] *= -1
                idx = acc(p['indices']).reshape(-1) if 'indices' in p else np.arange(len(pos))
                idx = idx.reshape(-1, 3).astype(int)
                tris.append((w[idx].astype('f4').reshape(-1, 9), uv[idx].astype('f4').reshape(-1, 6),
                             np.full(len(idx), mid | (0x80000000 if is_terrain else 0), 'u4')))
                if is_terrain:
                    if height is None: height = {}
                    ij = np.round(w[:, [0, 2]] / CELL).astype(int)
                    for (a, c), y in zip(ij.tolist(), w[:, 1].tolist()):
                        height[(a, c)] = y
        for c in n.get('children', []):
            walk(c, M, skip)

    walk(g['scenes'][0]['nodes'][0], np.eye(4), None)
    W = max(k[0] for k in height) + 1
    H = max(k[1] for k in height) + 1
    hf = np.zeros((H, W), 'f4')
    for (a, c), y in height.items():
        hf[c, a] = y
    P = np.concatenate([t[0] for t in tris]); U = np.concatenate([t[1] for t in tris]); M = np.concatenate([t[2] for t in tris])
    with open(out, 'wb') as f:
        f.write(b'OGEO' + struct.pack('<4I', W, H, len(mats), len(P)))
        f.write(hf.tobytes())
        for solid, tw, th, col, alpha in mats:
            f.write(struct.pack('<2I3f', tw, th, *col) + alpha)
        rec = np.zeros(len(P), dtype=[('p', '<f4', 9), ('uv', '<f4', 6), ('m', '<u4')])
        rec['p'] = P; rec['uv'] = U; rec['m'] = M
        f.write(rec.tobytes())
    print(slug, 'cells', W, H, 'tris', len(P), 'terrain', int((M >> 31).sum()), 'height', hf.min(), hf.max())


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
