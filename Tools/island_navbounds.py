"""island_navbounds.py - give Lvl_Island a REAL navigation bounds volume (2026-09-04).

Root cause found by -NavProbe + every -game log since 07-27: "Unable to find RecastNavMesh instance".
build_island_level.py spawned its NavMeshBoundsVolume with spawn_actor_from_class, which creates a
brush actor with NO brush model, so its bounds are empty, the navigation system registers nothing,
no nav data is ever created, and every enemy on the island has been standing at its spawner since
the level was built. This deletes the empty volume(s) and creates one through
unreal.WrecktideEditorTools.create_nav_bounds_volume (a real cube brush, the editor's own factory
path), logs both bounds, saves the level. Fail-closed. Markers: WRECKTIDE-NAVBOUNDS.
Usage: run_py.cmd <this file> [verify]
"""
import sys
import unreal

M = "WRECKTIDE-NAVBOUNDS"
MAP = "/Game/Lvl_Island"
CENTER = (0.0, 0.0, 0.0)
EXTENT = (7600.0, 7600.0, 700.0)   # the whole 14000 beach ring plus the pier reach, z -700..700
VERIFY_ONLY = any(a.lower() == "verify" for a in sys.argv[1:])


def log(msg):
    unreal.log_warning("%s %s" % (M, msg))


def fatal(msg):
    unreal.log_error("%s FATAL %s" % (M, msg))
    unreal.log_error("%s RESULT=FAIL (nothing saved)" % M)
    raise SystemExit(1)


les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if not les.load_level(MAP):
    fatal("load_level failed for %s" % MAP)

tools = unreal.WrecktideEditorTools
# WRECK COVE ONLY (2026-09-07). Gallows Reach owns NavBounds_Island2 in the same level; deleting it
# here would leave the second island with a bounds volume gone and every navy post standing still,
# which is exactly the failure this script was written to fix in the first place.
old = [a for a in eas.get_all_level_actors()
       if isinstance(a, unreal.NavMeshBoundsVolume)
       and str(a.get_actor_label()) not in ("NavBounds_Island2", "NavBounds_Island3")]
for a in old:
    size = tools.get_actor_bounds_size(a)
    log("existing volume %s bounds size=(%.0f, %.0f, %.0f) scale=%s" % (a.get_actor_label(), size.x, size.y, size.z, a.get_actor_scale3d()))
if VERIFY_ONLY:
    ok = any(tools.get_actor_bounds_size(a).x > 1000.0 for a in old)
    log("RESULT=VERIFY-ONLY volumes=%d real_bounds=%s" % (len(old), ok))
    raise SystemExit(0)

for a in old:
    eas.destroy_actor(a)
log("deleted %d old volume(s)" % len(old))

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
vol = tools.create_nav_bounds_volume(world, unreal.Vector(*CENTER), unreal.Vector(*EXTENT))
if vol is None:
    fatal("create_nav_bounds_volume returned None")
size = tools.get_actor_bounds_size(vol)
log("new volume %s bounds size=(%.0f, %.0f, %.0f)" % (vol.get_actor_label(), size.x, size.y, size.z))
if size.x < EXTENT[0] * 1.9 or size.z < EXTENT[2] * 1.9:
    fatal("new volume bounds too small: (%.0f, %.0f, %.0f)" % (size.x, size.y, size.z))
vol.set_actor_label("NavBounds_Island")

# (the step height comes from DefaultEngine.ini NavMeshResolutionParams; the actor scalar is deprecated in 5.6)

spent = tools.build_navigation_synchronously(world, 300.0)
log("navigation built headless in %.1fs" % spent)
if spent < 0.0:
    fatal("navigation build unavailable")

if not les.save_current_level():
    fatal("save_current_level failed")
log("RESULT=OK nav bounds volume rebuilt + level saved")
