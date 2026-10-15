"""pose_recon2.py - READ-ONLY recon V2 for the crooked-pose fix (day 2026-07-28).
V1's dir() filter had no 'mesh'/'preview' keywords, so it never surfaced the preview-mesh
API - the prime suspect for why auto-align was a visual no-op (align math runs against the
preview mesh; runtime retargets against the SYNTY meshes). Dumps: full controller API,
preview meshes + IK rigs both sides, every retarget pose with its stored bone offsets,
global settings, and the Synty meshes' skeleton assignment. WRECKTIDE-POSE2 markers. Saves
nothing."""
import unreal

M = "WRECKTIDE-POSE2"

RTG = "/Game/Characters/UE4_Mannequin/Rigs/RTG_UEFN_to_UE4_Mannequin"
CAPTAIN = "/Game/PolygonPirates/Meshes/CharactersUE4Mannequin/SK_Chr_Pirate_Captain_01"
DECKHAND = "/Game/PolygonPirates/Meshes/CharactersUE4Mannequin/SK_Chr_Pirate_Deckhand_01"


def log(msg):
    unreal.log_warning("%s %s" % (M, msg))


def log_long(prefix, text, cap=4000):
    text = text[:cap]
    for i in range(0, len(text), 380):
        log("%s|%s" % (prefix, text[i:i + 380]))


eal = unreal.EditorAssetLibrary
rtg = eal.load_asset(RTG)
if rtg is None:
    log("FATAL RTG LOAD FAIL")
    raise SystemExit(1)
ctl = unreal.IKRetargeterController.get_controller(rtg)

# 1) FULL API surface, chunked
api = sorted(m for m in dir(ctl) if not m.startswith("_"))
for i in range(0, len(api), 10):
    log("API[%02d] %s" % (i, ", ".join(api[i:i + 10])))

# 2) preview meshes + ik rigs per side
for side_name in ("SOURCE", "TARGET"):
    side = getattr(unreal.RetargetSourceOrTarget, side_name)
    try:
        m = ctl.get_preview_mesh(side)
        log("%s preview_mesh = %s" % (side_name, m.get_path_name() if m else None))
    except Exception as e:  # noqa: BLE001
        log("%s get_preview_mesh FAIL %r" % (side_name, e))
    try:
        rig = ctl.get_ik_rig(side)
        log("%s ik_rig = %s" % (side_name, rig.get_path_name() if rig else None))
    except Exception as e:  # noqa: BLE001
        log("%s get_ik_rig FAIL %r" % (side_name, e))

# 3) retarget poses: names, current, and each pose's stored offsets (raw struct text)
for side_name in ("SOURCE", "TARGET"):
    side = getattr(unreal.RetargetSourceOrTarget, side_name)
    try:
        cur = str(ctl.get_current_retarget_pose_name(side))
        poses = ctl.get_retarget_poses(side)
        log("%s current=%s poses=%s" % (side_name, cur, [str(k) for k in poses.keys()]))
        for pname, pose in poses.items():
            try:
                log_long("%s pose[%s]" % (side_name, pname), pose.export_text())
            except Exception as e:  # noqa: BLE001
                log("%s pose[%s] export FAIL %r" % (side_name, pname, e))
    except Exception as e:  # noqa: BLE001
        log("%s poses FAIL %r" % (side_name, e))

# 4) global settings
try:
    log_long("global", ctl.get_global_settings().export_text())
except Exception as e:  # noqa: BLE001
    log("global settings FAIL %r" % e)

# 5) the Synty meshes: do they share one skeleton? (one pose fix must cover both)
for label, path in (("CAPTAIN", CAPTAIN), ("DECKHAND", DECKHAND)):
    mesh = eal.load_asset(path)
    if mesh is None:
        log("%s LOAD FAIL" % label)
        continue
    sk = mesh.get_editor_property("skeleton")
    log("%s skeleton = %s" % (label, sk.get_path_name() if sk else None))

log("AnimPoseExtensions available = %s" % hasattr(unreal, "AnimPoseExtensions"))
log("RECON2 DONE")
