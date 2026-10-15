"""map_capture.py - build the Wrecktide island map picture and the beacon art (2026-09-06, lane L4).

WHY THIS IS NOT A SCENE CAPTURE. The sanctioned headless harness is tools\\run_py_args.cmd, and it
launches UnrealEditor-Cmd with -nullrhi (tools\\run_py_args.cmd:16). With no RHI there is no renderer,
so a USceneCaptureComponent2D writes an empty render target and ExportRenderTarget writes a black PNG.
Rendering would need an interactive editor, which this lane may never open. RESEARCH section 4 also
lands on the same answer for the SHIPPING side: a live scene capture costs a fixed ~0.4 ms per frame
whatever the target size, and stylised games (POTCO is the direct precedent) ship AUTHORED map art.
So the island picture is DRAWN here from the island's own tier geometry, which is the exact same
source of truth the level was built from (Tools\\build_island_level.py:119-123):

    beach ring  14000 x 14000  X,Y in [-7000, +7000]  top z = -40
    central field 10000 x 10000  X,Y in [-5000, +5000]  top z =   0
    hill        3000 x 3000 centred (2600, 2600)        top z = +40
    sea level Z = -120

The picture is therefore correct by construction rather than by eyeballing a screenshot.

This script bakes WRECK COVE's plate only. Gallows Reach has its own baker, Tools\\island2_map.py,
and the two plates meet in ONE C++ table: WrecktideMap::GetIslandCharts() in
Source\\Wrecktide\\UI\\WrecktideMapView.cpp, which is what the radar and the full map read to decide
whose picture to draw. A new island is a new baker plus a row in that table; keep all three in step.

WORLD TO MAP MAPPING (mirrored in C++ at Source\\Wrecktide\\UI\\WrecktideMapView.h, keep the two in step):
    centre      (0, 0)
    half extent 10000 uu  (ortho width 20000)
    U = (WorldY + 10000) / 20000        map RIGHT  = world +Y
    V = (10000 - WorldX) / 20000        map UP     = world +X  (labelled N on the rim)
Half extent is 10000 and not the 8000 the spec sketched because the pier deck runs to Y = -9110 and
the player's boat is moored at (-700, -8300): a 16000 ortho width would cut both off the map edge.

OUTPUT (all original art, drawn from primitives; nothing traced, scanned or borrowed)
    Content\\UI\\Map\\island_map.png  2048x2048  ->  /Game/UI/Map/T_IslandMap
    Content\\UI\\Map\\map_rim.png      512x512   ->  /Game/UI/Map/T_MapRim     minimap rope rim + round mask
    Content\\UI\\Map\\beam_grad.png     64x256   ->  /Game/UI/Map/T_BeamGrad   mission beam vertical fade
    Content\\UI\\Map\\map_glow.png     256x256   ->  /Game/UI/Map/T_MapGlow    beacon base disc + sparkles
    /Game/UI/Map/M_WrecktideBeacon              unlit TRANSLUCENT two-sided, params Mask/BeamColour/Intensity/OpacityScale

/Game/UI is already covered by +DirectoriesToAlwaysCook in Config\\DefaultGame.ini:21, so /Game/UI/Map
cooks with no new staging rule (both the HUD textures and the beacon material are loaded by STRING at
runtime, so nothing else would keep them alive through a cook).

Run: tools\\run_py_args.cmd <this file>            (add "verify" to check without writing)
Markers: WRECKTIDE-MAPART ... , final line RESULT=OK or RESULT=FAIL.
"""
import math
import os
import struct
import sys
import zlib

import unreal

M = "WRECKTIDE-MAPART"
DEST = "/Game/UI/Map"
MAT_PATH = "%s/M_WrecktideBeacon" % DEST

# --- world to map, the single source of truth on the python side -------------------------------
HALF_EXTENT = 10000.0
MAP_SIZE = 2048
RIM_SIZE = 512
GLOW_SIZE = 256
RING_SIZE = 256
GRAD_W, GRAD_H = 64, 256

# --- island geometry, from Tools\build_island_level.py:119-123 ----------------------------------
BEACH_HALF = 7000.0
FIELD_HALF = 5000.0
HILL_CENTRE = (2600.0, 2600.0)
HILL_HALF = 1500.0
PIER_X = (-300.0, 300.0)
PIER_Y = (-9110.0, -6900.0)

# --- palette (warm sea chart, not a satellite photo) --------------------------------------------
SEA_DEEP = (28, 58, 78)
SEA_SHALLOW = (58, 112, 128)
SURF = (196, 214, 208)
SAND = (214, 186, 132)
SAND_DARK = (186, 156, 104)
GRASS = (96, 126, 74)
GRASS_DARK = (74, 100, 58)
HILL = (122, 116, 74)
HILL_RIM = (150, 140, 92)
WOOD = (72, 48, 28)
ROPE_LIGHT = (198, 164, 108)
ROPE_DARK = (120, 92, 54)
PLAQUE = (58, 40, 24)


def log(msg):
    unreal.log_warning("%s %s" % (M, msg))


def fatal(msg):
    unreal.log_error("%s FATAL %s" % (M, msg))
    unreal.log_error("%s RESULT=FAIL" % M)
    raise SystemExit(1)


# ------------------------------------------------------------------------------------------------
# Minimal PNG writer. stdlib only (zlib + struct): Pillow is not installed in the editor's python,
# and adding a dependency to a headless build tool is not worth one RGBA blit.
# ------------------------------------------------------------------------------------------------
def write_png(path, width, height, rows):
    raw = bytearray()
    for row in rows:
        raw.append(0)  # filter type 0, no prediction: the compressor still does the work
        raw.extend(row)

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)  # 8 bit, colour type 6 = RGBA
    blob = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b"")
    with open(path, "wb") as handle:
        handle.write(blob)
    return len(blob)


def lerp3(a, b, t):
    t = 0.0 if t < 0.0 else (1.0 if t > 1.0 else t)
    return (int(a[0] + (b[0] - a[0]) * t), int(a[1] + (b[1] - a[1]) * t), int(a[2] + (b[2] - a[2]) * t))


def coast_wobble(ang):
    """A little three-harmonic wobble so the tiers do not read as three nested squares."""
    return 1.0 + 0.042 * math.sin(3.0 * ang + 0.7) + 0.028 * math.sin(5.0 * ang + 2.1) + 0.017 * math.sin(8.0 * ang - 1.2)


def rounded_square(ax, ay, half):
    """Superellipse test: <1 inside, 1 on the shore. Exponent 3 keeps the square tier readable
    while rounding the corners enough to look like land."""
    if half <= 0.0:
        return 99.0
    nx = abs(ax) / half
    ny = abs(ay) / half
    return nx * nx * nx + ny * ny * ny


def build_island_map():
    """One RGBA row per texture row. Iterates 2048 rows x 2048 columns, ~10 s in the editor python."""
    rows = []
    span = 2.0 * HALF_EXTENT
    for py in range(MAP_SIZE):
        world_x = HALF_EXTENT - (py + 0.5) / MAP_SIZE * span
        row = bytearray()
        # cheap per-row sea banding so the water is not a flat fill
        band = 0.5 + 0.5 * math.sin(world_x / 620.0)
        for px in range(MAP_SIZE):
            world_y = (px + 0.5) / MAP_SIZE * span - HALF_EXTENT
            ang = math.atan2(world_y, world_x)
            wob = coast_wobble(ang)

            # the pier is real level geometry (Tools\island_dress.py:261-265) and it runs OUT over the
            # water, so it is drawn before the sea test or only its landward sliver would show
            if PIER_X[0] <= world_x <= PIER_X[1] and PIER_Y[0] <= world_y <= PIER_Y[1]:
                row += bytes((WOOD[0], WOOD[1], WOOD[2], 255))
                continue

            beach = rounded_square(world_x, world_y, BEACH_HALF * wob)
            if beach >= 1.0:
                # open water: deep out at the edges, shallow near the shore, with a surf line
                shore = beach - 1.0
                if shore < 0.055:
                    col = lerp3(SURF, SEA_SHALLOW, shore / 0.055)
                else:
                    col = lerp3(SEA_SHALLOW, SEA_DEEP, min(1.0, (shore - 0.055) / 1.4))
                    col = (int(col[0] + band * 5), int(col[1] + band * 6), int(col[2] + band * 7))
                row += bytes((col[0], col[1], col[2], 255))
                continue

            field = rounded_square(world_x, world_y, FIELD_HALF * wob)
            hx = world_x - HILL_CENTRE[0]
            hy = world_y - HILL_CENTRE[1]
            hill = rounded_square(hx, hy, HILL_HALF)

            if hill < 1.0:
                col = HILL_RIM if hill > 0.86 else lerp3(HILL, HILL_RIM, 0.35 * (0.5 + 0.5 * math.sin(hill * 9.0)))
            elif field < 1.0:
                col = lerp3(GRASS, GRASS_DARK, 0.5 + 0.5 * math.sin(world_x / 380.0 + world_y / 460.0))
                if field > 0.93:
                    col = lerp3(col, SAND_DARK, (field - 0.93) / 0.07)
            else:
                col = lerp3(SAND, SAND_DARK, 0.5 + 0.5 * math.sin(world_y / 310.0 + world_x / 270.0))
                if beach > 0.94:
                    col = lerp3(col, SURF, (beach - 0.94) / 0.06 * 0.45)

            row += bytes((col[0], col[1], col[2], 255))
        rows.append(row)
    return rows


def build_rim():
    """Rope-bound porthole. Transparent window in the middle, opaque ring and plaque outside so the
    square minimap tile underneath is masked into a disc at any rotation."""
    rows = []
    for py in range(RIM_SIZE):
        v = (py + 0.5) / RIM_SIZE - 0.5
        row = bytearray()
        for px in range(RIM_SIZE):
            u = (px + 0.5) / RIM_SIZE - 0.5
            d = math.sqrt(u * u + v * v)
            ang = math.atan2(v, u)
            if d < 0.360:
                row += bytes((0, 0, 0, 0))
            elif d < 0.396:
                twist = 0.5 + 0.5 * math.sin(ang * 46.0)
                col = lerp3(ROPE_DARK, ROPE_LIGHT, twist)
                row += bytes((col[0], col[1], col[2], 255))
            elif d < 0.560:
                shade = 0.5 + 0.5 * math.sin(ang * 7.0 + d * 30.0)
                col = lerp3(PLAQUE, WOOD, shade * 0.6)
                row += bytes((col[0], col[1], col[2], 255))
            elif d < 0.600:
                a = int(255 * (1.0 - (d - 0.560) / 0.040))
                row += bytes((PLAQUE[0], PLAQUE[1], PLAQUE[2], max(0, a)))
            else:
                row += bytes((0, 0, 0, 0))
        rows.append(row)
    return rows


def build_glow():
    """Radial falloff for the beacon base disc and the orbiting sparkles."""
    rows = []
    for py in range(GLOW_SIZE):
        v = (py + 0.5) / GLOW_SIZE - 0.5
        row = bytearray()
        for px in range(GLOW_SIZE):
            u = (px + 0.5) / GLOW_SIZE - 0.5
            d = min(1.0, math.sqrt(u * u + v * v) * 2.0)
            f = (1.0 - d)
            f = f * f * (3.0 - 2.0 * f)  # smoothstep, no hard rim on the glow
            level = int(255 * f)
            row += bytes((level, level, level, level))
        rows.append(row)
    return rows


def build_ring():
    """Base disc: the same soft radial glow with a crisp gold annulus, so the foot of the beam reads
    as a marked spot on the ground and not just a smudge."""
    rows = []
    for py in range(RING_SIZE):
        v = (py + 0.5) / RING_SIZE - 0.5
        row = bytearray()
        for px in range(RING_SIZE):
            u = (px + 0.5) / RING_SIZE - 0.5
            d = math.sqrt(u * u + v * v) * 2.0
            if d >= 1.0:
                row += bytes((0, 0, 0, 0))
                continue
            f = 1.0 - d
            f = f * f * (3.0 - 2.0 * f)
            level = 0.42 * f
            # gold ring at 0.74 of the radius, 0.06 wide, smooth shouldered
            ring = max(0.0, 1.0 - abs(d - 0.74) / 0.09)
            level = min(1.0, level + ring * ring * 0.95)
            byte = int(255 * level)
            row += bytes((byte, byte, byte, byte))
        rows.append(row)
    return rows


def build_grad():
    """Vertical fade for the sky beam: strong at the base, nothing at the very top.

    The first version used (1 - t) SQUARED and that killed the beam in practice: the squared curve
    is already under 0.25 by mid height, so the part of the beam standing against the SKY (which is
    the part a player actually sees over the camp roofs) was almost fully transparent and measured
    below a colour distance of 60 against the sky. Exponent 0.55 keeps the beam substantial over its
    whole height and still reaches zero at the top. Symmetric enough that it reads the same whichever
    cap the engine cylinder puts V=0 on."""
    rows = []
    for py in range(GRAD_H):
        t = (py + 0.5) / GRAD_H  # UE cylinder V runs 0 at one cap to 1 at the other
        f = max(0.0, 1.0 - t) ** 0.55
        level = int(255 * f)
        row = bytearray()
        for _ in range(GRAD_W):
            row += bytes((level, level, level, level))
        rows.append(row)
    return rows


# ------------------------------------------------------------------------------------------------
def import_png(png_path, name, compression, mips, srgb):
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
    asset.set_editor_property("compression_settings", compression)
    asset.set_editor_property("mip_gen_settings", mips)
    asset.set_editor_property("srgb", srgb)
    unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)
    log("imported %s (%dx%d)" % (path, asset.blueprint_get_size_x(), asset.blueprint_get_size_y()))
    return asset


def build_material(mask_texture):
    """M_WrecktideBeacon: unlit, TRANSLUCENT, two sided.

    It was additive at first and that was the wrong call: additive can only ADD to what is behind it,
    so a warm gold beam over a bright daytime sky moved nearly nothing and read as invisible in the
    first captures. Translucent lets the beam OWN its pixels: Emissive = BeamColour * Intensity gives
    the glow, Opacity = Mask * OpacityScale gives the shape and the fade, and a saturated gold at 0.75
    opacity is unmistakable against sky. Translucent surfaces do not write scene depth in UE, which is
    the "no depth write" the beam wants anyway, and depth TEST stays on so the beam is properly
    occluded by the fort and the tents.

    One material drives all three pieces; the instances only swap Mask (vertical gradient for the
    beam, radial ring for the base disc, radial glow for the sparkles), the colour and the two scalars.
    """
    if unreal.EditorAssetLibrary.does_asset_exist(MAT_PATH):
        unreal.EditorAssetLibrary.delete_asset(MAT_PATH)

    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_WrecktideBeacon", DEST, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        return None

    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)

    mel = unreal.MaterialEditingLibrary
    mask = mel.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -640, 0)
    mask.set_editor_property("parameter_name", "Mask")
    mask.set_editor_property("texture", mask_texture)

    colour = mel.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -640, 240)
    colour.set_editor_property("parameter_name", "BeamColour")
    colour.set_editor_property("default_value", unreal.LinearColor(1.0, 0.62, 0.12, 1.0))

    intensity = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -640, 400)
    intensity.set_editor_property("parameter_name", "Intensity")
    intensity.set_editor_property("default_value", 3.0)

    opacity_scale = mel.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -640, 540)
    opacity_scale.set_editor_property("parameter_name", "OpacityScale")
    opacity_scale.set_editor_property("default_value", 0.75)

    emissive = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -300, 300)
    mel.connect_material_expressions(colour, "", emissive, "A")
    mel.connect_material_expressions(intensity, "", emissive, "B")
    mel.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    opacity = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, -300, 520)
    mel.connect_material_expressions(mask, "R", opacity, "A")
    mel.connect_material_expressions(opacity_scale, "", opacity, "B")
    mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)

    mel.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    return material


def main():
    verify = "verify" in [a.lower() for a in sys.argv[1:]]
    content = os.path.abspath(unreal.Paths.project_content_dir())
    out_dir = os.path.join(content, "UI", "Map")

    if verify:
        ok = True
        for name in ("T_IslandMap", "T_MapRim", "T_BeamGrad", "T_MapGlow", "T_MapRing", "M_WrecktideBeacon"):
            path = "%s/%s" % (DEST, name)
            present = unreal.EditorAssetLibrary.does_asset_exist(path)
            log("verify %s = %s" % (path, "OK" if present else "MISSING"))
            ok = ok and present
        log("RESULT=%s (verify)" % ("OK" if ok else "FAIL"))
        return 0 if ok else 1

    if not os.path.isdir(out_dir):
        os.makedirs(out_dir)

    log("drawing island picture %dx%d, half extent %.0f uu" % (MAP_SIZE, MAP_SIZE, HALF_EXTENT))
    size = write_png(os.path.join(out_dir, "island_map.png"), MAP_SIZE, MAP_SIZE, build_island_map())
    log("island_map.png %d bytes" % size)
    write_png(os.path.join(out_dir, "map_rim.png"), RIM_SIZE, RIM_SIZE, build_rim())
    write_png(os.path.join(out_dir, "map_glow.png"), GLOW_SIZE, GLOW_SIZE, build_glow())
    write_png(os.path.join(out_dir, "map_ring.png"), RING_SIZE, RING_SIZE, build_ring())
    write_png(os.path.join(out_dir, "beam_grad.png"), GRAD_W, GRAD_H, build_grad())
    log("wrote 5 source pngs to %s" % out_dir)

    island = import_png(os.path.join(out_dir, "island_map.png"), "T_IslandMap",
                        unreal.TextureCompressionSettings.TC_DEFAULT,
                        unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP, True)
    rim = import_png(os.path.join(out_dir, "map_rim.png"), "T_MapRim",
                     unreal.TextureCompressionSettings.TC_EDITOR_ICON,
                     unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS, True)
    glow = import_png(os.path.join(out_dir, "map_glow.png"), "T_MapGlow",
                      unreal.TextureCompressionSettings.TC_DEFAULT,
                      unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP, True)
    ring = import_png(os.path.join(out_dir, "map_ring.png"), "T_MapRing",
                      unreal.TextureCompressionSettings.TC_DEFAULT,
                      unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP, True)
    grad = import_png(os.path.join(out_dir, "beam_grad.png"), "T_BeamGrad",
                      unreal.TextureCompressionSettings.TC_DEFAULT,
                      unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP, True)

    missing = [n for n, a in (("T_IslandMap", island), ("T_MapRim", rim), ("T_MapGlow", glow),
                              ("T_MapRing", ring), ("T_BeamGrad", grad)) if a is None]
    if missing:
        fatal("textures missing after import: %s" % ", ".join(missing))

    material = build_material(glow)
    if material is None:
        fatal("could not create %s" % MAT_PATH)
    log("created %s (unlit translucent two sided)" % MAT_PATH)

    log("mapping: centre (0,0) half extent %.0f, U=(Y+%.0f)/%.0f, V=(%.0f-X)/%.0f"
        % (HALF_EXTENT, HALF_EXTENT, 2.0 * HALF_EXTENT, HALF_EXTENT, 2.0 * HALF_EXTENT))
    log("RESULT=OK 5 textures + 1 material under %s" % DEST)
    return 0


main()
