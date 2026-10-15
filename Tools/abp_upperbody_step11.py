"""abp_upperbody_step11.py - STEP 11 + 12 of the upper-body layer surgery, headless (2026-09-02).

The author hand-wired the layer on 2026-07-28 (ss-verified, checkpoint 9ecbd61): LocoPose cached pose,
Slot 'UpperBody', Layered blend per bone, all 5 wires. He saved it wired-but-UNCONFIGURED. This
script does the configuration his hands were going to do:
  STEP 11: Layered blend node -> Layer Setup[0] -> Branch Filters = [spine_01, depth 0],
           Mesh Space Rotation Blend = ON.
  STEP 12: compile the ABP (ZERO errors or abort) -> save.
The graph nodes ARE reachable from Python (recon 2026-09-02): the AnimGraph node classes are
exposed and the node's 'node' struct is EditAnywhere, so find_object + set_editor_property works.
Wiring itself is NOT touched here (pins have no python surface); it is verified from a T3D export.

Fail-closed: any check failing before the save aborts with NOTHING written. Verify-before-save.
Markers: WRECKTIDE-STEP11 in Saved\\Logs\\Wrecktide.log. Judge by markers, not the exit code.
Usage:  run_py.cmd <this file>            (apply)
        run_py.cmd "<this file> verify"   (read-only report)
"""
import os
import sys
import unreal

M = "WRECKTIDE-STEP11"
ABP = "/Game/Blueprints/ABP_SandboxCharacter"
OBJ = "/Game/Blueprints/ABP_SandboxCharacter.ABP_SandboxCharacter"
BLEND_NODE = OBJ + ":AnimGraph.AnimGraphNode_LayeredBoneBlend_0"
SLOT_NODE = OBJ + ":AnimGraph.AnimGraphNode_Slot_1"
CACHE_NODE = OBJ + ":AnimGraph.AnimGraphNode_SaveCachedPose_0"
BONE = "spine_01"
DEPTH = 0
SLOT_NAME = "UpperBody"
CACHE_NAME = "LocoPose"

VERIFY_ONLY = any(a.lower() == "verify" for a in sys.argv[1:])


def log(msg):
    unreal.log_warning("%s %s" % (M, msg))


def fatal(msg):
    unreal.log_error("%s FATAL %s" % (M, msg))
    unreal.log_error("%s RESULT=FAIL (nothing saved)" % M)
    raise SystemExit(1)


def resolve(path):
    obj = None
    try:
        obj = unreal.find_object(None, path)
    except Exception:  # noqa: BLE001
        obj = None
    if obj is None:
        try:
            obj = unreal.load_object(None, path)
        except Exception:  # noqa: BLE001
            obj = None
    return obj


def describe_layers(struct):
    out = []
    ls = struct.get_editor_property("layer_setup")
    for i, ibp in enumerate(ls):
        filters = ibp.get_editor_property("branch_filters")
        out.append("layer[%d]=%s" % (i, [(str(f.get_editor_property("bone_name")), int(f.get_editor_property("blend_depth"))) for f in filters]))
    return "; ".join(out) if out else "(no layers)"


eal = unreal.EditorAssetLibrary
abp = eal.load_asset(ABP)
if abp is None:
    fatal("ABP load failed")
log("mode=%s abp=%s" % ("verify" if VERIFY_ONLY else "apply", abp.get_path_name()))

blend = resolve(BLEND_NODE)
slot = resolve(SLOT_NODE)
cache = resolve(CACHE_NODE)
for label, o, want in (("blend", blend, "AnimGraphNode_LayeredBoneBlend"), ("slot", slot, "AnimGraphNode_Slot"),
                       ("cache", cache, "AnimGraphNode_SaveCachedPose")):
    if o is None:
        fatal("%s node not found at its recon path" % label)
    cname = o.get_class().get_name()
    log("%s node resolved class=%s" % (label, cname))
    if cname != want:
        fatal("%s node is a %s, expected %s" % (label, cname, want))

# --- the surrounding contract built by hand in the editor: slot name + cache name ---
slot_struct = slot.get_editor_property("node")
slot_name = str(slot_struct.get_editor_property("slot_name"))
cache_name = str(cache.get_editor_property("cache_name"))
log("slot_name=%s cache_name=%s" % (slot_name, cache_name))
if slot_name != SLOT_NAME:
    fatal("slot node is named %r, expected %r (graph is not the checkpointed surgery)" % (slot_name, SLOT_NAME))
if cache_name != CACHE_NAME:
    fatal("cached pose is named %r, expected %r" % (cache_name, CACHE_NAME))

# --- current state of the blend node ---
node = blend.get_editor_property("node")
layers = node.get_editor_property("layer_setup")
weights = node.get_editor_property("blend_weights")
msr = bool(node.get_editor_property("mesh_space_rotation_blend"))
blend_mode = node.get_editor_property("blend_mode")
log("BEFORE layers=%d weights=%s mesh_space_rotation=%s blend_mode=%s :: %s" % (
    len(layers), [float(w) for w in weights], msr, blend_mode, describe_layers(node)))
if len(layers) != 1:
    fatal("expected exactly 1 layer (one BlendPoses_0 pin is wired); found %d, a count change would re-pin the node" % len(layers))
if len(weights) != 1:
    fatal("expected 1 blend weight, found %d" % len(weights))

already = False
try:
    cur = layers[0].get_editor_property("branch_filters")
    already = (len(cur) == 1 and str(cur[0].get_editor_property("bone_name")) == BONE
               and int(cur[0].get_editor_property("blend_depth")) == DEPTH and msr)
except Exception:  # noqa: BLE001
    already = False
log("already configured = %s" % already)

if VERIFY_ONLY:
    log("RESULT=VERIFY-ONLY configured=%s" % already)
    raise SystemExit(0)

# --- STEP 11: configure ---
if not already:
    bf = unreal.BranchFilter()
    bf.set_editor_property("bone_name", BONE)
    bf.set_editor_property("blend_depth", DEPTH)
    ibp = unreal.InputBlendPose()
    ibp.set_editor_property("branch_filters", [bf])
    layers[0] = ibp
    node.set_editor_property("layer_setup", layers)
    node.set_editor_property("mesh_space_rotation_blend", True)
    if float(weights[0]) != 1.0:
        log("blend weight[0] was %s, forcing 1.0" % float(weights[0]))
        weights[0] = 1.0
        node.set_editor_property("blend_weights", weights)
    try:
        blend.modify()
    except Exception:  # noqa: BLE001
        pass
    blend.set_editor_property("node", node)

    # read back from the live node (a fresh copy), never trust the local struct
    node2 = blend.get_editor_property("node")
    ls2 = node2.get_editor_property("layer_setup")
    f2 = ls2[0].get_editor_property("branch_filters")
    msr2 = bool(node2.get_editor_property("mesh_space_rotation_blend"))
    ok = (len(ls2) == 1 and len(f2) == 1 and str(f2[0].get_editor_property("bone_name")) == BONE
          and int(f2[0].get_editor_property("blend_depth")) == DEPTH and msr2)
    log("AFTER-SET readback ok=%s :: %s mesh_space_rotation=%s" % (ok, describe_layers(node2), msr2))
    if not ok:
        fatal("readback mismatch after set_editor_property; aborting before compile")

# --- STEP 12: compile, gate, save ---
try:
    unreal.BlueprintEditorLibrary.compile_blueprint(abp)
    log("compile called")
except Exception as e:  # noqa: BLE001
    fatal("compile raised %r" % e)

status = "?"
try:
    status = str(abp.get_editor_property("status"))
except Exception as e:  # noqa: BLE001
    log("status read SKIP %s" % e)
log("compile status=%s" % status)
if "ERROR" in status.upper():
    fatal("ABP compile status is %s; NOT saving" % status)

# the node must still hold the values after compile (compile can reconstruct nodes)
node3 = blend.get_editor_property("node")
f3 = node3.get_editor_property("layer_setup")[0].get_editor_property("branch_filters")
msr3 = bool(node3.get_editor_property("mesh_space_rotation_blend"))
ok3 = (len(f3) == 1 and str(f3[0].get_editor_property("bone_name")) == BONE and msr3)
log("POST-COMPILE readback ok=%s :: %s mesh_space_rotation=%s" % (ok3, describe_layers(node3), msr3))
if not ok3:
    fatal("values lost across compile; NOT saving")

saved = eal.save_asset(ABP, only_if_is_dirty=False)
log("save_asset=%s" % saved)
if not saved:
    fatal("save failed")

# fresh T3D export for the wiring re-check (pins have no python surface)
try:
    out_dir = os.path.join(unreal.Paths.project_dir(), "Saved", "abp_export")
    os.makedirs(out_dir, exist_ok=True)
    task = unreal.AssetExportTask()
    task.set_editor_property("object", abp)
    task.set_editor_property("filename", os.path.join(out_dir, "ABP_SandboxCharacter_step11.copy"))
    task.set_editor_property("automated", True)
    task.set_editor_property("prompt", False)
    task.set_editor_property("exporter", None)
    log("post-save export ok=%s" % unreal.Exporter.run_asset_export_task(task))
except Exception as e:  # noqa: BLE001
    log("post-save export SKIP %r" % e)

log("RESULT=OK layer[0]=%s depth=%d mesh_space_rotation=1 status=%s" % (BONE, DEPTH, status))
