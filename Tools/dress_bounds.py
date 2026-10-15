"""dress_bounds.py - READ-ONLY: dump local bounding boxes + collision prim counts for the Synty
meshes the island dressing pass wants to place, so placements are computed from real sizes
instead of guessed. WRECKTIDE-BOUNDS markers. Saves nothing."""
import unreal

M = "WRECKTIDE-BOUNDS"
ROOT = "/Game/PolygonPirates/Meshes/"
MESHES = [
    "Buildings/SM_Bld_Shanty_Preset_Bar_01", "Buildings/SM_Bld_Shanty_Preset_01", "Buildings/SM_Bld_Shanty_Preset_02",
    "Buildings/SM_Bld_Shanty_Preset_03", "Buildings/SM_Bld_Rickety_House_01", "Buildings/SM_Bld_Rickety_House_02",
    "Buildings/SM_Bld_Tent_01", "Buildings/SM_Bld_Tent_02", "Buildings/SM_Bld_Tent_03", "Buildings/SM_Bld_Tent_05",
    "Buildings/SM_Bld_Rickety_Dock_01", "Buildings/SM_Bld_Rickety_Dock_02", "Buildings/SM_Bld_Rickety_Dock_03",
    "Buildings/SM_Bld_Rickety_Dock_Platform_01", "Buildings/SM_Bld_Rickety_Dock_Stairs_01",
    "Buildings/SM_Bld_Dock_01", "Buildings/SM_Bld_Dock_02", "Buildings/SM_Bld_Dock_03", "Buildings/SM_Bld_Dock_Stairs_01",
    "Buildings/SM_Bld_Fort_Tower_01", "Buildings/SM_Bld_Fort_Wall_01", "Buildings/SM_Bld_Fort_Wall_02",
    "Buildings/SM_Bld_Fort_Base_01", "Buildings/SM_Bld_Fort_Door_01", "Buildings/SM_Bld_Fort_Stairs_01",
    "Buildings/SM_Bld_Stone_Wall_01", "Buildings/SM_Bld_Shop_01",
    "Props/SM_Prop_Campfire_01", "Props/SM_Prop_Table_01", "Props/SM_Prop_Stool_01", "Props/SM_Prop_Barrel_01",
    "Props/SM_Prop_Crate_01", "Props/SM_Prop_Chest_Treasure_01", "Props/SM_Prop_BottleTorch_01",
    "Props/SM_Prop_Lantern_01", "Props/SM_Prop_Cannon_01", "Props/SM_Prop_CannonBalls_01", "Props/SM_Prop_Well_01",
    "Props/SM_Prop_Pole_01", "Props/SM_Prop_Gallows_01", "Props/SM_Prop_Grave_01", "Props/SM_Prop_Skeleton_Cage_01",
    "Props/SM_Prop_Skeleton_Ground_01", "Props/SM_Prop_TreasurePile_01", "Props/SM_Prop_Shipwreck_01",
    "Props/SM_Prop_Shipwreck_01_Sails", "Props/SM_Prop_Anchor_01", "Props/SM_Prop_Sack_01", "Props/SM_Prop_Fence_01",
    "Props/SM_Prop_Rope_Fence_01", "Props/SM_Prop_FishRack_01", "Props/SM_Prop_Sign_Attachment_01",
    "Environments/SM_Env_PalmTree_01", "Environments/SM_Env_PalmTree_Tall_01", "Environments/SM_Env_Bush_01",
    "Environments/SM_Env_Rock_Skull_01", "Environments/SM_Env_Rock_Huge_01", "Environments/SM_Env_Rock_Large_01",
    "Environments/SM_Env_Background_Island_01", "Environments/SM_Env_Background_Island_03",
    "Environments/SM_Env_Cloud_01", "Environments/SM_Env_GrassPatch_01", "Environments/SM_Env_Mangrove_Tree_01",
    "Ships/SM_Veh_Boat_Medium_01_Hull", "Ships/SM_Veh_Boat_Medium_01_Mast", "Ships/SM_Veh_Boat_Medium_01_Sails_Up_Pirate",
    "Ships/SM_Veh_Boat_Rowing_01_Hull", "Ships/SM_Veh_Boat_Small_01_Hull_Pirate", "Ships/SM_Veh_Flag_Pirate_01",
    "Ships/SM_Veh_Boat_Large_01_Hull_Pirate",
]


def log(msg):
    unreal.log_warning("%s %s" % (M, msg))


eal = unreal.EditorAssetLibrary
for rel in MESHES:
    path = ROOT + rel
    mesh = eal.load_asset(path)
    if mesh is None:
        log("%s MISSING" % rel)
        continue
    box = mesh.get_bounding_box()
    mn, mx = box.min, box.max
    try:
        prims = unreal.EditorStaticMeshLibrary.get_simple_collision_count(mesh)
    except Exception:  # noqa: BLE001
        try:
            prims = unreal.StaticMeshEditorSubsystem().get_simple_collision_count(mesh)
        except Exception:  # noqa: BLE001
            prims = -1
    log("%s min=(%.0f,%.0f,%.0f) max=(%.0f,%.0f,%.0f) size=(%.0f,%.0f,%.0f) prims=%s" % (
        rel.split("/")[-1], mn.x, mn.y, mn.z, mx.x, mx.y, mx.z, mx.x - mn.x, mx.y - mn.y, mx.z - mn.z, prims))
log("BOUNDS DONE")
