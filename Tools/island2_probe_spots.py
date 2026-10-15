"""island2_probe_spots.py - report what stands on -Island2Probe's sample spots (2026-09-07).

Diagnostic only: writes nothing. -Island2Probe traces WorldStatic straight down at a handful of
points and asserts the tier height it finds; after the dressing pass two of those traces started
reporting a prop's roof instead of the tier. This lists every level actor within a small radius of
each sample point with its label, class, world bounds and whether it still has collision, so the
sample points can be moved to clear ground on evidence instead of on a guess.

Markers: WRECKTIDE-SPOTS. Usage: tools\\run_py_args.cmd Tools\\island2_probe_spots.py
"""
import unreal

M = "WRECKTIDE-SPOTS"
MAP = "/Game/Lvl_Island"
SPOTS = (
    ("beach", 12000.0, -20000.0, 900.0),
    ("field", 13000.0, -14500.0, 700.0),
    ("square", 11400.0, -16800.0, 700.0),
    ("lighthouse", 14200.0, -13800.0, 700.0),
    ("navA", 13200.0, -14600.0, 600.0),
    ("navB", 10800.0, -17400.0, 600.0),
)


def log(msg):
    unreal.log_warning("%s %s" % (M, msg))


les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if not les.load_level(MAP):
    unreal.log_error("%s FATAL load_level failed" % M)
    raise SystemExit(1)

tools = unreal.WrecktideEditorTools
actors = eas.get_all_level_actors()
log("level actors=%d" % len(actors))

for (name, sx, sy, radius) in SPOTS:
    log("---- spot %s at (%.0f, %.0f) radius %.0f ----" % (name, sx, sy, radius))
    for a in actors:
        loc = a.get_actor_location()
        if abs(loc.x - sx) > radius or abs(loc.y - sy) > radius:
            continue
        size = tools.get_actor_bounds_size(a)
        collision = "?"
        try:
            comp = a.get_editor_property("StaticMeshComponent")
            collision = str(comp.get_collision_enabled()) if comp else "no-smc"
        except Exception:  # noqa: BLE001
            collision = "n/a"
        log("  %-28s %-26s at (%.0f, %.0f, %.0f) size=(%.0f, %.0f, %.0f) collision=%s"
            % (a.get_actor_label(), a.get_class().get_name(), loc.x, loc.y, loc.z,
               size.x, size.y, size.z, collision))

log("RESULT=OK (diagnostic, nothing written)")
