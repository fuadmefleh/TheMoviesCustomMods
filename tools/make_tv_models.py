"""Builds all the TV buildings' models. Each takes its working parts (drop zones, walk points,
doors) from a game building, with the zones renamed as TV zones. The TV buildings are
modelled from scratch in the base game's style ('blocks', tools/blender_build.py), dressed
with new parts (tools/blender_props.py: ON AIR signs, transmitter towers, satellite dishes,
rooftop units; the sign textures are drawn here) and lit with a baked lightmap like the
game's buildings (tools/blender_bake.py).

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
# The game textures a from-scratch building is dressed in (tools/blender_build.py roles);
# 'style' per building and 'roles' per block override them.
BUILD_STYLE = {'wall': 'wall_ph54', 'base': 'wall_ph55', 'trim': 'wall_ph40', 'glass': 'glas_ph08',
               'frame': 'wall_ph40', 'door': 'door_ph02', 'roof': 'wall_ph55', 'tiles': 'roof_ph04'}
FONT = os.path.join(os.environ.get('WINDIR', r'C:\Windows'), 'Fonts', 'ARLRDBD.TTF')

BUILDINGS = [
    {
        'name': 'tv_script_office', 'donor': 'scriptoffice_3',
        'style': {'wall': 'wall_ph05', 'base': 'brick_ph03', 'door': 'door_ph10'},
        'blocks': [
            {'x': 9.5, 'y': -1.5, 'w': 22.0, 'd': 12.0, 'h': 10.5, 'floors': 3, 'roof': 'hip', 'pitch': 3.5,
             'doors': [{'side': 'f', 'w': 2.2}]},
            {'x': 1.5, 'y': -12.0, 'w': 9.0, 'd': 9.0, 'h': 4.0, 'floors': 1, 'roof': 'flat',
             'doors': [{'side': 'f', 'w': 1.6}], 'windows': {'spacing': 3.0}},
        ],
        'rooms': {'room_so_comedy': 'room_tvw_sitcom', 'room_so_action': 'room_tvw_crime',
                  'room_so_romdrama': 'room_tvw_drama', 'room_so_horror': 'room_tvw_soap',
                  'room_so_scifi': 'room_tvw_scifi', 'room_so_fire': 'room_tvw_variety',
                  'room_so_office': 'room_tvw_office', 'room_so_hall': 'room_tvw_hall'},
        # The writers' sit-and-type scenes, so TV writers work at the desks too.
        'scenes': {'so_comedy': 'tvw_sitcom', 'so_action': 'tvw_crime', 'so_romdrama': 'tvw_drama',
                   'so_horror': 'tvw_soap', 'so_scifi': 'tvw_scifi', 'so_stunt': 'tvw_variety', 'so_hall': 'tvw_hall'},
        # The hall (office zone) is where the writers actually sit and type.
        'extra_scenes': {'so_hall': 'tvw_office'},
        # The floor plan: six writers' rooms either side of a hall running from the door, the
        # writers' office in the annex.
        'zones': {'room_': [[-1.2, 4.2], [-1.2, -7.2], [-2.7, -7.2], [-2.7, -16.2], [5.7, -16.2], [5.7, -7.2],
                            [20.2, -7.2], [20.2, 4.2]],
                  'room_tvw_soap': [-1.2, 0.3, 5.8, 4.2], 'room_tvw_drama': [6.0, 0.3, 13.0, 4.2],
                  'room_tvw_scifi': [13.2, 0.3, 20.2, 4.2], 'room_tvw_variety': [-1.2, -7.2, 3.2, -3.3],
                  'room_tvw_crime': [3.4, -7.2, 7.8, -3.3], 'room_tvw_sitcom': [11.2, -7.2, 20.2, -3.3],
                  'room_tvw_hall': [[-1.2, -3.1], [8.0, -3.1], [8.0, -7.2], [11.0, -7.2], [11.0, -3.1], [20.2, -3.1],
                                    [20.2, 0.1], [-1.2, 0.1]],
                  'room_tvw_office': [-2.7, -16.2, 5.7, -7.8]},
        'props': [
            {'type': 'sign', 'x': 1.5, 'y': -13.5, 'w': 5.4, 'h': 1.4, 'legs': 0.5, 'texture': 'tvp_writers.png'},
            {'type': 'mast', 'x': 4.0, 'y': -9.5, 'height': 6.0},
        ],
    },
    {
        'name': 'tv_office', 'donor': 'customscriptoffice',
        'style': {'wall': 'brick_ph01', 'door': 'door_ph10'},
        'blocks': [
            {'x': 0.0, 'y': 0.0, 'w': 26.0, 'd': 12.0, 'h': 11.0, 'floors': 3, 'roof': 'flat',
             'doors': [{'side': 'f', 'w': 2.4, 'h': 2.7}]},
        ],
        'rooms': {'room_cso_create': 'room_tv_writers', 'room_cso_canit': 'room_tv_cast',
                  'room_cso_blank': 'room_tv_showrun', 'room_cso_hall': 'room_tv_lobby'},
        'zones': {'room_': [-12.7, -5.7, 12.7, 5.7], 'room_tv_lobby': [-4.0, -5.7, 4.0, 0.0],
                  'room_tv_showrun': [-12.7, -5.7, -4.2, 5.7], 'room_tv_writers': [-4.0, 0.2, 12.7, 5.7],
                  'room_tv_cast': [4.2, -5.7, 12.7, 0.0]},
        'props': [
            {'type': 'sign', 'x': 6.0, 'y': -2.0, 'w': 4.0, 'h': 1.3, 'legs': 0.6, 'texture': 'tvp_onair.png'},
            {'type': 'mast', 'x': -9.0, 'y': 3.0, 'height': 6.0},
        ],
    },
    {
        'name': 'tv_soundstage', 'donor': 'stageschool',
        'style': {'wall': 'wall_ph04', 'base': 'brick_ph03', 'tiles': 'steel_ph09', 'bigdoor': 'steel_ph16'},
        'blocks': [
            {'x': 0.0, 'y': -0.3, 'w': 26.0, 'd': 13.0, 'h': 10.0, 'floors': 1, 'roof': 'gable', 'pitch': 2.5,
             'bands': False, 'windows': {'w': 1.2, 'h': 0.8, 'spacing': 4.0, 'sill': 7.6},
             'doors': [{'side': 'f', 'at': -5.0, 'w': 6.0, 'h': 6.0, 'role': 'bigdoor', 'canopy': False},
                       {'side': 'f', 'at': 5.0, 'w': 1.2, 'h': 2.3}]},
        ],
        'rooms': {'room_ss_extras': 'room_tvs_audience', 'room_ss_actors': 'room_tvs_stage',
                  'room_ss_directors': 'room_tvs_control', 'room_ss_fire': 'room_tvs_green',
                  'room_ss_starmaker': 'room_tvs_makeup'},
        'zones': {'room_': [-12.7, -6.5, 12.7, 5.9], 'room_tvs_stage': [-12.7, 0.2, 2.0, 5.9],
                  'room_tvs_audience': [-12.7, -6.5, -1.0, 0.0], 'room_tvs_makeup': [2.2, 0.2, 7.4, 5.9],
                  'room_tvs_control': [7.6, 0.2, 12.7, 5.9], 'room_tvs_green': [6.6, -6.5, 12.7, 0.0]},
        'props': [
            {'type': 'sign', 'x': 0.0, 'y': -2.5, 'w': 6.0, 'h': 1.8, 'legs': 0.5, 'texture': 'tvp_onair.png'},
            {'type': 'sign', 'x': 8.0, 'y': -2.5, 'w': 4.6, 'h': 1.2, 'legs': 0.3, 'texture': 'tvp_stage.png'},
            {'type': 'spot', 'x': -11.0, 'y': -8.6},
            {'type': 'spot', 'x': 11.0, 'y': -8.6},
            {'type': 'spot', 'x': 1.5, 'y': -8.6},
        ],
    },
    {
        'name': 'tv_network_hq', 'donor': 'publicity_office',
        'style': {'wall': 'wall_ph40', 'trim': 'wall_ph55', 'frame': 'wall_ph55', 'door': 'glas_ph08'},
        'blocks': [
            {'x': 2.2, 'y': 3.0, 'w': 13.0, 'd': 10.0, 'h': 28.0, 'floors': 8, 'roof': 'flat',
             'windows': {'w': 1.6, 'h': 2.1, 'spacing': 2.2, 'sill': 0.7}},
            {'x': 2.2, 'y': -5.0, 'w': 18.0, 'd': 7.0, 'h': 6.5, 'floors': 2, 'roof': 'flat',
             'windows': {'w': 2.4, 'h': 2.0, 'spacing': 3.4, 'sill': 0.6},
             'doors': [{'side': 'f', 'w': 3.0, 'h': 2.6}]},
        ],
        'rooms': {'room_publicity_pr': 'room_tvn_programming', 'room_publicity_hall': 'room_tvn_lobby'},
        'zones': {'room_': [[-6.5, -8.2], [10.9, -8.2], [10.9, -1.8], [8.4, -1.8], [8.4, 7.7], [-4.0, 7.7],
                            [-4.0, -1.8], [-6.5, -1.8]],
                  'room_tvn_lobby': [-6.5, -8.2, 10.9, -1.8], 'room_tvn_programming': [-4.0, -1.6, 8.4, 7.7]},
        'props': [
            {'type': 'tower', 'x': 5.5, 'y': 5.5, 'height': 9.0, 'base': 2.0},
            {'type': 'dish', 'x': -1.5, 'y': 5.0, 'radius': 1.3, 'tilt': 45, 'face': 200},
            {'type': 'sign', 'x': 2.2, 'y': -6.5, 'w': 6.0, 'h': 2.2, 'legs': 0.6, 'texture': 'tvp_network.png'},
        ],
    },
    {
        'name': 'tv_control_room', 'donor': 'research',
        'style': {'wall': 'brick_ph02', 'door': 'door_ph10'},
        'blocks': [
            {'x': -1.0, 'y': -1.5, 'w': 22.0, 'd': 12.0, 'h': 10.0, 'floors': 3, 'roof': 'flat',
             'doors': [{'side': 'f', 'w': 2.2}]},
            {'x': -9.0, 'y': 6.5, 'w': 4.0, 'd': 4.0, 'h': 3.0, 'floors': 1, 'roof': 'flat', 'bands': False,
             'windows': {'w': 0}, 'roles': {'wall': 'wall_ph55'},
             'doors': [{'side': 'l', 'w': 1.2, 'h': 2.2, 'canopy': False}]},
        ],
        'rooms': {'room_res_fac': 'room_tvc_transmit', 'room_res_fire': 'room_tvc_engineers',
                  'room_res_set': 'room_tvc_monitors', 'room_res_cos': 'room_tvc_tapes',
                  'room_res_mov': 'room_tvc_uplink', 'room_res_hall': 'room_tvc_hall'},
        'zones': {'room_': [[-11.7, -7.2], [9.7, -7.2], [9.7, 4.2], [-7.3, 4.2], [-7.3, 8.2], [-10.7, 8.2],
                            [-10.7, 4.2], [-11.7, 4.2]],
                  'room_tvc_hall': [-4.5, -7.2, 2.5, -3.0], 'room_tvc_engineers': [-11.7, -7.2, -4.7, 4.2],
                  'room_tvc_tapes': [-4.5, -2.8, 2.5, 4.2], 'room_tvc_monitors': [2.7, 1.5, 9.7, 4.2],
                  'room_tvc_transmit': [2.7, -2.8, 9.7, 1.3], 'room_tvc_uplink': [2.7, -7.2, 9.7, -3.0]},
        'props': [
            {'type': 'dish', 'x': 15.0, 'y': -4.5, 'radius': 2.6, 'tilt': 40, 'face': 200},
            {'type': 'dish', 'x': 15.5, 'y': 4.0, 'radius': 2.0, 'tilt': 50, 'face': 160},
            {'type': 'mast', 'x': 6.0, 'y': 1.0, 'height': 9.0},
            {'type': 'sign', 'x': -3.0, 'y': -6.0, 'w': 5.0, 'h': 1.2, 'legs': 0.4, 'texture': 'tvp_control.png'},
        ],
    },
    {
        'name': 'tv_archive', 'donor': 'postproduction',
        'style': {'wall': 'wall_ph48', 'base': 'brick_ph03', 'door': 'door_ph02'},
        'blocks': [
            {'x': -1.8, 'y': 1.0, 'w': 18.0, 'd': 13.0, 'h': 7.0, 'floors': 2, 'roof': 'hip', 'pitch': 3.0,
             'windows': {'w': 1.0, 'h': 1.2, 'spacing': 3.6}, 'doors': [{'side': 'f', 'w': 1.8}]},
        ],
        'rooms': {'room_postprod_postprod': 'room_tva_vault', 'room_postprod_hall': 'room_tva_hall'},
        'zones': {'room_': [-10.5, -5.2, 6.9, 7.2], 'room_tva_hall': [-10.5, -5.2, 6.9, -1.2],
                  'room_tva_vault': [-10.5, -1.0, 6.9, 7.2]},
        'props': [
            {'type': 'sign', 'x': -1.4, 'y': -8.6, 'w': 5.2, 'h': 1.4, 'legs': 0.4, 'texture': 'tvp_archive.png'},
            {'type': 'sign', 'x': 5.0, 'y': 5.0, 'w': 2.6, 'h': 2.6, 'legs': 0.6, 'texture': 'tvp_reel.png'},
        ],
    },
    {
        'name': 'tv_datacenter', 'donor': 'sell',
        'style': {'wall': 'wall_ph55c', 'base': 'wall_ph55', 'trim': 'steel_ph17', 'frame': 'steel_ph17',
                  'door': 'steel_ph16'},
        'blocks': [
            {'x': 0.0, 'y': -1.0, 'w': 20.0, 'd': 12.0, 'h': 6.5, 'floors': 1, 'roof': 'flat', 'bands': False,
             'windows': {'w': 2.6, 'h': 0.7, 'spacing': 4.0, 'sill': 4.6},
             'doors': [{'side': 'f', 'w': 1.4, 'h': 2.4}]},
            {'x': 8.0, 'y': 6.3, 'w': 3.4, 'd': 2.6, 'h': 2.2, 'floors': 1, 'roof': 'flat', 'bands': False,
             'cornice': False, 'windows': {'w': 0}, 'roles': {'wall': 'steel_ph24b'}},
        ],
        'rooms': {'room_sell_sell': 'room_tvd_servers', 'room_sell_window': 'room_tvd_noc', 'room_hall': 'room_tvd_hall'},
        'zones': {'room_': [-9.7, -6.7, 9.7, 4.7], 'room_tvd_hall': [-3.0, -6.7, 3.0, -3.0],
                  'room_tvd_noc': [3.2, -6.7, 9.7, -3.0], 'room_tvd_servers': [-9.7, -2.8, 9.7, 4.7]},
        'props': [{'type': 'box', 'x': x, 'y': y, 'w': 1.6, 'd': 1.6, 'h': 1.0, 'texture': 'tvp_ac.png'}
                  for y in (-4.0, 2.0) for x in (-5.0, 0.0, 5.0)] + [
            {'type': 'dish', 'x': -8.0, 'y': 3.0, 'radius': 1.4, 'tilt': 45, 'face': 180},
            {'type': 'sign', 'x': 0.0, 'y': -6.0, 'w': 4.2, 'h': 1.0, 'legs': 0.4, 'texture': 'tvp_datacenter.png'},
        ],
    },
    {
        # The studio's accountants, and its shares. Jobseekers dropped on the hiring desk
        # (room_tvk_hire, a real hire room: src/rooms.cpp) become accountants; drop your bag of shares on the share
        # desk to sell, the investors' briefcase there to buy back, or in the boardroom to
        # pay them a dividend (src/tv_shares.cpp). A sober brick office with a stone entrance.
        'name': 'tv_accounting', 'donor': 'customscriptoffice',
        'style': {'wall': 'brick_ph02', 'base': 'wall_ph04', 'trim': 'wall_ph04', 'frame': 'wall_ph40',
                  'door': 'door_ph10'},
        'blocks': [
            {'x': 0.0, 'y': 0.5, 'w': 26.0, 'd': 12.0, 'h': 8.0, 'floors': 2, 'roof': 'flat',
             'windows': {'w': 1.2, 'h': 1.9, 'spacing': 2.4, 'sill': 0.8}},
            {'x': 0.0, 'y': -6.3, 'w': 6.0, 'd': 2.0, 'h': 5.0, 'floors': 1, 'roof': 'flat', 'bands': False,
             'windows': {'w': 0}, 'roles': {'wall': 'wall_ph04'},
             'doors': [{'side': 'f', 'w': 2.0, 'h': 2.8, 'canopy': False}]},
        ],
        'rooms': {'room_cso_create': 'room_tvk_books', 'room_cso_canit': 'room_tvk_shares',
                  'room_cso_blank': 'room_tvk_board', 'room_cso_hall': 'room_tvk_hire'},
        # The accountants sit at the desks and type: the script office hall's scene (src/rooms.cpp).
        # The hiring desk needs a scene too, or it has no places and takes nobody: the crew
        # facility's hiring desk's. Jobseekers wait out front (room_tvk_wait, src/tv_accounts.cpp),
        # standing about like extras waiting on the pre-production lot.
        'scenes': {'so_hall': 'tvk_books', 'cf_hire': 'tvk_hire'},
        # Jobseekers wait out front (room_tvk_wait, src/tv_accounts.cpp), standing about. A
        # scene's positions are in the building's own coordinates, so the waiting places are
        # made here on the forecourt rather than copied from another building.
        'zones': {'room_': [-12.7, -5.2, 12.7, 6.2], 'room_tvk_hire': [-4.0, -5.2, 4.0, 0.8],
                  'room_tvk_board': [-12.7, -5.2, -4.2, 6.2], 'room_tvk_books': [-4.0, 1.0, 12.7, 6.2],
                  'room_tvk_shares': [4.2, -5.2, 12.7, 0.8], 'room_tvk_wait': [2.2, -10.8, 6.6, -8.4]},
        'stand_scenes': {'tvk_wait': [(3.0, -9.4), (4.4, -9.8), (5.8, -9.3), (3.6, -10.4), (5.1, -10.5)]},
        'props': [
            {'type': 'sign', 'x': 0.0, 'y': -7.35, 'w': 5.6, 'h': 0.8, 'z': 3.6, 'legs': 0, 'texture': 'tvp_accounts.png'},
            {'type': 'sign', 'x': -5.5, 'y': -8.6, 'w': 2.2, 'h': 1.5, 'legs': 0.8, 'texture': 'tvp_ledger.png'},
            {'type': 'box', 'x': -8.0, 'y': 3.0, 'w': 1.6, 'd': 1.6, 'h': 1.0, 'texture': 'tvp_ac.png', 'min_roof': 1.0},
        ],
    },
    {
        # Toon Studio: the Animation Studio (src/tv_animation.cpp). Drop a film's script in the
        # story room to animate it; animators (hired at room_tva_hire, a real hire room: src/rooms.cpp)
        # draw its scenes at the desks and paint them in Ink & Paint. A 1930s Spanish-revival studio,
        # cream stucco under red tiles, with tall north-light windows for the drawing boards.
        'name': 'animation_studio', 'donor': 'customscriptoffice',
        'style': {'wall': 'wall_ph54', 'base': 'wall_ph55', 'trim': 'wall_ph40', 'tiles': 'roof_ph04',
                  'door': 'door_ph10'},
        'blocks': [
            {'x': 0.0, 'y': 0.5, 'w': 26.0, 'd': 12.0, 'h': 8.0, 'floors': 2, 'roof': 'hip', 'pitch': 3.0,
             'windows': {'w': 1.8, 'h': 2.4, 'spacing': 2.6, 'sill': 0.6}},
            {'x': 0.0, 'y': -6.3, 'w': 6.0, 'd': 2.0, 'h': 5.0, 'floors': 1, 'roof': 'flat', 'bands': False,
             'windows': {'w': 0}, 'doors': [{'side': 'f', 'w': 2.0, 'h': 2.8}]},
        ],
        'rooms': {'room_cso_create': 'room_tva_desks', 'room_cso_canit': 'room_tva_ink',
                  'room_cso_blank': 'room_tva_story', 'room_cso_hall': 'room_tva_hire'},
        # Animators sit at the boards and work: the script office hall's sit-and-type scene, in
        # both drawing rooms. The hiring desk takes the crew facility's hiring desk's scene.
        'scenes': {'so_hall': 'tva_desks', 'cf_hire': 'tva_hire'},
        'extra_scenes': {'so_hall': 'tva_ink'},
        'zones': {'room_': [-12.7, -5.2, 12.7, 6.2], 'room_tva_hire': [-4.0, -5.2, 4.0, 0.8],
                  'room_tva_story': [-12.7, -5.2, -4.2, 0.8], 'room_tva_voice': [-12.7, 1.0, -4.2, 6.2],
                  'room_tva_desks': [-4.0, 1.0, 12.7, 6.2],
                  'room_tva_ink': [4.2, -5.2, 12.7, 0.8], 'room_tva_wait': [2.2, -10.8, 6.6, -8.4]},
        # The Voice Booth, the back half of the Story Room: stars stand at five microphones.
        'stand_scenes': {'tva_wait': [(3.0, -9.4), (4.4, -9.8), (5.8, -9.3), (3.6, -10.4), (5.1, -10.5)],
                         'tva_voice': [(-11.2, 4.6), (-9.6, 5.0), (-8.0, 4.6), (-6.4, 5.0), (-4.9, 4.6)]},
        'props': [
            {'type': 'sign', 'x': 0.0, 'y': -7.35, 'w': 5.6, 'h': 1.0, 'z': 3.5, 'legs': 0, 'texture': 'tvp_cartoons.png'},
            {'type': 'sign', 'x': -5.5, 'y': -8.6, 'w': 2.0, 'h': 2.0, 'legs': 0.8, 'texture': 'tvp_mascot.png'},
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

    img = sign(1024, 144, (22, 60, 40), (8, 30, 18), (225, 190, 90))
    centred_text(img, 'ACCOUNTANTS', 92, (240, 212, 120))
    img.save(os.path.join(out, 'tvp_accounts.png'))

    # A ledger page: ruled columns of figures and a gold dollar.
    img = sign(384, 256, (245, 240, 220), (225, 218, 190), (22, 60, 40))
    d = ImageDraw.Draw(img)
    for y in range(40, 236, 22):
        d.line([(24, y), (360, y)], fill=(150, 185, 210), width=2)
    d.line([(230, 24), (230, 232)], fill=(200, 80, 80), width=3)
    f = font(20)
    for i, fig in enumerate(['12,400', '8,150', '31,975', '4,020', '17,300', '9,880', '83,725']):
        d.text((250, 20 + i * 22), fig, font=f, fill=(40, 40, 60) if i < 6 else (22, 110, 50))
    d.line([(240, 172), (350, 172)], fill=(40, 40, 60), width=2)
    d.text((70, 30), '$', font=font(170), fill=(205, 160, 40))
    img.save(os.path.join(out, 'tvp_ledger.png'))

    img = sign(512, 136, (40, 20, 70), (15, 5, 35), (255, 120, 220))
    centred_text(img, 'TV WRITERS', 64, (255, 200, 245), glow=(255, 90, 210, 255))
    img.save(os.path.join(out, 'tvp_writers.png'))

    # The Animation Studio: CARTOONS in bouncing letters of every colour.
    img = sign(1024, 184, (250, 236, 200), (235, 210, 160), (200, 40, 40))
    colours = [(215, 45, 45), (240, 150, 20), (40, 140, 60), (30, 90, 200), (150, 50, 170)]
    f = font(120)
    d = ImageDraw.Draw(img)
    x = (img.width - sum(int(d.textlength(ch, font=f)) + 6 for ch in 'CARTOONS') + 6) // 2
    for i, ch in enumerate('CARTOONS'):
        y = 22 + (14 if i % 2 else -6)
        d.text((x + 5, y + 5), ch, font=f, fill=(90, 50, 30))
        d.text((x, y), ch, font=f, fill=colours[i % len(colours)])
        x += int(d.textlength(ch, font=f)) + 6
    img.save(os.path.join(out, 'tvp_cartoons.png'))

    # The studio mascot board: a beaming cartoon star with big eyes and white gloves.
    img = sign(256, 256, (70, 160, 230), (30, 90, 170), (255, 255, 255))
    d = ImageDraw.Draw(img)
    cx, cy = 128, 132
    pts = [(cx + (96 if k % 2 == 0 else 42) * math.cos(-math.pi / 2 + k * math.pi / 5),
            cy + (96 if k % 2 == 0 else 42) * math.sin(-math.pi / 2 + k * math.pi / 5)) for k in range(10)]
    d.polygon([(px + 5, py + 5) for px, py in pts], fill=(30, 40, 80))
    d.polygon(pts, fill=(255, 210, 40), outline=(40, 30, 20))
    for ex in (110, 146):
        d.ellipse([ex - 13, 104, ex + 13, 140], fill=(255, 255, 255), outline=(40, 30, 20), width=3)
        d.ellipse([ex - 5, 118, ex + 7, 136], fill=(30, 25, 25))
    d.arc([100, 128, 156, 170], 20, 160, fill=(150, 30, 30), width=6)
    for gx, gy in ((40, 108), (216, 108)):
        d.ellipse([gx - 16, gy - 14, gx + 16, gy + 14], fill=(255, 255, 255), outline=(40, 30, 20), width=3)
    img.save(os.path.join(out, 'tvp_mascot.png'))

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


def stand_scenes(zones, install):
    """Scenes of one person standing about at a given spot (x, y in the building's own
    coordinates), one per place: the script office's idle scene moved there."""
    sys.path.insert(0, TOOLS)
    import struct
    import scenes as flm
    raw = flm.unwrap(building.game_file('data/scene/interactions/ai_room_so_stunt_00.flm'))
    recs = flm.parse(raw)['recs']
    out = os.path.join(building.CONTENT, 'data', 'scene', 'interactions')
    os.makedirs(out, exist_ok=True)
    made = 0
    for zone, places in zones.items():
        for n, (x, y) in enumerate(places):
            d = bytearray(raw)
            for kind, _, at in recs:
                if kind == 6:     # Character: position at floats 14, 15 of the body
                    struct.pack_into('<2f', d, at + 8 + 14 * 4, x, y)
                elif kind == 1:   # AnimBox (the idle loop): position at floats 2, 3
                    struct.pack_into('<2f', d, at + 8 + 2 * 4, x, y)
            data = flm.wrap(bytes(d))
            name = 'ai_room_%s_%02d.flm' % (zone, n)
            open(os.path.join(out, name), 'wb').write(data)
            if install:
                dst = os.path.join(building.GAME, 'Data', 'scene', 'interactions')
                os.makedirs(dst, exist_ok=True)
                open(os.path.join(dst, name), 'wb').write(data)
            made += 1
    if zones:
        print('  %d standing places made' % made)


# Where a scene record keeps positions (floats into the record body, after type and size):
# every box at 2, 3; a walk's path points too; a character at 14, 15; a prop at 7, 8.
SCENE_XY = {0: (2,), 1: (2,), 2: (2, 13, 16, 19, 22), 3: (2,), 4: (2,), 9: (2,), 6: (14,), 5: (7,)}


def scene_places(raw):
    """The people's places in a scene (its characters' positions)."""
    import struct
    import scenes as flm
    out = []
    for kind, _, at in flm.parse(raw)['recs']:
        if kind == 6:
            x, y = struct.unpack_from('<2f', raw, at + 8 + 14 * 4)
            if x or y:
                out.append((x, y))
    return out


def shift_scene(raw, dx, dy):
    """A scene moved by (dx, dy) metres: its people, props and paths."""
    import struct
    import scenes as flm
    d = bytearray(raw)
    for kind, size, at in flm.parse(raw)['recs']:
        for i in SCENE_XY.get(kind, ()):
            o = at + 8 + i * 4
            if o + 8 > at + size:
                continue
            x, y = struct.unpack_from('<2f', d, o)
            if x or y:
                struct.pack_into('<2f', d, o, x + dx, y + dy)
    return bytes(d)


def zone_centre(shape):
    if isinstance(shape[0], (int, float)):
        return (shape[0] + shape[2]) / 2, (shape[1] + shape[3]) / 2
    return sum(p[0] for p in shape) / len(shape), sum(p[1] for p in shape) / len(shape)


def copy_scenes(scenes, install, zones=None):
    """The donor's AI scenes (people working in a zone) under the new zones' names. A scene's
    positions are in its building's own coordinates, so for a zone laid out anew (zones) the
    scenes are moved to its middle."""
    sys.path.insert(0, TOOLS)
    import scenes as flm
    out = os.path.join(building.CONTENT, 'data', 'scene', 'interactions')
    os.makedirs(out, exist_ok=True)
    copied = 0
    for old, new in scenes.items():
        files = []
        for n in range(10):
            data = building.game_file('data/scene/interactions/ai_room_%s_%02d.flm' % (old, n))
            if data is not None:
                files.append((n, data))
        shape = (zones or {}).get('room_' + new)
        if shape and files:
            places = [p for _, data in files for p in scene_places(flm.unwrap(data))]
            if places:
                cx, cy = zone_centre(shape)
                dx = cx - sum(p[0] for p in places) / len(places)
                dy = cy - sum(p[1] for p in places) / len(places)
                files = [(n, flm.wrap(shift_scene(flm.unwrap(data), dx, dy))) for n, data in files]
                print('  scenes %s moved %.1f, %.1f m into their zone' % (new, dx, dy))
        for n, data in files:
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


def run_blender(blender, script, work, spec_data, name, done):
    spec = os.path.join(work, 'build.json')
    json.dump(spec_data, open(spec, 'w'), indent=1)
    r = subprocess.run([blender, '-b', '--factory-startup', '-P', os.path.join(TOOLS, script), '--', work, spec],
                       capture_output=True, text=True)
    lines = [l for l in r.stdout.splitlines() if script[:-3] in l or 'Error' in l or 'Traceback' in l]
    print('\n'.join(lines) or r.stdout[-2000:])
    if r.returncode != 0 or not any(done in l for l in lines):
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
        for stale in ('lightmap.json', 'lightmap.png', 'thumbnail.png', 'blueprint.png'):   # from an earlier run
            if os.path.exists(os.path.join(work, stale)):
                os.remove(os.path.join(work, stale))
        make_textures(os.path.join(work, 'textures'))
        if b.get('badge'):
            badge_thumbnail(b['donor'], b['badge'], os.path.join(work, 'thumbnail.png'))
        if b.get('blocks'):
            style = dict(BUILD_STYLE, **b.get('style', {}))
            textures = set(style.values()) | {t for blk in b['blocks'] for t in blk.get('roles', {}).values()}
            for tex in textures:   # the game textures the new building is dressed in
                dst = os.path.join(work, 'textures', tex + '.dds')
                if not os.path.exists(dst):
                    data = building.game_file('data/textures/%s.dds' % tex)
                    if data is None:
                        sys.exit('no game texture ' + tex)
                    open(dst, 'wb').write(data)
            run_blender(blender, 'blender_build.py', work, {'rooms': b['rooms'], 'zones': b.get('zones', {}), 'style': style,
                                                            'blocks': b['blocks']},
                        b['name'], 'blender_build: removed')
        spec = os.path.join(work, 'spec.json')
        json.dump({'rooms': b['rooms'], 'props': b['props']}, open(spec, 'w'), indent=1)
        r = subprocess.run([blender, '-b', '--factory-startup', '-P', os.path.join(TOOLS, 'blender_props.py'), '--', work, spec],
                           capture_output=True, text=True)
        lines = [l for l in r.stdout.splitlines() if 'blender_props' in l or 'Error' in l or 'Traceback' in l]
        print('\n'.join(lines) or r.stdout[-2000:])
        if r.returncode != 0 or not any('blender_props:' in l for l in lines):
            print(r.stderr[-3000:])
            sys.exit('Blender failed for ' + b['name'])
        if b.get('blocks'):
            # Lighting like the game's buildings, then the menu picture of the new building.
            r = subprocess.run([blender, '-b', '--factory-startup', '-P', os.path.join(TOOLS, 'blender_bake.py'), '--', work,
                                'lm_%s_fac_0.dds' % b['name']], capture_output=True, text=True)
            print('\n'.join(l for l in r.stdout.splitlines() if 'blender_bake' in l or 'Error' in l))
            if r.returncode != 0 or not os.path.exists(os.path.join(work, 'lightmap.json')):
                print(r.stdout[-2000:], r.stderr[-2000:])
                sys.exit('lightmap bake failed for ' + b['name'])
            r = subprocess.run([blender, '-b', '--factory-startup', '-P', os.path.join(TOOLS, 'blender_build.py'), '--', work,
                                '--thumb', os.path.join(work, 'thumbnail.png')], capture_output=True, text=True)
            if r.returncode != 0 or not os.path.exists(os.path.join(work, 'thumbnail.png')):
                print(r.stdout[-2000:], r.stderr[-2000:])
                sys.exit('thumbnail failed for ' + b['name'])
            # The blueprint sheet on the ground while it is placed: the new building from above.
            sheet = os.path.join(work, 'blueprint.png')
            rect = [str(v) for v in building.work_lot_rect(work)]
            r = subprocess.run([blender, '-b', '--factory-startup', '-P', os.path.join(TOOLS, 'blender_blueprint.py'), '--',
                                work, *rect, sheet], capture_output=True, text=True)
            if r.returncode != 0 or not os.path.exists(sheet):
                print(r.stdout[-2000:], r.stderr[-2000:])
                sys.exit('blueprint sheet failed for ' + b['name'])
        building.build(work, b['name'], install=install)
        copy_scenes(b.get('scenes', {}), install, b.get('zones'))
        copy_scenes(b.get('extra_scenes', {}), install, b.get('zones'))
        stand_scenes(b.get('stand_scenes', {}), install)


if __name__ == '__main__':
    main()
