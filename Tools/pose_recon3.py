"""pose_recon3.py - READ-ONLY recon V3 (pose pairing 2026-07-28). The driver-peek proved the
source idle is perfectly straight, so the retarget manufactures the crookedness. This dumps
the numbers to find where: (1) both IK rigs' retarget chains (name, bones, goal), (2) the
RTG's chain mapping + per-chain settings, (3) the retarget op stack, (4) ref-pose leg-bone
local rotations for driver vs Synty meshes via AnimPose. WRECKTIDE-POSE3 markers. Saves nothing."""
import unreal

M = "WRECKTIDE-POSE3"
RTG = "/Game/Characters/UE4_Mannequin/Rigs/RTG_UEFN_to_UE4_Mannequin"
MESHES = {
    "UEFN": "/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin",
    "UE4M": "/Game/Characters/UE4_Mannequin/Meshes/SKM_UE4_Mannequin",
    "CAPT": "/Game/PolygonPirates/Meshes/CharactersUE4Mannequin/SK_Chr_Pirate_Captain_01",
}
LEG_BONES = ["pelvis", "thigh_l", "calf_l", "foot_l", "ball_l", "thigh_r", "calf_r", "foot_r", "ball_r"]


def log(msg):
    unreal.log_warning("%s %s" % (M, msg))


def log_long(prefix, text, cap=3000):
    text = text[:cap]
    for i in range(0, len(text), 380):
        log("%s|%s" % (prefix, text[i:i + 380]))


eal = unreal.EditorAssetLibrary
rtg = eal.load_asset(RTG)
ctl = unreal.IKRetargeterController.get_controller(rtg)

# 1) both IK rigs: retarget chain definitions
for side_name in ("SOURCE", "TARGET"):
    side = getattr(unreal.RetargetSourceOrTarget, side_name)
    rig = ctl.get_ik_rig(side)
    if rig is None:
        log("%s ik_rig None" % side_name)
        continue
    rigctl = unreal.IKRigController.get_controller(rig)
    try:
        chains = rigctl.get_retarget_chains()
        for c in chains:
            try:
                nm = c.get_editor_property("chain_name")
                sb = c.get_editor_property("start_bone").get_editor_property("bone_name")
                eb = c.get_editor_property("end_bone").get_editor_property("bone_name")
                gl = c.get_editor_property("ik_goal_name")
                log("%s rigchain %s: %s -> %s goal=%s" % (side_name, nm, sb, eb, gl))
            except Exception as e:  # noqa: BLE001
                log("%s rigchain struct read FAIL %r; raw=%s" % (side_name, e, c.export_text()[:200]))
    except Exception as e:  # noqa: BLE001
        log("%s get_retarget_chains FAIL %r" % (side_name, e))

# 2) RTG chain mapping + settings
try:
    all_cs = ctl.get_all_chain_settings()
    log("chain settings count = %d" % len(all_cs))
    for cs in all_cs:
        try:
            log_long("chainset", cs.export_text(), cap=600)
        except Exception as e:  # noqa: BLE001
            log("chainset export FAIL %r" % e)
except Exception as e:  # noqa: BLE001
    log("get_all_chain_settings FAIL %r" % e)

target_rig = ctl.get_ik_rig(unreal.RetargetSourceOrTarget.TARGET)
if target_rig is not None:
    try:
        tchains = unreal.IKRigController.get_controller(target_rig).get_retarget_chains()
        for c in tchains:
            nm = c.get_editor_property("chain_name")
            try:
                src = ctl.get_source_chain(nm)
                log("map target %s <- source %s" % (nm, src))
            except Exception as e:  # noqa: BLE001
                log("get_source_chain(%s) FAIL %r" % (nm, e))
    except Exception as e:  # noqa: BLE001
        log("target chain sweep FAIL %r" % e)

# 3) op stack
try:
    n = ctl.get_num_retarget_ops()
    log("retarget ops = %d" % n)
    for i in range(n):
        try:
            nm = ctl.get_op_name(i)
            en = ctl.get_retarget_op_enabled(i)
            log("op[%d] %s enabled=%s" % (i, nm, en))
        except Exception as e:  # noqa: BLE001
            log("op[%d] read FAIL %r" % (i, e))
except Exception as e:  # noqa: BLE001
    log("op stack FAIL %r" % e)

# 4) ref-pose leg rotations per mesh via AnimPose
ape = getattr(unreal, "AnimPoseExtensions", None)
if ape is None:
    log("AnimPoseExtensions MISSING")
else:
    api = sorted(m for m in dir(ape) if not m.startswith("_"))
    log("AnimPoseExtensions API: %s" % ", ".join(api))
    for label, path in MESHES.items():
        mesh = eal.load_asset(path)
        if mesh is None:
            log("%s mesh load FAIL" % label)
            continue
        pose = None
        for attempt in ("get_ref_pose_mesh", "get_ref_pose"):
            fn = getattr(ape, attempt, None)
            if fn is None:
                continue
            try:
                p = unreal.AnimPose()
                r = fn(mesh, p)
                pose = p if r is None else (r if isinstance(r, unreal.AnimPose) else p)
                log("%s ref pose via %s OK" % (label, attempt))
                break
            except Exception as e:  # noqa: BLE001
                log("%s %s form-a FAIL %r" % (label, attempt, e))
                try:
                    r = fn(mesh)
                    if isinstance(r, unreal.AnimPose):
                        pose = r
                        log("%s ref pose via %s(mesh) OK" % (label, attempt))
                        break
                except Exception as e2:  # noqa: BLE001
                    log("%s %s form-b FAIL %r" % (label, attempt, e2))
        if pose is None:
            log("%s NO ref pose accessor worked" % label)
            continue
        for bone in LEG_BONES:
            try:
                t = ape.get_bone_pose(pose, unreal.Name(bone), unreal.AnimPoseSpaces.LOCAL)
                q = t.rotation
                r = q.rotator()
                loc = t.translation
                log("%s %s local rot=(P%.2f Y%.2f R%.2f) loc=(%.2f, %.2f, %.2f)"
                    % (label, bone, r.pitch, r.yaw, r.roll, loc.x, loc.y, loc.z))
            except Exception as e:  # noqa: BLE001
                log("%s %s bone read FAIL %r" % (label, bone, e))

log("RECON3 DONE")
