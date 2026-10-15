"""enemyskin_recon2.py - dump the SOURCE rig's chain bone NAMES properly (BoneReference.bone_name,
never str(struct)). Read-only. WRECKTIDE-ESKIN2 markers."""
import unreal

M = "WRECKTIDE-ESKIN2"

def log(msg):
    unreal.log_warning(f"{M} {msg}")

eal = unreal.EditorAssetLibrary
for rig_path, label in (("/Game/Characters/UEFN_Mannequin/Rigs/IK_UEFN_Mannequin", "SRC"),
                        ("/Game/Characters/UE4_Mannequin/Rigs/IK_UE4_Mannequin_Retarget", "TGT")):
    rig = eal.load_asset(rig_path)
    ctl = unreal.IKRigController.get_controller(rig)
    root = str(ctl.get_retarget_root())
    log(f"{label} root={root}")
    for c in ctl.get_retarget_chains():
        name = str(c.get_editor_property("chain_name"))
        sb = c.get_editor_property("start_bone")
        eb = c.get_editor_property("end_bone")
        sbn = str(sb.get_editor_property("bone_name")) if sb else "None"
        ebn = str(eb.get_editor_property("bone_name")) if eb else "None"
        log(f"{label} {name}|{sbn}|{ebn}")
log("DONE")
