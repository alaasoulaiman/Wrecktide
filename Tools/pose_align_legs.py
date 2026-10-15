"""pose_align_legs.py - rebuild the TARGET retarget pose with per-bone alignment (2026-09-04 night).

Gait numbers (tools\\gait_analyze.py on a -PoseProbe raw log) showed the pirate skin copying the
mannequin's knee FLEXION exactly while its knee BEND PLANE was off: left knee 21 cm outside the
hip->ankle line vs 7 cm, right knee 23 cm inside vs 3 cm, thighs splayed +48 / -41 deg. The 07-28
'SyntyStraight' pose was made with the default ChainToChain auto-align, which only matches each leg's
overall direction; the Synty bind pose's own knee bow survives and rotates with every stride.
This creates a fresh pose from the bind pose and aligns it MESH_TO_MESH (every mapped bone's
direction to its source bone), logs the resulting leg offsets, makes it current, saves.
Reversible: set_current_retarget_pose('SyntyStraight' or 'Default Pose', TARGET).
Usage: run_py.cmd <this file> [method]   method = MESH_TO_MESH (default) | CHAIN_TO_CHAIN | LOCAL_ROTATION_AXES
Markers: WRECKTIDE-LEGALIGN."""
import sys
import unreal

M = "WRECKTIDE-LEGALIGN"
RTG = "/Game/Characters/UE4_Mannequin/Rigs/RTG_UEFN_to_UE4_Mannequin"
METHOD_NAME = (sys.argv[1] if len(sys.argv) > 1 else "MESH_TO_MESH").upper()
POSE_NAME = "SyntyMeshAligned" if METHOD_NAME == "MESH_TO_MESH" else "Synty_" + METHOD_NAME
LEG_BONES = ["pelvis", "thigh_l", "calf_l", "foot_l", "thigh_r", "calf_r", "foot_r", "spine_01"]


def log(msg):
    unreal.log_warning("%s %s" % (M, msg))


def fatal(msg):
    unreal.log_error("%s FATAL %s" % (M, msg))
    unreal.log_error("%s RESULT=FAIL (nothing saved)" % M)
    raise SystemExit(1)


eal = unreal.EditorAssetLibrary
rtg = eal.load_asset(RTG)
if rtg is None:
    fatal("RTG load failed")
ctl = unreal.IKRetargeterController.get_controller(rtg)
side = unreal.RetargetSourceOrTarget.TARGET

before = str(ctl.get_current_retarget_pose_name(side))
log("target pose before = %s" % before)

method = getattr(unreal.RetargetAutoAlignMethod, METHOD_NAME, None)
if method is None:
    fatal("unknown method %s; have %s" % (METHOD_NAME, [m for m in dir(unreal.RetargetAutoAlignMethod) if m.isupper()]))

# a fresh pose from the bind pose (Default Pose has zero offsets), so nothing from SyntyStraight leaks in
try:
    ctl.duplicate_retarget_pose(unreal.Name("Default Pose"), unreal.Name(POSE_NAME), side)
    log("duplicated Default Pose -> %s" % POSE_NAME)
except Exception as e:  # noqa: BLE001
    log("duplicate quirk (%r), pose may already exist; resetting it" % e)
ctl.set_current_retarget_pose(unreal.Name(POSE_NAME), side)
if str(ctl.get_current_retarget_pose_name(side)) != POSE_NAME:
    fatal("could not make %s current" % POSE_NAME)
try:
    ctl.reset_retarget_pose(unreal.Name(POSE_NAME), unreal.Array(unreal.Name), side)
    log("pose reset to bind")
except Exception as e:  # noqa: BLE001
    log("reset quirk (non-fatal): %r" % e)

ctl.auto_align_all_bones(side, method)
log("auto_align_all_bones(TARGET, %s) done" % METHOD_NAME)

for bone in LEG_BONES:
    try:
        q = ctl.get_rotation_offset_for_retarget_pose_bone(unreal.Name(bone), side)
        r = q.rotator()
        log("offset %s = P=%.2f Y=%.2f R=%.2f" % (bone, r.pitch, r.yaw, r.roll))
    except Exception as e:  # noqa: BLE001
        log("offset %s unreadable: %r" % (bone, e))

if not eal.save_loaded_asset(rtg, only_if_is_dirty=False):
    fatal("save failed")
log("RESULT=OK target pose = %s (%s) saved; revert = set_current_retarget_pose('%s')" % (POSE_NAME, METHOD_NAME, before))
