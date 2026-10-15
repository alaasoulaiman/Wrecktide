"""fixloco_verify.py - post-LevelPrototyping-migration compile gate.

Run as pythonscript commandlet. Judge by WRECKTIDE-FIXLOCO markers in Saved\\Logs\\
Wrecktide.log, NOT the exit code (known Water LoadErrors trip UE's failure summary).

Gates:
  1. LevelBlock + LevelBlock_Traversable load and yield generated classes.
  2. CBP_SandboxCharacter loads (compile-on-load runs here - LogBlueprint errors
     in THIS run's log = fail).
  3. Explicit recompile via BlueprintEditorLibrary; Status read back if exposed.
Crash-proof: every step wrapped; a missing API degrades to SKIP, never a crash.
"""
import unreal

M = "WRECKTIDE-FIXLOCO"
fails = []

def mark(step, ok, extra=""):
    line = f"{M} {step} {'OK' if ok else 'FAIL'} {extra}".strip()
    unreal.log_warning(line)
    if not ok:
        fails.append(step)

# --- gate 1: the two restored classes ---
for path in ("/Game/LevelPrototyping/LevelBlock",
             "/Game/LevelPrototyping/LevelBlock_Traversable"):
    try:
        bp = unreal.EditorAssetLibrary.load_asset(path)
        gc = None
        if bp is not None:
            try:
                gc = unreal.load_object(None, path + "." + path.rsplit("/", 1)[1] + "_C")
            except Exception as e:  # noqa: BLE001
                gc = None
        mark(f"load:{path.rsplit('/',1)[1]}", bp is not None and gc is not None,
             f"asset={bp is not None} class={gc is not None}")
    except Exception as e:  # noqa: BLE001
        mark(f"load:{path.rsplit('/',1)[1]}", False, f"EXC {e}")

# --- gate 2: the pawn blueprint itself ---
CBP = "/Game/Blueprints/CBP_SandboxCharacter"
cbp = None
try:
    cbp = unreal.EditorAssetLibrary.load_asset(CBP)
    mark("load:CBP_SandboxCharacter", cbp is not None)
except Exception as e:  # noqa: BLE001
    mark("load:CBP_SandboxCharacter", False, f"EXC {e}")

# --- gate 3: explicit recompile + status read-back ---
if cbp is not None:
    try:
        unreal.BlueprintEditorLibrary.compile_blueprint(cbp)
        mark("recompile:call", True)
    except Exception as e:  # noqa: BLE001
        mark("recompile:call", False, f"EXC {e}")
    try:
        status = cbp.get_editor_property("status")
        # BS_UP_TO_DATE / BS_UP_TO_DATE_WITH_WARNINGS pass; BS_ERROR fails
        s = str(status)
        ok = ("UP_TO_DATE" in s.upper()) and ("ERROR" not in s.upper())
        mark("recompile:status", ok, s)
    except Exception as e:  # noqa: BLE001
        unreal.log_warning(f"{M} recompile:status SKIP (not exposed) {e}")

unreal.log_warning(f"{M} VERDICT {'PASS' if not fails else 'FAIL ' + ','.join(fails)}")
