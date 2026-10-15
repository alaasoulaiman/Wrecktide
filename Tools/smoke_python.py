# Chunk 0 smoke test: proves PythonScriptPlugin + EditorScriptingUtilities load and the
# headless editor-scripting rail (import unreal, EditorAssetLibrary) is live.
import unreal

# unreal.log routes to LogPython so markers land in the -log file (plain print goes to the
# child stdout and is easy to lose).
unreal.log("WRECKTIDE-PY-OK unreal module imported; engine=%s" % unreal.SystemLibrary.get_engine_version())

# Prove EditorScriptingUtilities-class APIs resolve (EditorAssetLibrary lives in that module's
# surface); a trivial existence check on a known project asset.
eal = unreal.EditorAssetLibrary
exists = eal.does_asset_exist("/Game/Variant_Combat/Blueprints/BP_CombatCharacter")
unreal.log("WRECKTIDE-PY-EAL BP_CombatCharacter exists=%s" % exists)
unreal.log("WRECKTIDE-PY-DONE")
