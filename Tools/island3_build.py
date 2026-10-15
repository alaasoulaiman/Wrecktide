"""Build Mangrove Hollow, idempotently, in /Game/Lvl_Island.

Centre (-14000,13000); beach 9000 square, X -18500..-9500, Y 8500..17500, top -40.
Field 6000 square, top 0, with a 2600 square opening for the exposed bog at -20.
Village (-11900,12100), 1600 square, top 60; apron top 20 makes both rises <=40.
Altar (-16100,14400), 800 square, top 80; apron top 40. All solid to Z -140.
Nav half extents (5000,5000,700). Existing sea is checked, never rebuilt.
Reserved probe samples: beach (-14000,9000), field (-16500,11000), bog centre,
village (-11900,12100), altar (-16250,14450). Keep these free of dressing.
Run with run_py_args.cmd; optional verify reads the saved geometry without writing.
"""
import sys
import unreal

MAP = "/Game/Lvl_Island"
CENTRE = (-14000.0, 13000.0)
BEACH_HALF = 4500.0
FIELD_HALF = 3000.0
BOG_HALF = 1300.0
VILLAGE = (-11900.0, 12100.0)
ALTAR = (-16100.0, 14400.0)
LANDING = (-11200.0, 9900.0)
NAV_EXTENT = (5000.0, 5000.0, 700.0)
NAV_LABEL = "NavBounds_Island3"
POSTS = (
    (-15500, 11000, 100, "BogCultist"),
    (-14000, 10800, 100, "BogCultist"),
    (-16400, 12600, 100, "BogCultist"),
    (-12800, 15000, 100, "HexAcolyte"),
    (-15700, 15700, 100, "HexAcolyte"),
    (-14400, 13900, 80, "BogWraith"),
    (-16000, 14550, 180, "HexMother"),
)
CHEST = (-16400, 14200, 80)
M = "WRECKTIDE-ISLE3"

eal = unreal.EditorAssetLibrary
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
tools = unreal.WrecktideEditorTools


def log(message):
    unreal.log_warning("%s %s" % (M, message))


def require(condition, message):
    if not condition:
        unreal.log_error("%s RESULT=FAIL %s (level not saved)" % (M, message))
        raise SystemExit(1)


def material(name, colour):
    path = "/Game/Materials/" + name
    asset = eal.load_asset(path) if eal.does_asset_exist(path) else None
    if asset is None:
        parent = eal.load_asset("/Engine/BasicShapes/BasicShapeMaterial")
        require(parent, "missing basic material")
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, "/Game/Materials", unreal.MaterialInstanceConstant,
            unreal.MaterialInstanceConstantFactoryNew())
        require(asset, "could not create " + name)
        unreal.MaterialEditingLibrary.set_material_instance_parent(asset, parent)
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
            asset, "Color", unreal.LinearColor(*colour))
        unreal.MaterialEditingLibrary.update_material_instance(asset)
        require(eal.save_loaded_asset(asset), "could not save " + name)
    return asset


def skin_material(name, colour, opacity=1.0):
    path = "/Game/Materials/" + name
    if eal.does_asset_exist(path):
        return eal.load_asset(path)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, "/Game/Materials", unreal.Material, unreal.MaterialFactoryNew())
    require(asset, "create skin material " + name)
    asset.set_editor_property("used_with_skeletal_mesh", True)
    mel = unreal.MaterialEditingLibrary
    colour_node = mel.create_material_expression(asset, unreal.MaterialExpressionConstant3Vector)
    colour_node.set_editor_property("constant", unreal.LinearColor(*colour))
    mel.connect_material_property(colour_node, "", unreal.MaterialProperty.MP_BASE_COLOR)
    if opacity < 1.0:
        asset.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        asset.set_editor_property("two_sided", True)
        alpha = mel.create_material_expression(asset, unreal.MaterialExpressionConstant)
        alpha.set_editor_property("r", opacity)
        mel.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY)
        mel.connect_material_property(colour_node, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(asset)
    require(eal.save_loaded_asset(asset), "save skin material " + name)
    return asset


def main():
    require(les.load_level(MAP), "load level")
    actors = list(eas.get_all_level_actors())
    old = [a for a in actors if str(a.get_actor_label()).startswith("ISLE3_")
           or str(a.get_actor_label()) == NAV_LABEL]
    sea_ok = False
    # Check the actual saved brush bounds cover the island and 2000 uu beyond every beach.
    for actor in actors:
        if not isinstance(actor, unreal.PhysicsVolume) or not actor.get_editor_property("water_volume"):
            continue
        size = tools.get_actor_bounds_size(actor)
        loc = actor.get_actor_location()
        covered = (abs(CENTRE[0] - loc.x) + BEACH_HALF + 2000 <= size.x * 0.5
                   and abs(CENTRE[1] - loc.y) + BEACH_HALF + 2000 <= size.y * 0.5
                   and loc.z - size.z * 0.5 < -400 < loc.z + size.z * 0.5)
        sea_ok |= covered
        log("sea %s centre=%s size=%s covers=%s" % (actor.get_actor_label(), loc, size, covered))
    require(sea_ok, "existing sea does not cover Mangrove Hollow")
    if "verify" in [arg.lower() for arg in sys.argv[1:]]:
        navs = [a for a in old if str(a.get_actor_label()) == NAV_LABEL]
        slabs = [a for a in old if isinstance(a, unreal.StaticMeshActor)]
        require(len(navs) == 1 and tools.get_actor_bounds_size(navs[0]).x >= 9999, "nav brush")
        require(len(slabs) >= 10, "missing terrain slabs")
        posts = [a for a in old if str(a.get_actor_label()).startswith("ISLE3_post_")]
        chests = [a for a in old if str(a.get_actor_label()) == "ISLE3_chest"]
        if posts or "population" in sys.argv:
            require(len(posts) == len(POSTS), "seven posts")
            for i, (x, y, z, row) in enumerate(POSTS):
                matches = [a for a in posts if str(a.get_actor_label()) == "ISLE3_post_%02d" % i]
                require(len(matches) == 1, "post identity")
                actor = matches[0]
                require(str(actor.get_editor_property("EnemyRowOverride")) == row, "post row " + row)
                require((actor.get_actor_location() - unreal.Vector(x, y, z)).length() < 1, "post spot " + row)
            require(len(chests) == 1 and str(chests[0].get_editor_property("ChestId")) == "AltarChest", "altar chest")
        log("population posts=%d chests=%d" % (len(posts), len(chests)))
        log("RESULT=OK verify actors=%d slabs=%d sea=1 nav=1 (nothing written)" % (len(old), len(slabs)))
        return

    cube = eal.load_asset("/Engine/BasicShapes/Cube")
    require(cube, "missing cube")
    mud = material("MI_MangroveMud", (0.075, 0.10, 0.055, 1.0))
    bank = material("MI_MangroveBank", (0.20, 0.24, 0.12, 1.0))
    sand = material("MI_MangroveSand", (0.32, 0.30, 0.19, 1.0))
    wood = material("MI_MangroveWood", (0.20, 0.12, 0.065, 1.0))
    stone = material("MI_MangroveStone", (0.12, 0.18, 0.16, 1.0))
    cloth = skin_material("M_MangroveCloth", (0.045, 0.07, 0.055, 1.0))
    spirit = skin_material("M_MangroveWraith", (0.06, 0.30, 0.27, 1.0), 0.55)
    for actor in old:
        require(eas.destroy_actor(actor), "could not clear " + str(actor.get_actor_label()))
    for actor in actors:
        if not str(actor.get_actor_label()).startswith("DRESS_farisland"):
            continue
        loc = actor.get_actor_location()
        size = tools.get_actor_bounds_size(actor)
        overlaps = (abs(loc.x - CENTRE[0]) <= BEACH_HALF + size.x * 0.5
                    and abs(loc.y - CENTRE[1]) <= BEACH_HALF + size.y * 0.5)
        log("decor %s at=%s size=%s overlaps=%s" % (actor.get_actor_label(), loc, size, overlaps))
        if overlaps:
            require(eas.destroy_actor(actor), "could not remove overlapping decor")

    def slab(name, x, y, hx, hy, top, mat):
        actor = eas.spawn_actor_from_class(unreal.StaticMeshActor,
            unreal.Vector(x, y, (top - 140.0) * 0.5), unreal.Rotator())
        require(actor, "spawn " + name)
        comp = actor.get_editor_property("StaticMeshComponent")
        comp.set_editor_property("StaticMesh", cube)
        comp.set_material(0, mat)
        actor.set_actor_scale3d(unreal.Vector(hx / 50.0, hy / 50.0, (top + 140.0) / 100.0))
        actor.set_actor_label("ISLE3_" + name)
        actor.set_folder_path("MangroveHollow/Terrain")

    cx, cy = CENTRE
    slab("beach", cx, cy, BEACH_HALF, BEACH_HALF, -40, sand)
    # Four field boxes leave the lower bog exposed; a single field box would bury it.
    for side in (-1, 1):
        slab("field_x_%d" % side, cx + side * 2150, cy, 850, FIELD_HALF, 0, bank)
        slab("field_y_%d" % side, cx, cy + side * 2150, BOG_HALF, 850, 0, bank)
    slab("bog", cx, cy, BOG_HALF, BOG_HALF, -20, mud)
    slab("village_step", *VILLAGE, 900, 900, 20, wood)
    slab("village", *VILLAGE, 800, 800, 60, wood)
    slab("altar_step", *ALTAR, 600, 600, 40, stone)
    slab("altar", *ALTAR, 400, 400, 80, stone)
    # Altar inlays give both runtime skin materials a level reference for cooking.
    slab("altar_cloth", -15850, 14150, 60, 60, 81, cloth)
    slab("altar_spirit", -15850, 14300, 60, 60, 81, spirit)

    spawner = eal.load_blueprint_class("/Game/Variant_Combat/Blueprints/AI/BP_CombatEnemySpawner")
    enemy = eal.load_blueprint_class("/Game/Variant_Combat/Blueprints/AI/BP_CombatEnemy")
    require(spawner and enemy, "enemy classes")
    for i, (x, y, z, row) in enumerate(POSTS):
        actor = eas.spawn_actor_from_class(spawner, unreal.Vector(x, y, z), unreal.Rotator())
        require(actor, "post " + row)
        actor.set_actor_label("ISLE3_post_%02d" % i)
        actor.set_folder_path("MangroveHollow/Posts")
        actor.set_editor_property("EnemyClass", enemy)
        actor.set_editor_property("EnemyRowOverride", row)
        actor.set_editor_property("bShouldSpawnEnemiesImmediately", True)
        actor.set_editor_property("SpawnCount", 1)
    chest = eas.spawn_actor_from_class(unreal.WrecktideTreasureChest, unreal.Vector(*CHEST), unreal.Rotator())
    require(chest, "altar chest")
    chest.set_actor_label("ISLE3_chest")
    chest.set_folder_path("MangroveHollow")
    chest.set_editor_property("ChestId", "AltarChest")
    chest.set_editor_property("GoldAmount", 180)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    nav = tools.create_nav_bounds_volume(world, unreal.Vector(cx, cy, 0), unreal.Vector(*NAV_EXTENT))
    require(nav, "create nav bounds")
    size = tools.get_actor_bounds_size(nav)
    require(size.x >= 9999 and size.y >= 9999 and size.z >= 1399, "nav brush size")
    nav.set_actor_label(NAV_LABEL)
    nav.set_folder_path("MangroveHollow")
    spent = tools.build_navigation_synchronously(world, 300.0)
    require(spent >= 0, "navigation build")
    require(les.save_current_level(), "save level")
    log("RESULT=OK centre=(-14000,13000) beachHalf=4500 navHalf=(5000,5000,700) navSeconds=%.1f" % spent)


main()
