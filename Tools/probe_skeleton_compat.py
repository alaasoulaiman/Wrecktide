# Chunk B de-risk: does the combat attack montage's skeleton match / is it compatible with the
# GAS UEFN character skeleton? If not, a reparented pirate would play attacks as silent no-ops.
import unreal

def skel_of(path, kind):
    a = unreal.EditorAssetLibrary.load_asset(path)
    if a is None:
        unreal.log_error("WRECKTIDE-SKEL missing %s %s" % (kind, path))
        return None, None
    sk = None
    for prop in ("skeleton",):
        try:
            sk = a.get_editor_property(prop)
        except Exception:
            sk = None
        if sk:
            break
    if sk is None:
        try:
            sk = a.get_skeleton()
        except Exception:
            sk = None
    unreal.log("WRECKTIDE-SKEL %s %s -> skeleton=%s" % (kind, path, (sk.get_path_name() if sk else "None")))
    return a, sk

am, am_skel = skel_of("/Game/Variant_Combat/Anims/AM_ComboAttack", "montage")
skm, gas_skel = skel_of("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin", "gas-mesh")
mannequin_mesh, manny_skel = skel_of("/Game/Characters/Mannequins/Meshes/SK_Mannequin", "combat-mesh")

def list_compat(sk, label):
    if sk is None:
        return []
    try:
        compat = sk.get_editor_property("compatible_skeletons")
    except Exception as e:
        unreal.log("WRECKTIDE-SKEL %s compatible_skeletons unreadable: %s" % (label, e))
        return []
    names = []
    for c in (compat or []):
        try:
            names.append(c.get_path_name())
        except Exception:
            names.append(str(c))
    unreal.log("WRECKTIDE-SKEL %s compatible=%s" % (label, names))
    return names

gas_compat = list_compat(gas_skel, "gas-skel")
manny_compat = list_compat(manny_skel, "manny-skel")

verdict = "UNKNOWN"
if am_skel and gas_skel:
    if am_skel.get_path_name() == gas_skel.get_path_name():
        verdict = "SAME-SKELETON (montage plays directly)"
    elif gas_skel and am_skel.get_path_name() in gas_compat:
        verdict = "COMPATIBLE (montage skeleton listed on gas skeleton)"
    elif manny_skel and gas_skel.get_path_name() in manny_compat:
        verdict = "COMPATIBLE-REVERSE (gas skeleton listed on manny)"
    else:
        verdict = "INCOMPATIBLE (need to register compatibility or retarget montages)"
unreal.log("WRECKTIDE-SKEL VERDICT %s" % verdict)
