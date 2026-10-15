# Chunk D: build Lvl_Island headlessly - a walkable sand island in a WaterBodyOcean with the
# full combat loop (PlayerStart, 4 hot spawners, lights, dynamic-navmesh bounds), gamemode
# override to BP_CombatGameMode (the project's GlobalDefaultGameMode is the ThirdPerson one).
#
# Layout (z axis): ocean surface -120 | beach ring top -40 | field top 0 | hill top +40
# (40uu steps stay under the CMC MaxStepHeight of 45 - no ramps needed).
# Idempotent: an existing Lvl_Island is deleted and rebuilt. Startup-map config is NOT touched
# here (file edit after the fresh-process verify passes).
#   UnrealEditor-Cmd <proj> -run=pythonscript -script=<full path> \
#     "-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities" -unattended -nosplash -nullrhi
import unreal

MAP = "/Game/Lvl_Island"
# /Engine/BasicShapes/Cube: 100uu, CENTER pivot, GUARANTEED simple box collision. The template's
# SM_Cube let the pawn fall straight through in-game despite a BlockAll profile (run 1 evidence:
# underwater captures + MOVE_Falling with healthy speeds) - profile means nothing without prims.
CUBE = "/Engine/BasicShapes/Cube"
GM = "/Game/Variant_Combat/Blueprints/BP_CombatGameMode"
SPAWNER_BP = "/Game/Variant_Combat/Blueprints/AI/BP_CombatEnemySpawner"
ENEMY_BP = "/Game/Variant_Combat/Blueprints/AI/BP_CombatEnemy"

Z_WATER = -120.0
FAILED = []


def log(msg):
    unreal.log("WRECKTIDE-ISLAND %s" % msg)


def err(msg):
    FAILED.append(msg)
    unreal.log_error("WRECKTIDE-ISLAND %s" % msg)


def fatal(msg):
    unreal.log_error("WRECKTIDE-ISLAND FATAL %s" % msg)
    unreal.log_error("WRECKTIDE-ISLAND RESULT=FAIL")
    raise SystemExit(1)


def v(x, y, z):
    return unreal.Vector(x, y, z)


def r(pitch, yaw, roll):
    return unreal.Rotator(pitch=pitch, yaw=yaw, roll=roll)


def main():
    eal = unreal.EditorAssetLibrary
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    cube = eal.load_asset(CUBE)
    gm_bp = eal.load_asset(GM)
    spawner_bp = eal.load_asset(SPAWNER_BP)
    enemy_bp = eal.load_asset(ENEMY_BP)
    if not all((cube, gm_bp, spawner_bp, enemy_bp)):
        fatal("core asset load failed cube=%s gm=%s spawner=%s enemy=%s" % (cube, gm_bp, spawner_bp, enemy_bp))

    # scatter palette DERIVED from the Synty pack (never hardcode paths that can be searched)
    def find_meshes(*keywords):
        found = []
        for p in eal.list_assets("/Game/PolygonPirates/Meshes", recursive=True, include_folder=False):
            clean = p.split(".")[0]
            base = clean.rsplit("/", 1)[-1]
            if any(k.lower() in base.lower() for k in keywords) and base.startswith("SM_"):
                found.append(clean)
        return sorted(found)

    palms = find_meshes("Palm")
    piles = find_meshes("Beach_Pile")
    rocks = find_meshes("Env_Rock")
    crates = find_meshes("Crate")
    log("palette palms=%d piles=%d rocks=%d crates=%d" % (len(palms), len(piles), len(rocks), len(crates)))

    # ------------------------------------------------------------------ level
    if eal.does_asset_exist(MAP):
        log("existing Lvl_Island found - deleting for a clean rebuild")
        if not eal.delete_asset(MAP):
            fatal("could not delete existing Lvl_Island")
    if not les.new_level(MAP):
        fatal("new_level failed")
    log("new level created at %s" % MAP)

    spawned = {}

    def place_mesh(mesh_asset, loc, scale, yaw=0.0, name_hint="mesh", hidden=False, collision=True):
        # NEVER spawn_actor_from_object here: the asset-factory placement path hard-crashes in
        # headless commandlets (EXCEPTION_ACCESS_VIOLATION in EditorFramework, proven by
        # chunkd_spawn_probe.py). Class-spawn + set the mesh property survives.
        a = eas.spawn_actor_from_class(unreal.StaticMeshActor, loc, r(0, yaw, 0))
        if a is None:
            err("spawn failed for %s at %s" % (name_hint, loc))
            return None
        comp = a.get_editor_property("StaticMeshComponent")
        comp.set_editor_property("StaticMesh", mesh_asset)
        a.set_actor_scale3d(scale)
        if hidden:
            a.set_actor_hidden_in_game(True)
        if not collision:
            # visual-only scatter must not become a physics or navigation obstacle
            comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        spawned[name_hint] = spawned.get(name_hint, 0) + 1
        return a

    def place_class(cls, loc, rot=None, scale=None, name_hint="actor"):
        a = eas.spawn_actor_from_class(cls, loc, rot or r(0, 0, 0))
        if a is None:
            err("spawn failed for %s" % name_hint)
            return None
        if scale is not None:
            a.set_actor_scale3d(scale)
        spawned[name_hint] = spawned.get(name_hint, 0) + 1
        return a

    # ------------------------------------------------------------------ landform (SM_Cube tiers)
    # beach ring 14000x14000, top -40
    place_mesh(cube, v(0, 0, -80), v(140, 140, 0.8), name_hint="beach_base")
    # central field 10000x10000, top 0
    place_mesh(cube, v(0, 0, -40), v(100, 100, 0.8), name_hint="field_base")
    # hill 3000x3000, top +40 (traversal step, NE corner)
    place_mesh(cube, v(2600, 2600, 0), v(30, 30, 0.8), name_hint="hill")

    # invisible surf walls at the beach perimeter (hidden actors keep collision)
    wall_s = v(140, 1, 6)
    for loc, yaw in (
        (v(0, 7050, 200), 0.0), (v(0, -7050, 200), 0.0),
        (v(7050, 0, 200), 90.0), (v(-7050, 0, 200), 90.0),
    ):
        place_mesh(cube, loc, wall_s, yaw=yaw, name_hint="surf_wall", hidden=True)

    # ------------------------------------------------------------------ scatter (visual only)
    def scatter(assets, spots, name_hint, scale=1.0):
        if not assets:
            log("no assets for %s scatter - skipped" % name_hint)
            return
        for i, (x, y, z, yaw) in enumerate(spots):
            mesh = eal.load_asset(assets[i % len(assets)])
            if mesh:
                place_mesh(mesh, v(x, y, z), v(scale, scale, scale), yaw=yaw, name_hint=name_hint, collision=False)

    # palms around the beach ring (top -40)
    scatter(palms, [
        (-5800, -5200, -40, 15), (-6100, 3800, -40, 140), (5600, -5900, -40, 250),
        (6100, 4400, -40, 70), (-4200, 6200, -40, 300), (4600, 6100, -40, 200),
        (-6200, -1500, -40, 90), (6300, -900, -40, 30),
    ], "palm")
    # beach piles + rocks near the surf
    scatter(piles, [
        (-5200, 800, -40, 0), (5300, 2400, -40, 120), (900, -6200, -40, 200), (-1800, 6200, -40, 45),
    ], "pile")
    scatter(rocks, [
        (-3400, -6100, -40, 10), (3800, 6300, -40, 250), (6400, -3300, -40, 130),
    ], "rock", scale=1.5)
    # crates near spawn
    scatter(crates, [
        (-700, -4600, 0, 20), (-900, -4500, 0, 65),
    ], "crate")

    # ------------------------------------------------------------------ sky + light (movable, no bake)
    # atmosphere FIRST so the skylight's captured-scene capture sees a sky, not a void; a stale
    # empty capture would serialize and leave shadowed surfaces near-black under potato mode
    place_class(unreal.SkyAtmosphere, v(0, 0, 0), name_hint="atmosphere")
    sun = place_class(unreal.DirectionalLight, v(0, 0, 2000), rot=r(-48, 35, 0), name_hint="sun")
    if sun:
        sun_c = sun.get_editor_property("LightComponent")
        sun_c.set_editor_property("Intensity", 3.0)
        sun_c.set_editor_property("Mobility", unreal.ComponentMobility.MOVABLE)
    sky = place_class(unreal.SkyLight, v(0, 0, 2000), name_hint="skylight")
    sky_c = None
    if sky:
        sky_c = sky.get_editor_property("LightComponent")
        sky_c.set_editor_property("Mobility", unreal.ComponentMobility.MOVABLE)
    fog = place_class(unreal.ExponentialHeightFog, v(0, 0, Z_WATER), name_hint="fog")
    if fog:
        fog_c = fog.get_editor_property("Component")
        fog_c.set_editor_property("FogDensity", 0.015)

    # ------------------------------------------------------------------ ocean
    ocean = place_class(unreal.WaterBodyOcean, v(0, 0, Z_WATER), name_hint="ocean")
    zones = unreal.GameplayStatics.get_all_actors_of_class(
        unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world(), unreal.WaterZone)
    log("water zones after ocean spawn: %d" % len(zones))
    if ocean and len(zones) == 0:
        place_class(unreal.WaterZone, v(0, 0, Z_WATER), name_hint="waterzone")
        log("water zone spawned manually")

    # ------------------------------------------------------------------ gameplay
    place_class(unreal.PlayerStart, v(0, -5200, 60), rot=r(0, 90, 0), name_hint="playerstart")

    spawner_cls = spawner_bp.generated_class()
    enemy_cls = enemy_bp.generated_class()
    # one Enemies.csv row per spawner (2026-09-02 variety): melee-heavy so the melee shop loop keeps
    # levelling, one easy cadet; each row carries its own Synty skin (FEnemyTableRow::SkinMesh)
    spawner_rows = ("Cutthroat", "SkeletonDeckhand", "NavyCadet", "Cutthroat")
    for loc, row in zip((v(2800, 0, 100), v(-2800, 0, 100), v(0, 2800, 100), v(0, -2800, 100)), spawner_rows):
        s = place_class(spawner_cls, loc, name_hint="spawner")
        if s:
            s.set_editor_property("EnemyClass", enemy_cls)
            s.set_editor_property("bShouldSpawnEnemiesImmediately", True)
            s.set_editor_property("SpawnCount", 1)
            s.set_editor_property("EnemyRowOverride", row)

    nav = place_class(unreal.NavMeshBoundsVolume, v(0, 0, 0), scale=v(80, 80, 12), name_hint="navbounds")
    if nav is None:
        err("nav bounds missing - enemies will not path")

    # ------------------------------------------------------------------ gamemode override
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    ws = unreal.GameplayStatics.get_actor_of_class(world, unreal.WorldSettings)
    if ws is None:
        fatal("WorldSettings not reachable - gamemode override impossible")
    ws.set_editor_property("DefaultGameMode", gm_bp.generated_class())
    back = ws.get_editor_property("DefaultGameMode")
    log("WorldSettings.DefaultGameMode -> %s" % (back.get_path_name() if back else None))
    if back is None or "BP_CombatGameMode" not in back.get_path_name():
        fatal("gamemode override did not take")

    # refresh the skylight capture now that atmosphere + ocean + island all exist
    if sky_c is not None:
        try:
            sky_c.recapture_sky()
            log("skylight recaptured")
        except Exception as e:
            log("skylight recapture unavailable (non-fatal): %s" % e)

    # ------------------------------------------------------------------ save + verdict
    if not les.save_current_level():
        fatal("save_current_level failed")
    log("level saved. spawned: %s" % spawned)

    if FAILED:
        unreal.log_error("WRECKTIDE-ISLAND %d non-fatal issue(s): %s" % (len(FAILED), FAILED))
        unreal.log_error("WRECKTIDE-ISLAND RESULT=PARTIAL (level saved; review issues)")
        raise SystemExit(1)
    log("RESULT=OK island built + saved")


try:
    main()
except SystemExit:
    raise
except Exception as e:
    unreal.log_error("WRECKTIDE-ISLAND unhandled exception: %r" % e)
    unreal.log_error("WRECKTIDE-ISLAND RESULT=FAIL (exception)")
    raise SystemExit(1)
