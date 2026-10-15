"""pose_fix_localoffset.py - the 2026-09-04 crooked-legs fix (the traced PNG round).
ROOT CAUSE (-PoseProbe numbers, build 41): both bind poses are straight, the hidden mannequin idles
at ~19 deg of knee bend, the pirate skin at ~41 deg, and the skin's feet sit 4.0 cm ABOVE their
bind height. The 'Retarget IK Goals' op carries StaticLocalOffset X=+4 (LeftLeg) / X=-4 (RightLeg)
in the FOOT bone's local space, and on the UE4-mannequin skeleton family the foot bone's local X
axis points straight UP in component space (left foot quat ~(0,-.71,0,.71): X -> +Z; the mirrored
right foot maps -X -> +Z too). So both foot goals are lifted 4 cm, the hip-to-ankle line loses 4 cm,
and the knees fold to absorb it: 22 deg of manufactured bend while idle, 10-24 deg while moving.
FIX: zero the StaticLocalOffset on both leg chains (arms untouched). Fail-closed with readback.
REVERT: re-run with LEG_OFFSETS = {"LeftLeg": (4,0,0), "RightLeg": (-4,0,0)}.
WRECKTIDE-LOCALOFF markers."""
import unreal

RTG = "/Game/Characters/UE4_Mannequin/Rigs/RTG_UEFN_to_UE4_Mannequin"
OP_NAME = "Retarget IK Goals"
LEG_OFFSETS = {"LeftLeg": (0.0, 0.0, 0.0), "RightLeg": (0.0, 0.0, 0.0)}


def log(msg):
    unreal.log_warning("WRECKTIDE-LOCALOFF %s" % msg)


def fatal(msg):
    unreal.log_error("WRECKTIDE-LOCALOFF FATAL %s" % msg)
    unreal.log_error("WRECKTIDE-LOCALOFF RESULT=FAIL (nothing saved)")
    raise SystemExit(1)


eal = unreal.EditorAssetLibrary
rtg = eal.load_asset(RTG)
if rtg is None:
    fatal("RTG load failed")
ctl = unreal.IKRetargeterController.get_controller(rtg)

op_index = None
for i in range(ctl.get_num_retarget_ops()):
    if str(ctl.get_op_name(i)) == OP_NAME:
        op_index = i
        break
if op_index is None:
    fatal("op '%s' not found" % OP_NAME)
opc = ctl.get_op_controller(op_index)
if opc is None:
    fatal("op controller unavailable")

settings = opc.get_settings()
chains = settings.get_editor_property("chains_to_retarget")

changed = 0
for idx in range(len(chains)):
    entry = chains[idx]
    name = str(entry.get_editor_property("target_chain_name"))
    if name not in LEG_OFFSETS:
        continue
    before = entry.get_editor_property("static_local_offset")
    want = LEG_OFFSETS[name]
    entry.set_editor_property("static_local_offset", unreal.Vector(*want))
    chains[idx] = entry
    changed += 1
    log("chain %s static_local_offset (%.2f, %.2f, %.2f) -> (%.2f, %.2f, %.2f)"
        % (name, before.x, before.y, before.z, want[0], want[1], want[2]))
if changed != len(LEG_OFFSETS):
    fatal("expected to change %d leg chains, changed %d" % (len(LEG_OFFSETS), changed))

settings.set_editor_property("chains_to_retarget", chains)
opc.set_settings(settings)

verify = opc.get_settings().get_editor_property("chains_to_retarget")
seen = 0
for idx in range(len(verify)):
    entry = verify[idx]
    name = str(entry.get_editor_property("target_chain_name"))
    if name in LEG_OFFSETS:
        now = entry.get_editor_property("static_local_offset")
        want = LEG_OFFSETS[name]
        log("verify %s static_local_offset = (%.2f, %.2f, %.2f)" % (name, now.x, now.y, now.z))
        if abs(now.x - want[0]) > 1e-4 or abs(now.y - want[1]) > 1e-4 or abs(now.z - want[2]) > 1e-4:
            fatal("readback mismatch on %s" % name)
        seen += 1
if seen != len(LEG_OFFSETS):
    fatal("readback saw %d leg chains" % seen)

if not eal.save_loaded_asset(rtg, only_if_is_dirty=False):
    fatal("save failed")
log("RESULT=OK leg goal static_local_offset zeroed + saved (revert = re-run with the +4/-4 table)")
