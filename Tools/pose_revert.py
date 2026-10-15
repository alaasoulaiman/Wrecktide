# pose_revert.py - revert both pose experiments on RTG_UEFN_to_UE4_Mannequin to the exact
# state that was play tested: bEnableIK=True + TARGET current pose = Default Pose (AutoAligned
# left in place but inactive). Verify-before-save.
import unreal

RTG = "/Game/Characters/UE4_Mannequin/Rigs/RTG_UEFN_to_UE4_Mannequin"


def log(msg):
    unreal.log("WRECKTIDE-POSEREVERT %s" % msg)


def fatal(msg):
    unreal.log_error("WRECKTIDE-POSEREVERT FATAL %s" % msg)
    raise SystemExit(1)


rtg = unreal.EditorAssetLibrary.load_asset(RTG)
ctl = unreal.IKRetargeterController.get_controller(rtg)

gs = ctl.get_global_settings()
gs.set_editor_property("bEnableIK", True)
ctl.set_global_settings(gs)
if not ctl.get_global_settings().get_editor_property("bEnableIK"):
    fatal("bEnableIK restore failed")
log("bEnableIK restored True")

side = unreal.RetargetSourceOrTarget.TARGET
ctl.set_current_retarget_pose(unreal.Name("Default Pose"), side)
now = str(ctl.get_current_retarget_pose_name(side))
if now != "Default Pose":
    fatal("pose restore failed: %s" % now)
log("TARGET pose restored to Default Pose")

if not unreal.EditorAssetLibrary.save_loaded_asset(rtg, only_if_is_dirty=False):
    fatal("save failed")
log("RESULT=OK baseline state restored + saved")
