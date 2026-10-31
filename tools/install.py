#!/usr/bin/env python3
"""Install the game's files for Hard Truck 2 native from your own GOG copy.

    tools/install.py SOURCE [--to DIR] [--innoextract PATH]

SOURCE is the GOG installer (setup_hard_truck_2_king_of_the_road_1.3_amd_rdna_fix_(52932).exe),
unpacked with innoextract without running it, or a folder where the game is installed (GOG
Galaxy, the installer under Windows or Wine). DIR defaults to ~/.local/share/hardtruck2-native/game,
the folder the Makefile and the game look in.

Only the game's own files are copied: the 341 of the GOG 1.3 release
(builds/ht2_gog13.manifest.sha256), each checked against its hash; GOG's extras (the installer's
redistributables, icons) are left out. king.exe must be the release the translator knows
(builds/*.py, SHA256)."""
import argparse, glob, hashlib, os, re, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_TO = os.path.expanduser('~/.local/share/hardtruck2-native/game')
MANIFEST = os.path.join(ROOT, 'builds', 'ht2_gog13.manifest.sha256')


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def known_builds():
    out = {}
    for p in glob.glob(os.path.join(ROOT, 'builds', '*.py')):
        t = open(p).read()
        m = re.search(r"SHA256\s*=\s*'([0-9a-f]{64})'", t)
        n = re.search(r"NAME\s*=\s*'([^']+)'", t)
        if m:
            out[m.group(1)] = n.group(1) if n else os.path.basename(p)
    return out


def manifest():
    out = []
    for line in open(MANIFEST):
        h, p = line.rstrip('\n').split('  ', 1)
        out.append((p, h))
    return out


def find_ci(top, rel):
    """rel under top, ignoring case part by part (installs made on Windows differ in case)"""
    p = top
    for part in rel.split('/'):
        try:
            hit = next((e for e in os.listdir(p) if e.lower() == part.lower()), None)
        except OSError:
            return None
        if hit is None:
            return None
        p = os.path.join(p, hit)
    return p


def innoextract_path(given):
    for c in ([given] if given else []) + [os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'innoextract'),
                                           os.path.join(ROOT, 'third_party', 'innoextract', 'innoextract'),
                                           shutil.which('innoextract') or '']:
        if c and os.path.isfile(c) and os.access(c, os.X_OK):
            return c
    raise SystemExit('innoextract is needed to unpack the GOG installer (or install the game and pass its folder)')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('source')
    ap.add_argument('--to', default=DEFAULT_TO)
    ap.add_argument('--innoextract')
    a = ap.parse_args()
    src, to = os.path.abspath(os.path.expanduser(a.source)), os.path.abspath(os.path.expanduser(a.to))
    if os.path.exists(to) and os.listdir(to):
        raise SystemExit(f'{to} is not empty: remove it first (or pass --to)')
    tmp = None
    os.makedirs(os.path.dirname(to), exist_ok=True)
    try:
        if os.path.isdir(src):
            top = src
        else:
            tmp = tempfile.mkdtemp(prefix='ht2-gog-', dir=os.path.dirname(to) or None)
            print(f'unpacking {os.path.basename(src)} (innoextract; nothing is run) ...', flush=True)
            r = subprocess.run([innoextract_path(a.innoextract), '-e', '-s', '--language=en-US', '-d', tmp, src],
                               stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
            if r.returncode:
                raise SystemExit(f'{src}: innoextract could not unpack it: {r.stderr.strip()[-300:]}')
            top = tmp
        files = manifest()
        missing, bad = [], []
        os.makedirs(to, exist_ok=True)
        for i, (rel, h) in enumerate(files):
            p = find_ci(top, rel)
            if not p:
                missing.append(rel)
                continue
            dst = os.path.join(to, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(p, dst)
            if sha256(dst) != h:
                bad.append(rel)
            if i % 40 == 0:
                print(f'copying the game files ({i}/{len(files)}) ...', flush=True)
    finally:
        if tmp and os.path.exists(tmp):
            shutil.rmtree(tmp, ignore_errors=True)
    exe = os.path.join(to, 'king.exe')
    if not os.path.exists(exe):
        raise SystemExit(f'{src}: no king.exe (is it Hard Truck 2 - King of the Road from GOG?)')
    build = known_builds().get(sha256(exe))
    if missing:
        print(f'warning: {len(missing)} of the release\'s files not found, e.g. {", ".join(missing[:5])}')
    if bad:
        print(f'warning: {len(bad)} files differ from the GOG 1.3 release, e.g. {", ".join(bad[:5])}')
    if not build:
        print(f'king.exe has sha256 {sha256(exe)}, a release the translator has no build for (builds/): '
              'the copy is kept, but it will not run until one is added')
        sys.exit(2)
    print(f'king.exe is the {build} release: {len(files) - len(missing)} files ready in {to}')
    sys.exit(1 if missing or bad else 0)


if __name__ == '__main__':
    main()
