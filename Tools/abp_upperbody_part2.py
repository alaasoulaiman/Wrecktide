"""abp_upperbody_part2.py - PART 2 + the headless tail of the upper-body layer surgery (2026-09-02).

After STEP 11/12 (abp_upperbody_step11.py) the ABP has a working 'UpperBody' slot layered over the
locomotion pose from spine_01 up. This script moves the PLAYER's attack montages onto that slot and
points the player pawn at them, leaving the enemy montages on DefaultSlot untouched:
  1. AM_ComboAttack_Player + AM_ChargedAttack_Player: every slot track -> 'UpperBody'
     (UWrecktideEditorTools::SetMontageSlotName, C++ because SlotAnimTracks has no python surface)
  2. register 'UpperBody' on BOTH skeletons (montage skeleton SK_Mannequin, driver SK_UEFN_Mannequin)
  3. CBP_SandboxCharacter CDO: ComboAttackMontage / ChargedAttackMontage -> the _Player copies
  4. verify every value from the live objects, THEN save (montages, skeletons, CBP)
Fail-closed: any check failing before the save phase aborts with NOTHING written.
Markers: WRECKTIDE-PART2. Usage: run_py.cmd <this file> [verify]
"""
import sys
import unreal

M = "WRECKTIDE-PART2"
SLOT = "UpperBody"
MONTAGES = {
    "combo": "/Game/Variant_Combat/Anims/AM_ComboAttack_Player",
    "charged": "/Game/Variant_Combat/Anims/AM_ChargedAttack_Player",
}
ENEMY_MONTAGES = ["/Game/Variant_Combat/Anims/AM_ComboAttack", "/Game/Variant_Combat/Anims/AM_ChargedAttack"]
SKELETONS = {
    "montage": "/Game/Characters/Mannequins/Meshes/SK_Mannequin",
    "driver": "/Game/Characters/UEFN_Mannequin/Meshes/SK_UEFN_Mannequin",
}
CBP = "/Game/Blueprints/CBP_SandboxCharacter"
CDO_PROPS = {"combo": "combo_attack_montage", "charged": "charged_attack_montage"}
VERIFY_ONLY = any(a.lower() == "verify" for a in sys.argv[1:])


def log(msg):
    unreal.log_warning("%s %s" % (M, msg))


def fatal(msg):
    unreal.log_error("%s FATAL %s" % (M, msg))
    unreal.log_error("%s RESULT=FAIL (nothing saved)" % M)
    raise SystemExit(1)


tools = getattr(unreal, "WrecktideEditorTools", None)
if tools is None:
    fatal("unreal.WrecktideEditorTools missing: the editor DLL predates Systems/WrecktideEditorTools.cpp, rebuild first")

eal = unreal.EditorAssetLibrary
log("mode=%s" % ("verify" if VERIFY_ONLY else "apply"))

montages = {}
for key, path in MONTAGES.items():
    m = eal.load_asset(path)
    if m is None or not isinstance(m, unreal.AnimMontage):
        fatal("montage %s missing or not an AnimMontage" % path)
    montages[key] = m
    log("%s BEFORE slots=%s skeleton=%s" % (m.get_name(), [str(n) for n in tools.get_montage_slot_names(m)],
                                             m.get_editor_property("skeleton").get_name()))

for path in ENEMY_MONTAGES:
    m = eal.load_asset(path)
    if m is None:
        fatal("enemy montage %s missing" % path)
    names = [str(n) for n in tools.get_montage_slot_names(m)]
    if any(n == SLOT for n in names):
        fatal("enemy montage %s is on %s; enemies must stay on DefaultSlot" % (m.get_name(), SLOT))
    log("%s (enemy, untouched) slots=%s" % (m.get_name(), names))

skeletons = {}
for key, path in SKELETONS.items():
    sk = eal.load_asset(path)
    if sk is None or not isinstance(sk, unreal.Skeleton):
        fatal("skeleton %s missing" % path)
    skeletons[key] = sk
    log("skeleton %s BEFORE slots=%s" % (sk.get_name(), [str(n) for n in tools.get_skeleton_slot_names(sk)]))

cbp = eal.load_asset(CBP)
if cbp is None:
    fatal("CBP load failed")
cdo = unreal.get_default_object(cbp.generated_class())
if cdo is None:
    fatal("CBP has no CDO")
for key, prop in CDO_PROPS.items():
    cur = cdo.get_editor_property(prop)
    log("CBP CDO %s BEFORE = %s" % (prop, cur.get_path_name() if cur else None))

if VERIFY_ONLY:
    log("RESULT=VERIFY-ONLY")
    raise SystemExit(0)

# ---- 1. montage slots ----
for key, m in montages.items():
    renamed = tools.set_montage_slot_name(m, SLOT)
    names = [str(n) for n in tools.get_montage_slot_names(m)]
    log("%s set slot -> renamed=%d now=%s" % (m.get_name(), renamed, names))
    if renamed < 0 or not names or any(n != SLOT for n in names):
        fatal("%s slot tracks did not all move to %s" % (m.get_name(), SLOT))

# ---- 2. skeleton registry ----
for key, sk in skeletons.items():
    added = tools.register_skeleton_slot(sk, SLOT)
    names = [str(n) for n in tools.get_skeleton_slot_names(sk)]
    log("skeleton %s register -> added=%s now=%s" % (sk.get_name(), added, names))
    if SLOT not in names:
        fatal("skeleton %s does not list %s after registration" % (sk.get_name(), SLOT))

# ---- 3. CDO repoint ----
for key, prop in CDO_PROPS.items():
    cdo.set_editor_property(prop, montages[key])
    got = cdo.get_editor_property(prop)
    log("CBP CDO %s AFTER = %s" % (prop, got.get_path_name() if got else None))
    if got is None or got.get_path_name() != montages[key].get_path_name():
        fatal("CDO %s did not take the _Player montage" % prop)

# ---- 4. save phase (all verified above) ----
saved = []
for key, m in montages.items():
    if not eal.save_asset(MONTAGES[key], only_if_is_dirty=False):
        fatal("save failed %s" % MONTAGES[key])
    saved.append(m.get_name())
for key, sk in skeletons.items():
    if not eal.save_asset(SKELETONS[key], only_if_is_dirty=False):
        fatal("save failed %s" % SKELETONS[key])
    saved.append(sk.get_name())
if not eal.save_asset(CBP, only_if_is_dirty=False):
    fatal("save failed %s" % CBP)
saved.append("CBP_SandboxCharacter")
log("RESULT=OK saved=%s" % saved)
