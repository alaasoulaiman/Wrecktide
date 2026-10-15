"""import_sfx.py - import WRECKTIDE_SFX_SOURCE\\*.wav (default Content\\Audio\\SFX) as USoundWave assets under /Game/Audio/SFX (2026-09-04 night).
Idempotent (replace existing), fail-closed, saves each asset. Markers: WRECKTIDE-SFX.
Usage: run_py.cmd <this file> [name ...]
With no arguments every wav in the source folder is (re)imported, which re-saves every sound asset. Pass one or
more base names (e.g. sfx_splash) to import only those, so adding one cue leaves the other assets
byte-untouched in git."""
import os
import sys
import unreal

M = "WRECKTIDE-SFX"
SRC = os.environ.get("WRECKTIDE_SFX_SOURCE", os.path.join(unreal.Paths.project_dir(), "Content", "Audio", "SFX"))
DEST = "/Game/Audio/SFX"


def log(msg):
    unreal.log_warning("%s %s" % (M, msg))


def fatal(msg):
    unreal.log_error("%s FATAL %s" % (M, msg))
    unreal.log_error("%s RESULT=FAIL" % M)
    raise SystemExit(1)


ONLY = [a for a in sys.argv[1:] if not a.startswith("-")]
wavs = sorted(f for f in os.listdir(SRC) if f.lower().endswith(".wav"))
if ONLY:
    wanted = set(n.lower() for n in ONLY)
    wavs = [f for f in wavs if os.path.splitext(f)[0].lower() in wanted]
    log("filter %s -> %d wav(s)" % (",".join(ONLY), len(wavs)))
if not wavs:
    fatal("no wav files in %s%s" % (SRC, (" matching %s" % ONLY) if ONLY else ""))

tasks = []
for f in wavs:
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", os.path.join(SRC, f))
    task.set_editor_property("destination_path", DEST)
    task.set_editor_property("destination_name", os.path.splitext(f)[0])
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    tasks.append(task)

unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

ok = 0
for f in wavs:
    name = os.path.splitext(f)[0]
    path = "%s/%s" % (DEST, name)
    asset = unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if asset is not None and name == "ambient_sea" and not asset.get_editor_property("looping"):
        asset.set_editor_property("looping", True)
        unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)
        log("set looping on %s" % path)
    if asset is None:
        log("MISSING after import: %s" % path)
        continue
    try:
        dur = asset.get_editor_property("duration")
    except Exception:  # noqa: BLE001
        dur = -1.0
    log("imported %s (%s, %.2fs)" % (path, asset.get_class().get_name(), dur))
    ok += 1

if ok != len(wavs):
    fatal("imported %d of %d" % (ok, len(wavs)))
log("RESULT=OK %d sounds under %s" % (ok, DEST))
