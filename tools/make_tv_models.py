"""Builds all the TV buildings' models. Each takes its working parts (drop zones, walk points,
doors) from a game building, with the zones renamed as TV zones; the TV buildings get a
new body of CC0 kit models ('kit', tools/blender_kit.py) and are dressed in Blender
with new parts (tools/blender_props.py): ON AIR signs, transmitter towers, satellite
dishes, rooftop units. The sign textures are drawn here.

Usage: python make_tv_models.py --blender <blender.exe> [--install] [name ...]
       (or set BLENDER)
"""
import json
import math
import os
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import building  # noqa: E402

TOOLS = os.path.dirname(os.path.abspath(__file__))
# New bodies for the TV buildings: CC0 kit models (third_party/kenney, see tools/blender_kit.py).
KIT_ROOT = os.path.join(os.path.dirname(TOOLS), 'third_party', 'kenney')
# The game textures each role of a kit body gets (tools/blender_kit.py); 'style' per building
# overrides them.
KIT_STYLE = {'wall': 'wall_ph54', 'trim': 'wall_ph55c', 'roof': 'wall_ph55', 'dark': 'plas_ph01',
             'glass': 'glas_ph08', 'panel': 'steel_ph09', 'frame': 'steel_ph17', 'accent': 'wall_ph47b'}
FONT = os.path.join(os.environ.get('WINDIR', r'C:\Windows'), 'Fonts', 'ARLRDBD.TTF')

BUILDINGS = [
    {
        'name': 'tv_script_office', 'style': {'wall': 'wall_ph05', 'trim': 'wall_ph47b'},
        
        'kit': [{'kit': 'city-kit-commercial', 'model': 'building-j', 'x': 9.5, 'y': -1.5, 'w': 24.0, 'd': 14.0, 'h': 16.0},
                {'kit': 'city-kit-commercial', 'model': 'building-c', 'x': 1.0, 'y': -12.5, 'w': 9.0, 'd': 9.0, 'h': 9.0}],
        'donor': 'scriptoffice_3',
        'rooms': {'room_so_comedy': 'room_tvw_sitcom', 'room_so_action': 'room_tvw_crime',
                  'room_so_romdrama': 'room_tvw_drama', 'room_so_horror': 'room_tvw_soap',
                  'room_so_scifi': 'room_tvw_scifi', 'room_so_fire': 'room_tvw_variety',
                  'room_so_office': 'room_tvw_office', 'room_so_hall': 'room_tvw_hall'},
        # The writers' sit-and-type scenes, so TV writers work at the desks too.
        'scenes': {'so_comedy': 'tvw_sitcom', 'so_action': 'tvw_crime', 'so_romdrama': 'tvw_drama',
                   'so_horror': 'tvw_soap', 'so_scifi': 'tvw_scifi', 'so_stunt': 'tvw_variety', 'so_hall': 'tvw_hall'},
        # The hall (office zone) is where the writers actually sit and type.
        'extra_scenes': {'so_hall': 'tvw_office'},
        'props': [
            {'type': 'sign', 'x': 4.0, 'y': -7.2, 'w': 5.4, 'h': 1.4, 'legs': 0.5, 'texture': 'tvp_writers.png'},
            {'type': 'mast', 'x': 17.0, 'y': -2.0, 'height': 6.0},
        ],
    },
    {
        'name': 'tv_office', 'style': {'wall': 'brick_ph01', 'trim': 'wall_ph40'},
        
        'kit': [{'kit': 'city-kit-commercial', 'model': 'building-k', 'x': 0.0, 'y': 0.0, 'w': 26.0, 'd': 12.0, 'h': 15.0}],
        'donor': 'customscriptoffice',
        'rooms': {'room_cso_create': 'room_tv_writers', 'room_cso_canit': 'room_tv_cast',
                  'room_cso_blank': 'room_tv_showrun', 'room_cso_hall': 'room_tv_lobby'},
        'props': [
            {'type': 'sign', 'x': 9.5, 'y': 1.2, 'w': 4.0, 'h': 1.3, 'legs': 0.6, 'texture': 'tvp_onair.png'},
            {'type': 'mast', 'x': 12.0, 'y': 3.4, 'height': 6.0},
        ],
    },
    {
        'name': 'tv_soundstage', 'style': {'panel': 'steel_ph09', 'frame': 'wall_ph55c'},
        
        'kit': [{'kit': 'space-kit', 'model': 'hangar_largeB', 'x': 0.0, 'y': -0.3, 'w': 26.0, 'd': 13.0, 'h': 11.0, 'rot': 90}],
        'donor': 'stageschool',
        'rooms': {'room_ss_extras': 'room_tvs_audience', 'room_ss_actors': 'room_tvs_stage',
                  'room_ss_directors': 'room_tvs_control', 'room_ss_fire': 'room_tvs_green',
                  'room_ss_starmaker': 'room_tvs_makeup'},
        'props': [
            {'type': 'sign', 'x': 0.0, 'y': -1.0, 'w': 6.0, 'h': 1.8, 'legs': 0.5, 'texture': 'tvp_onair.png'},
            {'type': 'sign', 'x': -8.5, 'y': -3.6, 'w': 4.6, 'h': 1.2, 'legs': 0.3, 'texture': 'tvp_stage.png'},
            {'type': 'spot', 'x': -11.0, 'y': -1.2},
            {'type': 'spot', 'x': 11.0, 'y': -1.2},
            {'type': 'spot', 'x': 5.5, 'y': -1.2},
        ],
    },
    {
        'name': 'tv_network_hq', 'style': {'wall': 'wall_ph40', 'trim': 'wall_ph55'},
        
        'kit': [{'kit': 'city-kit-commercial', 'model': 'building-skyscraper-a', 'x': 2.2, 'y': 3.0, 'w': 13.0, 'd': 11.0, 'h': 30.0},
                {'kit': 'city-kit-commercial', 'model': 'building-e', 'x': 2.2, 'y': -5.0, 'w': 18.0, 'd': 7.5, 'h': 7.0},
                {'kit': 'space-kit', 'model': 'satelliteDish_large', 'x': 0.0, 'y': 5.0, 'scale': 3.0, 'z': 'roof', 'solid': False},
                {'kit': 'space-kit', 'model': 'machine_wireless', 'x': 5.0, 'y': 1.0, 'scale': 3.0, 'z': 'roof', 'solid': False}],
        'donor': 'publicity_office',
        'rooms': {'room_publicity_pr': 'room_tvn_programming', 'room_publicity_hall': 'room_tvn_lobby'},
        'props': [
            {'type': 'tower', 'x': 5.5, 'y': 6.0, 'height': 9.0, 'base': 2.0},
            {'type': 'sign', 'x': 2.2, 'y': -6.5, 'w': 6.0, 'h': 2.2, 'legs': 0.6, 'texture': 'tvp_network.png'},
        ],
    },
    {
        'name': 'tv_control_room', 'style': {'wall': 'brick_ph02', 'trim': 'wall_ph55c'},
        
        'kit': [{'kit': 'city-kit-commercial', 'model': 'building-k', 'x': -1.0, 'y': -1.5, 'w': 22.0, 'd': 12.0, 'h': 13.0},
                {'kit': 'space-kit', 'model': 'satelliteDish_large', 'x': 15.0, 'y': -4.5, 'scale': 7.0},
                {'kit': 'space-kit', 'model': 'satelliteDish_detailed', 'x': 15.5, 'y': 4.0, 'scale': 6.0},
                {'kit': 'space-kit', 'model': 'structure_closed', 'x': -9.0, 'y': 6.5, 'scale': 3.5},
                {'kit': 'space-kit', 'model': 'machine_wireless', 'x': -6.0, 'y': -1.0, 'scale': 3.0, 'z': 'roof', 'solid': False}],
        'donor': 'research',
        'rooms': {'room_res_fac': 'room_tvc_transmit', 'room_res_fire': 'room_tvc_engineers',
                  'room_res_set': 'room_tvc_monitors', 'room_res_cos': 'room_tvc_tapes',
                  'room_res_mov': 'room_tvc_uplink', 'room_res_hall': 'room_tvc_hall'},
        'props': [
            {'type': 'mast', 'x': 6.0, 'y': 1.0, 'height': 9.0},
            {'type': 'sign', 'x': -3.0, 'y': -6.6, 'w': 5.0, 'h': 1.2, 'legs': 0.4, 'texture': 'tvp_control.png'},
        ],
    },
    {
        'name': 'tv_archive', 'style': {'wall': 'wall_ph48', 'trim': 'brick_ph03'},
        
        'kit': [{'kit': 'city-kit-commercial', 'model': 'building-e', 'x': -1.8, 'y': 1.0, 'w': 20.0, 'd': 14.0, 'h': 9.0}],
        'donor': 'postproduction',
        'rooms': {'room_postprod_postprod': 'room_tva_vault', 'room_postprod_hall': 'room_tva_hall'},
        'props': [
            {'type': 'sign', 'x': -1.4, 'y': -8.6, 'w': 5.2, 'h': 1.4, 'legs': 0.4, 'texture': 'tvp_archive.png'},
            {'type': 'sign', 'x': 6.0, 'y': 7.0, 'w': 2.6, 'h': 2.6, 'legs': 0.6, 'texture': 'tvp_reel.png'},
        ],
    },
    {
        'name': 'tv_datacenter', 'style': {'panel': 'steel_ph24b', 'frame': 'steel_ph17'},
        
        'kit': [{'kit': 'space-kit', 'model': 'hangar_smallA', 'x': 0.0, 'y': -1.0, 'w': 20.0, 'd': 12.0, 'h': 8.0},
                {'kit': 'space-kit', 'model': 'machine_generatorLarge', 'x': 9.0, 'y': 4.5, 'scale': 3.0},
                {'kit': 'space-kit', 'model': 'machine_generator', 'x': -5.0, 'y': -4.0, 'scale': 3.0, 'z': 'roof', 'solid': False},
                {'kit': 'space-kit', 'model': 'machine_generator', 'x': 0.0, 'y': -4.0, 'scale': 3.0, 'z': 'roof', 'solid': False},
                {'kit': 'space-kit', 'model': 'machine_generator', 'x': 5.0, 'y': -4.0, 'scale': 3.0, 'z': 'roof', 'solid': False},
                {'kit': 'space-kit', 'model': 'machine_generator', 'x': -5.0, 'y': 2.0, 'scale': 3.0, 'z': 'roof', 'solid': False},
                {'kit': 'space-kit', 'model': 'machine_generator', 'x': 0.0, 'y': 2.0, 'scale': 3.0, 'z': 'roof', 'solid': False},
                {'kit': 'space-kit', 'model': 'machine_generator', 'x': 5.0, 'y': 2.0, 'scale': 3.0, 'z': 'roof', 'solid': False}],
        'donor': 'sell',
        'rooms': {'room_sell_sell': 'room_tvd_servers', 'room_sell_window': 'room_tvd_noc', 'room_hall': 'room_tvd_hall'},
        'props': [
            {'type': 'dish', 'x': -9.0, 'y': 5.0, 'radius': 1.4, 'tilt': 45, 'face': 180},
            {'type': 'sign', 'x': 0.0, 'y': -6.0, 'w': 4.2, 'h': 1.0, 'legs': 0.4, 'texture': 'tvp_datacenter.png'},
        ],
    },
]


# Luxury buildings revived from cut content: the Script Office: Ultimate and trailers 6-9.
# They keep their donor's zones; 'tier' draws a badge on the menu picture.
def _trailer(n, props, tier):
    return {'name': 'trailer%d' % n, 'donor': 'trailer5', 'rooms': {}, 'props': props, 'badge': tier}


_DISH = {'type': 'dish', 'x': -0.5, 'y': 6.0, 'radius': 0.9, 'tilt': 50, 'face': 200}
_STAR = {'type': 'sign', 'x': -0.5, 'y': -4.5, 'w': 3.6, 'h': 1.1, 'legs': 0.3, 'face': 270}
_TUB = {'type': 'box', 'x': -0.5, 'y': 1.5, 'w': 3.0, 'd': 3.0, 'h': 0.5, 'texture': 'lux_tub.png', 'min_roof': 1.0}
_PAD = {'type': 'box', 'x': -0.5, 'y': -1.6, 'w': 4.2, 'd': 4.2, 'h': 0.08, 'texture': 'lux_helipad.png', 'min_roof': 1.0}

LUXURY = [
    {
        'name': 'scriptoffice_5', 'donor': 'scriptoffice_4', 'rooms': {}, 'badge': '5',
        'props': [
            {'type': 'sign', 'x': 8.0, 'y': -5.0, 'w': 6.4, 'h': 1.6, 'legs': 0.6, 'texture': 'lux_ultimate.png'},
            {'type': 'spot', 'x': 3.0, 'y': -5.5, 'face': 20},
            {'type': 'spot', 'x': 13.0, 'y': -5.5, 'face': -20},
        ],
    },
    _trailer(6, [_DISH, {'type': 'box', 'x': -0.5, 'y': -3.0, 'w': 1.4, 'd': 1.4, 'h': 0.8, 'texture': 'tvp_ac.png',
                         'min_roof': 1.0}], '6'),
    _trailer(7, [_DISH, dict(_STAR, texture='lux_executive.png')], '7'),
    _trailer(8, [_DISH, _TUB, dict(_STAR, y=-5.2, texture='lux_superstar.png')], '8'),
    _trailer(9, [_TUB, _PAD, dict(_STAR, y=5.6, texture='lux_legendary.png')], '9'),
]


# ---------------------------------------------------------------------------------------
# Textures

def font(size):
    try:
        return ImageFont.truetype(FONT, size)
    except OSError:
        return ImageFont.load_default()


def centred_text(img, text, size, fill, glow=None, y=None):
    d = ImageDraw.Draw(img)
    f = font(size)
    box = d.textbbox((0, 0), text, font=f)
    w, h = box[2] - box[0], box[3] - box[1]
    pos = ((img.width - w) // 2 - box[0], (img.height - h) // 2 - box[1] if y is None else y)
    if glow:
        layer = Image.new('RGBA', img.size, (0, 0, 0, 0))
        ImageDraw.Draw(layer).text(pos, text, font=f, fill=glow)
        img.alpha_composite(layer.filter(ImageFilter.GaussianBlur(size / 8)))
    d.text(pos, text, font=f, fill=fill)


def sign(w, h, bg_top, bg_bottom, border):
    img = Image.new('RGBA', (w, h))
    d = ImageDraw.Draw(img)
    for yy in range(h):
        t = yy / max(1, h - 1)
        d.line([(0, yy), (w, yy)], fill=tuple(int(a + (b - a) * t) for a, b in zip(bg_top, bg_bottom)) + (255,))
    d.rounded_rectangle([6, 6, w - 7, h - 7], radius=min(w, h) // 8, outline=border, width=6)
    return img


def make_textures(out):
    os.makedirs(out, exist_ok=True)
    img = sign(512, 160, (220, 30, 35), (130, 10, 15), (255, 235, 235))
    centred_text(img, 'ON AIR', 100, (255, 255, 255), glow=(255, 200, 200, 255))
    img.save(os.path.join(out, 'tvp_onair.png'))

    img = sign(512, 128, (120, 15, 20), (70, 5, 10), (250, 210, 60))
    d = ImageDraw.Draw(img)
    for x in range(24, 512, 32):
        for y in (16, 112):
            d.ellipse([x - 6, y - 6, x + 6, y + 6], fill=(255, 230, 90))
    centred_text(img, 'STAGE 1', 64, (255, 240, 200), glow=(255, 200, 80, 255))
    img.save(os.path.join(out, 'tvp_stage.png'))

    img = sign(512, 224, (40, 110, 210), (12, 40, 110), (255, 255, 255))
    d = ImageDraw.Draw(img)
    cx, cy = 110, 112
    for i in range(16):
        a0, a1 = i * math.pi / 8, i * math.pi / 8 + math.pi / 16
        d.polygon([(cx, cy), (cx + 400 * math.cos(a0), cy + 400 * math.sin(a0)),
                   (cx + 400 * math.cos(a1), cy + 400 * math.sin(a1))], fill=(255, 255, 255, 28))
    d.rounded_rectangle([50, 70, 170, 170], radius=18, fill=(60, 60, 70), outline=(255, 255, 255), width=5)
    d.rounded_rectangle([64, 84, 140, 156], radius=14, fill=(150, 210, 255))
    d.line([(95, 70), (75, 35)], fill=(255, 255, 255), width=5)
    d.line([(115, 70), (140, 32)], fill=(255, 255, 255), width=5)
    d.ellipse([150, 100, 162, 112], fill=(250, 210, 60))
    layer = Image.new('RGBA', img.size, (0, 0, 0, 0))
    f = font(60)
    ImageDraw.Draw(layer).text((200, 60), 'NETWORK', font=f, fill=(255, 255, 255))
    ImageDraw.Draw(layer).text((205, 125), 'HEADQUARTERS', font=font(32), fill=(250, 220, 80))
    img.alpha_composite(layer)
    img.save(os.path.join(out, 'tvp_network.png'))

    img = sign(512, 128, (20, 70, 45), (8, 35, 22), (220, 180, 70))
    d = ImageDraw.Draw(img)
    for x in range(14, 512, 26):
        d.rectangle([x, 12, x + 12, 22], fill=(220, 180, 70))
        d.rectangle([x, 106, x + 12, 116], fill=(220, 180, 70))
    centred_text(img, 'MEDIA ARCHIVE', 54, (240, 210, 110))
    img.save(os.path.join(out, 'tvp_archive.png'))

    img = sign(256, 256, (30, 30, 40), (10, 10, 20), (220, 180, 70))
    d = ImageDraw.Draw(img)
    d.ellipse([28, 28, 228, 228], fill=(190, 195, 205), outline=(255, 255, 255), width=6)
    for i in range(5):
        a = i * 2 * math.pi / 5
        px, py = 128 + 58 * math.cos(a), 128 + 58 * math.sin(a)
        d.ellipse([px - 26, py - 26, px + 26, py + 26], fill=(30, 30, 40))
    d.ellipse([112, 112, 144, 144], fill=(30, 30, 40))
    img.save(os.path.join(out, 'tvp_reel.png'))

    img = sign(512, 128, (30, 60, 80), (10, 25, 40), (90, 220, 255))
    centred_text(img, 'BROADCAST CONTROL', 46, (200, 245, 255), glow=(90, 220, 255, 255))
    img.save(os.path.join(out, 'tvp_control.png'))

    img = sign(512, 128, (25, 35, 60), (5, 10, 25), (60, 255, 140))
    centred_text(img, 'DATA CENTER', 56, (170, 255, 200), glow=(60, 255, 140, 255))
    img.save(os.path.join(out, 'tvp_datacenter.png'))

    img = sign(512, 136, (40, 20, 70), (15, 5, 35), (255, 120, 220))
    centred_text(img, 'TV WRITERS', 64, (255, 200, 245), glow=(255, 90, 210, 255))
    img.save(os.path.join(out, 'tvp_writers.png'))

    img = Image.new('RGBA', (64, 256), (240, 240, 240, 255))
    d = ImageDraw.Draw(img)
    for y in range(0, 256, 64):
        d.rectangle([0, y, 64, y + 31], fill=(200, 25, 30))
    img.save(os.path.join(out, 'tvp_stripes.png'))

    img = Image.new('RGBA', (64, 64), (175, 180, 188, 255))
    d = ImageDraw.Draw(img)
    for y in range(0, 64, 4):
        d.line([(0, y), (64, y)], fill=(160, 165, 172))
    img.save(os.path.join(out, 'tvp_metal.png'))

    Image.new('RGBA', (32, 32), (255, 40, 30, 255)).save(os.path.join(out, 'tvp_beacon.png'))

    img = Image.new('RGBA', (64, 64), (235, 235, 238, 255))
    ImageDraw.Draw(img).ellipse([8, 8, 56, 56], outline=(205, 205, 212), width=3)
    img.save(os.path.join(out, 'tvp_dish.png'))

    img = Image.new('RGBA', (128, 128), (150, 155, 160, 255))
    d = ImageDraw.Draw(img)
    d.rectangle([4, 4, 123, 123], outline=(110, 115, 120), width=4)
    d.ellipse([24, 24, 104, 104], fill=(60, 64, 70), outline=(200, 200, 205), width=4)
    for i in range(4):
        a = i * math.pi / 2 + 0.4
        d.line([(64, 64), (64 + 34 * math.cos(a), 64 + 34 * math.sin(a))], fill=(170, 175, 180), width=8)
    img.save(os.path.join(out, 'tvp_ac.png'))

    img = Image.new('RGBA', (64, 64), (40, 40, 45, 255))
    ImageDraw.Draw(img).ellipse([12, 12, 52, 52], fill=(255, 250, 210), outline=(150, 150, 150), width=4)
    img.save(os.path.join(out, 'tvp_lamp.png'))

    # Luxury buildings.
    img = sign(512, 128, (40, 30, 10), (10, 8, 2), (255, 215, 90))
    centred_text(img, 'ULTIMATE SCRIPTS', 50, (255, 225, 120), glow=(255, 190, 60, 255))
    img.save(os.path.join(out, 'lux_ultimate.png'))
    for name, label, top, bottom in (('executive', 'EXECUTIVE', (60, 20, 30), (25, 5, 10)),
                                     ('superstar', 'SUPERSTAR', (30, 20, 70), (10, 5, 30)),
                                     ('legendary', 'LEGENDARY', (20, 20, 20), (0, 0, 0))):
        img = sign(512, 156, top, bottom, (255, 215, 90))
        d = ImageDraw.Draw(img)
        for cx in (52, 460):
            pts = [(cx + 34 * math.cos(-math.pi / 2 + k * math.pi / 5) * (1 if k % 2 == 0 else 0.45),
                    78 + 34 * math.sin(-math.pi / 2 + k * math.pi / 5) * (1 if k % 2 == 0 else 0.45)) for k in range(10)]
            d.polygon(pts, fill=(255, 215, 90))
        centred_text(img, label, 62, (255, 230, 150), glow=(255, 190, 60, 255))
        img.save(os.path.join(out, 'lux_%s.png' % name))

    img = Image.new('RGBA', (128, 128), (60, 170, 220, 255))
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, 127, 127], outline=(235, 235, 240), width=12)
    for y in range(20, 110, 14):
        d.arc([10, y - 8, 118, y + 8], 200, 340, fill=(170, 230, 255), width=2)
    img.save(os.path.join(out, 'lux_tub.png'))

    img = Image.new('RGBA', (128, 128), (70, 72, 76, 255))
    d = ImageDraw.Draw(img)
    d.ellipse([10, 10, 118, 118], outline=(250, 210, 40), width=6)
    d.rectangle([40, 32, 52, 96], fill=(245, 245, 245))
    d.rectangle([76, 32, 88, 96], fill=(245, 245, 245))
    d.rectangle([52, 58, 76, 70], fill=(245, 245, 245))
    img.save(os.path.join(out, 'lux_helipad.png'))


def badge_thumbnail(donor, label, path):
    """The donor's menu picture with a gold rosette carrying the tier."""
    import io
    data = building.game_file('data/textures/thumbs/sets/fac_%s.dds' % donor)
    try:
        img = Image.open(io.BytesIO(data)).convert('RGBA').resize((128, 128)) if data else None
    except Exception:
        img = None
    if img is None:
        img = Image.new('RGBA', (128, 128), (40, 60, 120, 255))
    d = ImageDraw.Draw(img)
    d.ellipse([78, 78, 124, 124], fill=(250, 205, 60), outline=(120, 80, 10), width=3)
    f = font(32)
    w = d.textlength(label, font=f)
    d.text((101 - w / 2, 82), label, font=f, fill=(90, 50, 0))
    img.save(path)


def copy_scenes(scenes, install):
    """The donor's AI scenes (people working in a zone) under the new zones' names."""
    out = os.path.join(building.CONTENT, 'data', 'scene', 'interactions')
    os.makedirs(out, exist_ok=True)
    copied = 0
    for old, new in scenes.items():
        for n in range(10):
            data = building.game_file('data/scene/interactions/ai_room_%s_%02d.flm' % (old, n))
            if data is None:
                continue
            name = 'ai_room_%s_%02d.flm' % (new, n)
            open(os.path.join(out, name), 'wb').write(data)
            if install:
                dst = os.path.join(building.GAME, 'Data', 'scene', 'interactions')
                os.makedirs(dst, exist_ok=True)
                open(os.path.join(dst, name), 'wb').write(data)
            copied += 1
    if scenes:
        print('  %d zone scenes copied' % copied)


def find_blender():
    if '--blender' in sys.argv:
        return sys.argv[sys.argv.index('--blender') + 1]
    if os.environ.get('BLENDER'):
        return os.environ['BLENDER']
    root = r'C:\Program Files\Blender Foundation'
    if os.path.isdir(root):
        for d in sorted(os.listdir(root), reverse=True):
            exe = os.path.join(root, d, 'blender.exe')
            if os.path.exists(exe):
                return exe
    sys.exit('Blender not found: pass --blender <path to blender.exe> or set BLENDER')


def run_blender(blender, script, work, spec_data, name):
    spec = os.path.join(work, 'kit.json')
    json.dump(spec_data, open(spec, 'w'), indent=1)
    r = subprocess.run([blender, '-b', '--factory-startup', '-P', os.path.join(TOOLS, script), '--', work, spec],
                       capture_output=True, text=True)
    lines = [l for l in r.stdout.splitlines() if 'blender_kit' in l or 'Error' in l or 'Traceback' in l]
    print('\n'.join(lines) or r.stdout[-2000:])
    if r.returncode != 0 or not any('blender_kit: removed' in l for l in lines):
        print(r.stderr[-3000:])
        sys.exit('Blender failed for ' + name)


def main():
    blender = find_blender()
    install = '--install' in sys.argv
    skip = {sys.argv[sys.argv.index('--blender') + 1]} if '--blender' in sys.argv else set()
    only = [a for a in sys.argv[1:] if not a.startswith('--') and a not in skip]
    for b in BUILDINGS + LUXURY:
        if only and b['name'] not in only:
            continue
        work = os.path.join(tempfile.gettempdir(), 'moviesmod_models', b['name'])
        building.export(b['donor'], work)
        make_textures(os.path.join(work, 'textures'))
        if b.get('badge'):
            badge_thumbnail(b['donor'], b['badge'], os.path.join(work, 'thumbnail.png'))
        if b.get('kit'):
            style = dict(KIT_STYLE, **b.get('style', {}))
            for tex in style.values():   # the game textures the new body is dressed in
                dst = os.path.join(work, 'textures', tex + '.dds')
                if not os.path.exists(dst):
                    data = building.game_file('data/textures/%s.dds' % tex)
                    if data is None:
                        sys.exit('no game texture ' + tex)
                    open(dst, 'wb').write(data)
            run_blender(blender, 'blender_kit.py', work, {'rooms': b['rooms'], 'kit_root': KIT_ROOT, 'parts': b['kit'],
                                                          'style': style}, b['name'])
        spec = os.path.join(work, 'spec.json')
        json.dump({'rooms': b['rooms'], 'props': b['props']}, open(spec, 'w'), indent=1)
        r = subprocess.run([blender, '-b', '--factory-startup', '-P', os.path.join(TOOLS, 'blender_props.py'), '--', work, spec],
                           capture_output=True, text=True)
        lines = [l for l in r.stdout.splitlines() if 'blender_props' in l or 'Error' in l or 'Traceback' in l]
        print('\n'.join(lines) or r.stdout[-2000:])
        if r.returncode != 0 or not any('blender_props:' in l for l in lines):
            print(r.stderr[-3000:])
            sys.exit('Blender failed for ' + b['name'])
        if b.get('kit'):
            # The menu picture of the new building (replaces the donor's).
            r = subprocess.run([blender, '-b', '--factory-startup', '-P', os.path.join(TOOLS, 'blender_kit.py'), '--', work,
                                '--thumb', os.path.join(work, 'thumbnail.png')], capture_output=True, text=True)
            if r.returncode != 0 or not os.path.exists(os.path.join(work, 'thumbnail.png')):
                print(r.stdout[-2000:], r.stderr[-2000:])
                sys.exit('thumbnail failed for ' + b['name'])
        building.build(work, b['name'], install=install)
        copy_scenes(b.get('scenes', {}), install)
        copy_scenes(b.get('extra_scenes', {}), install)


if __name__ == '__main__':
    main()
