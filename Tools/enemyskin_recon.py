"""enemyskin_recon.py - READ-ONLY recon for the enemy pirate-skin chunk.

Answers, with WRECKTIDE-ESKIN markers (judge by markers, not exit code):
  1. Is IKRigController / IKRetargeterController / creation factories exposed (Route 2)?
  2. What retarget chains + bones does IK_UEFN_Mannequin (source rig of the WORKING captain
     RTG) reference, and do ALL those bones exist in the enemy skeleton SK_Mannequin (Route 1)?
  3. Enemy BP CDO facts: mesh, skeleton, anim class.
Saves NOTHING.
"""
import unreal

M = "WRECKTIDE-ESKIN"

def log(msg):
    unreal.log_warning(f"{M} {msg}")

eal = unreal.EditorAssetLibrary

# ---- 1. API surface (Route 2 feasibility) ----
for cls in ("IKRigController", "IKRetargeterController", "IKRigDefinition", "IKRetargeter",
            "IKRigDefinitionFactory", "IKRetargetFactory", "AssetToolsHelpers"):
    log(f"api {cls} = {hasattr(unreal, cls)}")

# ---- 2. chains + bone coverage (Route 1 feasibility) ----
SRC_RIG = "/Game/Characters/UEFN_Mannequin/Rigs/IK_UEFN_Mannequin"
TGT_RIG = "/Game/Characters/UE4_Mannequin/Rigs/IK_UE4_Mannequin_Retarget"
ENEMY_SKEL_MESH = "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"

def chain_report(rig_path, label):
    rig = eal.load_asset(rig_path)
    if rig is None:
        log(f"{label} LOAD FAIL {rig_path}")
        return None
    try:
        ctl = unreal.IKRigController.get_controller(rig)
    except Exception as e:  # noqa: BLE001
        log(f"{label} controller unavailable: {e}")
        return None
    chains = []
    try:
        for c in ctl.get_retarget_chains():
            name = str(c.get_editor_property("chain_name"))
            start = str(c.get_editor_property("start_bone"))
            end = str(c.get_editor_property("end_bone"))
            chains.append((name, start, end))
        root = str(ctl.get_retarget_root())
        log(f"{label} root={root} chains={len(chains)}")
        for name, start, end in chains:
            log(f"{label} chain {name}: {start} -> {end}")
    except Exception as e:  # noqa: BLE001
        log(f"{label} chain read failed: {e}")
        return None
    return chains, root

src = chain_report(SRC_RIG, "SRCRIG")
tgt = chain_report(TGT_RIG, "TGTRIG")

# enemy skeleton bone set, via the mesh's skeleton
mesh = eal.load_asset(ENEMY_SKEL_MESH)
bones = set()
if mesh is not None:
    skel = mesh.get_editor_property("skeleton")
    log(f"enemy mesh skeleton = {skel.get_path_name() if skel else None}")
    try:
        # 5.6: Skeleton reference pose bone names via the editor subsystem-free path
        names = unreal.AnimationLibrary.get_bone_names(mesh) if hasattr(unreal.AnimationLibrary, "get_bone_names") else None
    except Exception:  # noqa: BLE001
        names = None
    if names is None:
        # fallback: spawn nothing; use SkeletalMesh API if exposed
        try:
            names = mesh.get_bone_names()
        except Exception as e:  # noqa: BLE001
            log(f"bone-name API not exposed on mesh: {e}")
            names = None
    if names is not None:
        bones = {str(n) for n in names}
        log(f"enemy bones count={len(bones)}")
else:
    log(f"enemy mesh LOAD FAIL {ENEMY_SKEL_MESH}")

if src and bones:
    missing = []
    for name, start, end in src[0]:
        for b in (start, end):
            if b and b != "None" and b not in bones:
                missing.append(f"{name}:{b}")
    log(f"ROUTE1 bone coverage: missing={len(missing)} {missing if missing else '(ALL SOURCE-CHAIN BONES EXIST ON ENEMY SKELETON)'}")
    r = src[1]
    log(f"ROUTE1 root '{r}' on enemy = {r in bones}")
elif src and not bones:
    log("ROUTE1 coverage UNRESOLVED (no bone list API) - settle at runtime probe")

# ---- 3. enemy BP CDO facts ----
ebp = eal.load_asset("/Game/Variant_Combat/Blueprints/AI/BP_CombatEnemy")
if ebp is not None:
    cdo = unreal.get_default_object(ebp.generated_class())
    try:
        mc = cdo.get_editor_property("Mesh")
        log(f"enemyBP mesh={mc.get_editor_property('SkeletalMesh').get_path_name() if mc else None}")
        log(f"enemyBP animclass={mc.get_editor_property('AnimClass').get_path_name() if mc and mc.get_editor_property('AnimClass') else None}")
        log(f"enemyBP rel loc/rot={mc.get_editor_property('RelativeLocation').export_text()} / {mc.get_editor_property('RelativeRotation').export_text()}")
    except Exception as e:  # noqa: BLE001
        log(f"enemyBP CDO read fail: {e}")
else:
    log("enemyBP LOAD FAIL")

log("RECON DONE")
