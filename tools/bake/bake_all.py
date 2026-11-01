#!/usr/bin/env python3
"""Bake the lighting of every track (or those named) into the game's maps folder: one bake per layout per sun direction,
the looks sharing a sun sharing a bake, and index.txt, which light.c reads. Sun and ambient come from the rip's sidecars,
which evaluated each look's .key file.

    bake_all.py [--threads N] [SLUG ...]"""
import glob, json, math, os, re, subprocess, sys

T = os.path.expanduser('~/DevWork/Car Models/Off-Road Redneck Racing/models/tracks')
OUT = os.path.expanduser(os.environ.get('ORR_LIGHTMAPS', '~/.local/share/offroad-native/lightmaps'))
HERE = os.path.dirname(os.path.abspath(__file__))
WORK = 'work/bake'
TINT = (1.0, 0.92, 0.82)        # what the ground bounces, warm [guess: dirt; the amount is measured, only this tint is chosen]


def main(args):
    threads = '12'
    if args[:1] == ['--threads']:
        threads = args[1]
        args = args[2:]
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(WORK, exist_ok=True)
    subprocess.check_call(['gcc', '-O3', '-march=native', '-ffast-math', '-w', '-o', f'{WORK}/bake', f'{HERE}/bake.c', '-lz', '-lm', '-lpthread'])
    index = {}
    p = f'{OUT}/index.txt'
    if os.path.exists(p):
        for l in open(p):
            index[tuple(l.split()[:3])] = l
    slugs = args or sorted(os.path.basename(f)[:-5] for f in glob.glob(f'{T}/*_track*.json'))
    for slug in slugs:
        j = json.load(open(f'{T}/{slug}.json'))
        m = re.search(r'_track(\d)(_redneck)?$', slug)
        layout = m.group(1) + ('r' if m.group(2) else '')
        env = j['place'].lower()
        geo = f'{WORK}/{slug}.geo'
        rtg = f'{OUT}/{slug}.rtg'
        if not os.path.exists(geo) or not os.path.exists(rtg):
            subprocess.check_call([sys.executable, f'{HERE}/export.py', slug, geo, rtg])
        oll = f'{OUT}/{slug}.oll'
        if not os.path.exists(oll):
            subprocess.check_call(['nice', '-n', '15', f'{WORK}/bake', '--lamps', geo, oll + '.part', threads])
            if os.path.exists(oll + '.part'):
                os.replace(oll + '.part', oll)
        suns = []
        for t in j['times']:
            g = t['sun']['dir']
            d = (-g[0], g[1], g[2])                  # glTF back to the game's axes
            for n, s in enumerate(suns):
                if sum(a * b for a, b in zip(s, d)) > math.cos(math.radians(1.0)):
                    break
            else:
                suns.append(d)
                n = len(suns) - 1
            name = f'{slug}_{n}.olm'
            if not os.path.exists(f'{OUT}/{name}'):
                print(slug, n, d, flush=True)
                subprocess.check_call(['nice', '-n', '15', f'{WORK}/bake', geo, f'{OUT}/{name}.part', *map(str, d), threads])
                os.replace(f'{OUT}/{name}.part', f'{OUT}/{name}')
            c, a = t['sun']['color'], t['ambient']
            index[(env, layout, t['id'])] = ' '.join([env, layout, t['id'], name, *map(str, suns[n]), *map(str, c), *map(str, a), *map(str, TINT)]) + '\n'
        with open(p + '.part', 'w') as f:
            f.writelines(index[k] for k in sorted(index))
        os.replace(p + '.part', p)
        os.remove(geo)


if __name__ == '__main__':
    main(sys.argv[1:])
