#!/usr/bin/env python3
"""A bake as pictures: view.py FILE.olm OUTPREFIX (sun, sky, bounce, one grid layer)"""
import struct, sys, zlib
from PIL import Image
d = open(sys.argv[1], 'rb').read()
h = struct.unpack_from('<10I', d, 0); p = 56
def sec():
    global p
    n, c = struct.unpack_from('<QQ', d, p)
    r = zlib.decompress(d[p + 16:p + 16 + c]); p += 16 + c
    return r
_, W, H, sw, sh, aw, ah, gw, gh, gl = h
hf, sun, amb, grid = sec(), sec(), sec(), sec()
Image.frombytes('L', (sw, sh), sun).resize((1024, 1024 * sh // sw)).save(sys.argv[2] + '_sun.png')
a = Image.frombytes('LA', (aw, ah), amb)
a.getchannel('L').save(sys.argv[2] + '_sky.png'); a.getchannel('A').save(sys.argv[2] + '_bounce.png')
print('mean sun', sum(sun) / len(sun) / 255, 'sky', sum(amb[0::2]) / (len(amb) // 2) / 255, 'bounce', sum(amb[1::2]) / (len(amb) // 2) / 255)
