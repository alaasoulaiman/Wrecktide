"""Mangrove Hollow: stilt huts, drowned ruins, roots and a landing marker.

Deletes only DRESS3_* actors. Placement uses mesh bounds, with guaranteed cube
blockers for solid huts. Reserved terrain samples and village/altar paths stay
clear. Run after island3_build.py; optional verify never writes. Existing island
dressing and the sea volume are never rebuilt.
"""
import math
import sys
import unreal

eal = unreal.EditorAssetLibrary
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
tools = unreal.WrecktideEditorTools
ROOT = "/Game/PolygonPirates/Meshes/"
COUNT = 0


def log(msg):
    unreal.log_warning("WRECKTIDE-DRESS3 " + msg)


def need(ok, msg):
    if not ok:
        unreal.log_error("WRECKTIDE-DRESS3 RESULT=FAIL " + msg)
        raise SystemExit(1)


def label(actor, name):
    global COUNT
    COUNT += 1
    actor.set_actor_label("DRESS3_%s_%03d" % (name, COUNT))
    actor.set_folder_path("MangroveHollow/Scenery")


def box(name, x, y, bottom, sx, sy, sz, mat, collision=True):
    actor = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(x, y, bottom + sz / 2), unreal.Rotator())
    need(actor, name)
    comp = actor.get_editor_property("StaticMeshComponent")
    comp.set_static_mesh(eal.load_asset("/Engine/BasicShapes/Cube"))
    comp.set_material(0, mat)
    if not collision:
        comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    actor.set_actor_scale3d(unreal.Vector(sx / 100, sy / 100, sz / 100))
    label(actor, name)
    return actor


def prop(name, rel, x, y, z, width, yaw=0):
    mesh = eal.load_asset(ROOT + rel)
    need(mesh, "mesh " + rel)
    bounds = mesh.get_bounding_box()
    size = bounds.max - bounds.min
    scale = width / max(size.x, size.y)
    centre = (bounds.max + bounds.min) * 0.5
    angle = math.radians(yaw)
    dx = scale * (centre.x * math.cos(angle) - centre.y * math.sin(angle))
    dy = scale * (centre.x * math.sin(angle) + centre.y * math.cos(angle))
    actor = eas.spawn_actor_from_class(unreal.StaticMeshActor,
        unreal.Vector(x - dx, y - dy, z - bounds.min.z * scale), unreal.Rotator(yaw=yaw))
    need(actor, name)
    comp = actor.get_editor_property("StaticMeshComponent")
    comp.set_static_mesh(mesh)
    comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))
    label(actor, name)
    log("%s footprint=%.0fx%.0f height=%.0f at=(%.0f,%.0f,%.0f)" %
        (name, size.x * scale, size.y * scale, size.z * scale, x, y, z))
    return size.z * scale


def main():
    need(les.load_level("/Game/Lvl_Island"), "load")
    old = [a for a in eas.get_all_level_actors() if str(a.get_actor_label()).startswith("DRESS3_")]
    if "verify" in sys.argv:
        huts = sum("DRESS3_hut_" in str(a.get_actor_label()) for a in old)
        trees = sum("DRESS3_mangrove_" in str(a.get_actor_label()) for a in old)
        need(huts == 3 and trees >= 20, "huts and mangroves")
        log("RESULT=OK verify actors=%d huts=%d mangroves=%d" % (len(old), huts, trees))
        return
    for actor in old:
        need(eas.destroy_actor(actor), "clear scenery")
    wood = eal.load_asset("/Game/Materials/MI_MangroveWood")
    stone = eal.load_asset("/Game/Materials/MI_MangroveStone")
    need(wood and stone, "run island3_build first")
    # Small Synty shanties on exposed stilts; the open central deck is the village street.
    for x, y, yaw in [(-12400, 12450, 90), (-11450, 12500, 90), (-12400, 11850, 0)]:
        height = prop("stilts", "Buildings/SM_Bld_Shanty_Base_Stilts_01", x, y, 60, 440, yaw)
        hut_height = prop("hut", "Buildings/SM_Bld_Shanty_Room_01", x, y, 60 + height, 480, yaw)
        # Four posts and the hut body are kept out of the navmesh as one compact obstacle.
        blocker = box("hut_base", x, y, 60, 380, 380, height + hut_height, wood)
        blocker.set_actor_hidden_in_game(True)
        prop("roof", "Buildings/SM_Bld_Shanty_Roof_01", x, y, 60 + height + hut_height, 570, yaw)
    # Sunken walls in the central mud, separated by broad walkable paths.
    for x, y, yaw in [(-14700, 12400, 20), (-13200, 13550, -15), (-14700, 13700, 70)]:
        prop("drowned", "Buildings/SM_Bld_Shanty_Room_02", x, y, -180, 580, yaw)
    # Mangrove roots frame the bog and the village, with the landing approach left open.
    spots = [(-17000, 11300), (-17200, 13800), (-16800, 15700), (-14500, 15800),
             (-12300, 15600), (-11300, 14800), (-10800, 13000), (-10800, 11000),
             (-14900, 11800), (-13200, 12100), (-15100, 13400), (-13300, 14300),
             (-15100, 14500), (-12300, 13900), (-16000, 11700), (-11800, 13800),
             (-17700, 10100), (-17700, 15100), (-15100, 17000), (-11600, 16600),
             (-10100, 15200), (-10000, 10400), (-13000, 9500), (-16000, 9500)]
    for i, (x, y) in enumerate(spots):
        z = 0 if abs(x + 14000) < 3000 and abs(y - 13000) < 3000 else -40
        if abs(x + 14000) < 1300 and abs(y - 13000) < 1300:
            z = -20
        prop("mangrove", "Environments/SM_Env_Mangrove_Tree_0%d" % (1 + i % 2), x, y, z - 12, 650 + (i % 3) * 130, i * 47)
    # Landing guide posts face the approach from Wreck Cove; no rails obstruct the beach.
    for x in (-11450, -10950):
        box("landing_post", x, 9300, -40, 32, 32, 250, wood)
        prop("landing_lantern", "Props/SM_Prop_Lantern_02", x, 9300, 210, 75)
    for x, y in [(-12550, 12800), (-11200, 12800), (-15775, 14675)]:
        box("lantern_post", x, y, 80, 25, 25, 260, wood)
        prop("lantern", "Props/SM_Prop_Lantern_02", x, y, 340, 65)
    prop("altar_relic", "Props/SM_Prop_Skeleton_Cage_01", -16100, 14700, 80, 180)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    need(tools.build_navigation_synchronously(world, 300) >= 0, "navigation")
    need(les.save_current_level(), "save")
    log("RESULT=OK actors=%d village=(-11900,12100) landing=(-11200,9900)" % COUNT)


main()
