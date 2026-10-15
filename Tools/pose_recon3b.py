"""pose_recon3b.py - READ-ONLY: discover AnimPoseExtensions signatures via docstrings, then
dump leg-bone REF-POSE local transforms for driver vs Synty meshes. WRECKTIDE-POSE3B markers."""
import unreal

M = "WRECKTIDE-POSE3B"
MESHES = {
    "UEFN": "/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin",
    "UE4M": "/Game/Characters/UE4_Mannequin/Meshes/SKM_UE4_Mannequin",
    "CAPT": "/Game/PolygonPirates/Meshes/CharactersUE4Mannequin/SK_Chr_Pirate_Captain_01",
    "DECK": "/Game/PolygonPirates/Meshes/CharactersUE4Mannequin/SK_Chr_Pirate_Deckhand_01",
}
LEG_BONES = ["pelvis", "thigh_l", "calf_l", "foot_l", "ball_l", "thigh_r", "calf_r", "foot_r", "ball_r"]


def log(msg):
    unreal.log_warning("%s %s" % (M, msg))


ape = unreal.AnimPoseExtensions
for fname in ("get_reference_pose", "get_ref_bone_pose", "get_bone_pose", "get_bone_names"):
    fn = getattr(ape, fname, None)
    doc = (fn.__doc__ or "no doc").replace("\n", " ")[:400] if fn else "MISSING"
    log("DOC %s :: %s" % (fname, doc))

eal = unreal.EditorAssetLibrary
for label, path in MESHES.items():
    mesh = eal.load_asset(path)
    if mesh is None:
        log("%s load FAIL" % label)
        continue
    skel = mesh.get_editor_property("skeleton")
    if skel is None:
        log("%s has no skeleton" % label)
        continue
    log("%s skeleton = %s" % (label, skel.get_path_name()))
    try:
        pose = ape.get_reference_pose(skel)
    except Exception as e:  # noqa: BLE001
        log("%s get_reference_pose(skeleton) FAIL %r" % (label, e))
        continue
    for bone in LEG_BONES:
        try:
            t = ape.get_bone_pose(pose, unreal.Name(bone), unreal.AnimPoseSpaces.LOCAL)
            r = t.rotation.rotator()
            loc = t.translation
            log("%s %s LOCAL rot P=%.2f Y=%.2f R=%.2f loc=(%.2f, %.2f, %.2f)"
                % (label, bone, r.pitch, r.yaw, r.roll, loc.x, loc.y, loc.z))
        except Exception as e:  # noqa: BLE001
            log("%s %s FAIL %r" % (label, bone, e))
    # pelvis height in component space = proportion ground truth for the IK scaling story
    try:
        t = ape.get_bone_pose(pose, unreal.Name("pelvis"), unreal.AnimPoseSpaces.WORLD)
        log("%s pelvis COMPONENT height z=%.2f" % (label, t.translation.z))
    except Exception as e:  # noqa: BLE001
        log("%s pelvis world FAIL %r" % (label, e))

log("RECON3B DONE")
