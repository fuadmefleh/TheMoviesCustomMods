"""Draws the mod's HUD icons in the style of the game's data/textures/ui pictures (64x64:
a pale silhouette with a dark teal outline, details cut in the outline colour).

  python job_icons.py [--install] [--preview out.png]

job_acct.dds     the accountants' page of the people card (src/careers.cpp): a calculator
shares_bag.dds   the studio's shares on the right-hand list (src/projectlist.cpp): a money bag
shares_case.dds  the investors' shares there: a briefcase
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import building  # noqa: E402

WHITE = (250, 252, 252, 255)
CARD = (222, 230, 230, 255)   # the project cards' pale grey
OUTLINE = (38, 74, 80, 255)
S = 256   # drawn large, then scaled to 64


def silhouette():
    """The white shape and the parts cut out of it (drawn in the outline colour)."""
    shape = Image.new('L', (S, S), 0)
    d = ImageDraw.Draw(shape)
    d.rounded_rectangle([58, 28, 198, 228], radius=22, fill=255)     # calculator body
    d.ellipse([150, 150, 236, 236], fill=255)                          # a coin in front of it
    cut = Image.new('L', (S, S), 0)
    c = ImageDraw.Draw(cut)
    c.rounded_rectangle([78, 48, 178, 92], radius=8, fill=255)        # display
    for row in range(3):
        for col in range(3):
            x, y = 80 + col * 34, 108 + row * 34
            if not (row == 2 and col == 2):
                c.rounded_rectangle([x, y, x + 24, y + 24], radius=5, fill=255)
    c.ellipse([150, 150, 236, 236], fill=0)                            # the coin covers the keys
    c.ellipse([162, 162, 224, 224], width=7, outline=255)              # coin rim
    dollar(c, 193, 193, 64)   # on the coin
    return shape, cut


def font(size):
    return ImageFont.truetype(os.path.join(os.environ.get('WINDIR', r'C:\Windows'), 'Fonts', 'ARLRDBD.TTF'), size)


def dollar(c, x, y, size):
    f = font(size)
    box = c.textbbox((0, 0), '$', font=f)
    c.text((x - (box[0] + box[2]) / 2, y - (box[1] + box[3]) / 2), '$', font=f, fill=255)


def bag():
    shape = Image.new('L', (S, S), 0)
    d = ImageDraw.Draw(shape)
    d.ellipse([40, 84, 216, 236], fill=255)                       # the sack
    d.polygon([(96, 96), (160, 96), (150, 60), (106, 60)], fill=255)   # its neck
    d.polygon([(88, 30), (128, 52), (168, 30), (176, 58), (80, 58)], fill=255)   # the tied top
    cut = Image.new('L', (S, S), 0)
    c = ImageDraw.Draw(cut)
    c.line([(98, 70), (158, 70)], fill=255, width=9)               # the tie
    dollar(c, 128, 166, 96)
    return shape, cut


def case():
    shape = Image.new('L', (S, S), 0)
    d = ImageDraw.Draw(shape)
    d.rounded_rectangle([92, 40, 164, 84], radius=14, fill=255)    # handle
    d.rounded_rectangle([28, 70, 228, 216], radius=18, fill=255)   # the case
    cut = Image.new('L', (S, S), 0)
    c = ImageDraw.Draw(cut)
    c.rounded_rectangle([110, 54, 146, 70], radius=6, fill=255)    # the handle's hole
    c.line([(28, 118), (228, 118)], fill=255, width=7)             # the lid's edge
    c.rectangle([112, 108, 144, 132], fill=255)                    # the clasp
    # A share certificate sticking out of the lid.
    c.polygon([(60, 100), (100, 86), (104, 112), (64, 118)], fill=255)
    dollar(c, 128, 172, 70)
    return shape, cut


def render(parts, fill, card=False):
    shape, cut = parts
    outline = shape.filter(ImageFilter.MaxFilter(23))
    img = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    img.paste(OUTLINE, mask=outline)
    img.paste(fill, mask=shape)
    img.paste(OUTLINE, mask=cut)
    return fit_card(img) if card else img.resize((64, 64), Image.LANCZOS)


CARD_BOX = 52      # the game's project-card pictures (ui/filmpro_*.dds) fill about 54x56 of 64x64
CARD_CENTRE = (32, 31)


def fit_card(img):
    """Centres the drawing on the project card's picture spot: cropped to its outline and
    scaled to CARD_BOX, so nothing touches the edges (the card's star bar covers the bottom)."""
    art = img.crop(img.getchannel('A').getbbox())
    k = CARD_BOX * S / 64 / max(art.size)
    art = art.resize((round(art.width * k), round(art.height * k)), Image.LANCZOS)
    big = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    c = (CARD_CENTRE[0] * S // 64, CARD_CENTRE[1] * S // 64)
    big.alpha_composite(art, (c[0] - art.width // 2, c[1] - art.height // 2))
    return big.resize((64, 64), Image.LANCZOS)


def draw():
    return {'job_acct': render(silhouette(), WHITE), 'shares_bag': render(bag(), CARD, card=True),
            'shares_case': render(case(), CARD, card=True)}


def main():
    icons = draw()
    out = os.path.join(building.CONTENT, 'data', 'textures', 'ui')
    os.makedirs(out, exist_ok=True)
    dst = os.path.join(building.GAME, 'Data', 'textures', 'ui')
    for name, img in icons.items():
        building.write_dds(img, os.path.join(out, name + '.dds'))
        if '--install' in sys.argv:
            os.makedirs(dst, exist_ok=True)
            building.write_dds(img, os.path.join(dst, name + '.dds'))
    print('wrote', ', '.join(icons), 'to', out, '(and the game)' if '--install' in sys.argv else '')
    if '--preview' in sys.argv:
        bg = Image.new('RGBA', (64 * len(icons), 64), (60, 60, 60, 255))
        for i, img in enumerate(icons.values()):
            bg.alpha_composite(img, (64 * i, 0))
        bg.resize((bg.width * 3, 192), Image.NEAREST).save(sys.argv[sys.argv.index('--preview') + 1])


if __name__ == '__main__':
    main()
