"""island_enemy_variety.py - set one Enemies.csv row per spawner on Lvl_Island (2026-09-02).

Surgical alternative to re-running build_island_level.py: loads the island, finds the four placed
BP_CombatEnemySpawner actors, maps them by position (+X, -X, +Y, -Y) to the same row list the
builder now uses, writes EnemyRowOverride, verifies by readback, saves the level. Fail-closed.
Markers: WRECKTIDE-VARIETY. Usage: run_py.cmd <this file> [verify]
"""
import sys
import unreal

M = "WRECKTIDE-VARIETY"
MAP = "/Game/Lvl_Island"
SPAWNER_CLASS = "BP_CombatEnemySpawner_C"
ROWS_BY_SIDE = {"+X": "Cutthroat", "-X": "SkeletonDeckhand", "+Y": "NavyCadet", "-Y": "Cutthroat"}
VERIFY_ONLY = any(a.lower() == "verify" for a in sys.argv[1:])


def log(msg):
    unreal.log_warning("%s %s" % (M, msg))


def fatal(msg):
    unreal.log_error("%s FATAL %s" % (M, msg))
    unreal.log_error("%s RESULT=FAIL (nothing saved)" % M)
    raise SystemExit(1)


def side_of(loc):
    if abs(loc.x) >= abs(loc.y):
        return "+X" if loc.x > 0 else "-X"
    return "+Y" if loc.y > 0 else "-Y"


les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if not les.load_level(MAP):
    fatal("load_level failed for %s" % MAP)

spawners = [a for a in eas.get_all_level_actors() if a.get_class().get_name() == SPAWNER_CLASS]
log("spawners found=%d" % len(spawners))
if len(spawners) != 4:
    fatal("expected 4 spawners, found %d" % len(spawners))

plan = []
for s in spawners:
    loc = s.get_actor_location()
    side = side_of(loc)
    cur = str(s.get_editor_property("EnemyRowOverride"))
    plan.append((s, side, ROWS_BY_SIDE[side], cur))
    log("%s at (%.0f, %.0f) side=%s current=%s target=%s" % (s.get_actor_label(), loc.x, loc.y, side, cur, ROWS_BY_SIDE[side]))
if sorted(p[1] for p in plan) != sorted(ROWS_BY_SIDE.keys()):
    fatal("spawners do not cover the four sides once each: %s" % [p[1] for p in plan])

if VERIFY_ONLY:
    ok = all(p[3] == p[2] for p in plan)
    log("RESULT=VERIFY-ONLY configured=%s" % ok)
    raise SystemExit(0)

for s, side, row, cur in plan:
    s.modify()
    s.set_editor_property("EnemyRowOverride", row)
    back = str(s.get_editor_property("EnemyRowOverride"))
    if back != row:
        fatal("%s readback %s != %s" % (s.get_actor_label(), back, row))

if not les.save_current_level():
    fatal("save_current_level failed")
log("RESULT=OK rows=%s" % [(p[1], p[2]) for p in plan])
