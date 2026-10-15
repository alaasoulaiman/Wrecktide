"""pose_fix_synty.py - crooked-pose fix attempt (day 2026-07-28, pairing session).
ROOT CAUSE (recon2): the RTG's TARGET preview mesh was the stock SKM_UE4_Mannequin, so the
overnight auto-align computed ~3-degree leg corrections for a mesh that was never crooked.
The runtime targets are the SYNTY meshes (captain/deckhand, shared skeleton). Fix: preview
mesh -> Synty captain, duplicate Default Pose -> 'SyntyStraight', auto-align against the
Synty bind pose. FAIL-CLOSED: if the recomputed leg offsets are still ~zero, abort with
NOTHING saved. Revert lever: pose_revert2.py (set Default Pose current + mannequin preview).
Usage: UnrealEditor-Cmd <proj> -run=pythonscript -script=<this>
  "-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities" -unattended -nosplash -nullrhi
Judge by WRECKTIDE-POSEFIX markers in Saved/Logs/Wrecktide.log, never the exit code."""
import math

import unreal

RTG = "/Game/Characters/UE4_Mannequin/Rigs/RTG_UEFN_to_UE4_Mannequin"
CAPTAIN = "/Game/PolygonPirates/Meshes/CharactersUE4Mannequin/SK_Chr_Pirate_Captain_01"
POSE_NAME = "SyntyStraight"
LEG_BONES = ["thigh_l", "calf_l", "foot_l", "thigh_r", "calf_r", "foot_r"]
# quat vector-part magnitude ~= sin(angle/2); 0.02 ~= 2.3 degrees
LEG_OFFSET_MIN = 0.02


def log(msg):
    unreal.log_warning("WRECKTIDE-POSEFIX %s" % msg)


def fatal(msg):
    unreal.log_error("WRECKTIDE-POSEFIX FATAL %s" % msg)
    unreal.log_error("WRECKTIDE-POSEFIX RESULT=FAIL (nothing saved)")
    raise SystemExit(1)


eal = unreal.EditorAssetLibrary
rtg = eal.load_asset(RTG)
if rtg is None:
    fatal("RTG load failed")
captain = eal.load_asset(CAPTAIN)
if captain is None:
    fatal("captain mesh load failed")
ctl = unreal.IKRetargeterController.get_controller(rtg)
target = unreal.RetargetSourceOrTarget.TARGET

before_preview = ctl.get_preview_mesh(target)
log("preview before = %s" % (before_preview.get_path_name() if before_preview else None))

# 1) point the target preview at the ACTUAL runtime mesh family
ctl.set_preview_mesh(target, captain)
now_preview = ctl.get_preview_mesh(target)
if now_preview is None or "Pirate_Captain" not in now_preview.get_name():
    fatal("set_preview_mesh did not take: %s" % (now_preview.get_name() if now_preview else None))
log("preview now = %s" % now_preview.get_path_name())

# 2) working pose: duplicate Default Pose -> SyntyStraight, make it current
existing = [str(n) for n in (ctl.get_retarget_poses(target) or {}).keys()]
log("existing target poses = %s" % existing)
if POSE_NAME not in existing:
    try:
        ctl.duplicate_retarget_pose(unreal.Name("Default Pose"), unreal.Name(POSE_NAME), target)
        log("duplicated Default Pose -> %s" % POSE_NAME)
    except Exception as e:  # noqa: BLE001
        fatal("duplicate_retarget_pose failed: %r" % e)
ctl.set_current_retarget_pose(unreal.Name(POSE_NAME), target)
if str(ctl.get_current_retarget_pose_name(target)) != POSE_NAME:
    fatal("set_current_retarget_pose did not take")
log("current target pose = %s" % POSE_NAME)

# 3) auto-align, now against the Synty bind pose (form discovery per pose_align lesson)
try:
    ctl.auto_align_all_bones(target)
    log("auto_align_all_bones(target) OK")
except Exception as e1:  # noqa: BLE001
    log("1-arg form failed (%r); trying method enum" % e1)
    method_enum = getattr(unreal, "RetargetAutoAlignMethod", None)
    if method_enum is None:
        fatal("no RetargetAutoAlignMethod enum and 1-arg form failed")
    members = [m for m in dir(method_enum) if not m.startswith("_") and m.isupper()]
    try:
        ctl.auto_align_all_bones(target, getattr(method_enum, members[0]))
        log("auto_align_all_bones(target, %s) OK" % members[0])
    except Exception as e2:  # noqa: BLE001
        fatal("auto-align failed both forms: %r" % e2)

# 4) FAIL-CLOSED verify: the leg offsets must be real this time
max_leg = 0.0
for bone in LEG_BONES:
    try:
        q = ctl.get_rotation_offset_for_retarget_pose_bone(unreal.Name(bone), target)
        mag = math.sqrt(q.x * q.x + q.y * q.y + q.z * q.z)
        max_leg = max(max_leg, mag)
        log("leg offset %s = (%.4f, %.4f, %.4f, %.4f) mag=%.4f (~%.1f deg)"
            % (bone, q.x, q.y, q.z, q.w, mag, 2.0 * math.degrees(math.asin(min(1.0, mag)))))
    except Exception as e:  # noqa: BLE001
        log("offset read %s FAIL %r" % (bone, e))
if max_leg < LEG_OFFSET_MIN:
    fatal("leg offsets still ~zero (max mag %.4f) - Synty preview did not change the align; "
          "hypothesis dead, aborting with nothing saved" % max_leg)

# 5) save
if not eal.save_loaded_asset(rtg, only_if_is_dirty=False):
    fatal("save failed")
log("RESULT=OK pose=%s aligned-for-Synty + saved (max leg offset mag %.4f)" % (POSE_NAME, max_leg))
