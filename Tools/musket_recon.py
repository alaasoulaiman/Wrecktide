"""Read-only musket mesh, pose and enemy montage-slot inspection. Saves no assets."""
import unreal


def report(label, value):
    unreal.log_warning("WRECKTIDE-MUSKET-RECON %s %s" % (label, value))


def read_properties(asset, names):
    for name in names:
        try:
            value = asset.get_editor_property(name)
            report(name, value.export_text() if hasattr(value, "export_text") else str(value))
        except Exception as error:
            report(name, str(error))


mesh = unreal.load_asset("/Game/PolygonPirates/Meshes/Weapons/SM_Wep_Rifle_01")
report("rifle bounds", mesh.get_bounds())
for pose_path in ("Pistol/MF_Pistol_Idle_ADS", "Rifle/MF_Rifle_Idle_ADS"):
    pose = unreal.load_asset("/Game/Characters/Mannequins/Anims/" + pose_path)
    report("pose", pose_path)
    read_properties(pose, ["skeleton", "additive_anim_type", "ref_pose_type", "sequence_length"])
blueprint = unreal.load_asset("/Game/Variant_Combat/Blueprints/AI/BP_CombatEnemy")
cdo = unreal.get_default_object(blueprint.generated_class())
driver = cdo.get_editor_property("mesh")
report("driver mesh", driver.get_skeletal_mesh_asset())
read_properties(driver, ["anim_class", "relative_rotation"])
abp = unreal.load_asset("/Game/Variant_Combat/Anims/ABP_Manny_Combat")
for index in range(8):
    node = unreal.find_object(None, abp.get_path_name() + ":AnimGraph.AnimGraphNode_Slot_%d" % index)
    if node:
        read_properties(node, ["node"])
report("complete", "no assets saved")
