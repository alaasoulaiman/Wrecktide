"""upperbody_verify.py - FRESH-PROCESS re-proof of the upper-body layer surgery, read from disk.

Run AFTER abp_upperbody_step11.py + abp_upperbody_part2.py, as its own commandlet, so nothing is
trusted from the process that wrote it. Also the compile gate: the ABP and the CBP are recompiled
here and the caller greps THIS run's log for 'LogBlueprint: Error' (the python verdict alone is a
false green, per the 2026-07-27 lesson). Markers: WRECKTIDE-UBVERIFY.
"""
import unreal

M = "WRECKTIDE-UBVERIFY"
ABP = "/Game/Blueprints/ABP_SandboxCharacter"
OBJ = ABP + ".ABP_SandboxCharacter"
BLEND_NODE = OBJ + ":AnimGraph.AnimGraphNode_LayeredBoneBlend_0"
SLOT_NODE = OBJ + ":AnimGraph.AnimGraphNode_Slot_1"
DEFAULT_SLOT_NODE = OBJ + ":AnimGraph.AnimGraphNode_Slot_0"
CACHE_NODE = OBJ + ":AnimGraph.AnimGraphNode_SaveCachedPose_0"
CBP = "/Game/Blueprints/CBP_SandboxCharacter"
ENEMY_BP = "/Game/Variant_Combat/Blueprints/AI/BP_CombatEnemy"
PLAYER_MONTAGES = {"combo_attack_montage": "/Game/Variant_Combat/Anims/AM_ComboAttack_Player",
                   "charged_attack_montage": "/Game/Variant_Combat/Anims/AM_ChargedAttack_Player"}
ENEMY_MONTAGES = {"combo_attack_montage": "/Game/Variant_Combat/Anims/AM_ComboAttack",
                  "charged_attack_montage": "/Game/Variant_Combat/Anims/AM_ChargedAttack"}
SKELETONS = ["/Game/Characters/Mannequins/Meshes/SK_Mannequin", "/Game/Characters/UEFN_Mannequin/Meshes/SK_UEFN_Mannequin"]

fails = []


def mark(step, ok, extra=""):
    unreal.log_warning("%s %s %s %s" % (M, step, "OK" if ok else "FAIL", extra))
    if not ok:
        fails.append(step)


def resolve(path):
    try:
        o = unreal.find_object(None, path)
    except Exception:  # noqa: BLE001
        o = None
    if o is None:
        try:
            o = unreal.load_object(None, path)
        except Exception:  # noqa: BLE001
            o = None
    return o


eal = unreal.EditorAssetLibrary
tools = getattr(unreal, "WrecktideEditorTools", None)
mark("editor-tools-exposed", tools is not None)

# ---- ABP: node config + names ----
abp = eal.load_asset(ABP)
mark("abp-load", abp is not None)
if abp is not None:
    blend = resolve(BLEND_NODE)
    slot = resolve(SLOT_NODE)
    dslot = resolve(DEFAULT_SLOT_NODE)
    cache = resolve(CACHE_NODE)
    mark("abp-nodes-resolve", all(x is not None for x in (blend, slot, dslot, cache)))
    if blend is not None:
        node = blend.get_editor_property("node")
        layers = node.get_editor_property("layer_setup")
        filters = layers[0].get_editor_property("branch_filters") if len(layers) == 1 else []
        cfg = [(str(f.get_editor_property("bone_name")), int(f.get_editor_property("blend_depth"))) for f in filters]
        msr = bool(node.get_editor_property("mesh_space_rotation_blend"))
        weights = [float(w) for w in node.get_editor_property("blend_weights")]
        mark("abp-layer-config", cfg == [("spine_01", 0)] and msr and weights == [1.0],
             "layers=%s mesh_space_rotation=%s weights=%s" % (cfg, msr, weights))
    if slot is not None and dslot is not None:
        sn = str(slot.get_editor_property("node").get_editor_property("slot_name"))
        dn = str(dslot.get_editor_property("node").get_editor_property("slot_name"))
        mark("abp-slot-names", sn == "UpperBody" and dn == "DefaultSlot", "layer=%s passthrough=%s" % (sn, dn))
    if cache is not None:
        mark("abp-cache-name", str(cache.get_editor_property("cache_name")) == "LocoPose")
    try:
        unreal.BlueprintEditorLibrary.compile_blueprint(abp)
        mark("abp-compile-call", True)
    except Exception as e:  # noqa: BLE001
        mark("abp-compile-call", False, repr(e))

# ---- CBP: compile + CDO montages ----
cbp = eal.load_asset(CBP)
mark("cbp-load", cbp is not None)
if cbp is not None:
    try:
        unreal.BlueprintEditorLibrary.compile_blueprint(cbp)
        mark("cbp-compile-call", True)
    except Exception as e:  # noqa: BLE001
        mark("cbp-compile-call", False, repr(e))
    cdo = unreal.get_default_object(cbp.generated_class())
    for prop, path in PLAYER_MONTAGES.items():
        v = cdo.get_editor_property(prop) if cdo else None
        got = v.get_path_name().split(".")[0] if v else None
        mark("cbp-cdo-%s" % prop, got == path, "got=%s" % got)

# ---- enemy BP keeps the originals ----
ebp = eal.load_asset(ENEMY_BP)
mark("enemy-bp-load", ebp is not None)
if ebp is not None:
    ecdo = unreal.get_default_object(ebp.generated_class())
    for prop, path in ENEMY_MONTAGES.items():
        try:
            v = ecdo.get_editor_property(prop)
            got = v.get_path_name().split(".")[0] if v else None
            mark("enemy-cdo-%s" % prop, got == path, "got=%s" % got)
        except Exception as e:  # noqa: BLE001
            mark("enemy-cdo-%s" % prop, False, repr(e)[:120])

# ---- montage slots + skeleton registry (from disk) ----
if tools is not None:
    for prop, path in PLAYER_MONTAGES.items():
        m = eal.load_asset(path)
        names = [str(n) for n in tools.get_montage_slot_names(m)] if m else None
        mark("montage-slot-%s" % path.rsplit("/", 1)[-1], names == ["UpperBody"], "slots=%s" % names)
    for prop, path in ENEMY_MONTAGES.items():
        m = eal.load_asset(path)
        names = [str(n) for n in tools.get_montage_slot_names(m)] if m else None
        mark("montage-slot-%s" % path.rsplit("/", 1)[-1], names == ["DefaultSlot"], "slots=%s" % names)
    for path in SKELETONS:
        sk = eal.load_asset(path)
        names = [str(n) for n in tools.get_skeleton_slot_names(sk)] if sk else []
        mark("skeleton-slot-%s" % path.rsplit("/", 1)[-1], "UpperBody" in names and "DefaultSlot" in names, "slots=%s" % names)

unreal.log_warning("%s VERDICT %s" % (M, "PASS" if not fails else "FAIL " + ",".join(fails)))
