"""pose_dup_montages.py - duplicate the two attack montages as PLAYER-ONLY copies so the
upper-body slot change cannot break enemy attacks (ACombatEnemy plays the originals).
New assets only - safe beside an open editor. Fail-closed. WRECKTIDE-DUPMONT markers."""
import unreal

PAIRS = [
    ("/Game/Variant_Combat/Anims/AM_ComboAttack", "/Game/Variant_Combat/Anims/AM_ComboAttack_Player"),
    ("/Game/Variant_Combat/Anims/AM_ChargedAttack", "/Game/Variant_Combat/Anims/AM_ChargedAttack_Player"),
]


def log(msg):
    unreal.log_warning("WRECKTIDE-DUPMONT %s" % msg)


def fatal(msg):
    unreal.log_error("WRECKTIDE-DUPMONT FATAL %s" % msg)
    raise SystemExit(1)


eal = unreal.EditorAssetLibrary
for src, dst in PAIRS:
    if eal.does_asset_exist(dst):
        log("%s already exists, skipping" % dst)
        continue
    if not eal.does_asset_exist(src):
        fatal("source missing: %s" % src)
    dup = eal.duplicate_asset(src, dst)
    if dup is None:
        fatal("duplicate failed: %s" % dst)
    if not eal.save_asset(dst, only_if_is_dirty=False):
        fatal("save failed: %s" % dst)
    log("duplicated %s -> %s" % (src.rsplit('/', 1)[-1], dst.rsplit('/', 1)[-1]))
log("RESULT=OK player-only montage copies ready")
