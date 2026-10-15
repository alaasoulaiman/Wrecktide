"""pose_dial0.py - READ-ONLY: dump the RTG root/pelvis settings + per-op settings surface
before turning any dial. WRECKTIDE-DIAL0 markers. Saves nothing."""
import unreal

M = "WRECKTIDE-DIAL0"
RTG = "/Game/Characters/UE4_Mannequin/Rigs/RTG_UEFN_to_UE4_Mannequin"


def log(msg):
    unreal.log_warning("%s %s" % (M, msg))


def log_long(prefix, text, cap=2400):
    text = text[:cap]
    for i in range(0, len(text), 380):
        log("%s|%s" % (prefix, text[i:i + 380]))


rtg = unreal.EditorAssetLibrary.load_asset(RTG)
ctl = unreal.IKRetargeterController.get_controller(rtg)

try:
    rs = ctl.get_root_settings()
    log_long("rootsettings", rs.export_text())
except Exception as e:  # noqa: BLE001
    log("get_root_settings FAIL %r" % e)

try:
    n = ctl.get_num_retarget_ops()
    for i in range(n):
        nm = str(ctl.get_op_name(i))
        try:
            opc = ctl.get_op_controller(i)
            if opc is None:
                log("op[%d] %s controller=None" % (i, nm))
                continue
            api = [m for m in dir(opc) if not m.startswith("_") and ("setting" in m.lower() or "get" in m.lower())][:20]
            log("op[%d] %s controller api: %s" % (i, nm, ", ".join(api)))
            for getter in ("get_settings",):
                fn = getattr(opc, getter, None)
                if fn is None:
                    continue
                try:
                    s = fn()
                    log_long("op[%d] %s settings" % (i, nm), s.export_text())
                except Exception as e:  # noqa: BLE001
                    log("op[%d] %s %s FAIL %r" % (i, nm, getter, e))
        except Exception as e:  # noqa: BLE001
            log("op[%d] %s controller FAIL %r" % (i, nm, e))
except Exception as e:  # noqa: BLE001
    log("op sweep FAIL %r" % e)

# root offset currently stored in each target pose
for pose_name in ("Default Pose", "SyntyStraight"):
    try:
        off = ctl.get_root_offset_in_retarget_pose(unreal.Name(pose_name), unreal.RetargetSourceOrTarget.TARGET)
        log("target pose %s root offset = (%.2f, %.2f, %.2f)" % (pose_name, off.x, off.y, off.z))
    except Exception as e:  # noqa: BLE001
        log("root offset read %s FAIL %r" % (pose_name, e))

log("DIAL0 DONE")
