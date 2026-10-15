"""island_sand.py - give the island's three cube tiers a sand material (2026-09-02 night).

The tiers are /Engine/BasicShapes/Cube actors placed by build_island_level.py (beach 140x140,
field 100x100, hill 30x30, all z-scale 0.8) and render as the engine's grey checker. This makes a
MaterialInstanceConstant of BasicShapeMaterial with a sand Color and overrides slot 0 on exactly
those three actors (surf walls are hidden cubes and are left alone). Idempotent, fail-closed,
verify-before-save. Markers: WRECKTIDE-SAND. Usage: run_py.cmd <this file> [verify]
"""
import sys
import unreal

M = "WRECKTIDE-SAND"
MAP = "/Game/Lvl_Island"
MI_PATH = "/Game/Materials/MI_IslandSand"
PARENT = "/Engine/BasicShapes/BasicShapeMaterial"
CUBE = "/Engine/BasicShapes/Cube"
SAND = unreal.LinearColor(0.78, 0.68, 0.46, 1.0)
VERIFY_ONLY = any(a.lower() == "verify" for a in sys.argv[1:])


def log(msg):
    unreal.log_warning("%s %s" % (M, msg))


def fatal(msg):
    unreal.log_error("%s FATAL %s" % (M, msg))
    unreal.log_error("%s RESULT=FAIL (nothing saved)" % M)
    raise SystemExit(1)


eal = unreal.EditorAssetLibrary
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

# ---- 1. the material instance ----
mi = eal.load_asset(MI_PATH) if eal.does_asset_exist(MI_PATH) else None
if mi is None and not VERIFY_ONLY:
    parent = eal.load_asset(PARENT)
    if parent is None:
        fatal("parent material missing: %s" % PARENT)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    factory = unreal.MaterialInstanceConstantFactoryNew()
    mi = tools.create_asset("MI_IslandSand", "/Game/Materials", unreal.MaterialInstanceConstant, factory)
    if mi is None:
        fatal("create_asset failed for %s" % MI_PATH)
    unreal.MaterialEditingLibrary.set_material_instance_parent(mi, parent)
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mi, "Color", SAND)
    unreal.MaterialEditingLibrary.update_material_instance(mi)
    if not eal.save_asset(MI_PATH, only_if_is_dirty=False):
        fatal("save failed for %s" % MI_PATH)
    log("created %s parent=%s color=%s" % (MI_PATH, PARENT, SAND))
if mi is None:
    fatal("material instance missing (verify mode)")
got = unreal.MaterialEditingLibrary.get_material_instance_vector_parameter_value(mi, "Color")
log("MI color readback = %s" % got)

# ---- 2. the tier actors ----
if not les.load_level(MAP):
    fatal("load_level failed for %s" % MAP)
tiers = []
for a in eas.get_all_level_actors():
    if not isinstance(a, unreal.StaticMeshActor):
        continue
    comp = a.static_mesh_component
    mesh = comp.static_mesh if comp else None
    if mesh is None or mesh.get_path_name().split(".")[0] != CUBE:
        continue
    if a.is_hidden_ed() or comp.hidden_in_game:
        continue
    scale = a.get_actor_scale3d()
    if scale.x < 30.0 or scale.y < 30.0:
        continue
    # WRECK COVE ONLY (2026-09-07). Gallows Reach added its own big cube tiers to the same level and
    # sands them itself in Tools\island2_build.py, so they must not count here or the exact-3 check
    # below would fail and this script would refuse to run at all.
    loc = a.get_actor_location()
    if abs(loc.x) > 6000.0 or abs(loc.y) > 6000.0:
        continue
    tiers.append((a, scale))
log("tier cubes found=%d %s" % (len(tiers), [(t[0].get_actor_label(), round(t[1].x), round(t[1].y)) for t in tiers]))
if len(tiers) != 3:
    fatal("expected exactly 3 tier cubes (beach/field/hill), found %d" % len(tiers))

if VERIFY_ONLY:
    ok = all((t[0].static_mesh_component.get_material(0) is not None
              and t[0].static_mesh_component.get_material(0).get_path_name().split(".")[0] == MI_PATH) for t in tiers)
    log("RESULT=VERIFY-ONLY sand=%s" % ok)
    raise SystemExit(0)

for a, scale in tiers:
    a.modify()
    a.static_mesh_component.set_material(0, mi)
    back = a.static_mesh_component.get_material(0)
    if back is None or back.get_path_name().split(".")[0] != MI_PATH:
        fatal("%s material readback failed" % a.get_actor_label())
    log("%s (%dx%d) -> %s" % (a.get_actor_label(), scale.x, scale.y, MI_PATH))

if not les.save_current_level():
    fatal("save_current_level failed")
log("RESULT=OK 3 tiers sanded")
