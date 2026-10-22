"""Packages a release: dist/MoviesMod-<version>.zip, laid out like the game folder, so
installing is "extract into <game folder>".

  dinput8.dll                      the mod (from build/, run build.bat first)
  MoviesMod.ini                    settings
  Data/...                         buildings, models, costumes, research packs, title colours
  MoviesMod/video_templates/       export profiles the DLL rewrites for HD export
  MoviesMod/tools/                 extract_ui.py + pak.py (optional: the TV screen's artwork)
  MoviesMod-README.txt             install instructions

Usage: python package_release.py <version>
"""
import os
import re
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CRLF = chr(13) + chr(10)
LF = chr(10)

README = """MoviesMod {version} for The Movies: Stunts & Effects v1.2
============================================================

Requires MoviesSE.exe v1.2 (The Movies with the Stunts & Effects expansion, patched to 1.2).
On any other version the mod switches itself off and the game runs unmodded.

INSTALL
1. Close the game.
2. Extract everything in this zip into your game folder, usually
     C:\\Program Files (x86)\\Lionhead Studios Ltd\\The Movies
   and let it merge folders (dinput8.dll and MoviesMod.ini sit next to MoviesSE.exe).
3. Optional, for the Television screen's artwork (banners and rosettes taken from your
   own copy of the game): with Python 3 and Pillow installed, run in the game folder
     python MoviesMod\\tools\\extract_ui.py .
4. Start the game. MoviesMod.log appears in the game folder.

PLAYING
- F8 or the TV button on the right of the HUD opens the Television screen.
- Everything can be tuned or switched off in MoviesMod.ini (each setting is explained there).

UNINSTALL
Delete dinput8.dll (the game then runs unmodded). To remove the content too, delete
MoviesMod.ini, the MoviesMod folder and the files this zip added under Data.

Source, docs and issues: https://github.com/fuadmefleh/TheMoviesCustomMods
"""


def windows_lines(text):
    return text.replace(CRLF, LF).replace(LF, CRLF)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    version = sys.argv[1]
    dll = os.path.join(ROOT, 'build', 'dinput8.dll')
    if not os.path.exists(dll):
        sys.exit('build/dinput8.dll not found - run build.bat first')
    os.makedirs(os.path.join(ROOT, 'dist'), exist_ok=True)
    out = os.path.join(ROOT, 'dist', 'MoviesMod-%s.zip' % version)
    files = [(dll, 'dinput8.dll')]
    for folder, prefix in ((os.path.join(ROOT, 'content', 'data'), 'Data'),
                           (os.path.join(ROOT, 'content', 'video_templates'), 'MoviesMod/video_templates')):
        for dirpath, _, names in os.walk(folder):
            for n in sorted(names):
                if 'tv_test' in n:
                    continue   # the building pipeline's test building
                path = os.path.join(dirpath, n)
                files.append((path, prefix + '/' + os.path.relpath(path, folder).replace(os.sep, '/')))
    for tool in ('extract_ui.py', 'pak.py'):
        files.append((os.path.join(ROOT, 'tools', tool), 'MoviesMod/tools/' + tool))
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
        for src, arc in files:
            z.write(src, arc)
        # Settings as a new player gets them: no high-resolution texture pack installed.
        ini = open(os.path.join(ROOT, 'MoviesMod.ini'), encoding='utf-8').read()
        ini = re.sub(r'(?m)^(CharacterTextureSize\s*=\s*)\d+', r'\g<1>512', ini)
        z.writestr('MoviesMod.ini', windows_lines(ini))
        z.writestr('MoviesMod-README.txt', windows_lines(README.format(version=version)))
    print('%s: %d files, %.1f MB' % (out, len(files) + 2, os.path.getsize(out) / 1e6))


if __name__ == '__main__':
    main()
