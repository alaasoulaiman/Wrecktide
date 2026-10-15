# pose_ik_off.py - crooked-pose fix attempt 2: disable the IK pass on
# RTG_UEFN_to_UE4_Mannequin (global settings), leaving Root+FK+Post on. Full-body IK was
# planting feet at source-proportioned goals -> bent knees on the differently-proportioned
# Synty skins. Pure FK rotation retarget = source rotations replayed, GTA-clean.
# Reversible: set b_enable_ik back to True. Verify-before-save.
import unreal

RTG = "/Game/Characters/UE4_Mannequin/Rigs/RTG_UEFN_to_UE4_Mannequin"


def log(msg):
    unreal.log("WRECKTIDE-IKOFF %s" % msg)


def fatal(msg):
    unreal.log_error("WRECKTIDE-IKOFF FATAL %s" % msg)
    unreal.log_error("WRECKTIDE-IKOFF RESULT=FAIL (nothing saved)")
    raise SystemExit(1)


rtg = unreal.EditorAssetLibrary.load_asset(RTG)
if rtg is None:
    fatal("RTG load failed")
ctl = unreal.IKRetargeterController.get_controller(rtg)

gs = ctl.get_global_settings()
log("before: " + gs.export_text()[:200])
gs.set_editor_property("bEnableIK", False)
ctl.set_global_settings(gs)

back = ctl.get_global_settings()
log("after:  " + back.export_text()[:200])
if back.get_editor_property("bEnableIK"):
    fatal("bEnableIK still True after set")

if not unreal.EditorAssetLibrary.save_loaded_asset(rtg, only_if_is_dirty=False):
    fatal("save failed")
log("RESULT=OK IK pass disabled (FK-only retarget) + saved")
