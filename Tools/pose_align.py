# pose_align.py - crooked-pose fix attempt 1 (night 2026-07-28): create a duplicate retarget
# pose on a side of RTG_UEFN_to_UE4_Mannequin and run UE 5.6's auto_align_all_bones on it.
# Fully reversible: reverting = set_current_retarget_pose('Default Pose') on that side.
# Usage: pass side via env-like arg file? Simplest: edit SIDE below per run. Verify-before-save.
#   UnrealEditor-Cmd <proj> -run=pythonscript -script=<full path> \
#     "-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities" -unattended -nosplash -nullrhi
import unreal

RTG = "/Game/Characters/UE4_Mannequin/Rigs/RTG_UEFN_to_UE4_Mannequin"
SIDE_NAME = "TARGET"          # TARGET = the Synty/UE4-mannequin side (the skins)
POSE_NAME = "AutoAligned"


def log(msg):
    unreal.log("WRECKTIDE-POSEALIGN %s" % msg)


def fatal(msg):
    unreal.log_error("WRECKTIDE-POSEALIGN FATAL %s" % msg)
    unreal.log_error("WRECKTIDE-POSEALIGN RESULT=FAIL (nothing saved)")
    raise SystemExit(1)


rtg = unreal.EditorAssetLibrary.load_asset(RTG)
if rtg is None:
    fatal("RTG load failed")
ctl = unreal.IKRetargeterController.get_controller(rtg)
side = getattr(unreal.RetargetSourceOrTarget, SIDE_NAME)

before = str(ctl.get_current_retarget_pose_name(side))
log("side=%s current pose before = %s" % (SIDE_NAME, before))

# create-or-reuse the working pose, then make it current
existing = []
try:
    existing = [str(n) for n in (ctl.get_retarget_poses(side) or {}).keys()]
except Exception as e:  # noqa: BLE001
    log("pose enumerate quirk: %s" % e)
log("existing poses = %s" % existing)
if POSE_NAME not in existing:
    try:
        ctl.duplicate_retarget_pose(unreal.Name(before), unreal.Name(POSE_NAME), side)
        log("duplicated %s -> %s" % (before, POSE_NAME))
    except Exception as e:  # noqa: BLE001
        fatal("duplicate_retarget_pose failed: %r" % e)
ctl.set_current_retarget_pose(unreal.Name(POSE_NAME), side)
now = str(ctl.get_current_retarget_pose_name(side))
if now != POSE_NAME:
    fatal("set_current_retarget_pose did not take: %s" % now)

# the alignment itself; signature differs across builds - try the simple form first
aligned = False
try:
    ctl.auto_align_all_bones(side)
    aligned = True
    log("auto_align_all_bones(side) OK")
except Exception as e1:  # noqa: BLE001
    log("1-arg form failed (%r); trying with method enum" % e1)
    try:
        method_enum = getattr(unreal, "RetargetAutoAlignMethod", None)
        if method_enum is not None:
            members = [m for m in dir(method_enum) if not m.startswith("_") and m.isupper()]
            log("align methods available: %s" % members)
            ctl.auto_align_all_bones(side, getattr(method_enum, members[0]))
            aligned = True
            log("auto_align_all_bones(side, %s) OK" % members[0])
    except Exception as e2:  # noqa: BLE001
        fatal("auto-align failed both forms: %r" % e2)
if not aligned:
    fatal("auto-align never ran")

if not unreal.EditorAssetLibrary.save_loaded_asset(rtg, only_if_is_dirty=False):
    fatal("save failed")
log("RESULT=OK side=%s pose=%s aligned + saved (revert = set Default Pose current)" % (SIDE_NAME, POSE_NAME))
