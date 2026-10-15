"""island_sea_volume.py - make the sea SWIMMABLE and delete the surf walls (2026-09-06).

Player report: "I also fell through the water rather than swimming." The ocean is a Water-plugin
AWaterBodyOcean whose collision is query-only and overlaps pawns, so nothing ever caught a pirate
who left the pier; with no KillZ either he fell for about four minutes before the engine default
(-1,048,575) destroyed him. A UCharacterMovementComponent only enters MOVE_Swimming through an
APhysicsVolume with bWaterVolume set (Engine CharacterMovementComponent.cpp, PhysicsVolumeChanged),
and none existed in the level.

This script:
  1. deletes any previous SEA_volume and creates one through UWrecktideEditorTools.create_water_volume
     (the editor Box Volume factory path proven by island_navbounds.py; a volume from
     spawn_actor_from_class carries an EMPTY brush, so its bounds are zero and nothing overlaps it),
     centred at (0, 0, -820) with extent (25600, 25600, 700) = z -1520..-120 across the whole
     51200 x 51200 water zone, top exactly on the water surface Z_WATER = -120;
  2. sets WorldSettings KillZ to -3000, a metre and a half under the sea floor, so anything that
     does slip through dies in seconds instead of minutes (ACombatCharacter::FellOutOfWorld);
  3. DELETES every surf wall. They were hidden BlockAll cubes at the beach perimeter whose only job
     was to stop the player walking into the sea. Swimming is now the feature, so they are in the
     way. Two families exist: the four originals from build_island_level.py (unlabelled hidden
     engine cubes, scale.z 6, z = 200, at |x| or |y| = 7050) and the DRESS_surfwall* segments
     island_dress.py made when it split the south wall for the pier gap;
  4. rebuilds navigation (the walls were navigation-blocking geometry) and saves the level.

WARNING for whoever runs the dressing pass next: island_dress.py PRESERVES DRESS_surfwall* actors
across re-runs and re-creates the split south wall when it does not find one. Re-running it will put
the surf walls back and wall the sea off again. Re-run THIS script after it, or teach island_dress.py
to skip the wall block.

Fail-closed: nothing is saved unless the volume has real brush bounds. Markers: WRECKTIDE-SEA.
Usage: run_py_args.cmd <this file> [verify]
"""
import sys
import unreal

M = "WRECKTIDE-SEA"
MAP = "/Game/Lvl_Island"
CUBE = "/Engine/BasicShapes/Cube"
Z_WATER = -120.0
LABEL = "SEA_volume"
CENTER = (0.0, 0.0, -820.0)
EXTENT = (25600.0, 25600.0, 700.0)   # top = -820 + 700 = -120 = the water surface
KILL_Z = -3000.0
VERIFY_ONLY = any(a.lower() == "verify" for a in sys.argv[1:])


def log(msg):
    unreal.log_warning("%s %s" % (M, msg))


def fatal(msg):
    unreal.log_error("%s FATAL %s" % (M, msg))
    unreal.log_error("%s RESULT=FAIL (nothing saved)" % M)
    raise SystemExit(1)


def is_surf_wall(a):
    """A surf wall: a DRESS_surfwall* actor, or a hidden engine-cube StaticMeshActor standing 600 uu
    tall at z=200 on the beach perimeter. The originals carry no label, so geometry is the only
    honest test; every condition below is checked so no other hidden blocker can be caught by it."""
    if str(a.get_actor_label()).startswith("DRESS_surfwall"):
        return True
    if not isinstance(a, unreal.StaticMeshActor):
        return False
    comp = a.static_mesh_component
    mesh = comp.static_mesh if comp else None
    if mesh is None or mesh.get_path_name().split(".")[0] != CUBE:
        return False
    try:
        if not bool(a.get_editor_property("hidden")):
            return False
    except Exception:  # noqa: BLE001 - property name drift must not silently skip a wall
        log("could not read hidden flag on %s; judging it on geometry alone" % a.get_actor_label())
    loc = a.get_actor_location()
    sc = a.get_actor_scale3d()
    return abs(loc.z - 200.0) < 5.0 and sc.z >= 5.0


les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if not les.load_level(MAP):
    fatal("load_level failed for %s" % MAP)

tools = unreal.WrecktideEditorTools
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
actors = eas.get_all_level_actors()

volumes = [a for a in actors if isinstance(a, unreal.PhysicsVolume) and a.get_actor_label() == LABEL]
walls = [a for a in actors if is_surf_wall(a)]
ws = unreal.GameplayStatics.get_actor_of_class(world, unreal.WorldSettings)

if VERIFY_ONLY:
    ok = True
    if len(volumes) != 1:
        log("VERIFY volumes labelled %s: %d (want 1)" % (LABEL, len(volumes)))
        ok = False
    else:
        vol = volumes[0]
        size = tools.get_actor_bounds_size(vol)
        water = vol.get_editor_property("water_volume")
        loc = vol.get_actor_location()
        log("VERIFY volume %s at (%.0f, %.0f, %.0f) bounds=(%.0f, %.0f, %.0f) water=%s priority=%s fluid_friction=%.2f"
            % (vol.get_actor_label(), loc.x, loc.y, loc.z, size.x, size.y, size.z, water,
               vol.get_editor_property("priority"), vol.get_editor_property("fluid_friction")))
        if not water:
            log("VERIFY bWaterVolume is FALSE")
            ok = False
        if size.x < EXTENT[0] * 1.9 or size.z < EXTENT[2] * 1.9:
            log("VERIFY brush bounds too small (empty brush?)")
            ok = False
        if abs((loc.z + EXTENT[2]) - Z_WATER) > 1.0:
            log("VERIFY volume top %.0f != water surface %.0f" % (loc.z + EXTENT[2], Z_WATER))
            ok = False
    kz = ws.get_editor_property("kill_z") if ws else None
    log("VERIFY KillZ=%s (want %.0f)" % (kz, KILL_Z))
    if ws is None or abs(float(kz) - KILL_Z) > 1.0:
        ok = False
    log("VERIFY surf walls remaining: %d (want 0)" % len(walls))
    if walls:
        ok = False
    log("RESULT=%s (verify only, nothing written)" % ("OK" if ok else "FAIL"))
    raise SystemExit(0 if ok else 1)

# ------------------------------------------------------------------ idempotency
for a in volumes:
    log("deleting previous %s" % a.get_actor_label())
    eas.destroy_actor(a)

# ------------------------------------------------------------------ the sea volume
vol = tools.create_water_volume(world, unreal.Vector(*CENTER), unreal.Vector(*EXTENT), LABEL)
if vol is None:
    fatal("create_water_volume returned None")
size = tools.get_actor_bounds_size(vol)
log("sea volume %s bounds size=(%.0f, %.0f, %.0f) top=%.0f" % (vol.get_actor_label(), size.x, size.y, size.z, CENTER[2] + EXTENT[2]))
if size.x < EXTENT[0] * 1.9 or size.z < EXTENT[2] * 1.9:
    fatal("sea volume brush is empty or too small: (%.0f, %.0f, %.0f)" % (size.x, size.y, size.z))
if not vol.get_editor_property("water_volume"):
    fatal("bWaterVolume did not stick")

# ------------------------------------------------------------------ KillZ
if ws is None:
    fatal("WorldSettings not reachable - KillZ cannot be set")
ws.modify()
ws.set_editor_property("kill_z", KILL_Z)
log("KillZ set to %.0f (was the engine default)" % ws.get_editor_property("kill_z"))

# ------------------------------------------------------------------ surf walls
removed = 0
for a in walls:
    loc = a.get_actor_location()
    sc = a.get_actor_scale3d()
    log("removing surf wall %s at (%.0f, %.0f, %.0f) scale=(%.1f, %.1f, %.1f)"
        % (a.get_actor_label(), loc.x, loc.y, loc.z, sc.x, sc.y, sc.z))
    if eas.destroy_actor(a):
        removed += 1
log("surf walls removed: %d of %d found" % (removed, len(walls)))
if removed != len(walls):
    fatal("could not remove every surf wall")

# ------------------------------------------------------------------ navigation + save
spent = tools.build_navigation_synchronously(world, 300.0)
log("navigation rebuilt headless in %.1fs" % spent)
if spent < 0.0:
    fatal("navigation build unavailable")

if not les.save_current_level():
    fatal("save_current_level failed")
log("RESULT=OK sea volume + KillZ %.0f, %d surf walls removed, level saved" % (KILL_Z, removed))
