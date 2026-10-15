"""island_dress.py - dress Lvl_Island with the Synty POLYGON Pirate pack (2026-09-04).

Surgical and idempotent: loads the island, deletes every actor labelled DRESS_* from a previous run,
then places the set again from real mesh bounds (Tools\dress_bounds.py numbers), saves the level.
Nothing built by build_island_level.py / island_sand.py / island_enemy_variety.py is touched except
the SOUTH surf wall, which is split once to open a gap for the walkable pier (detected on re-runs).

Layout it relies on (build_island_level.py): field 10000x10000 top z=0, beach ring to +-7000 top
z=-40, hill 3000x3000 at (2600,2600) top z=+40, water z=-120, PlayerStart (0,-5200) facing +Y,
shop stall at (-400,-5200), bounty board at (+400,-5200), spawners at (+-2800,0) and (0,+-2800).

Sets: PIER (walkable cube deck + rails, moored medium ship, lanterns), CAMP at the spawn (tavern,
tents, campfire, table, barrels, torches, flag pole, well, beached rowboat, cannons), PIRATE STASH
at +X, SKELETON GRAVEYARD at -X, NAVY POST at +Y, HILL FORT with a SkeletonCaptain spawner,
SHIPWRECK on the east beach, far background islands, clouds, extra palms/bushes/rocks, ambient sound.
Walkable surfaces are engine cubes (guaranteed collision); Synty meshes that report zero simple
collision get a hidden blocker cube when they must block. Markers: WRECKTIDE-DRESS.
Usage: run_py.cmd <this file> [verify]
"""
import math
import sys
import unreal

M = "WRECKTIDE-DRESS"
MAP = "/Game/Lvl_Island"
CUBE = "/Engine/BasicShapes/Cube"
PARENT_MAT = "/Engine/BasicShapes/BasicShapeMaterial"
MI_WOOD = "/Game/Materials/MI_DockWood"
WOOD = unreal.LinearColor(0.36, 0.23, 0.12, 1.0)
ROOT = "/Game/PolygonPirates/Meshes/"
SPAWNER_BP = "/Game/Variant_Combat/Blueprints/AI/BP_CombatEnemySpawner"
ENEMY_BP = "/Game/Variant_Combat/Blueprints/AI/BP_CombatEnemy"
AMBIENT = "/Game/Audio/Ambient/BackgroundMix"
BOSS_ROW = "SkeletonCaptain"
BOSS_SPAWNER_LOC = (2000.0, 1800.0, 140.0)
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


def ground(x, y):
    """Top surface z of the island tiers at (x, y)."""
    if 1100.0 <= x <= 4100.0 and 1100.0 <= y <= 4100.0:
        return 40.0
    if abs(x) <= 5000.0 and abs(y) <= 5000.0:
        return 0.0
    if abs(x) <= 7000.0 and abs(y) <= 7000.0:
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


spawned = {}


def label(actor, kind):
    n = spawned.get(kind, 0) + 1
    spawned[kind] = n
    actor.set_actor_label("DRESS_%s_%02d" % (kind, n))
    actor.set_folder_path("Dress")


def place_cube(cube, loc, scale, yaw=0.0, kind="cube", hidden=False, material=None):
    a = eas.spawn_actor_from_class(unreal.StaticMeshActor, loc, r(0, yaw, 0))
    if a is None:
        err("cube spawn failed %s" % kind)
        return None
    comp = a.get_editor_property("StaticMeshComponent")
    comp.set_editor_property("StaticMesh", cube)
    a.set_actor_scale3d(scale)
    if hidden:
        a.set_actor_hidden_in_game(True)
    if material is not None:
        comp.set_material(0, material)
    label(a, kind)
    return a


CUBE_ASSET = None


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
        # no simple collision on the mesh: a hidden cube the size of its footprint blocks instead
        cx = (box.min.x + box.max.x) * 0.5 * scale
        cy = (box.min.y + box.max.y) * 0.5 * scale
        sx = max((box.max.x - box.min.x) * scale, 50.0)
        sy = max((box.max.y - box.min.y) * scale, 50.0)
        sz = min((box.max.z - box.min.z) * scale, 400.0)
        ox, oy = rot2d(cx, cy, yaw)
        place_cube(CUBE_ASSET, v(x + ox, y + oy, z + box.min.z * scale + sz * 0.5), v(sx / 100.0, sy / 100.0, sz / 100.0),
                   yaw=yaw, kind="blocker", hidden=True)
    return a


def main():
    if not les.load_level(MAP):
        fatal("load_level failed for %s" % MAP)
    global CUBE_ASSET
    cube = eal.load_asset(CUBE)
    if cube is None:
        fatal("engine cube missing")
    CUBE_ASSET = cube

    # ------------------------------------------------------------ idempotency: clear the last run
    # the surf-wall segments are structural (they close the beach edge): never recycle them
    old = [a for a in eas.get_all_level_actors()
           if str(a.get_actor_label()).startswith("DRESS_") and not str(a.get_actor_label()).startswith("DRESS_surfwall")]
    log("previous dress actors: %d" % len(old))
    if VERIFY_ONLY:
        log("RESULT=VERIFY-ONLY dress_actors=%d" % len(old))
        raise SystemExit(0)
    for a in old:
        eas.destroy_actor(a)

    # ------------------------------------------------------------ wood material for the pier
    wood = eal.load_asset(MI_WOOD) if eal.does_asset_exist(MI_WOOD) else None
    if wood is None:
        parent = eal.load_asset(PARENT_MAT)
        if parent is None:
            fatal("parent material missing")
        tools = unreal.AssetToolsHelpers.get_asset_tools()
        wood = tools.create_asset("MI_DockWood", "/Game/Materials", unreal.MaterialInstanceConstant,
                                  unreal.MaterialInstanceConstantFactoryNew())
        if wood is None:
            fatal("create_asset failed for %s" % MI_WOOD)
        unreal.MaterialEditingLibrary.set_material_instance_parent(wood, parent)
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(wood, "Color", WOOD)
        unreal.MaterialEditingLibrary.update_material_instance(wood)
        if not eal.save_asset(MI_WOOD, only_if_is_dirty=False):
            fatal("save failed for %s" % MI_WOOD)
        log("created %s" % MI_WOOD)

    # ------------------------------------------------------------ south surf wall: open the pier gap
    walls = []
    for a in eas.get_all_level_actors():
        if not isinstance(a, unreal.StaticMeshActor):
            continue
        comp = a.static_mesh_component
        m = comp.static_mesh if comp else None
        if m is None or m.get_path_name().split(".")[0] != CUBE:
            continue
        loc = a.get_actor_location()
        sc = a.get_actor_scale3d()
        if abs(loc.y + 7050.0) < 5.0 and sc.z > 5.0:
            walls.append(a)
    west = [w for w in walls if w.get_actor_location().x < -1000.0]
    east = [w for w in walls if w.get_actor_location().x > 1000.0]
    slivers = [w for w in walls if abs(w.get_actor_location().x) <= 500.0 and w.get_actor_scale3d().x < 2.0]
    whole = [w for w in walls if abs(w.get_actor_location().x) <= 100.0 and w.get_actor_scale3d().x > 100.0]

    def place_east_and_slivers():
        e = place_cube(cube, v(3700.0, -7050.0, 200.0), v(66.0, 1.0, 6.0), kind="surfwall", hidden=True)
        # close the slivers beside the deck so the gap is exactly the pier
        place_cube(cube, v(-355.0, -7050.0, 200.0), v(0.9, 1.0, 6.0), kind="surfwall", hidden=True)
        place_cube(cube, v(355.0, -7050.0, 200.0), v(0.9, 1.0, 6.0), kind="surfwall", hidden=True)
        return e is not None

    if west and east and len(slivers) >= 2:
        log("south surf wall already split (west, east, %d slivers)" % len(slivers))
    elif len(whole) == 1 and not west and not east:
        w = whole[0]
        w.modify()
        w.set_actor_location(v(-3700.0, -7050.0, 200.0), False, False)
        w.set_actor_scale3d(v(66.0, 1.0, 6.0))
        ok = place_east_and_slivers()
        log("south surf wall split: west segment moved, east segment %s" % ("placed" if ok else "FAILED"))
    elif west and not east:
        # an earlier run recycled the east segment: restore it (the west half is the moved original)
        for sl in slivers:
            eas.destroy_actor(sl)
        ok = place_east_and_slivers()
        log("south surf wall east segment restored: %s" % ("placed" if ok else "FAILED"))
    else:
        err("south surf wall in an unexpected state (walls=%d west=%d east=%d slivers=%d whole=%d), pier gap NOT touched"
            % (len(walls), len(west), len(east), len(slivers), len(whole)))

    # ------------------------------------------------------------ PIER (walkable, engine cubes)
    deck_top = -40.0
    place_cube(cube, v(0.0, -8000.0, deck_top - 20.0), v(6.0, 22.0, 0.4), kind="pierdeck", material=wood)
    for x in (-310.0, 310.0):
        place_cube(cube, v(x, -8000.0, deck_top + 55.0), v(0.2, 22.2, 1.1), kind="pierrail", material=wood)
    place_cube(cube, v(0.0, -9110.0, deck_top + 55.0), v(6.4, 0.2, 1.1), kind="pierrail", material=wood)
    for y in (-7500.0, -8300.0, -9050.0):
        for x in (-300.0, 300.0):
            place("Props/SM_Prop_Lantern_01", x, y, z=deck_top + 110.0, block=False, kind="lantern")
    place("Props/SM_Prop_Barrel_01", 200.0, -7300.0, z=deck_top, block=False, kind="pierprop")
    place("Props/SM_Prop_Crate_01", 120.0, -7300.0, yaw=20.0, z=deck_top, block=False, kind="pierprop")
    place("Props/SM_Prop_Sack_01", -200.0, -8900.0, yaw=40.0, z=deck_top, block=False, kind="pierprop")
    place("Props/SM_Prop_Anchor_01", -220.0, -7250.0, yaw=90.0, z=deck_top, block=False, kind="pierprop")
    # the moored ship: hull + mast + sails share one pivot; keel 60 below the water line
    for part in ("Ships/SM_Veh_Boat_Medium_01_Hull_Pirate", "Ships/SM_Veh_Boat_Medium_01_Mast",
                 "Ships/SM_Veh_Boat_Medium_01_Sails_Up_Pirate", "Ships/SM_Veh_Boat_Medium_01_Rigging"):
        place(part, 760.0, -8300.0, yaw=0.0, z=-180.0, block=part.endswith("Hull_Pirate"), kind="ship")
    place("Ships/SM_Veh_Flag_Pirate_01", 760.0, -8300.0 + 980.0, z=-180.0 + 1850.0, yaw=0.0, block=False, kind="shipflag")

    # ------------------------------------------------------------ CAMP at the spawn
    place("Buildings/SM_Bld_Shanty_Preset_Bar_01", -1500.0, -4300.0, yaw=90.0, kind="tavern")
    place("Buildings/SM_Bld_Tent_01", 1200.0, -4500.0, yaw=-30.0, kind="tent")
    place("Buildings/SM_Bld_Tent_02", 1700.0, -4100.0, yaw=25.0, kind="tent")
    place("Props/SM_Prop_Campfire_01", 0.0, -4500.0, block=False, kind="campfire")
    for ang in (30.0, 150.0, 270.0):
        sx, sy = rot2d(115.0, 0.0, ang)
        place("Props/SM_Prop_Stool_01", sx, -4500.0 + sy, yaw=ang + 180.0, block=False, kind="stool")
    place("Props/SM_Prop_Table_01", 450.0, -4750.0, yaw=15.0, kind="table")
    place("Props/SM_Prop_Stool_01", 560.0, -4750.0, yaw=90.0, block=False, kind="stool")
    place("Props/SM_Prop_Stool_01", 340.0, -4750.0, yaw=-90.0, block=False, kind="stool")
    for (bx, by, byaw) in ((-680.0, -5380.0, 0.0), (-760.0, -5290.0, 35.0), (-700.0, -5190.0, 70.0)):
        place("Props/SM_Prop_Barrel_01", bx, by, yaw=byaw, kind="barrel")
    place("Props/SM_Prop_Crate_01", -780.0, -4980.0, yaw=15.0, kind="crate")
    place("Props/SM_Prop_Crate_02", -700.0, -4900.0, yaw=50.0, kind="crate")
    for (tx, ty) in ((-550.0, -4800.0), (550.0, -4800.0), (-300.0, -5550.0), (300.0, -5550.0)):
        place("Props/SM_Prop_BottleTorch_01", tx, ty, block=False, kind="torch")
    place("Props/SM_Prop_Pole_01", 0.0, -4000.0, kind="flagpole")
    place("Ships/SM_Veh_Flag_Pirate_01", 0.0, -4000.0, z=ground(0.0, -4000.0) + 1000.0, yaw=0.0, block=False, kind="flag")
    place("Props/SM_Prop_Well_01", 900.0, -3800.0, yaw=20.0, kind="well")
    place("Props/SM_Prop_Chest_01", -1300.0, -4950.0, yaw=100.0, kind="chest")
    place("Props/SM_Prop_Chest_Treasure_01", -1300.0, -4950.0, yaw=100.0, block=False, kind="chestgold")
    place("Props/SM_Prop_FishRack_01", 650.0, -5450.0, yaw=-20.0, kind="fishrack")
    place("Ships/SM_Veh_Boat_Rowing_01_Hull_Pirate", 1500.0, -6300.0, yaw=30.0, kind="rowboat")
    place("Props/SM_Prop_Oar_01", 1450.0, -6050.0, yaw=60.0, block=False, kind="oar")
    for (cx, cy) in ((-2200.0, -6200.0), (2200.0, -6200.0)):
        place("Props/SM_Prop_Cannon_01", cx, cy, yaw=180.0, kind="cannon")
        place("Props/SM_Prop_CannonBalls_02", cx + 120.0, cy + 60.0, block=False, kind="cannonballs")
    place("Props/SM_Prop_Sack_02", 1050.0, -4750.0, yaw=10.0, block=False, kind="sack")
    place("Props/SM_Prop_Sack_03", 1120.0, -4820.0, yaw=70.0, block=False, kind="sack")
    place("Props/SM_Prop_Fence_01", -900.0, -3700.0, yaw=0.0, kind="fence")
    place("Props/SM_Prop_Fence_02", -1470.0, -3700.0, yaw=0.0, kind="fence")

    # ------------------------------------------------------------ PIRATE STASH (+X spawner)
    place("Buildings/SM_Bld_Rickety_House_01", 3500.0, 350.0, yaw=200.0, kind="stashhouse")
    place("Props/SM_Prop_TreasurePile_01", 3050.0, -450.0, block=False, kind="treasure")
    place("Props/SM_Prop_Chest_02", 3150.0, -520.0, yaw=-30.0, kind="chest")
    for (bx, by) in ((3300.0, -350.0), (3380.0, -280.0), (3260.0, -260.0)):
        place("Props/SM_Prop_Barrel_02", bx, by, yaw=bx, kind="barrel")
    place("Props/SM_Prop_Crate_04", 3600.0, -150.0, yaw=35.0, kind="crate")
    place("Props/SM_Prop_Skeleton_Cage_01", 2450.0, 550.0, yaw=60.0, block=False, kind="cage")
    place("Props/SM_Prop_Gallows_01", 2350.0, 750.0, yaw=45.0, kind="gallows")
    place("Props/SM_Prop_BottleTorch_01", 3250.0, 150.0, block=False, kind="torch")

    # ------------------------------------------------------------ SKELETON GRAVEYARD (-X spawner)
    place("Environments/SM_Env_Rock_Skull_01", -3900.0, 550.0, yaw=115.0, z=ground(-3900.0, 550.0), kind="skullrock")
    for i, gx in enumerate((-2450.0, -2600.0, -2750.0, -2900.0, -3050.0)):
        place("Props/SM_Prop_Grave_0%d" % (i % 5 + 1), gx, -550.0, yaw=90.0 + (i * 7.0), kind="grave")
    place("Props/SM_Prop_Skeleton_Ground_01", -2950.0, 250.0, yaw=30.0, block=False, kind="bones")
    place("Props/SM_Prop_Skeleton_Ground_02", -2550.0, -250.0, yaw=200.0, block=False, kind="bones")
    place("Props/SM_Prop_Gallows_02", -3250.0, -750.0, yaw=90.0, kind="gallows")
    place("Props/SM_Prop_Campfire_01", -2800.0, -150.0, block=False, kind="campfire")
    place("Props/SM_Prop_Skeleton_Chest_01", -3300.0, 100.0, yaw=40.0, kind="chest")
    place("Environments/SM_Env_Mangrove_Tree_01", -3350.0, 950.0, yaw=20.0, block=False, kind="mangrove")
    place("Environments/SM_Env_Mangrove_Tree_02", -2250.0, 950.0, yaw=140.0, block=False, kind="mangrove")
    place("Props/SM_Prop_Skeleton_Shackle_01", -2700.0, 450.0, yaw=0.0, block=False, kind="shackle")

    # ------------------------------------------------------------ NAVY POST (+Y spawner)
    place("Buildings/SM_Bld_Tent_05", -550.0, 3350.0, yaw=0.0, kind="navytent")
    place("Buildings/SM_Bld_Tent_06", 550.0, 3350.0, yaw=0.0, kind="navytent")
    place("Props/SM_Prop_Cannon_02", 0.0, 3650.0, yaw=180.0, kind="cannon")
    place("Props/SM_Prop_Crate_Cannonballs_01", 150.0, 3700.0, yaw=20.0, kind="crate")
    place("Props/SM_Prop_Pole_01", 0.0, 3250.0, kind="flagpole")
    place("Ships/SM_Veh_Flag_British_01", 0.0, 3250.0, z=ground(0.0, 3250.0) + 1000.0, yaw=0.0, block=False, kind="flag")
    for wx in (-500.0, 0.0, 500.0):
        place("Buildings/SM_Bld_Stone_Wall_01", wx, 2350.0, yaw=0.0, kind="stonewall")
    place("Props/SM_Prop_Crate_Covered_01", -300.0, 3050.0, yaw=10.0, kind="crate")
    place("Props/SM_Prop_Barrel_04", 300.0, 3050.0, yaw=0.0, kind="barrel")

    # ------------------------------------------------------------ HILL FORT (+40 tier)
    # tower in the NE corner and ONE wall along the north edge: the first layout (tower mid-hill plus
    # two walls) carved the hill's navmesh into three islands and the boss could not be reached
    place("Buildings/SM_Bld_Fort_Tower_01", 3300.0, 3300.0, yaw=0.0, kind="forttower")
    place("Buildings/SM_Bld_Fort_Wall_02", 2000.0, 3950.0, yaw=0.0, kind="fortwall")
    place("Props/SM_Prop_Cannon_03", 1500.0, 2200.0, yaw=225.0, kind="cannon")
    place("Props/SM_Prop_Chest_Treasure_02", 2200.0, 2200.0, yaw=30.0, block=False, kind="chestgold")
    place("Props/SM_Prop_Chest_03", 2200.0, 2200.0, yaw=30.0, kind="chest")
    place("Props/SM_Prop_TreasurePile_03", 2350.0, 2050.0, block=False, kind="treasure")
    place("Props/SM_Prop_BottleTorch_01", 1700.0, 1700.0, block=False, kind="torch")
    place("Props/SM_Prop_BottleTorch_01", 2400.0, 1400.0, block=False, kind="torch")
    place("Ships/SM_Veh_Flag_Pirate_02", 3300.0, 3300.0 + 700.0, z=40.0 + 2000.0, yaw=0.0, block=False, kind="flag")

    # boss spawner: the Skeleton Captain guards the fort (Enemies.csv row SkeletonCaptain)
    spawner_bp = eal.load_asset(SPAWNER_BP)
    enemy_bp = eal.load_asset(ENEMY_BP)
    if spawner_bp and enemy_bp:
        existing = [a for a in eas.get_all_level_actors()
                    if a.get_class().get_name() == "BP_CombatEnemySpawner_C"
                    and str(a.get_editor_property("EnemyRowOverride")) == BOSS_ROW]
        for a in existing:
            eas.destroy_actor(a)
        s = eas.spawn_actor_from_class(spawner_bp.generated_class(), v(*BOSS_SPAWNER_LOC), r(0, 0, 0))
        if s:
            s.set_editor_property("EnemyClass", enemy_bp.generated_class())
            s.set_editor_property("bShouldSpawnEnemiesImmediately", True)
            s.set_editor_property("SpawnCount", 1)
            s.set_editor_property("EnemyRowOverride", BOSS_ROW)
            label(s, "bossspawner")
            log("boss spawner placed at %s row=%s" % (BOSS_SPAWNER_LOC, BOSS_ROW))
        else:
            err("boss spawner spawn failed")
    else:
        err("spawner/enemy BP missing, no boss spawner")

    # ------------------------------------------------------------ the player's sailing boat
    # AWrecktideBoat (C++ pawn, 2026-09-04): moored on the west side of the pier, bow to the open sea.
    # Guarded so the script still runs on a DLL that predates the class.
    boat_cls = None
    try:
        boat_cls = unreal.load_class(None, "/Script/Wrecktide.WrecktideBoat")
    except Exception as e:  # noqa: BLE001
        err("boat class unavailable (%s): build the C++ first" % e)
    if boat_cls is not None:
        b = eas.spawn_actor_from_class(boat_cls, v(-700.0, -8300.0, -120.0), r(0, -90, 0))
        if b:
            label(b, "playerboat")
            log("player boat placed at (-700, -8300, -120) yaw -90")
        else:
            err("player boat spawn failed")

    # ------------------------------------------------------------ three more spawners (8 enemies total)
    # the four builder spawners + the boss cover one enemy per set; a second body at the graveyard,
    # the navy post and the stash keeps every set busy. RewardSubsystem caps RESPAWNS at 6 alive.
    if spawner_bp and enemy_bp:
        for (loc, row) in (((-3300.0, -300.0, 100.0), "SkeletonDeckhand"),
                           ((600.0, 3000.0, 100.0), "NavyCadet"),
                           ((3200.0, 700.0, 100.0), "Cutthroat")):
            s2 = eas.spawn_actor_from_class(spawner_bp.generated_class(), v(*loc), r(0, 0, 0))
            if s2:
                s2.set_editor_property("EnemyClass", enemy_bp.generated_class())
                s2.set_editor_property("bShouldSpawnEnemiesImmediately", True)
                s2.set_editor_property("SpawnCount", 1)
                s2.set_editor_property("EnemyRowOverride", row)
                label(s2, "extraspawner")
            else:
                err("extra spawner spawn failed at %s" % (loc,))
        log("extra spawners placed: 3")

    # ------------------------------------------------------------ treasure chests (one-time gold, F)
    # AWrecktideTreasureChest (C++, 2026-09-04): opened state persists as Chest_<Id> in the save.
    # Replaces the static chest props at the same spots so a chest is never both decor and loot.
    chest_cls = None
    try:
        chest_cls = unreal.load_class(None, "/Script/Wrecktide.WrecktideTreasureChest")
    except Exception as e:  # noqa: BLE001
        err("treasure chest class unavailable (%s): build the C++ first" % e)
    if chest_cls is not None:
        for (cid, x, y, yaw, gold) in (("CampChest", -1300.0, -4950.0, 100.0, 40),
                                       ("StashChest", 3150.0, -520.0, -30.0, 120),
                                       ("FortChest", 2200.0, 2200.0, 30.0, 250),
                                       ("WreckChest", 5300.0, -2400.0, 60.0, 180),
                                       ("GraveChest", -3300.0, 100.0, 40.0, 90)):
            for a in list(eas.get_all_level_actors()):
                lab = str(a.get_actor_label())
                if (lab.startswith("DRESS_chest_") or lab.startswith("DRESS_chestgold_")) and                         abs(a.get_actor_location().x - x) < 5.0 and abs(a.get_actor_location().y - y) < 5.0:
                    eas.destroy_actor(a)
            c = eas.spawn_actor_from_class(chest_cls, v(x, y, ground(x, y)), r(0, yaw, 0))
            if c:
                c.set_editor_property("ChestId", cid)
                c.set_editor_property("GoldAmount", gold)
                label(c, "treasurechest")
            else:
                err("treasure chest spawn failed: %s" % cid)
        log("treasure chests placed: 5")

    # ------------------------------------------------------------ SHIPWRECK (east beach)
    place("Props/SM_Prop_Shipwreck_01", 6100.0, -1800.0, yaw=70.0, z=-150.0, roll=12.0, kind="wreck")
    place("Props/SM_Prop_Shipwreck_01_Sails", 6100.0, -1800.0, yaw=70.0, z=-150.0, roll=12.0, block=False, kind="wrecksails")
    place("Props/SM_Prop_Debris_01", 5500.0, -1200.0, yaw=30.0, block=False, kind="debris")
    place("Props/SM_Prop_Debris_02", 5700.0, -2500.0, yaw=120.0, block=False, kind="debris")

    # ------------------------------------------------------------ scenery
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    far = 12500.0
    try:
        zones = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.WaterZone)
        if zones:
            ext = zones[0].get_editor_property("zone_extent")
            log("water zone extent = %s" % ext)
            far = max(9000.0, min(far, float(min(ext.x, ext.y)) * 0.45))
    except Exception as e:  # noqa: BLE001
        log("water zone extent unavailable (%s), far=%.0f" % (e, far))
    for (rel, x, y, yaw, sc) in (
        ("Environments/SM_Env_Background_Island_01", 0.0, far * 1.3, 0.0, 3.5),
        ("Environments/SM_Env_Background_Island_03", -far * 1.2, far * 0.6, 60.0, 4.0),
        ("Environments/SM_Env_Background_Island_02", far * 1.15, -far * 0.8, -30.0, 3.5),
        ("Environments/SM_Env_Background_Island_05", -far * 0.9, -far * 1.2, 20.0, 3.0),
        ("Environments/SM_Env_Background_Island_04", far * 1.3, far * 0.9, 100.0, 3.0),
    ):
        place(rel, x, y, yaw=yaw, scale=sc, z=-125.0, block=False, kind="farisland")
    for i, (x, y) in enumerate(((-6000.0, 4000.0), (3000.0, 8000.0), (8000.0, -2000.0), (-2000.0, -9000.0), (-9000.0, -4000.0))):
        place("Environments/SM_Env_Cloud_0%d" % (i % 3 + 1), x, y, yaw=i * 50.0, scale=2.5, z=3200.0 + i * 350.0, block=False, kind="cloud")
    palms = ("Environments/SM_Env_PalmTree_01", "Environments/SM_Env_PalmTree_02", "Environments/SM_Env_PalmTree_03",
             "Environments/SM_Env_PalmTree_04", "Environments/SM_Env_PalmTree_Tall_01", "Environments/SM_Env_PalmTree_Tall_02")
    for i, (x, y) in enumerate(((-4300.0, -3800.0), (-4600.0, -2200.0), (-4400.0, 1800.0), (-3900.0, 3900.0),
                                (4300.0, -3600.0), (4600.0, -1500.0), (4500.0, 800.0), (1200.0, 4500.0),
                                (-1500.0, 4600.0), (2600.0, -4400.0), (-2300.0, -3900.0), (600.0, 4700.0))):
        place(palms[i % len(palms)], x, y, yaw=i * 37.0, z=ground(x, y), block=False, kind="palm")
    bushes = ("Environments/SM_Env_Bush_01", "Environments/SM_Env_Bush_02", "Environments/SM_Env_Plants_01",
              "Environments/SM_Env_Fern_01", "Environments/SM_Env_GrassPatch_01", "Environments/SM_Env_Flowers_01")
    for i in range(24):
        ang = i * 137.5
        rad = 1600.0 + (i * 173) % 2600
        x, y = rot2d(rad, 0.0, ang)
        if abs(x) < 1000.0 and y < -3000.0:
            continue  # keep the camp clear
        place(bushes[i % len(bushes)], x, y, yaw=ang, block=False, kind="bush")
    rocks = ("Environments/SM_Env_Rock_Large_01", "Environments/SM_Env_Rock_Large_03", "Environments/SM_Env_Rock_Large_05",
             "Environments/SM_Env_Rocks_01", "Environments/SM_Env_Rock_02")
    for i, (x, y) in enumerate(((-6300.0, -3800.0), (6200.0, 3600.0), (-5600.0, 5900.0), (5900.0, -5600.0), (-6400.0, 2200.0))):
        place(rocks[i % len(rocks)], x, y, yaw=i * 61.0, scale=1.4, kind="rock")
    place("Environments/SM_Env_Rock_Huge_01", -6000.0, 6000.0, yaw=30.0, scale=0.8, kind="bigrock")

    # ------------------------------------------------------------ ambient sound
    amb = eal.load_asset(AMBIENT)
    if amb is None:
        log("ambient asset missing: %s (skipped)" % AMBIENT)
    else:
        try:
            a = eas.spawn_actor_from_class(unreal.AmbientSound, v(0.0, 0.0, 200.0), r(0, 0, 0))
            ac = a.get_editor_property("AudioComponent")
            ac.set_editor_property("Sound", amb)
            label(a, "ambient")
            log("ambient sound placed: %s (%s)" % (amb.get_name(), amb.get_class().get_name()))
        except Exception as e:  # noqa: BLE001
            err("ambient sound failed: %r" % e)

    # ------------------------------------------------------------ save + verdict
    log("collision prims: %s" % {k.split(".")[-1]: c for k, c in PRIM_CACHE.items()})
    if not les.save_current_level():
        fatal("save_current_level failed")
    log("level saved. placed: %s" % spawned)
    if FAILED:
        unreal.log_error("%s %d issue(s): %s" % (M, len(FAILED), FAILED))
        unreal.log_error("%s RESULT=PARTIAL (level saved; review issues)" % M)
        raise SystemExit(1)
    log("RESULT=OK island dressed + saved")


try:
    main()
except SystemExit:
    raise
except Exception as e:
    unreal.log_error("%s unhandled exception: %r" % (M, e))
    unreal.log_error("%s RESULT=FAIL (exception)" % M)
    raise SystemExit(1)
