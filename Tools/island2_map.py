"""island2_map.py - bake GALLOWS REACH's chart plate, T_Island2Map (2026-09-07, spec C34.6).

The sibling of Tools\\map_capture.py, which bakes Wreck Cove's plate. Same reasoning applies in full:
the headless harness runs with -nullrhi, so a scene capture would write a black PNG; the picture is
therefore DRAWN from the island's own tier geometry, which is the same source of truth
Tools\\island2_build.py laid the cubes from, and is correct by construction rather than by eyeballing
a screenshot. The PNG writer is stdlib only (zlib + struct); Pillow is not in the editor's python.

WORLD TO MAP MAPPING (mirrored in C++ in Source\\Wrecktide\\UI\\WrecktideMapView.cpp, in the
GetIslandCharts() table; keep the two in step)
    centre      (12000, -16000)
    half extent 6000 uu   (ortho width 12000)
    U = (WorldY + 16000 + 6000) / 12000     map RIGHT = world +Y
    V = (6000 - (WorldX - 12000)) / 12000   map UP    = world +X  (labelled N on the rim)
Half extent 6000 and not 4500 so the water outside the beach ring, the landing pier and the sail lane
back to Wreck Cove all have room on the plate.

GEOMETRY, from Tools\\island2_build.py's header block
    beach ring   9000x9000  top -40    field 6000x6000 top 0
    garrison square 2600x2600 centred (11400, -16400) top +40
    lighthouse rock 1200x1200 centred (14200, -13800) top +80, tower 800x800 to +600
    stair: 16 treads from x 13700 marching -X at 200 uu, y -13800, 700 uu wide

OUTPUT   Content\\UI\\Map\\island2_map.png  2048x2048  ->  /Game/UI/Map/T_Island2Map
/Game/UI is already covered by +DirectoriesToAlwaysCook in Config\\DefaultGame.ini, so this texture
cooks with NO new staging rule even though the HUD loads it by string.

Run: tools\\run_py_args.cmd Tools\\island2_map.py      (add "verify" to check without writing)
Markers: WRECKTIDE-ISLE2MAP, final line RESULT=OK or RESULT=FAIL.
"""
import math
import os
import struct
import sys
import zlib

import unreal

M = "WRECKTIDE-ISLE2MAP"
DEST = "/Game/UI/Map"
ASSET = "T_Island2Map"

CENTRE_X = 12000.0
CENTRE_Y = -16000.0
HALF_EXTENT = 6000.0
MAP_SIZE = 2048

BEACH_HALF = 4500.0
FIELD_HALF = 3000.0
SQUARE_CENTRE = (11400.0, -16400.0)
SQUARE_HALF = 1300.0
LIGHT_CENTRE = (14200.0, -13800.0)
ROCK_HALF = 600.0
TOWER_HALF = 400.0
STAIR_X = (10600.0, 13800.0)
STAIR_Y = (-14150.0, -13450.0)
# the landing pier deck island2_dress.py builds, drawn like Wreck Cove's pier: over the water
PIER_X = (9300.0, 9700.0)
PIER_Y = (-13100.0, -11300.0)

SEA_DEEP = (28, 58, 78)
SEA_SHALLOW = (58, 112, 128)
SURF = (196, 214, 208)
SAND = (214, 186, 132)
SAND_DARK = (186, 156, 104)
GRASS = (88, 118, 78)
GRASS_DARK = (66, 94, 60)
STONE = (128, 130, 134)
STONE_DARK = (100, 102, 108)
STONE_RIM = (162, 164, 168)
WOOD = (72, 48, 28)

VERIFY_ONLY = any(a.lower() == "verify" for a in sys.argv[1:])


def log(msg):
    unreal.log_warning("%s %s" % (M, msg))


def fatal(msg):
    unreal.log_error("%s FATAL %s" % (M, msg))
    unreal.log_error("%s RESULT=FAIL" % M)
    raise SystemExit(1)


def write_png(path, width, height, rows):
    raw = bytearray()
    for row in rows:
        raw.append(0)  # filter type 0
        raw.extend(row)

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)  # 8 bit RGBA
    blob = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b""))
    with open(path, "wb") as handle:
        handle.write(blob)
    return len(blob)


def lerp3(a, b, t):
    t = 0.0 if t < 0.0 else (1.0 if t > 1.0 else t)
    return (int(a[0] + (b[0] - a[0]) * t), int(a[1] + (b[1] - a[1]) * t), int(a[2] + (b[2] - a[2]) * t))


def coast_wobble(ang):
    """Three harmonics, a different phase set from Wreck Cove's so the two islands do not read as
    the same shape twice."""
    return 1.0 + 0.050 * math.sin(3.0 * ang - 1.4) + 0.026 * math.sin(4.0 * ang + 0.9) + 0.015 * math.sin(7.0 * ang + 2.6)


def rounded_square(ax, ay, half):
    if half <= 0.0:
        return 99.0
    nx = abs(ax) / half
    ny = abs(ay) / half
    return nx * nx * nx + ny * ny * ny


def in_box(x, y, xr, yr):
    return xr[0] <= x <= xr[1] and yr[0] <= y <= yr[1]


def build_plate():
    rows = []
    span = 2.0 * HALF_EXTENT
    for py in range(MAP_SIZE):
        world_x = CENTRE_X + HALF_EXTENT - (py + 0.5) / MAP_SIZE * span
        row = bytearray()
        band = 0.5 + 0.5 * math.sin(world_x / 540.0)
        for px in range(MAP_SIZE):
            world_y = CENTRE_Y + (px + 0.5) / MAP_SIZE * span - HALF_EXTENT
            ax = world_x - CENTRE_X
            ay = world_y - CENTRE_Y
            ang = math.atan2(ay, ax)
            wob = coast_wobble(ang)

            # the landing pier runs OUT over the water, so it is drawn before the sea test
            if in_box(world_x, world_y, PIER_X, PIER_Y):
                row += bytes((WOOD[0], WOOD[1], WOOD[2], 255))
                continue

            beach = rounded_square(ax, ay, BEACH_HALF * wob)
            if beach >= 1.0:
                shore = beach - 1.0
                if shore < 0.055:
                    col = lerp3(SURF, SEA_SHALLOW, shore / 0.055)
                else:
                    col = lerp3(SEA_SHALLOW, SEA_DEEP, min(1.0, (shore - 0.055) / 1.4))
                    col = (int(col[0] + band * 5), int(col[1] + band * 6), int(col[2] + band * 7))
                row += bytes((col[0], col[1], col[2], 255))
                continue

            # lighthouse tower, then its rock, then the stair: all stone, brightest at the top
            lx = world_x - LIGHT_CENTRE[0]
            ly = world_y - LIGHT_CENTRE[1]
            tower = rounded_square(lx, ly, TOWER_HALF)
            rock = rounded_square(lx, ly, ROCK_HALF)
            square = rounded_square(world_x - SQUARE_CENTRE[0], world_y - SQUARE_CENTRE[1], SQUARE_HALF)
            field = rounded_square(ax, ay, FIELD_HALF * wob)

            if tower < 1.0:
                col = STONE_RIM
            elif rock < 1.0:
                col = lerp3(STONE, STONE_RIM, 0.4 * (1.0 - rock))
            elif in_box(world_x, world_y, STAIR_X, STAIR_Y):
                # banded treads, so the climb reads as a stair and not a ramp
                tread = int((13800.0 - world_x) / 200.0) % 2
                col = STONE if tread == 0 else STONE_DARK
            elif square < 1.0:
                # the garrison square: stone flags with thin darker SEAMS. An alternating
                # light/dark grid was the first attempt and read as an engine checker texture at
                # radar size, which is the last thing a map plate should look like.
                seam = math.fmod(abs(world_x), 300.0) < 26.0 or math.fmod(abs(world_y), 300.0) < 26.0
                col = STONE_DARK if seam else STONE
                if square > 0.90:
                    col = lerp3(col, GRASS_DARK, (square - 0.90) / 0.10)
            elif field < 1.0:
                col = lerp3(GRASS, GRASS_DARK, 0.5 + 0.5 * math.sin(world_x / 340.0 + world_y / 420.0))
                if field > 0.93:
                    col = lerp3(col, SAND_DARK, (field - 0.93) / 0.07)
            else:
                col = lerp3(SAND, SAND_DARK, 0.5 + 0.5 * math.sin(world_y / 290.0 + world_x / 250.0))
                if beach > 0.94:
                    col = lerp3(col, SURF, (beach - 0.94) / 0.06 * 0.45)

            row += bytes((col[0], col[1], col[2], 255))
        rows.append(row)
    return rows


def import_png(png_path, name):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", png_path)
    task.set_editor_property("destination_path", DEST)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    path = "%s/%s" % (DEST, name)
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        return None
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        return None
    asset.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
    asset.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
    asset.set_editor_property("srgb", True)
    # CLAMP, not the import default WRAP. The radar samples this plate at whatever UV the pirate's
    # position maps to, and a pirate at sea is outside [0,1]: wrapping would tile a second Gallows
    # Reach across the open water on the minimap.
    asset.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
    asset.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)
    log("imported %s (%dx%d)" % (path, asset.blueprint_get_size_x(), asset.blueprint_get_size_y()))
    return asset


def main():
    path = "%s/%s" % (DEST, ASSET)
    if VERIFY_ONLY:
        present = unreal.EditorAssetLibrary.does_asset_exist(path)
        log("verify %s = %s" % (path, "OK" if present else "MISSING"))
        log("RESULT=%s (verify)" % ("OK" if present else "FAIL"))
        return 0 if present else 1

    content = os.path.abspath(unreal.Paths.project_content_dir())
    out_dir = os.path.join(content, "UI", "Map")
    if not os.path.isdir(out_dir):
        os.makedirs(out_dir)

    log("drawing Gallows Reach %dx%d, centre (%.0f, %.0f), half extent %.0f uu"
        % (MAP_SIZE, MAP_SIZE, CENTRE_X, CENTRE_Y, HALF_EXTENT))
    png = os.path.join(out_dir, "island2_map.png")
    size = write_png(png, MAP_SIZE, MAP_SIZE, build_plate())
    log("island2_map.png %d bytes" % size)

    if import_png(png, ASSET) is None:
        fatal("texture missing after import: %s" % path)

    log("mapping: centre (%.0f,%.0f) half extent %.0f, U=(Y-%.0f+%.0f)/%.0f, V=(%.0f-(X-%.0f))/%.0f"
        % (CENTRE_X, CENTRE_Y, HALF_EXTENT, CENTRE_Y, HALF_EXTENT, 2.0 * HALF_EXTENT,
           HALF_EXTENT, CENTRE_X, 2.0 * HALF_EXTENT))
    log("RESULT=OK %s baked" % path)
    return 0


try:
    raise SystemExit(main())
except SystemExit:
    raise
except Exception as e:  # noqa: BLE001
    unreal.log_error("%s unhandled exception: %r" % (M, e))
    unreal.log_error("%s RESULT=FAIL (exception)" % M)
    raise SystemExit(1)
