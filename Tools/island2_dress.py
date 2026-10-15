"""island2_dress.py - dress GALLOWS REACH with the Synty POLYGON Pirate pack (2026-09-07, spec C34.3).

The sibling of Tools\\island_dress.py, aimed at the SECOND island only. Surgical and idempotent:
it deletes every actor labelled DRESS2_* from a previous run and places the set again from real
mesh bounds, then saves the level. Nothing on Wreck Cove is touched, and nothing built by
Tools\\island2_build.py (labels ISLE2_*) is touched either: the tiers, the stair, the enemy posts,
the magazine chest and the nav volume all survive a re-run of this script.

The DRESS_ prefix on island 1's actors and DRESS2_ here do not collide: island_dress.py clears
labels starting with "DRESS_" (with the underscore), so a Gallows Reach prop is invisible to it.

Layout it relies on (Tools\\island2_build.py, whose header is the authority):
    beach ring top -40, field top 0, garrison square 2600x2600 at (11400,-16400) top +40,
    lighthouse rock top +80 and tower top +600 at (14200,-13800), stair treads along y -13800,
    boss post at (11400,-16000), magazine chest at (13500,-17600), Ada at (12400,-15400),
    the second bounty board at (10400,-15400), landing beach at (9500,-12700).

RESERVED SPOTS, keep them clear of props: -Island2Probe traces WorldStatic straight down at
(13800, -20000) beach, (13000, -14500) field, (11400, -16800) square and (14350, -13950) lighthouse
platform, and asserts the tier height it finds there. A prop standing on one of those makes the
probe report the prop's roof; Tools\\island2_probe_spots.py lists what is near each of them.

Sets: LANDING PIER (walkable cube deck out over the water on the side facing Wreck Cove, rails and
lanterns), GARRISON SQUARE (the gallows and its trapdoor, a flag pole, barracks tents, fort wall
runs, lanterns, cannon), POWDER MAGAZINE (a hut, crates, barrels and cannonballs around the chest),
LIGHTHOUSE (a lantern and torches on the +600 platform, a rail of posts around it), and beach
scenery. Walkable surfaces are engine cubes (guaranteed collision); Synty meshes that report zero
simple collision get a hidden blocker cube when they must block.

Markers: WRECKTIDE-DRESS2. Usage: tools\\run_py_args.cmd Tools\\island2_dress.py [verify]
"""
import math
import sys

import unreal

M = "WRECKTIDE-DRESS2"
MAP = "/Game/Lvl_Island"
CUBE = "/Engine/BasicShapes/Cube"
PARENT_MAT = "/Engine/BasicShapes/BasicShapeMaterial"
MI_WOOD = "/Game/Materials/MI_DockWood"
MI_STONE = "/Game/Materials/MI_GallowsStone"
MI_NAVY = "/Game/Materials/MI_NavyBlue"
NAVY = unreal.LinearColor(0.10, 0.16, 0.38, 1.0)
ROOT = "/Game/PolygonPirates/Meshes/"
VERIFY_ONLY = any(a.lower() == "verify" for a in sys.argv[1:])
FAILED = []

# ---- the island, mirrored from Tools\island2_build.py ------------------------------------------
SQUARE_CENTRE = (11400.0, -16400.0)
LIGHT_CENTRE = (14200.0, -13800.0)
TOWER_TOP = 600.0
MAGAZINE = (13500.0, -17600.0)
LANDING = (9500.0, -12700.0)
PIER_X_HALF = 200.0
PIER_Y = (-13100.0, -11300.0)


def log(msg):
    unreal.log_warning("%s %s" % (M, msg))


def err(msg):
    FAILED.append(msg)
    unreal.log_error("%s %s" % (M, msg))


def fatal(msg):
    unreal.log_error("%s FATAL %s" % (M, msg))
    unreal.log_error("%s RESULT=FAIL (nothing saved)" % M)
    raise SystemExit(1)


def v(x, y, z):
    return unreal.Vector(x, y, z)


def r(pitch, yaw, roll):
    return unreal.Rotator(pitch=pitch, yaw=yaw, roll=roll)


def ground(x, y):
    """Top surface z of Gallows Reach at (x, y). Same shape as island_dress.py's helper, one
    island over, and it knows about the lighthouse stair so a prop cannot be buried in a tread."""
    if 13800.0 <= x <= 14600.0 and -14200.0 <= y <= -13400.0:
        return TOWER_TOP
    if 10600.0 <= x <= 13800.0 and -14150.0 <= y <= -13450.0:
        step = int((13800.0 - x) // 200.0)
        step = max(0, min(15, step))
        return TOWER_TOP - 40.0 * step
    if 13600.0 <= x <= 14800.0 and -14400.0 <= y <= -13200.0:
        return 80.0
    if 10100.0 <= x <= 12700.0 and -17700.0 <= y <= -15100.0:
        return 40.0
    if 9000.0 <= x <= 15000.0 and -19000.0 <= y <= -13000.0:
        return 0.0
    if 7500.0 <= x <= 16500.0 and -20500.0 <= y <= -11500.0:
        return -40.0
    return -120.0


eal = unreal.EditorAssetLibrary
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
try:
    sme = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
except Exception:  # noqa: BLE001
    sme = None

MESH_CACHE = {}
PRIM_CACHE = {}
COUNTS = {}
CUBE_ASSET = None


def mesh(rel):
    if rel in MESH_CACHE:
        return MESH_CACHE[rel]
    asset = eal.load_asset(ROOT + rel)
    if asset is None:
        err("mesh missing: %s" % rel)
    MESH_CACHE[rel] = asset
    return asset


def prims(asset):
    key = asset.get_path_name()
    if key in PRIM_CACHE:
        return PRIM_CACHE[key]
    count = -1
    if sme is not None:
        try:
            count = int(sme.get_simple_collision_count(asset)) + int(sme.get_convex_collision_count(asset))
        except Exception:  # noqa: BLE001
            count = -1
    PRIM_CACHE[key] = count
    return count


def rot2d(x, y, yaw_deg):
    a = math.radians(yaw_deg)
    return x * math.cos(a) - y * math.sin(a), x * math.sin(a) + y * math.cos(a)


def label(actor, kind):
    n = COUNTS.get(kind, 0) + 1
    COUNTS[kind] = n
    actor.set_actor_label("DRESS2_%s_%02d" % (kind, n))
    actor.set_folder_path("GallowsReach/Dress")


def place_cube(loc, scale, yaw=0.0, kind="cube", hidden=False, material=None):
    a = eas.spawn_actor_from_class(unreal.StaticMeshActor, loc, r(0, yaw, 0))
    if a is None:
        err("cube spawn failed %s" % kind)
        return None
    comp = a.get_editor_property("StaticMeshComponent")
    comp.set_editor_property("StaticMesh", CUBE_ASSET)
    a.set_actor_scale3d(scale)
    if hidden:
        a.set_actor_hidden_in_game(True)
    if material is not None:
        comp.set_material(0, material)
    label(a, kind)
    return a


def slab(cx, cy, half_x, half_y, top_z, bottom_z, kind, material=None):
    """A solid walkable box, the only kind of surface the pawn is ever asked to stand on."""
    height = top_z - bottom_z
    if height <= 0.0:
        err("slab %s has no height" % kind)
        return None
    return place_cube(v(cx, cy, (top_z + bottom_z) * 0.5),
                      v(half_x * 2.0 / 100.0, half_y * 2.0 / 100.0, height / 100.0),
                      kind=kind, material=material)


def place(rel, x, y, yaw=0.0, scale=1.0, z=None, sink=0.0, kind=None, block=True, roll=0.0, pitch=0.0):
    """Place a Synty mesh with its bounds bottom on the ground (or at explicit z). block=True keeps
    the mesh's own collision and adds a hidden blocker cube when the mesh reports zero prims;
    block=False makes it pure scenery (no collision, no navmesh obstacle)."""
    asset = mesh(rel)
    if asset is None:
        return None
    box = asset.get_bounding_box()
    if z is None:
        z = ground(x, y) - box.min.z * scale + sink
    a = eas.spawn_actor_from_class(unreal.StaticMeshActor, v(x, y, z), r(pitch, yaw, roll))
    if a is None:
        err("spawn failed %s" % rel)
        return None
    comp = a.get_editor_property("StaticMeshComponent")
    comp.set_editor_property("StaticMesh", asset)
    a.set_actor_scale3d(v(scale, scale, scale))
    kind = kind or rel.split("/")[-1].replace("SM_", "")[:18]
    label(a, kind)
    if not block:
        comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    elif prims(asset) == 0 and roll == 0.0 and pitch == 0.0:
        cx = (box.min.x + box.max.x) * 0.5 * scale
        cy = (box.min.y + box.max.y) * 0.5 * scale
        sx = max((box.max.x - box.min.x) * scale, 50.0)
        sy = max((box.max.y - box.min.y) * scale, 50.0)
        sz = min((box.max.z - box.min.z) * scale, 400.0)
        ox, oy = rot2d(cx, cy, yaw)
        place_cube(v(x + ox, y + oy, z + box.min.z * scale + sz * 0.5),
                   v(sx / 100.0, sy / 100.0, sz / 100.0), yaw=yaw, kind="blocker", hidden=True)
    return a


def ensure_material(path, short, colour):
    mi = eal.load_asset(path) if eal.does_asset_exist(path) else None
    if mi is not None:
        return mi
    parent = eal.load_asset(PARENT_MAT)
    if parent is None:
        fatal("parent material missing: %s" % PARENT_MAT)
    folder = path.rsplit("/", 1)[0]
    mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        short, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    if mi is None:
        fatal("create_asset failed for %s" % path)
    unreal.MaterialEditingLibrary.set_material_instance_parent(mi, parent)
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mi, "Color", colour)
    unreal.MaterialEditingLibrary.update_material_instance(mi)
    if not eal.save_asset(path, only_if_is_dirty=False):
        fatal("save failed for %s" % path)
    log("created %s" % path)
    return mi


def main():
    global CUBE_ASSET
    if not les.load_level(MAP):
        fatal("load_level failed for %s" % MAP)
    CUBE_ASSET = eal.load_asset(CUBE)
    if CUBE_ASSET is None:
        fatal("engine cube missing")

    old = [a for a in eas.get_all_level_actors() if str(a.get_actor_label()).startswith("DRESS2_")]
    log("previous Gallows Reach dress actors: %d" % len(old))
    if VERIFY_ONLY:
        log("RESULT=%s dress2_actors=%d" % ("OK" if old else "FAIL", len(old)))
        raise SystemExit(0 if old else 1)
    for a in old:
        eas.destroy_actor(a)

    wood = ensure_material(MI_WOOD, "MI_DockWood", unreal.LinearColor(0.36, 0.23, 0.12, 1.0))
    stone = ensure_material(MI_STONE, "MI_GallowsStone", unreal.LinearColor(0.42, 0.44, 0.47, 1.0))
    navy = ensure_material(MI_NAVY, "MI_NavyBlue", NAVY)

    # ------------------------------------------------------------ LANDING PIER (faces Wreck Cove)
    # The deck top sits at -40, flush with the beach ring, and runs out over the water so a boat
    # can come alongside. Engine cubes only: this is a surface the player walks on.
    deck_mid_y = (PIER_Y[0] + PIER_Y[1]) * 0.5
    deck_half_y = (PIER_Y[1] - PIER_Y[0]) * 0.5
    slab(LANDING[0], deck_mid_y, PIER_X_HALF, deck_half_y, -40.0, -160.0, "pierdeck", wood)
    rail_y = PIER_Y[0] + 150.0
    while rail_y <= PIER_Y[1]:
        for side in (-1.0, 1.0):
            place_cube(v(LANDING[0] + side * (PIER_X_HALF - 20.0), rail_y, 30.0),
                       v(0.35, 0.35, 1.4), kind="pierpost", material=wood)
        rail_y += 300.0
    for lamp_y in (PIER_Y[0] + 100.0, PIER_Y[1] - 100.0):
        place("Props/SM_Prop_Lantern_01", LANDING[0] - PIER_X_HALF + 60.0, lamp_y, z=-40.0, kind="pierlamp", block=False)
    place("Props/SM_Prop_Anchor_01", LANDING[0] + 520.0, PIER_Y[0] - 260.0, yaw=40.0, kind="anchor")
    place("Props/SM_Prop_Crate_01", LANDING[0] - 480.0, PIER_Y[0] - 180.0, yaw=15.0, kind="crate")
    place("Props/SM_Prop_Barrel_01", LANDING[0] - 560.0, PIER_Y[0] - 420.0, yaw=70.0, kind="barrel")

    # ------------------------------------------------------------ GARRISON SQUARE
    sx, sy = SQUARE_CENTRE
    # the gallows the island is named for, at the square's heart with the boss post 400 uu north
    place("Props/SM_Prop_Gallows_01", sx, sy, yaw=-90.0, kind="gallows")
    place("Props/SM_Prop_Gallows_Trapdoor_01", sx, sy, yaw=-90.0, kind="gallowstrap", block=False)
    place("Props/SM_Prop_Gallows_02", sx - 700.0, sy - 500.0, yaw=-60.0, kind="gallows")

    # the flag: a pole with a navy-blue cloth cube near its top, since the pack ships no flag mesh.
    # The cloth height comes from the pole's REAL bounds, not a guessed 520 uu, or the flag hangs in
    # mid air the first time Synty ships a different pole.
    pole = place("Props/SM_Prop_Pole_01", sx + 900.0, sy - 200.0, kind="flagpole")
    pole_mesh = mesh("Props/SM_Prop_Pole_01")
    if pole is not None and pole_mesh is not None:
        pole_box = pole_mesh.get_bounding_box()
        pole_height = float(pole_box.max.z - pole_box.min.z)
        flag_z = ground(sx + 900.0, sy - 200.0) + pole_height * 0.80
        place_cube(v(sx + 900.0, sy - 60.0, flag_z), v(0.12, 2.6, 1.6), kind="flag", material=navy)
        log("flag pole height %.0f uu, cloth at z %.0f" % (pole_height, flag_z))

    # barracks along the square's west edge
    for i, (bx, by, byaw) in enumerate((
        (sx - 1000.0, sy + 700.0, 90.0),
        (sx - 1000.0, sy - 100.0, 90.0),
        (sx - 1000.0, sy - 900.0, 90.0),
    )):
        place("Buildings/SM_Bld_Tent_0%d" % (i % 3 + 1), bx, by, yaw=byaw, kind="barracks")
    place("Props/SM_Prop_Campfire_01", sx - 600.0, sy + 300.0, kind="campfire", block=False)
    place("Props/SM_Prop_Table_01", sx - 620.0, sy - 500.0, yaw=20.0, kind="table")
    place("Props/SM_Prop_Stool_01", sx - 480.0, sy - 520.0, yaw=200.0, kind="stool")

    # A parapet along the square's south edge, and a cannon looking out to sea.
    # SCALE 0.35 IS DELIBERATE: SM_Bld_Fort_Wall_01 is a full castle curtain wall, roughly 2,000 uu
    # tall and 900 wide. Four at scale 1 made a black slab that filled the frame from the landing
    # beach and hid the whole square (_logs\isle2_landing_view1.png, first dressing pass). At 0.35
    # it reads as the low garrison wall the square wants, and it stops being a navmesh wall too.
    for i in range(5):
        place("Buildings/SM_Bld_Fort_Wall_01", sx - 900.0 + i * 450.0, sy - 1220.0, yaw=0.0,
              scale=0.35, kind="fortwall")
    place("Props/SM_Prop_Cannon_01", sx + 300.0, sy - 1100.0, yaw=-90.0, kind="cannon")
    place("Props/SM_Prop_CannonBalls_01", sx + 480.0, sy - 1080.0, kind="cannonballs", block=False)

    # lanterns on the four corners so the square reads at night
    for (lx, ly) in ((sx - 1150.0, sy + 1150.0), (sx + 1150.0, sy + 1150.0),
                     (sx - 1150.0, sy - 1150.0), (sx + 1150.0, sy - 1150.0)):
        place("Props/SM_Prop_BottleTorch_01", lx, ly, kind="squaretorch", block=False)

    # ------------------------------------------------------------ POWDER MAGAZINE (the R02 chest)
    mx, my = MAGAZINE
    place("Buildings/SM_Bld_Shanty_Room_01", mx + 350.0, my - 150.0, yaw=180.0, kind="magazine")
    for i, (px, py, pyaw) in enumerate((
        (mx - 260.0, my + 120.0, 10.0), (mx - 200.0, my - 240.0, 75.0),
        (mx + 120.0, my + 300.0, 200.0), (mx - 420.0, my - 60.0, 130.0),
    )):
        place("Props/SM_Prop_Crate_0%d" % (i % 4 + 1), px, py, yaw=pyaw, kind="powdercrate")
    for i, (px, py) in enumerate(((mx + 260.0, my + 180.0), (mx + 320.0, my - 320.0), (mx - 120.0, my + 420.0))):
        place("Props/SM_Prop_Barrel_0%d" % (i % 3 + 1), px, py, yaw=i * 47.0, kind="powderbarrel")
    place("Props/SM_Prop_Crate_Cannonballs_01", mx + 520.0, my + 40.0, yaw=25.0, kind="shotcrate")
    place("Props/SM_Prop_BottleTorch_01", mx - 520.0, my + 380.0, kind="magazinetorch", block=False)

    # ------------------------------------------------------------ LIGHTHOUSE (the R03 spot)
    lx, ly = LIGHT_CENTRE
    # a rail of short posts around the platform edge so the top reads as a place, not a cube
    for i in range(12):
        ang = i * 30.0
        ox, oy = rot2d(340.0, 0.0, ang)
        place_cube(v(lx + ox, ly + oy, TOWER_TOP + 70.0), v(0.3, 0.3, 1.4), kind="lightrail", material=stone)
    place("Props/SM_Prop_Lantern_02", lx, ly, z=TOWER_TOP, scale=2.2, kind="lightlamp", block=False)
    place("Props/SM_Prop_BottleTorch_01", lx + 240.0, ly + 240.0, z=TOWER_TOP, kind="lighttorch", block=False)
    place("Props/SM_Prop_BottleTorch_01", lx - 240.0, ly - 240.0, z=TOWER_TOP, kind="lighttorch", block=False)
    place("Props/SM_Prop_Crate_02", lx - 900.0, ly + 700.0, yaw=30.0, kind="lightcrate")

    # ------------------------------------------------------------ scenery on the beach ring
    palms = ("Environments/SM_Env_PalmTree_01", "Environments/SM_Env_PalmTree_03",
             "Environments/SM_Env_PalmTree_Tall_01", "Environments/SM_Env_PalmTree_04")
    for i, (px, py) in enumerate((
        (8300.0, -18800.0), (8600.0, -14200.0), (15600.0, -18600.0), (15800.0, -13400.0),
        (11000.0, -20000.0), (13500.0, -12200.0), (8200.0, -16500.0), (16000.0, -16000.0),
    )):
        place(palms[i % len(palms)], px, py, yaw=i * 41.0, block=False, kind="palm")
    rocks = ("Environments/SM_Env_Rock_Large_01", "Environments/SM_Env_Rock_Large_03",
             "Environments/SM_Env_Rocks_01", "Environments/SM_Env_Rock_02")
    for i, (px, py) in enumerate((
        (7900.0, -19600.0), (16200.0, -12400.0), (7800.0, -12600.0), (16100.0, -19800.0),
        (12000.0, -20200.0),
    )):
        place(rocks[i % len(rocks)], px, py, yaw=i * 67.0, scale=1.4, kind="rock")
    bushes = ("Environments/SM_Env_Bush_01", "Environments/SM_Env_Fern_01",
              "Environments/SM_Env_GrassPatch_01", "Environments/SM_Env_Plants_01")
    for i in range(18):
        ang = i * 137.5
        rad = 1500.0 + (i * 191) % 1500
        ox, oy = rot2d(rad, 0.0, ang)
        bx, by = 12000.0 + ox, -16000.0 + oy
        if 10100.0 <= bx <= 12700.0 and -17700.0 <= by <= -15100.0:
            continue  # keep the square's flagstones clear
        place(bushes[i % len(bushes)], bx, by, yaw=ang, block=False, kind="bush")

    # The props BLOCK, so they are navigation geometry. The level ships an editor-built navmesh, so
    # without this rebuild the saved navmesh would still be the bare-island one and the garrison
    # would path straight through the gallows and the tents.
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    spent = unreal.WrecktideEditorTools.build_navigation_synchronously(world, 300.0)
    log("navigation rebuilt headless in %.1fs" % spent)
    if spent < 0.0:
        fatal("navigation build unavailable")

    if not les.save_current_level():
        fatal("save_current_level failed")
    log("placed %s" % COUNTS)

    if FAILED:
        unreal.log_error("%s %d non-fatal issue(s): %s" % (M, len(FAILED), FAILED))
        unreal.log_error("%s RESULT=PARTIAL (level saved; review issues)" % M)
        raise SystemExit(1)
    log("RESULT=OK Gallows Reach dressed + level saved")


try:
    main()
except SystemExit:
    raise
except Exception as e:  # noqa: BLE001
    unreal.log_error("%s unhandled exception: %r" % (M, e))
    unreal.log_error("%s RESULT=FAIL (exception)" % M)
    raise SystemExit(1)
