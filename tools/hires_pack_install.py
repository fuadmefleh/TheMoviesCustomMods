"""Installs or removes the high-resolution people texture pack (tools/make_hires_pack.py).

The pack is several GB, so it stays on another drive: each <game>/Data/textures/<folder>
becomes a directory junction to <pack>/Data/textures/<folder>. The game sees ordinary loose
files (which override the paks). The folder's previous loose files are moved to
<pack>/backup/<folder> and restored by 'uninstall'. Also sets [MovieMaker]
CharacterTextureSize in MoviesMod.ini (1024 installed, 512 removed), without which bigger
layers are painted into the game's 512x512 people textures and gain nothing.

Usage: python hires_pack_install.py install|uninstall|status <pack_root>
"""
import os
import re
import shutil
import subprocess
import sys

GAME = r'C:\Program Files (x86)\Lionhead Studios Ltd\The Movies'
FOLDERS = ['costumes', 'people', 'hair', 'makeup', 'accessories']


def game_running():
    out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq MoviesSE.exe'], capture_output=True, text=True).stdout
    return 'MoviesSE.exe' in out


def is_junction(path):
    try:
        return bool(os.readlink(path))
    except (OSError, ValueError):
        return False


def set_texture_size(size):
    for ini in (os.path.join(GAME, 'MoviesMod.ini'),
                os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'MoviesMod.ini')):
        if not os.path.exists(ini):
            continue
        text = open(ini, encoding='utf-8', newline='').read()
        new = re.sub(r'(?m)^(CharacterTextureSize\s*=\s*)\d+', r'\g<1>%d' % size, text)
        if new != text:
            open(ini, 'w', encoding='utf-8', newline='').write(new)
    print('CharacterTextureSize = %d' % size)


def install(pack):
    for folder in FOLDERS:
        live = os.path.join(GAME, 'Data', 'textures', folder)
        target = os.path.join(pack, 'Data', 'textures', folder)
        if not os.path.isdir(target):
            print('%s: not in the pack, skipped' % folder)
            continue
        if is_junction(live):
            print('%s: already linked' % folder)
            continue
        if os.path.isdir(live):
            backup = os.path.join(pack, 'backup', folder)
            if os.path.exists(backup):
                sys.exit('%s exists - an earlier install was not undone; check it by hand' % backup)
            # Anything that isn't a texture the pack replaced (e.g. a text file) goes along.
            for dirpath, _, files in os.walk(live):
                for f in files:
                    rel = os.path.relpath(os.path.join(dirpath, f), live)
                    dst = os.path.join(target, rel)
                    if not os.path.exists(dst):
                        os.makedirs(os.path.dirname(dst), exist_ok=True)
                        shutil.copy2(os.path.join(dirpath, f), dst)
            os.makedirs(os.path.dirname(backup), exist_ok=True)
            shutil.move(live, backup)
        subprocess.run(['cmd', '/c', 'mklink', '/J', live, target], check=True, capture_output=True)
        print('%s: linked to %s' % (folder, target))
    set_texture_size(1024)


def uninstall(pack):
    for folder in FOLDERS:
        live = os.path.join(GAME, 'Data', 'textures', folder)
        backup = os.path.join(pack, 'backup', folder)
        if is_junction(live):
            os.rmdir(live)   # removes the link only
            print('%s: unlinked' % folder)
        if os.path.isdir(backup) and not os.path.exists(live):
            shutil.move(backup, live)
            print('%s: original loose files restored' % folder)
    set_texture_size(512)


def status(pack):
    for folder in FOLDERS:
        live = os.path.join(GAME, 'Data', 'textures', folder)
        print('%-12s %s' % (folder, 'linked -> ' + os.readlink(live) if is_junction(live)
                            else 'game folder' if os.path.isdir(live) else 'none'))


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in ('install', 'uninstall', 'status'):
        sys.exit(__doc__)
    if sys.argv[1] != 'status' and game_running():
        sys.exit('The Movies is running - close it first')
    {'install': install, 'uninstall': uninstall, 'status': status}[sys.argv[1]](sys.argv[2])


if __name__ == '__main__':
    main()
