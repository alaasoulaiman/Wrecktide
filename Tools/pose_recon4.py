"""pose_recon4.py - READ-ONLY recon for the 2026-09-04 crooked-legs round. Saves nothing.
Dumps: (1) the IKRetargeterController API surface that touches poses/offsets, (2) the current
retarget pose on each side + every stored pose, (3) per-bone rotation offsets of the leg bones in
the CURRENT pose on each side (whatever getter this build exposes), (4) the target IK Rig
(IK_UE4_Mannequin_Retarget): chains (start/end/goal) + every solver with its properties + goal
settings, (5) the same for the source rig if resolvable. WRECKTIDE-POSE4 markers."""
import unreal

M = "WRECKTIDE-POSE4"
RTG = "/Game/Characters/UE4_Mannequin/Rigs/RTG_UEFN_to_UE4_Mannequin"
LEG_BONES = ["pelvis", "thigh_l", "calf_l", "foot_l", "ball_l", "thigh_r", "calf_r", "foot_r", "ball_r"]


def log(msg):
    unreal.log_warning("%s %s" % (M, msg))


def log_long(prefix, text, cap=6000):
    text = text[:cap]
    for i in range(0, len(text), 380):
        log("%s|%s" % (prefix, text[i:i + 380]))


def dump_props(label, obj, cap=3000):
    names = [n for n in dir(obj) if not n.startswith("_")]
    out = []
    for n in names:
        attr = getattr(obj, n, None)
        if callable(attr):
            continue
        try:
            v = obj.get_editor_property(n)
        except Exception:  # noqa: BLE001
            continue
        try:
            vs = v.export_text() if hasattr(v, "export_text") else str(v)
        except Exception:  # noqa: BLE001
            vs = repr(v)
        out.append("%s=%s" % (n, vs[:300]))
    log_long(label, "; ".join(out), cap)


eal = unreal.EditorAssetLibrary
rtg = eal.load_asset(RTG)
if rtg is None:
    log("RTG load FAIL")
    raise SystemExit(1)
ctl = unreal.IKRetargeterController.get_controller(rtg)

# (1) API surface
api = [m for m in dir(ctl) if not m.startswith("_") and any(k in m.lower() for k in ("pose", "offset", "align", "preview", "mesh", "rig"))]
log("ctl api: %s" % ", ".join(api))

# (2) poses
for side_name in ("SOURCE", "TARGET"):
    side = getattr(unreal.RetargetSourceOrTarget, side_name)
    try:
        cur = str(ctl.get_current_retarget_pose_name(side))
    except Exception as e:  # noqa: BLE001
        cur = "FAIL %r" % e
    try:
        names = [str(n) for n in ctl.get_retarget_pose_names(side)]
    except Exception as e:  # noqa: BLE001
        names = ["FAIL %r" % e]
    log("%s current pose=%s all=%s" % (side_name, cur, names))
    try:
        mesh = ctl.get_preview_mesh(side)
        log("%s preview mesh=%s" % (side_name, mesh.get_path_name() if mesh else None))
    except Exception as e:  # noqa: BLE001
        log("%s preview mesh FAIL %r" % (side_name, e))
    try:
        rig = ctl.get_ik_rig(side)
        log("%s ik rig=%s" % (side_name, rig.get_path_name() if rig else None))
    except Exception as e:  # noqa: BLE001
        log("%s ik rig FAIL %r" % (side_name, e))
    # root offset (1-arg form in this build)
    try:
        off = ctl.get_root_offset_in_retarget_pose(side)
        log("%s root offset in current pose = (%.2f, %.2f, %.2f)" % (side_name, off.x, off.y, off.z))
    except Exception as e:  # noqa: BLE001
        log("%s root offset FAIL %r" % (side_name, e))
    # (3) per-bone rotation offsets
    for bone in LEG_BONES:
        got = False
        for fname in ("get_rotation_offset_for_retarget_pose_bone",):
            fn = getattr(ctl, fname, None)
            if fn is None:
                continue
            for args in ((unreal.Name(bone), side), (side, unreal.Name(bone))):
                try:
                    q = fn(*args)
                    r = q.rotator() if hasattr(q, "rotator") else q
                    try:
                        log("%s pose offset %s = P=%.2f Y=%.2f R=%.2f" % (side_name, bone, r.pitch, r.yaw, r.roll))
                    except Exception:  # noqa: BLE001
                        log("%s pose offset %s = %s" % (side_name, bone, str(q)))
                    got = True
                    break
                except Exception:  # noqa: BLE001
                    continue
            if got:
                break
        if not got:
            log("%s pose offset %s = (no getter worked)" % (side_name, bone))

# (4)/(5) IK rigs
for side_name in ("SOURCE", "TARGET"):
    side = getattr(unreal.RetargetSourceOrTarget, side_name)
    try:
        rig = ctl.get_ik_rig(side)
    except Exception as e:  # noqa: BLE001
        log("%s rig fetch FAIL %r" % (side_name, e))
        continue
    if rig is None:
        log("%s rig None" % side_name)
        continue
    rc = unreal.IKRigController.get_controller(rig)
    rapi = [m for m in dir(rc) if not m.startswith("_") and any(k in m.lower() for k in ("solver", "goal", "chain", "bone"))]
    log("%s rig api: %s" % (side_name, ", ".join(rapi)[:1500]))
    try:
        chains = [str(c) for c in rc.get_retarget_chain_names()]
    except Exception as e:  # noqa: BLE001
        chains = ["FAIL %r" % e]
    log("%s chains=%s" % (side_name, chains))
    for c in chains:
        if c not in ("LeftLeg", "RightLeg", "Root", "Spine", "LeftToe", "RightToe"):
            continue
        try:
            sb = rc.get_retarget_chain_start_bone(unreal.Name(c))
            eb = rc.get_retarget_chain_end_bone(unreal.Name(c))
            gl = rc.get_retarget_chain_goal(unreal.Name(c))
            log("%s chain %s start=%s end=%s goal=%s" % (side_name, c, sb, eb, gl))
        except Exception as e:  # noqa: BLE001
            log("%s chain %s FAIL %r" % (side_name, c, e))
    try:
        n = rc.get_num_solvers()
    except Exception as e:  # noqa: BLE001
        log("%s get_num_solvers FAIL %r" % (side_name, e))
        n = 0
    for i in range(n):
        try:
            s = rc.get_solver_at_index(i)
        except Exception as e:  # noqa: BLE001
            log("%s solver[%d] FAIL %r" % (side_name, i, e))
            continue
        log("%s solver[%d] class=%s enabled=%s" % (side_name, i, s.get_class().get_name(), getattr(s, "is_enabled", lambda: "?")()))
        dump_props("%s solver[%d] props" % (side_name, i), s)
        # per-goal + per-bone settings if the solver exposes them
        for getter in ("get_goal_settings", "get_bone_settings", "get_goal_settings_for_goal"):
            fn = getattr(s, getter, None)
            if fn is not None:
                log("%s solver[%d] has %s" % (side_name, i, getter))
    # goals
    try:
        goals = rc.get_all_goals()
        for g in goals:
            log("%s goal %s bone=%s" % (side_name, g.get_editor_property("goal_name"), g.get_editor_property("bone_name")))
            dump_props("%s goal %s props" % (side_name, g.get_editor_property("goal_name")), g, 1500)
    except Exception as e:  # noqa: BLE001
        log("%s goals FAIL %r" % (side_name, e))
    # bone settings on the target rig (FBIK per-bone stiffness / preferred angles live here)
    for bone in ("thigh_l", "calf_l", "foot_l", "thigh_r", "calf_r", "foot_r", "pelvis"):
        for i in range(n):
            try:
                bs = rc.get_bone_settings(unreal.Name(bone), i)
                if bs is not None:
                    dump_props("%s bone settings %s solver[%d]" % (side_name, bone, i), bs, 1200)
            except Exception:  # noqa: BLE001
                pass

log("RECON4 DONE")
