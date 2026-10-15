"""pose_final_legs.py - lock the winning leg configuration from the 2026-09-04 night gait matrix.

Measured (tools\\gait_analyze.py over -PoseProbe raw dumps on the pier, moving samples):
  pole op ON  (shipped state): knee lateral deviation skin vs mannequin 13 / 17 cm, thigh splay +35 / -34 deg
  pole op OFF, SyntyStraight : 0.1 / 0.2 cm and 0.8 / 0.6 deg, knee flexion within 0.5 deg, feet within 2 cm
The 'Pole Vector Alignment' op picks its reference axis from the retarget pose's chain plane; both
retarget poses have near-straight knees, so that axis is noise and the op steers the knees off by a
bias that grows with flexion. Winner = SyntyStraight pose + pole op disabled on both legs, no knee
pre-bend. This script restores SyntyStraight's calf offsets to their 07-28 values (the matrix's
pre-bend trials added to them), makes it current, disables the op on LeftLeg/RightLeg, saves.
Readback verified. Markers: WRECKTIDE-LEGFINAL."""
import unreal

M = "WRECKTIDE-LEGFINAL"
RTG = "/Game/Characters/UE4_Mannequin/Rigs/RTG_UEFN_to_UE4_Mannequin"
POSE = "SyntyStraight"
# 2026-09-04 08:20 recon (Tools\pose_recon4.py) of the shipped SyntyStraight offsets
CALF_OFFSET = {"calf_l": (-0.90, 1.89, -0.25), "calf_r": (-0.90, 1.89, -0.25)}
THIGH_OFFSET = {"thigh_l": (4.99, 1.32, 0.01), "thigh_r": (4.99, 1.32, 0.01)}
POLE_OP = "Pole Vector Alignment"


def log(msg):
    unreal.log_warning("%s %s" % (M, msg))


def fatal(msg):
    unreal.log_error("%s FATAL %s" % (M, msg))
    unreal.log_error("%s RESULT=FAIL (nothing saved)" % M)
    raise SystemExit(1)


eal = unreal.EditorAssetLibrary
rtg = eal.load_asset(RTG)
if rtg is None:
    fatal("RTG load failed")
ctl = unreal.IKRetargeterController.get_controller(rtg)
side = unreal.RetargetSourceOrTarget.TARGET

ctl.set_current_retarget_pose(unreal.Name(POSE), side)
if str(ctl.get_current_retarget_pose_name(side)) != POSE:
    fatal("could not make %s current" % POSE)

for table in (CALF_OFFSET, THIGH_OFFSET):
    for bone, (p, y, r) in table.items():
        ctl.set_rotation_offset_for_retarget_pose_bone(unreal.Name(bone), unreal.Rotator(pitch=p, yaw=y, roll=r).quaternion(), side)
        back = ctl.get_rotation_offset_for_retarget_pose_bone(unreal.Name(bone), side).rotator()
        log("%s offset -> P=%.2f Y=%.2f R=%.2f (readback P=%.2f Y=%.2f R=%.2f)" % (bone, p, y, r, back.pitch, back.yaw, back.roll))
        if abs(back.pitch - p) > 0.05 or abs(back.yaw - y) > 0.05:
            fatal("offset readback mismatch on %s" % bone)

idx = next((i for i in range(ctl.get_num_retarget_ops()) if str(ctl.get_op_name(i)) == POLE_OP), None)
if idx is None:
    fatal("op %s not found" % POLE_OP)
opc = ctl.get_op_controller(idx)
settings = opc.get_settings()
chains = settings.get_editor_property("chains_to_align")
for i in range(len(chains)):
    e = chains[i]
    if str(e.get_editor_property("target_chain_name")) in ("LeftLeg", "RightLeg"):
        e.set_editor_property("enabled", False)
        chains[i] = e
settings.set_editor_property("chains_to_align", chains)
opc.set_settings(settings)
for e in opc.get_settings().get_editor_property("chains_to_align"):
    name = str(e.get_editor_property("target_chain_name"))
    if name in ("LeftLeg", "RightLeg"):
        if bool(e.get_editor_property("enabled")):
            fatal("pole op still enabled on %s" % name)
        log("pole op %s enabled=False" % name)

if not eal.save_loaded_asset(rtg, only_if_is_dirty=False):
    fatal("save failed")
log("RESULT=OK pose=%s, calf/thigh offsets restored, leg pole alignment OFF, saved" % POSE)
