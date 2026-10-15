"""abp_layer_recon2.py - READ-ONLY recon (2026-09-02): can STEP 11 of the upper-body layer
surgery (configure the Layered blend per bone node) be done headlessly through Python?

Checks, with WRECKTIDE-LAYERREC2 markers in Saved\\Logs\\Wrecktide.log:
  1. fresh T3D export of ABP_SandboxCharacter (Saved\\abp_export\\ABP_SandboxCharacter_0902.copy)
  2. python exposure of the AnimGraph node classes + the LayeredBoneBlend structs
  3. graph walk: Blueprint.function_graphs / ubergraph_pages -> EdGraph.nodes (if exposed)
  4. fallback walk: object paths parsed out of the T3D export -> unreal.find_object / load_object
  5. for every LayeredBoneBlend / Slot / cached-pose node found: dump its 'node' struct
  6. skeleton slot registries (driver + montage skeletons) + the _Player montage slot names
  7. CBP CDO montage properties (which montage assets the player currently plays)
Nothing is saved. Judge by the markers, not the exit code (known Water LoadErrors).
"""
import os
import re
import unreal

M = "WRECKTIDE-LAYERREC2"
ABP = "/Game/Blueprints/ABP_SandboxCharacter"
CBP = "/Game/Blueprints/CBP_SandboxCharacter"
DRIVER_SKEL = "/Game/Characters/UEFN_Mannequin/Meshes/SK_UEFN_Mannequin"
MONTAGE_SKEL = "/Game/Characters/Mannequins/Meshes/SK_Mannequin"
MONTAGES = [
    "/Game/Variant_Combat/Anims/AM_ComboAttack",
    "/Game/Variant_Combat/Anims/AM_ChargedAttack",
    "/Game/Variant_Combat/Anims/AM_ComboAttack_Player",
    "/Game/Variant_Combat/Anims/AM_ChargedAttack_Player",
]


def log(msg):
    unreal.log_warning("%s %s" % (M, msg))


def safe(fn, label):
    try:
        return fn()
    except Exception as e:  # noqa: BLE001
        log("%s EXC %s" % (label, str(e)[:160]))
        return None


eal = unreal.EditorAssetLibrary

# ---- 1. fresh T3D export -------------------------------------------------------------
out_dir = os.path.join(unreal.Paths.project_dir(), "Saved", "abp_export")
os.makedirs(out_dir, exist_ok=True)
export_path = os.path.join(out_dir, "ABP_SandboxCharacter_0902.copy")
abp = eal.load_asset(ABP)
log("ABP load = %s class=%s" % (abp is not None, abp.get_class().get_name() if abp else None))
if abp is None:
    log("VERDICT FAIL abp load")
    raise SystemExit(1)


def do_export():
    task = unreal.AssetExportTask()
    task.set_editor_property("object", abp)
    task.set_editor_property("filename", export_path)
    task.set_editor_property("automated", True)
    task.set_editor_property("prompt", False)
    task.set_editor_property("exporter", None)
    ok = unreal.Exporter.run_asset_export_task(task)
    return ok


ok = safe(do_export, "export")
log("export ok=%s size=%s" % (ok, os.path.getsize(export_path) if os.path.exists(export_path) else None))

# ---- 2. class / struct exposure ------------------------------------------------------
for name in ("AnimGraphNode_Base", "AnimGraphNode_LayeredBoneBlend", "AnimGraphNode_Slot",
             "AnimGraphNode_SaveCachedPose", "AnimGraphNode_UseCachedPose", "AnimGraphNode_Root",
             "AnimGraphNode_StateMachine", "AnimNode_LayeredBoneBlend", "InputBlendPose", "BranchFilter",
             "EdGraph", "EdGraphNode", "K2Node", "AnimBlueprint", "BlueprintEditorLibrary",
             "AnimationBlueprintLibrary", "AnimBlueprintGeneratedClass"):
    log("exposed %s = %s" % (name, hasattr(unreal, name)))

# ---- 3. graph walk through reflected properties --------------------------------------
found_nodes = []
for prop in ("function_graphs", "ubergraph_pages", "macro_graphs", "delegate_signature_graphs"):
    graphs = safe(lambda p=prop: abp.get_editor_property(p), "abp.%s" % prop)
    if graphs is None:
        continue
    log("abp.%s = %d graphs" % (prop, len(graphs)))
    for g in graphs:
        gname = g.get_name() if g else None
        nodes = safe(lambda gg=g: gg.get_editor_property("nodes"), "graph.nodes(%s)" % gname)
        if nodes is None:
            log("  graph %s class=%s nodes UNEXPOSED" % (gname, g.get_class().get_name() if g else None))
            continue
        log("  graph %s class=%s nodes=%d" % (gname, g.get_class().get_name(), len(nodes)))
        for n in nodes:
            cname = n.get_class().get_name() if n else None
            if cname and ("LayeredBoneBlend" in cname or "AnimGraphNode_Slot" == cname or "CachedPose" in cname
                          or "AnimGraphNode_Root" == cname):
                found_nodes.append(n)
                log("    node %s (%s) path=%s" % (n.get_name(), cname, n.get_path_name()))

# ---- 4. fallback walk via T3D object paths -------------------------------------------
if not found_nodes and os.path.exists(export_path):
    try:
        with open(export_path, "r", encoding="utf-8", errors="replace") as fh:
            text = fh.read()
    except Exception as e:  # noqa: BLE001
        text = ""
        log("t3d read EXC %s" % e)
    pat = re.compile(r'Begin Object Class=(/Script/[\w.]+) Name="([^"]+)" ExportPath="?([^"\s]+)"?')
    hits = pat.findall(text)
    log("t3d Begin Object hits = %d" % len(hits))
    for cls, name, path in hits:
        if "LayeredBoneBlend" in cls or cls.endswith("AnimGraphNode_Slot") or "CachedPose" in cls or cls.endswith("AnimGraphNode_Root"):
            # ExportPath looks like /Script/AnimGraph.AnimGraphNode_X'/Game/...ABP.ABP:AnimGraph.AnimGraphNode_X_0'
            objpath = path
            mm = re.match(r"[^']+'([^']+)'", path)
            if mm:
                objpath = mm.group(1)
            obj = safe(lambda p=objpath: unreal.find_object(None, p), "find_object %s" % name)
            if obj is None:
                obj = safe(lambda p=objpath: unreal.load_object(None, p), "load_object %s" % name)
            log("t3d node %s cls=%s objpath=%s resolved=%s wrapped_as=%s" % (
                name, cls, objpath, obj is not None, obj.get_class().get_name() if obj else None))
            if obj is not None:
                found_nodes.append(obj)

# ---- 5. dump the interesting nodes ---------------------------------------------------
for n in found_nodes:
    cname = n.get_class().get_name()
    node_struct = safe(lambda nn=n: nn.get_editor_property("node"), "%s.node" % n.get_name())
    if node_struct is None:
        log("NODE %s (%s): 'node' struct UNEXPOSED" % (n.get_name(), cname))
        continue
    txt = safe(lambda s=node_struct: s.export_text(), "%s.node.export_text" % n.get_name())
    log("NODE %s (%s) node=%s" % (n.get_name(), cname, (txt or "")[:600]))
    if "LayeredBoneBlend" in cname:
        for prop in ("layer_setup", "blend_weights", "mesh_space_rotation_blend", "mesh_space_scale_blend",
                     "curve_blend_option", "blend_root_motion_based_on_root_bone", "blend_mode"):
            v = safe(lambda s=node_struct, p=prop: s.get_editor_property(p), "layered.%s" % prop)
            log("  layered.%s = %s" % (prop, v))
        ls = safe(lambda s=node_struct: s.get_editor_property("layer_setup"), "layer_setup2")
        if ls is not None:
            for i, ibp in enumerate(ls):
                bf = safe(lambda x=ibp: x.get_editor_property("branch_filters"), "branch_filters[%d]" % i)
                if bf is not None:
                    for f in bf:
                        log("  layer_setup[%d] filter bone=%s depth=%s" % (
                            i, f.get_editor_property("bone_name"), f.get_editor_property("blend_depth")))
                else:
                    log("  layer_setup[%d] = %s" % (i, ibp))
    if cname == "AnimGraphNode_Slot":
        v = safe(lambda s=node_struct: s.get_editor_property("slot_name"), "slot.slot_name")
        log("  slot.slot_name = %s" % v)
    if "CachedPose" in cname:
        v = safe(lambda nn=n: nn.get_editor_property("cache_name"), "cache_name")
        log("  cache_name = %s" % v)

# struct constructibility (needed for STEP 11 if the node is writable)
for sname in ("InputBlendPose", "BranchFilter"):
    if hasattr(unreal, sname):
        inst = safe(lambda s=sname: getattr(unreal, s)(), "construct %s" % sname)
        log("construct %s = %s props=%s" % (sname, inst is not None,
                                             [p for p in dir(inst) if not p.startswith("_")][:12] if inst else None))

# ---- 6. skeleton slot registries + montage slots ------------------------------------
for label, path in (("DRIVER_SKEL", DRIVER_SKEL), ("MONTAGE_SKEL", MONTAGE_SKEL)):
    sk = eal.load_asset(path)
    if sk is None:
        log("%s LOAD FAIL %s" % (label, path))
        continue
    groups = safe(lambda s=sk: s.get_editor_property("slot_groups"), "%s.slot_groups" % label)
    if groups is None:
        continue
    for g in groups:
        gn = safe(lambda x=g: x.get_editor_property("group_name"), "group_name")
        sn = safe(lambda x=g: x.get_editor_property("slot_names"), "slot_names")
        log("%s slot group %s slots=%s" % (label, gn, [str(x) for x in sn] if sn is not None else None))

for path in MONTAGES:
    m = eal.load_asset(path)
    if m is None:
        log("MONTAGE %s MISSING" % path)
        continue
    tracks = safe(lambda mm=m: mm.get_editor_property("slot_anim_tracks"), "slot_anim_tracks")
    names = []
    if tracks is not None:
        for t in tracks:
            names.append(str(safe(lambda tt=t: tt.get_editor_property("slot_name"), "slot_name")))
    sk = safe(lambda mm=m: mm.get_editor_property("skeleton"), "montage.skeleton")
    log("MONTAGE %s slots=%s skeleton=%s" % (m.get_name(), names, sk.get_name() if sk else None))

# ---- 7. CBP CDO montage properties ---------------------------------------------------
cbp = eal.load_asset(CBP)
if cbp is not None:
    cdo = safe(lambda: unreal.get_default_object(cbp.generated_class()), "cbp cdo")
    if cdo is not None:
        for prop in ("combo_attack_montage", "charged_attack_montage"):
            v = safe(lambda p=prop: cdo.get_editor_property(p), "cdo.%s" % prop)
            log("CBP CDO %s = %s" % (prop, v.get_path_name() if v else v))
        api = sorted(x for x in dir(cdo) if "montage" in x.lower())
        log("CBP CDO montage attrs: %s" % ", ".join(api))

log("VERDICT DONE nodes_found=%d" % len(found_nodes))
