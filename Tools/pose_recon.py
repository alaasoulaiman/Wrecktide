"""pose_recon.py - READ-ONLY recon for the crooked-pose chunk (night 2026-07-28).
Dumps RTG_UEFN_to_UE4_Mannequin's retarget-pose + per-chain settings surface so the fix
iteration has real data. WRECKTIDE-POSE markers. Saves nothing."""
import unreal

M = "WRECKTIDE-POSE"

def log(msg):
    unreal.log_warning(f"{M} {msg}")

eal = unreal.EditorAssetLibrary
rtg = eal.load_asset("/Game/Characters/UE4_Mannequin/Rigs/RTG_UEFN_to_UE4_Mannequin")
if rtg is None:
    log("RTG LOAD FAIL")
    raise SystemExit(1)

ctl = unreal.IKRetargeterController.get_controller(rtg)
log("controller API: " + ", ".join(sorted(m for m in dir(ctl) if not m.startswith("_") and any(
    k in m.lower() for k in ("pose", "align", "chain", "setting", "global", "root")))[:60]))

# retarget poses on each side
for side_name in ("SOURCE", "TARGET"):
    side = getattr(unreal.RetargetSourceOrTarget, side_name)
    try:
        cur = ctl.get_current_retarget_pose_name(side)
        names = list(ctl.get_retarget_pose_names(side)) if hasattr(ctl, "get_retarget_pose_names") else []
        log(f"{side_name} current pose={cur} available={[str(n) for n in names]}")
    except Exception as e:  # noqa: BLE001
        log(f"{side_name} pose read fail: {e}")

# global + per-chain settings
try:
    gs = ctl.get_global_settings()
    log("global settings = " + gs.export_text()[:400])
except Exception as e:  # noqa: BLE001
    log(f"global settings fail: {e}")

try:
    for name in ("Spine", "LeftLeg", "RightLeg", "LeftArm", "RightArm", "Root", "Neck", "Head"):
        try:
            cs = ctl.get_retarget_chain_settings(unreal.Name(name)) if hasattr(ctl, "get_retarget_chain_settings") else None
            if cs is not None:
                log(f"chain {name} = " + cs.export_text()[:300])
        except Exception as e:  # noqa: BLE001
            log(f"chain {name} fail: {e}")
except Exception as e:  # noqa: BLE001
    log(f"chain settings sweep fail: {e}")

log("RECON DONE")
