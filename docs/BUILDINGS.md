# Making new buildings

`tools/building.py` turns a model you edit in Blender into a working building: the outside
model, the floor plan you see while carrying a person, the drop zones, the construction
ghost, and the menu entry.

You need Python 3 with `numpy`, `scipy` and `Pillow`, and Blender (any 3.6+ or 4.x).

## 1. Start from an existing building

```
python tools/building.py export customscriptoffice work/my_studio
```

Any game building works as a starting point (`stageschool`, `research`, `casting_office`,
...; the names are the `fac_<name>.msh` files). This writes `building.obj`, `building.mtl`,
the textures, and `building.json` (settings carried over from the donor; leave it alone).

## 2. Edit it in Blender

File > Import > Wavefront (.obj), pick `building.obj`. Blender's default axes are right.
Blender's coordinates are the game's: metres, Z up, the lot centred near the origin.

Every object's **name** says what it is (Blender may add `.001`; that is ignored):

| Name | What it is |
|---|---|
| `fac.<group>.<n>` | The building from outside. Add as many objects as you like; a new group name (e.g. `fac.tv_mast.00`) makes a new group. |
| `fld.<group>.<n>` | The cut-away floor plan shown while you carry a person over the building. |
| `blp.<group>.<n>` | The construction ghost. **Delete these** after big shape changes: the tool then makes a new ghost that fits. |
| `room.<zone name>` | A drop zone: a flat shape on the floor. Names starting `room_tv_` are TV zones (`room_tv_cast`, `room_tv_writers`, `room_tv_showrun`); `room_` on its own is the whole-building zone. 31 characters max. |
| `ctl.lot_boundary` | The lot's outline (what the building occupies when placed). |
| `ctl.min_outline` | The building's footprint. |
| `ctl.clickable` | What you click to select the building. |
| `shape.<n>` | A box volume people and the camera keep out of; 8 corners, may be turned. |
| `pt.<name>.<n>` | A two-point line: where someone stands (first point) and which way they face. `_fp_door` = doorway, `_fp_dot` = path node, `repair_NN`, `leanspot_NN`, `peerspot`, `build_N`, `moveasset`. |
| `anchor.<name>.<model><n>` | A two-point line: an attachment point and its direction. `_sp_pavement_0` is where the pavement meets the lot. |

Missing `ctl.*` and `shape.*` objects are generated from the building's geometry.

**Materials:** the imported material names keep the game's settings. For a new material
give it an Image Texture (PNG/JPG/TGA/BMP or DDS); the tool converts it.

**Lighting:** meshes you leave untouched keep the game's baked lighting. New or edited
meshes are lit evenly.

**When you edit zones,** change them in `room.*` only: the floor plan model and the zone
labels are updated from them.

File > Export > Wavefront (.obj) over `building.obj` with the defaults (Materials on,
Path Mode "Relative" helps).

## 3. Build and install

```
python tools/building.py build work/my_studio tv_studio --install
```

This writes into `content/data/` and, with `--install`, copies into the game's `Data`:

- `meshes/fac_tv_studio.msh`, `fld_tv_studio.msh`, `blp_tv_studio.msh`
- `meshes/extrainfo/fac_tv_studio.inf` (points and zone labels)
- `textures/thumbs/sets/fac_tv_studio.dds` (menu picture: put `thumbnail.png` in the work
  folder to use your own)
- `facility/facility_tv_studio.ini` (only if missing: set the price there)

Then add its name and menu text to `[text]` in `MoviesMod.ini`:

```
FACILITY_TV_STUDIO = My TV Studio
BLURB_FACILITY_TV_STUDIO = What it's for.
```

The facility file must be called `facility_<name>.ini`: the build menu ignores anything
that does not start with `facility` or `trailer`.

**Zones are saved with each placed building.** After changing a model's zones, demolish
and rebuild that building in the game.

## Dressing buildings from a script

`tools/make_tv_models.py` builds every TV building this way, without opening Blender by hand:
it exports the donor, renames its zones, draws sign textures with Pillow, and runs
`tools/blender_props.py` in Blender to add parts that sit on the roof (found by ray-casting):
`sign`, `tower` (lattice transmitter), `dish`, `mast`, `box` (rooftop units) and `spot`.

```
python tools/make_tv_models.py --blender "C:/path/to/blender.exe" --install [tv_network_hq ...]
```

`tools/blender_preview.py` renders a building's OBJ to a PNG to check the result:

```
blender -b --factory-startup -P tools/blender_preview.py -- <work>/building.obj preview.png
```

## How it works

See [GAME_INTERNALS.md](GAME_INTERNALS.md) for the `.msh` and `.inf` formats. `tools/msh.py`
reads and writes models (every one of the game's 206 building models rebuilds
byte-identical), and `tools/hull.py` makes the convex hull block.

## Luxury variants

`python tools/make_tv_models.py --install scriptoffice_5 trailer6 trailer7 trailer8 trailer9` rebuilds the
revived luxury buildings (`LUXURY` in the script): the donor model plus rooftop props, and a menu
picture with a gold tier badge (`badge`). Their facility files (`facility_script_5star.ini`,
`trailer_6..9.ini`) already name the new meshes, so the tool doesn't write new ones. The game finds a
trailer's visit spot by the `visittrailer` desire, not by the trailer's number, so donor points work.

