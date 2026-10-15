"""pose_layer_recon.py - READ-ONLY: can the run-attack leg fix go fully headless?
Checks python exposure of: (1) montage blend-profile/blend-mask fields (mask legs out of the
attack montages = zero graph surgery), (2) montage slot tracks (retarget montages onto an
UpperBody slot), (3) the skeleton's slot registry + blend profiles. WRECKTIDE-LAYERREC markers."""
import unreal

M = "WRECKTIDE-LAYERREC"
MONTAGES = [
    "/Game/Variant_Combat/Anims/AM_ComboAttack",
    "/Game/Variant_Combat/Anims/AM_ChargedAttack",
]
SKELETON = "/Game/PolygonPirates/EpicContent/Mannequin/Character/Mesh/UE4_Mannequin_Skeleton"
DRIVER_SKEL = "/Game/Characters/UEFN_Mannequin/Meshes/SK_UEFN_Mannequin"


def log(msg):
    unreal.log_warning("%s %s" % (M, msg))


eal = unreal.EditorAssetLibrary

for path in MONTAGES:
    m = eal.load_asset(path)
    if m is None:
        log("MONTAGE %s LOAD FAIL" % path)
        continue
    name = m.get_name()
    api = sorted(x for x in dir(m) if not x.startswith("_") and ("blend" in x.lower() or "slot" in x.lower()))
    log("%s blend/slot attrs: %s" % (name, ", ".join(api)))
    for prop in ("blend_in", "blend_out", "blend_profile_in", "blend_profile_out",
                 "blend_mode_in", "blend_mode_out", "slot_anim_tracks", "blend_in_time", "blend_out_time"):
        try:
            v = m.get_editor_property(prop)
            if prop == "slot_anim_tracks":
                names = []
                for t in v:
                    try:
                        names.append(str(t.get_editor_property("slot_name")))
                    except Exception as e:  # noqa: BLE001
                        names.append("READFAIL %r" % e)
                log("%s %s = %d tracks, slots=%s" % (name, prop, len(v), names))
            else:
                try:
                    log("%s %s = %s" % (name, prop, v.export_text()[:200]))
                except Exception:  # noqa: BLE001
                    log("%s %s = %s" % (name, prop, v))
        except Exception as e:  # noqa: BLE001
            log("%s %s UNEXPOSED (%s)" % (name, prop, str(e)[:80]))
    # montage skeleton (which skeleton's slot registry matters for these montages)
    try:
        sk = m.get_editor_property("skeleton")
        log("%s skeleton = %s" % (name, sk.get_path_name() if sk else None))
    except Exception as e:  # noqa: BLE001
        log("%s skeleton read FAIL %r" % (name, e))

for label, path in (("SYNTY_SKEL", SKELETON), ("DRIVER_SKEL", DRIVER_SKEL)):
    sk = eal.load_asset(path)
    if sk is None:
        log("%s LOAD FAIL" % label)
        continue
    api = sorted(x for x in dir(sk) if not x.startswith("_") and ("slot" in x.lower() or "blend" in x.lower() or "profile" in x.lower()))
    log("%s slot/blend attrs: %s" % (label, ", ".join(api) if api else "(none)"))
    for prop in ("slot_groups", "blend_profiles"):
        try:
            v = sk.get_editor_property(prop)
            try:
                log("%s %s = %s" % (label, prop, str([str(x) for x in v])[:300]))
            except Exception:  # noqa: BLE001
                log("%s %s = %s" % (label, prop, str(v)[:200]))
        except Exception as e:  # noqa: BLE001
            log("%s %s UNEXPOSED (%s)" % (label, prop, str(e)[:80]))

log("has BlendProfile class = %s" % hasattr(unreal, "BlendProfile"))
if hasattr(unreal, "BlendProfile"):
    bp_api = sorted(x for x in dir(unreal.BlendProfile) if not x.startswith("_"))[:30]
    log("BlendProfile api: %s" % ", ".join(bp_api))
log("LAYERREC DONE")
