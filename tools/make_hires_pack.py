"""Builds the high-resolution people texture pack: every costume layer, face, hair, makeup
and accessory texture upscaled 2x by upscale_textures.py, on all CPU cores.

Sources: the game's loose files (<game>/Data/textures/<folder>, which override the paks,
including MoviesMod's own costumes) first, else the pak copies extracted to
work/chars/data/textures/<folder> (tools/pak.py extract). Output mirrors <out>/Data/textures.
Files already in <out> are skipped, so an interrupted run can be resumed.

Usage: python make_hires_pack.py <out_root> [--jobs N] [folder ...]
       (folders default to costumes people hair makeup accessories)

Install: tools/hires_pack_install.py links the folders into the game (see that file).
"""
import concurrent.futures as cf
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import upscale_textures  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = r'C:\Program Files (x86)\Lionhead Studios Ltd\The Movies'
FOLDERS = ['costumes', 'people', 'hair', 'makeup', 'accessories']


def sources(folder):
    """name -> source path; the game's loose files win over the pak extract."""
    found = {}
    for base in (os.path.join(ROOT, 'work', 'chars', 'data', 'textures', folder),
                 os.path.join(GAME, 'Data', 'textures', folder)):
        if not os.path.isdir(base):
            continue
        for dirpath, _, files in os.walk(base):
            for f in files:
                if f.lower().endswith('.dds'):
                    rel = os.path.relpath(os.path.join(dirpath, f), base).lower()
                    found[rel] = os.path.join(dirpath, f)
    return found


def work(job):
    src, dst = job
    try:
        out, info = upscale_textures.upscale(open(src, 'rb').read(), 2.0, 60.0, 2048, False)
    except Exception as e:
        return src, 'SKIP %s' % e
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    tmp = dst + '.part'
    open(tmp, 'wb').write(out)
    os.replace(tmp, dst)
    return src, None


def main(argv):
    out_root = argv[0]
    jobs_n = os.cpu_count() or 4
    folders = []
    args = list(argv[1:])
    while args:
        a = args.pop(0)
        if a == '--jobs':
            jobs_n = int(args.pop(0))
        else:
            folders.append(a)
    folders = folders or FOLDERS

    jobs = []
    for folder in folders:
        for rel, src in sorted(sources(folder).items()):
            dst = os.path.join(out_root, 'Data', 'textures', folder, rel)
            if not os.path.exists(dst):
                jobs.append((src, dst))
    print('%d textures to upscale with %d workers' % (len(jobs), jobs_n), flush=True)
    start, done, skipped = time.time(), 0, 0
    with cf.ProcessPoolExecutor(jobs_n) as pool:
        for src, err in pool.map(work, jobs, chunksize=4):
            done += 1
            if err:
                skipped += 1
                print('%s: %s' % (os.path.basename(src), err), flush=True)
            if done % 200 == 0 or done == len(jobs):
                rate = done / max(1e-6, time.time() - start)
                print('%d/%d done, %d skipped, %.1f/s, about %.0f min left'
                      % (done, len(jobs), skipped, rate, (len(jobs) - done) / max(rate, 1e-6) / 60), flush=True)
    print('finished: %d written, %d skipped' % (done - skipped, skipped))


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    main(sys.argv[1:])
