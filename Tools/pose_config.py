"""pose_config.py - set one retarget configuration for a gait experiment (2026-09-04 night).
Usage: run_py.cmd <this file> <target_pose_name> <pole:on|off> [kneebend_deg]
  target_pose_name : e.g. SyntyStraight | SyntyMeshAligned | Default Pose (must exist on the TARGET side)
  pole             : enable/disable the 'Pole Vector Alignment' op on LeftLeg + RightLeg
  kneebend_deg     : optional extra knee flexion (pitch on calf_l/calf_r) added to the target pose as a
                     rotation offset, so the leg chain plane is well defined for the pole axis pick
Fail-closed with readback, saves the RTG. Markers: WRECKTIDE-POSECFG."""
import math
import sys
import unreal

M = "WRECKTIDE-POSECFG"
RTG = "/Game/Characters/UE4_Mannequin/Rigs/RTG_UEFN_to_UE4_Mannequin"
POLE_OP = "Pole Vector Alignment"


def log(msg):
    unreal.log_warning("%s %s" % (M, msg))


def fatal(msg):
    unreal.log_error("%s FATAL %s" % (M, msg))
    unreal.log_error("%s RESULT=FAIL (nothing saved)" % M)
    raise SystemExit(1)


args = [a for a in sys.argv[1:] if a]
# the commandlet splits on spaces, so a pose name like "Default Pose" arrives as two tokens: the pose
# is everything before the on/off token
pole_idx = next((i for i, a in enumerate(args) if a.lower() in ("on", "off")), -1)
if pole_idx < 1:
    fatal("usage: pose_config.py <pose name> <on|off> [kneebend]; got %s" % args)
pose_name = " ".join(args[:pole_idx])
pole_on = args[pole_idx].lower() == "on"
knee_bend = float(args[pole_idx + 1]) if len(args) > pole_idx + 1 else 0.0

eal = unreal.EditorAssetLibrary
rtg = eal.load_asset(RTG)
if rtg is None:
    fatal("RTG load failed")
ctl = unreal.IKRetargeterController.get_controller(rtg)
side = unreal.RetargetSourceOrTarget.TARGET

# 1. target pose
ctl.set_current_retarget_pose(unreal.Name(pose_name), side)
now = str(ctl.get_current_retarget_pose_name(side))
if now != pose_name:
    fatal("pose %s did not become current (now %s)" % (pose_name, now))
log("target pose = %s" % now)

# 2. optional knee pre-bend on the target pose (calf pitch, local offset)
if abs(knee_bend) > 0.01:
    for bone in ("calf_l", "calf_r"):
        cur = ctl.get_rotation_offset_for_retarget_pose_bone(unreal.Name(bone), side)
        r = cur.rotator()
        new_rot = unreal.Rotator(pitch=r.pitch + knee_bend, yaw=r.yaw, roll=r.roll)
        ctl.set_rotation_offset_for_retarget_pose_bone(unreal.Name(bone), new_rot.quaternion(), side)
        back = ctl.get_rotation_offset_for_retarget_pose_bone(unreal.Name(bone), side).rotator()
        log("%s offset pitch %.2f -> %.2f (readback %.2f)" % (bone, r.pitch, r.pitch + knee_bend, back.pitch))

# 3. pole vector op on the legs
idx = None
for i in range(ctl.get_num_retarget_ops()):
    if str(ctl.get_op_name(i)) == POLE_OP:
        idx = i
        break
if idx is None:
    fatal("op %s not found" % POLE_OP)
opc = ctl.get_op_controller(idx)
settings = opc.get_settings()
chains = settings.get_editor_property("chains_to_align")
changed = 0
for i in range(len(chains)):
    entry = chains[i]
    name = str(entry.get_editor_property("target_chain_name"))
    if name in ("LeftLeg", "RightLeg"):
        entry.set_editor_property("enabled", pole_on)
        chains[i] = entry
        changed += 1
if changed != 2:
    fatal("expected 2 leg chains in the pole op, changed %d" % changed)
settings.set_editor_property("chains_to_align", chains)
opc.set_settings(settings)
verify = opc.get_settings().get_editor_property("chains_to_align")
for i in range(len(verify)):
    entry = verify[i]
    name = str(entry.get_editor_property("target_chain_name"))
    if name in ("LeftLeg", "RightLeg"):
        val = bool(entry.get_editor_property("enabled"))
        log("pole %s enabled=%s" % (name, val))
        if val != pole_on:
            fatal("pole readback mismatch on %s" % name)

if not eal.save_loaded_asset(rtg, only_if_is_dirty=False):
    fatal("save failed")
log("RESULT=OK pose=%s pole=%s kneebend=%.1f saved" % (pose_name, "on" if pole_on else "off", knee_bend))
