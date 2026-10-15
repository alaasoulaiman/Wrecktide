"""pose_fix_ikgoals.py - THE crooked-legs fix (pose pairing 2026-07-28).
ROOT CAUSE (dial0): the 'Retarget IK Goals' op has BlendToSource=1.0 on LeftLeg/RightLeg,
so foot IK goals sit at the SOURCE mannequin's absolute foot positions. Invisible when the
target is a mannequin (the GAS case); wrong for the short-legged Synty skins, whose feet
then over-reach = tiptoe / scissor / combo leg-clipping. Fix: BlendToSource -> 0.0 on both
leg chains (goals become proportion-scaled; still correct for mannequin-proportioned skins).
Fail-closed with readback verify. REVERT: re-run with TARGET_VALUE = 1.0.
WRECKTIDE-IKGOALS markers."""
import unreal

RTG = "/Game/Characters/UE4_Mannequin/Rigs/RTG_UEFN_to_UE4_Mannequin"
OP_NAME = "Retarget IK Goals"
LEG_CHAINS = ("LeftLeg", "RightLeg")
TARGET_VALUE = 0.0


def log(msg):
    unreal.log_warning("WRECKTIDE-IKGOALS %s" % msg)


def fatal(msg):
    unreal.log_error("WRECKTIDE-IKGOALS FATAL %s" % msg)
    unreal.log_error("WRECKTIDE-IKGOALS RESULT=FAIL (nothing saved)")
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
chains_prop = None
for cand in ("chains_to_retarget", "ChainsToRetarget"):
    try:
        chains = settings.get_editor_property(cand)
        chains_prop = cand
        break
    except Exception:  # noqa: BLE001
        continue
if chains_prop is None:
    fatal("chains property not found on settings struct: %s" % settings.export_text()[:300])

changed = 0
for idx in range(len(chains)):
    entry = chains[idx]
    try:
        name = str(entry.get_editor_property("target_chain_name"))
    except Exception as e:  # noqa: BLE001
        fatal("entry field target_chain_name unreadable: %r (entry=%s)" % (e, entry.export_text()[:200]))
    if name not in LEG_CHAINS:
        continue
    before = float(entry.get_editor_property("blend_to_source"))
    entry.set_editor_property("blend_to_source", TARGET_VALUE)
    chains[idx] = entry
    changed += 1
    log("chain %s blend_to_source %.2f -> %.2f" % (name, before, TARGET_VALUE))
if changed != len(LEG_CHAINS):
    fatal("expected to change %d leg chains, changed %d" % (len(LEG_CHAINS), changed))

settings.set_editor_property(chains_prop, chains)
opc.set_settings(settings)

# fresh readback through the controller before any save
verify = opc.get_settings().get_editor_property(chains_prop)
for idx in range(len(verify)):
    entry = verify[idx]
    name = str(entry.get_editor_property("target_chain_name"))
    if name in LEG_CHAINS:
        now = float(entry.get_editor_property("blend_to_source"))
        log("verify %s blend_to_source = %.2f" % (name, now))
        if abs(now - TARGET_VALUE) > 1e-6:
            fatal("readback mismatch on %s: %.3f" % (name, now))

if not eal.save_loaded_asset(rtg, only_if_is_dirty=False):
    fatal("save failed")
log("RESULT=OK leg IK goals blend_to_source=%.1f saved (revert = re-run with 1.0)" % TARGET_VALUE)
