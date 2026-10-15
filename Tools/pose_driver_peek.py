"""pose_driver_peek.py - TEMPORARY diagnostic (pose pairing 2026-07-28): blank the player's
CosmeticMeshAsset on the CBP CDO so the DRIVING UEFN mannequin renders, to answer one
question: does the source idle itself stand tiptoe/staggered, or is the retarget adding it?
Chunk C's C++ treats an unconfigured skin as the supported fallback (driver visible).
RESTORE = re-run chunkc_apply_cosmetic.py (the original apply script) + chunkc_verify.py.
Verify-before-save. WRECKTIDE-PEEK markers."""
import unreal

CBP = "/Game/Blueprints/CBP_SandboxCharacter"


def log(msg):
    unreal.log("WRECKTIDE-PEEK %s" % msg)


def fatal(msg):
    unreal.log_error("WRECKTIDE-PEEK FATAL %s" % msg)
    unreal.log_error("WRECKTIDE-PEEK RESULT=FAIL (nothing saved)")
    raise SystemExit(1)


eal = unreal.EditorAssetLibrary
bp = eal.load_asset(CBP)
if bp is None:
    fatal("CBP load failed")
cdo = unreal.get_default_object(bp.generated_class())
if cdo is None:
    fatal("CDO unreadable")

before = cdo.get_editor_property("CosmeticMeshAsset")
log("CosmeticMeshAsset before = %s" % (before.get_path_name() if before else None))
if before is None:
    log("already blank - nothing to do (restore = chunkc_apply_cosmetic.py)")
    raise SystemExit(0)

cdo.set_editor_property("CosmeticMeshAsset", None)
if cdo.get_editor_property("CosmeticMeshAsset") is not None:
    fatal("blank did not take")

if not eal.save_loaded_asset(bp, only_if_is_dirty=False):
    fatal("save failed")
log("RESULT=OK skin blanked, driver will render; RESTORE with chunkc_apply_cosmetic.py")
