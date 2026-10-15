"""pose_skin_swap.py - CONTROL EXPERIMENT (pose pairing 2026-07-28): put the stock UE4
mannequin on as the player's cosmetic skin. Identical skeleton + the exact mesh the GAS
target pose was tuned for = the perfect-case target. If it ALSO tiptoes, the op stack is
broken at runtime for any skin; if it stands straight, the defect lives in the Synty mesh's
own bind. RESTORE = chunkc_apply_cosmetic.py. Verify-before-save. WRECKTIDE-SWAP markers."""
import unreal

CBP = "/Game/Blueprints/CBP_SandboxCharacter"
SWAP_TO = "/Game/Characters/UE4_Mannequin/Meshes/SKM_UE4_Mannequin"


def log(msg):
    unreal.log("WRECKTIDE-SWAP %s" % msg)


def fatal(msg):
    unreal.log_error("WRECKTIDE-SWAP FATAL %s" % msg)
    unreal.log_error("WRECKTIDE-SWAP RESULT=FAIL (nothing saved)")
    raise SystemExit(1)


eal = unreal.EditorAssetLibrary
bp = eal.load_asset(CBP)
mesh = eal.load_asset(SWAP_TO)
if bp is None or mesh is None:
    fatal("load failed bp=%s mesh=%s" % (bp, mesh))
if not isinstance(mesh, unreal.SkeletalMesh):
    fatal("swap target is not a SkeletalMesh")
cdo = unreal.get_default_object(bp.generated_class())

before = cdo.get_editor_property("CosmeticMeshAsset")
log("skin before = %s" % (before.get_path_name() if before else None))
cdo.set_editor_property("CosmeticMeshAsset", mesh)
now = cdo.get_editor_property("CosmeticMeshAsset")
if now is None or now.get_name() != mesh.get_name():
    fatal("swap did not take")
if not eal.save_loaded_asset(bp, only_if_is_dirty=False):
    fatal("save failed")
log("RESULT=OK player skin = %s (control experiment; RESTORE with chunkc_apply_cosmetic.py)" % now.get_name())
