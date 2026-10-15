# Chunk A.2 gate: load the migrated GAS locomotion assets and confirm they resolve in the editor.
# Broken refs to intentionally-NOT-migrated demo assets (LevelPrototyping\LevelBlock*, the sandbox
# Widgets/PC/GM) are EXPECTED warnings, not failures. Run with:
#   UnrealEditor-Cmd <proj> -run=pythonscript -script=probe_asset_load.py
#     -EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities -unattended -nosplash -nullrhi
import unreal

CRITICAL = [
    "/Game/Blueprints/ABP_SandboxCharacter",
    "/Game/Blueprints/CBP_SandboxCharacter",
    "/Game/Blueprints/PreCMCTick",
    "/Game/Blueprints/RetargetedCharacters/ABP_GenericRetarget",
    "/Game/Characters/UE4_Mannequin/Rigs/RTG_UEFN_to_UE4_Mannequin",
    "/Game/Characters/UE4_Mannequin/Rigs/IK_UE4_Mannequin_Retarget",
    "/Game/Input/IMC_Sandbox",
]

eal = unreal.EditorAssetLibrary
ok = 0
fail = 0
for path in CRITICAL:
    exists = eal.does_asset_exist(path)
    asset = eal.load_asset(path) if exists else None
    if asset is not None:
        ok += 1
        unreal.log("WRECKTIDE-GASLOAD OK   %s (%s)" % (path, asset.get_class().get_name()))
    else:
        fail += 1
        unreal.log_error("WRECKTIDE-GASLOAD FAIL %s exists=%s loaded=None" % (path, exists))

# gameplay tag resolution (the migrated foley AnimNotifies serialize these)
try:
    tag = unreal.GameplayTagLibrary.request_gameplay_tag(unreal.Name("Foley.Event.Jump"), False)
    tag_valid = unreal.GameplayTagLibrary.is_gameplay_tag_valid(tag)
    unreal.log("WRECKTIDE-GASLOAD TAG Foley.Event.Jump valid=%s" % tag_valid)
except Exception as e:
    unreal.log_error("WRECKTIDE-GASLOAD TAG check errored: %s" % e)

unreal.log("WRECKTIDE-GASLOAD %s (ok=%d fail=%d)" % ("PASS" if fail == 0 else "FAIL", ok, fail))
