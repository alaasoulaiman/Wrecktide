"""island2_build.py - GALLOWS REACH, the second island (2026-09-07, spec C34.1/C34.2/C34.4).

The same tier method Tools\\build_island_level.py used for Wreck Cove, applied to a NEW landmass
south-east of it, written into the SAME level (Lvl_Island is one open sea with islands in it, not a
level per island). Everything this script places is labelled ISLE2_* so a re-run can delete exactly
its own work and nothing else; island 1's actors and the DRESS_* set are never touched, with ONE
deliberate exception documented at REMOVE THE DECOR ISLAND below.

GEOMETRY (world uu, z axis; 40 uu steps stay under the CMC MaxStepHeight of 45)
    centre                  (12000, -16000)
    beach ring   9000x9000  X 7500..16500   Y -20500..-11500   top z = -40
    field        6000x6000  X 9000..15000   Y -19000..-13000   top z =   0
    garrison sq  2600x2600  centred (11400, -16400)            top z = +40
    lighthouse rock 1200x1200 centred (14200, -13800)          top z = +80
    lighthouse tower 800x800 centred (14200, -13800)           top z = +600  (the platform)
    stair: 16 cubes marching -X from x 13700 at 200 uu treads, tops 600 down to 0 in 40 uu drops,
           so the last tread is flush with the field and the first abuts the tower's west face.
    sea level z = -120 (unchanged; the sea volume from island_sea_volume.py is +-25600 about the
           origin and already contains this island and 2000 uu of water off every beach).

WHY HERE: the sail lane from Wreck Cove's pier at (-700, -8300) runs south-east over open water with
nothing in it, and the landing beach at (9500, -12700) is about 14,000 uu from the pier. The decor
island SM_Env_Background_Island_02 that island_dress.py put in this corner is REMOVED, because the
design says the far islands become the real ones.

NAV: the volume goes through unreal.WrecktideEditorTools.create_nav_bounds_volume, the ONLY working
headless path (a NavMeshBoundsVolume from spawn_actor_from_class carries an EMPTY brush and produces
no navmesh at all; that cost five weeks once). Label NavBounds_Island2; island_navbounds.py has been
taught to leave it alone.

Idempotent, fail closed, verify mode. Markers: WRECKTIDE-ISLE2.
Usage: tools\\run_py_args.cmd Tools\\island2_build.py [verify]
"""
import sys

import unreal

M = "WRECKTIDE-ISLE2"
MAP = "/Game/Lvl_Island"
CUBE = "/Engine/BasicShapes/Cube"
SAND_MI = "/Game/Materials/MI_IslandSand"
STONE_MI = "/Game/Materials/MI_GallowsStone"
PARENT_MAT = "/Engine/BasicShapes/BasicShapeMaterial"
STONE = unreal.LinearColor(0.42, 0.44, 0.47, 1.0)
SPAWNER_BP = "/Game/Variant_Combat/Blueprints/AI/BP_CombatEnemySpawner"
ENEMY_BP = "/Game/Variant_Combat/Blueprints/AI/BP_CombatEnemy"

# ---- the island, one place ---------------------------------------------------------------------
CENTRE = (12000.0, -16000.0)
BEACH_HALF = 4500.0
FIELD_HALF = 3000.0
SQUARE_CENTRE = (11400.0, -16400.0)
SQUARE_HALF = 1300.0
LIGHT_CENTRE = (14200.0, -13800.0)
ROCK_HALF = 600.0
TOWER_HALF = 400.0
TOWER_TOP = 600.0
STAIR_STEPS = 16
STAIR_TREAD = 200.0
STAIR_RISE = 40.0
STAIR_X0 = 13700.0          # centre of the top tread, abutting the tower's west face at 13800
STAIR_Y = -13800.0
STAIR_WIDTH = 700.0
BURY_Z = -140.0             # every cube is solid down to here: no floating steps, no gaps
LANDING = (9500.0, -12700.0)

# Enemy posts. One Enemies.csv row per post, the Wreck Cove pattern (EnemyRowOverride).
POSTS = (
    (10600.0, -17200.0, 140.0, "NavyMarine"),
    (12200.0, -17200.0, 140.0, "NavyMarine"),
    (10600.0, -15600.0, 140.0, "NavyMarine"),
    (13400.0, -17400.0, 100.0, "NavyMusketeer"),
    (12600.0, -14200.0, 100.0, "NavyMusketeer"),
    (10200.0, -13600.0, 100.0, "NavyGarrisonOfficer"),
    (11400.0, -16000.0, 140.0, "LieutenantHarrow"),   # the gallows post
)

# The powder magazine chest (Chests.csv row MagazineChest, mission R02_Magazine).
MAGAZINE_CHEST = (13500.0, -17600.0, 0.0)

NAV_EXTENT = (5000.0, 5000.0, 700.0)
NAV_LABEL = "NavBounds_Island2"

VERIFY_ONLY = any(a.lower() == "verify" for a in sys.argv[1:])
FAILED = []


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


eal = unreal.EditorAssetLibrary
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

COUNTS = {}


def label(actor, kind):
    n = COUNTS.get(kind, 0) + 1
    COUNTS[kind] = n
    actor.set_actor_label("ISLE2_%s_%02d" % (kind, n))
    actor.set_folder_path("GallowsReach")


def slab(cube, cx, cy, half_x, half_y, top_z, kind, material=None, bottom_z=BURY_Z):
    """A solid box of engine cube from bottom_z up to top_z. The engine cube is 100 uu with a CENTRE
    pivot and guaranteed simple box collision, which is why every walkable surface in this game is
    one of these and not a Synty mesh."""
    height = top_z - bottom_z
    if height <= 0.0:
        err("slab %s has no height (top %.0f bottom %.0f)" % (kind, top_z, bottom_z))
        return None
    a = eas.spawn_actor_from_class(unreal.StaticMeshActor, v(cx, cy, (top_z + bottom_z) * 0.5), r(0, 0, 0))
    if a is None:
        err("slab spawn failed %s" % kind)
        return None
    comp = a.get_editor_property("StaticMeshComponent")
    comp.set_editor_property("StaticMesh", cube)
    a.set_actor_scale3d(v(half_x * 2.0 / 100.0, half_y * 2.0 / 100.0, height / 100.0))
    if material is not None:
        comp.set_material(0, material)
    label(a, kind)
    return a


def ensure_material(path, name, colour):
    mi = eal.load_asset(path) if eal.does_asset_exist(path) else None
    if mi is not None:
        return mi
    parent = eal.load_asset(PARENT_MAT)
    if parent is None:
        fatal("parent material missing: %s" % PARENT_MAT)
    folder, short = path.rsplit("/", 1)
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
    if not les.load_level(MAP):
        fatal("load_level failed for %s" % MAP)

    cube = eal.load_asset(CUBE)
    spawner_bp = eal.load_asset(SPAWNER_BP)
    enemy_bp = eal.load_asset(ENEMY_BP)
    if not all((cube, spawner_bp, enemy_bp)):
        fatal("core asset load failed cube=%s spawner=%s enemy=%s" % (cube, spawner_bp, enemy_bp))

    existing = [a for a in eas.get_all_level_actors() if str(a.get_actor_label()).startswith("ISLE2_")]
    navs = [a for a in eas.get_all_level_actors()
            if isinstance(a, unreal.NavMeshBoundsVolume) and str(a.get_actor_label()).startswith(NAV_LABEL)]

    if VERIFY_ONLY:
        tools = unreal.WrecktideEditorTools
        nav_ok = any(tools.get_actor_bounds_size(a).x > NAV_EXTENT[0] * 1.9 for a in navs)
        posts = [a for a in existing if "_post_" in str(a.get_actor_label())]
        log("VERIFY isle2 actors=%d navvolumes=%d nav_bounds_ok=%s posts=%d"
            % (len(existing), len(navs), nav_ok, len(posts)))
        ok = len(existing) > 0 and nav_ok and len(posts) >= 5
        log("RESULT=%s (verify only, nothing written)" % ("OK" if ok else "FAIL"))
        raise SystemExit(0 if ok else 1)

    # ------------------------------------------------------------ idempotency
    for a in existing:
        eas.destroy_actor(a)
    log("cleared %d actor(s) from a previous run" % len(existing))
    for a in navs:
        eas.destroy_actor(a)
    log("cleared %d previous %s volume(s)" % (len(navs), NAV_LABEL))

    # ------------------------------------------------------------ REMOVE THE DECOR ISLAND
    # island_dress.py scattered five SM_Env_Background_Island_* props on the horizon; one of them
    # stands where Gallows Reach now is. The design says the far islands become the real ones, so
    # the decor goes. Judged by POSITION, not by the mesh name in the dress script, because that
    # script derives its spots from the live water zone extent and can land anywhere in this corner.
    removed_decor = 0
    for a in list(eas.get_all_level_actors()):
        if not str(a.get_actor_label()).startswith("DRESS_farisland"):
            continue
        loc = a.get_actor_location()
        if abs(loc.x - CENTRE[0]) <= 9000.0 and abs(loc.y - CENTRE[1]) <= 9000.0:
            log("removing decor island %s at (%.0f, %.0f)" % (a.get_actor_label(), loc.x, loc.y))
            if eas.destroy_actor(a):
                removed_decor += 1
    log("decor islands removed from the Gallows Reach corner: %d" % removed_decor)

    sand = eal.load_asset(SAND_MI)
    if sand is None:
        err("sand material missing at %s; tiers keep the engine checker" % SAND_MI)
    stone = ensure_material(STONE_MI, "MI_GallowsStone", STONE)

    # ------------------------------------------------------------ landmass
    cx, cy = CENTRE
    slab(cube, cx, cy, BEACH_HALF, BEACH_HALF, -40.0, "beach", sand)
    slab(cube, cx, cy, FIELD_HALF, FIELD_HALF, 0.0, "field", sand)
    slab(cube, SQUARE_CENTRE[0], SQUARE_CENTRE[1], SQUARE_HALF, SQUARE_HALF, 40.0, "square", sand)
    slab(cube, LIGHT_CENTRE[0], LIGHT_CENTRE[1], ROCK_HALF, ROCK_HALF, 80.0, "rock", stone)
    slab(cube, LIGHT_CENTRE[0], LIGHT_CENTRE[1], TOWER_HALF, TOWER_HALF, TOWER_TOP, "tower", stone)

    # the stair: tread 0 is the top one, flush with the tower platform; tread 15 is flush with the field
    for i in range(STAIR_STEPS):
        top = TOWER_TOP - STAIR_RISE * i
        step_x = STAIR_X0 - STAIR_TREAD * i
        slab(cube, step_x, STAIR_Y, STAIR_TREAD * 0.5, STAIR_WIDTH * 0.5, top, "stair", stone)
    log("stair: %d treads, %.0f uu rise each, top %.0f at x %.0f down to %.0f at x %.0f"
        % (STAIR_STEPS, STAIR_RISE, TOWER_TOP, STAIR_X0, TOWER_TOP - STAIR_RISE * (STAIR_STEPS - 1),
           STAIR_X0 - STAIR_TREAD * (STAIR_STEPS - 1)))

    # ------------------------------------------------------------ enemy posts
    spawner_cls = spawner_bp.generated_class()
    enemy_cls = enemy_bp.generated_class()
    for (px, py, pz, row) in POSTS:
        s = eas.spawn_actor_from_class(spawner_cls, v(px, py, pz), r(0, 0, 0))
        if s is None:
            err("spawner spawn failed at (%.0f, %.0f) row=%s" % (px, py, row))
            continue
        s.set_editor_property("EnemyClass", enemy_cls)
        s.set_editor_property("bShouldSpawnEnemiesImmediately", True)
        s.set_editor_property("SpawnCount", 1)
        s.set_editor_property("EnemyRowOverride", row)
        back = str(s.get_editor_property("EnemyRowOverride"))
        if back != row:
            err("post readback %s != %s" % (back, row))
        label(s, "post")
    log("enemy posts placed: %d" % COUNTS.get("post", 0))

    # ------------------------------------------------------------ the powder magazine chest
    chest_cls = None
    try:
        chest_cls = unreal.load_class(None, "/Script/Wrecktide.WrecktideTreasureChest")
    except Exception as e:  # noqa: BLE001 - a missing class must be reported, never silently skipped
        err("treasure chest class unavailable: %r" % e)
    if chest_cls is not None:
        c = eas.spawn_actor_from_class(chest_cls, v(*MAGAZINE_CHEST), r(0, -90, 0))
        if c is None:
            err("magazine chest spawn failed")
        else:
            c.set_editor_property("ChestId", "MagazineChest")
            c.set_editor_property("GoldAmount", 120)
            label(c, "chest")

    # ------------------------------------------------------------ navigation
    tools = unreal.WrecktideEditorTools
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    vol = tools.create_nav_bounds_volume(world, v(cx, cy, 0.0), v(*NAV_EXTENT))
    if vol is None:
        fatal("create_nav_bounds_volume returned None")
    size = tools.get_actor_bounds_size(vol)
    log("nav volume bounds size=(%.0f, %.0f, %.0f)" % (size.x, size.y, size.z))
    if size.x < NAV_EXTENT[0] * 1.9 or size.z < NAV_EXTENT[2] * 1.9:
        fatal("nav volume brush is empty or too small: (%.0f, %.0f, %.0f)" % (size.x, size.y, size.z))
    vol.set_actor_label(NAV_LABEL)

    spent = tools.build_navigation_synchronously(world, 300.0)
    log("navigation rebuilt headless in %.1fs" % spent)
    if spent < 0.0:
        fatal("navigation build unavailable")

    if not les.save_current_level():
        fatal("save_current_level failed")
    log("landing beach at (%.0f, %.0f); spawned %s" % (LANDING[0], LANDING[1], COUNTS))

    if FAILED:
        unreal.log_error("%s %d non-fatal issue(s): %s" % (M, len(FAILED), FAILED))
        unreal.log_error("%s RESULT=PARTIAL (level saved; review issues)" % M)
        raise SystemExit(1)
    log("RESULT=OK Gallows Reach built + nav + level saved")


try:
    main()
except SystemExit:
    raise
except Exception as e:  # noqa: BLE001
    unreal.log_error("%s unhandled exception: %r" % (M, e))
    unreal.log_error("%s RESULT=FAIL (exception)" % M)
    raise SystemExit(1)
