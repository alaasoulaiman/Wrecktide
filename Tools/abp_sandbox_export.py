# NIGHT 2 recon: READ-ONLY text export of ABP_SandboxCharacter to find whether an upper-body
# montage slot / layered bone blend exists (run-attack leg-stiffness fix depends on it).
# Same AssetExportTask pattern as chunkc_abp_export.py.
import os
import unreal

ABP = "/Game/Blueprints/ABP_SandboxCharacter"


def log(msg):
    unreal.log("WRECKTIDE-ABPEXPORT2 %s" % msg)


out_dir = os.path.join(unreal.Paths.project_dir(), "Saved", "abp_export")
try:
    os.makedirs(out_dir, exist_ok=True)
except Exception:
    pass

try:
    task = unreal.AssetExportTask()
    asset = unreal.EditorAssetLibrary.load_asset(ABP)
    if asset is None:
        log("LOAD FAIL " + ABP)
        raise SystemExit(1)
    task.set_editor_property("object", asset)
    task.set_editor_property("filename", os.path.join(out_dir, "ABP_SandboxCharacter.copy"))
    task.set_editor_property("automated", True)
    task.set_editor_property("prompt", False)
    task.set_editor_property("exporter", None)
    ok = unreal.Exporter.run_asset_export_task(task)
    log("export ok=%s -> %s" % (ok, task.get_editor_property("filename")))
except Exception as e:  # noqa: BLE001
    log("export failed: %r" % e)
    raise SystemExit(1)
