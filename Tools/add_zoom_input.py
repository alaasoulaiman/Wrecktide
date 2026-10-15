# Add IA_CameraZoom (Axis1D) + a MouseWheelAxis row in IMC_Sandbox for the scroll-zoom
# feature (play test request 2026-07-27: scroll = camera zoom, capped). Duplicate-and-mutate pattern
# (factories for InputAction may not be exposed; duplicating a shipped IA is proven safe).
# Verify-before-save; fail-closed.
#   UnrealEditor-Cmd <proj> -run=pythonscript -script=<full path> \
#     "-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities" -unattended -nosplash -nullrhi
import unreal

SRC_IA = "/Game/Input/IA_Walk"                  # any small shipped IA to clone (GAS keeps IAs at /Game/Input root)
NEW_IA = "/Game/Input/IA_CameraZoom"
IMC = "/Game/Input/IMC_Sandbox"
WHEEL_KEY = "MouseWheelAxis"


def log(msg):
    unreal.log("WRECKTIDE-ZOOMINPUT %s" % msg)


def fatal(msg):
    unreal.log_error("WRECKTIDE-ZOOMINPUT FATAL %s" % msg)
    unreal.log_error("WRECKTIDE-ZOOMINPUT RESULT=FAIL")
    raise SystemExit(1)


eal = unreal.EditorAssetLibrary

# ---- 1. the action asset ----
if eal.does_asset_exist(NEW_IA):
    ia = eal.load_asset(NEW_IA)
    log("IA_CameraZoom already exists")
else:
    if not eal.duplicate_asset(SRC_IA, NEW_IA):
        fatal("duplicate_asset failed %s -> %s" % (SRC_IA, NEW_IA))
    ia = eal.load_asset(NEW_IA)
    if ia is None:
        fatal("clone loaded None")
if ia is None:
    fatal("IA load failed")

# enum member name varies across releases - discover it instead of guessing
axis1d = None
for name in dir(unreal.InputActionValueType):
    if "AXIS1" in name.upper().replace("_", ""):
        axis1d = getattr(unreal.InputActionValueType, name)
        log("enum member for Axis1D = %s" % name)
        break
if axis1d is None:
    fatal("no Axis1D member on InputActionValueType: %s" % [n for n in dir(unreal.InputActionValueType) if not n.startswith("_")])
ia.set_editor_property("ValueType", axis1d)
vt = str(ia.get_editor_property("ValueType"))
log("ValueType = %s" % vt)
if "AXIS1" not in vt.upper().replace("_", ""):
    fatal("ValueType did not take: %s" % vt)
# a clean axis action: no triggers/modifiers inherited from the clone source
ia.set_editor_property("Triggers", [])
ia.set_editor_property("Modifiers", [])
if not eal.save_loaded_asset(ia, only_if_is_dirty=False):
    fatal("IA save failed")
log("IA_CameraZoom saved")

# ---- 2. the IMC row ----
imc = eal.load_asset(IMC)
if imc is None:
    fatal("IMC_Sandbox load failed")
already = False
for m in imc.get_editor_property("Mappings"):
    a = m.get_editor_property("Action")
    k = m.get_editor_property("Key")
    try:
        k_txt = k.export_text()
    except Exception:
        k_txt = str(k)
    if a and a.get_name() == "IA_CameraZoom" and WHEEL_KEY in k_txt:
        already = True
if already:
    log("wheel row already present")
else:
    key = unreal.Key()
    key.set_editor_property("KeyName", WHEEL_KEY)
    imc.map_key(ia, key)
    log("mapped %s -> IA_CameraZoom" % WHEEL_KEY)

# verify from the asset
found = False
for m in imc.get_editor_property("Mappings"):
    a = m.get_editor_property("Action")
    k = m.get_editor_property("Key")
    try:
        k_txt = k.export_text()
    except Exception:
        k_txt = str(k)
    if a and a.get_name() == "IA_CameraZoom" and WHEEL_KEY in k_txt:
        found = True
if not found:
    fatal("wheel row missing after map_key")
if not eal.save_loaded_asset(imc, only_if_is_dirty=False):
    fatal("IMC save failed")
log("RESULT=OK IA_CameraZoom (Axis1D) + MouseWheelAxis row saved")
