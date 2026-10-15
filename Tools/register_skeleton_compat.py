# Chunk B step 1: register the combat montages' skeleton as COMPATIBLE on the GAS UEFN skeleton,
# so a reparented pirate can actually play the attack montages instead of silently no-opping.
#
# Direction matters: UE checks MeshSkeleton->IsCompatible(MontageSkeleton), so the skeleton the
# character MESH uses (UEFN) is the one that must list the montage's skeleton (Manny).
#
# Everything here is derived from the assets themselves, never from hardcoded skeleton paths --
# an earlier probe mislabelled SK_Mannequin as a mesh, and guessing that wrong silently produces
# a None skeleton and a meaningless "INCOMPATIBLE" verdict.
import unreal

GAS_MESH = "/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin"
MONTAGE_DIR = "/Game/Variant_Combat/Anims"


def log(msg):
    unreal.log("WRECKTIDE-COMPAT %s" % msg)


def err(msg):
    unreal.log_error("WRECKTIDE-COMPAT %s" % msg)


def skel_path(s):
    return s.get_path_name() if s else "None"


# ---------------------------------------------------------------- 1. target skeleton (from mesh)
mesh = unreal.EditorAssetLibrary.load_asset(GAS_MESH)
if mesh is None:
    err("FATAL missing GAS mesh %s" % GAS_MESH)
    raise SystemExit(1)

target = mesh.get_editor_property("skeleton")
if target is None:
    err("FATAL could not read skeleton off %s" % GAS_MESH)
    raise SystemExit(1)
log("target skeleton (from GAS mesh) = %s" % skel_path(target))


# ------------------------------------------------------- 2. discover EVERY montage + its skeleton
sources = {}   # path -> skeleton object
montages = []
for asset_path in unreal.EditorAssetLibrary.list_assets(MONTAGE_DIR, recursive=True, include_folder=False):
    clean = asset_path.split(".")[0]
    a = unreal.EditorAssetLibrary.load_asset(clean)
    if a is None or not isinstance(a, unreal.AnimMontage):
        continue
    montages.append(clean)
    try:
        s = a.get_editor_property("skeleton")
    except Exception as e:
        err("montage %s skeleton unreadable: %s" % (clean, e))
        continue
    log("montage %s -> skeleton=%s" % (clean, skel_path(s)))
    if s and s.get_path_name() != target.get_path_name():
        sources[s.get_path_name()] = s

log("found %d montage(s); %d distinct FOREIGN skeleton(s) to register" % (len(montages), len(sources)))
if not sources:
    log("NOTHING TO DO: every montage already sits on the target skeleton")
    log("RESULT=NOOP")
    raise SystemExit(0)


# ------------------------------------------------------------------- 3. bone-overlap sanity check
# Compatibility only actually works if the bone names line up. Report it rather than assume it.
def bone_names(sk):
    for attempt in ("get_bone_names", "bone_names"):
        try:
            fn = getattr(unreal.SkeletonEditorLibrary, attempt, None)
            if fn:
                return [str(b) for b in fn(sk)]
        except Exception:
            pass
    try:
        return [str(b) for b in unreal.SkeletonEditorLibrary.get_bone_names(sk)]
    except Exception:
        pass
    return None


tgt_bones = bone_names(target)
if tgt_bones is None:
    log("BONE-CHECK unavailable via python API; skeleton-editor helpers not exposed. "
        "Bone parity will be proven by -AttackProbe instead. Available bone-ish members: %s"
        % [m for m in dir(target) if "bone" in m.lower()])
else:
    log("BONE-CHECK target has %d bones" % len(tgt_bones))
    tset = set(tgt_bones)
    for p, s in sources.items():
        sb = bone_names(s)
        if sb is None:
            log("BONE-CHECK source %s unreadable" % p)
            continue
        sset = set(sb)
        missing = sorted(sset - tset)
        log("BONE-CHECK source %s: %d bones, %d shared, %d MISSING from target"
            % (p, len(sb), len(sset & tset), len(missing)))
        if missing:
            log("BONE-CHECK missing (first 25): %s" % missing[:25])


# ------------------------------------------------------------------------ 4. register the compat
existing = []
try:
    existing = list(target.get_editor_property("compatible_skeletons") or [])
except Exception as e:
    err("could not read compatible_skeletons: %s" % e)

existing_paths = set()
for c in existing:
    try:
        existing_paths.add(c.get_path_name())
    except Exception:
        existing_paths.add(str(c))
log("existing compatible_skeletons = %s" % sorted(existing_paths))

to_add = [s for p, s in sources.items() if p not in existing_paths]
if not to_add:
    log("all source skeletons ALREADY registered")
    log("RESULT=ALREADY-DONE")
    raise SystemExit(0)

method = None
new_list = existing + to_add
try:
    target.set_editor_property("compatible_skeletons", new_list)
    method = "set_editor_property(list of Skeleton)"
except Exception as e1:
    log("set_editor_property with Skeleton objects failed: %s" % e1)
    try:
        soft = [unreal.SoftObjectPath(c.get_path_name()) for c in new_list]
        target.set_editor_property("compatible_skeletons", soft)
        method = "set_editor_property(list of SoftObjectPath)"
    except Exception as e2:
        log("set_editor_property with SoftObjectPath failed: %s" % e2)
        fn = getattr(target, "add_compatible_skeleton", None)
        if fn:
            try:
                for s in to_add:
                    fn(s)
                method = "USkeleton.add_compatible_skeleton()"
            except Exception as e3:
                err("add_compatible_skeleton failed: %s" % e3)
        else:
            err("no add_compatible_skeleton on USkeleton either")

if method is None:
    err("FAILED to register compatibility by any method")
    log("RESULT=FAIL")
    raise SystemExit(1)
log("registered via %s" % method)


# ---------------------------------------------------------------------------- 5. save + re-verify
saved = unreal.EditorAssetLibrary.save_loaded_asset(target, only_if_is_dirty=False)
log("save_loaded_asset -> %s" % saved)

# Re-read from the in-memory object to confirm the property actually took.
after = []
try:
    after = list(target.get_editor_property("compatible_skeletons") or [])
except Exception as e:
    err("re-read failed: %s" % e)
after_paths = sorted({(c.get_path_name() if hasattr(c, "get_path_name") else str(c)) for c in after})
log("compatible_skeletons AFTER = %s" % after_paths)

missing_now = [p for p in sources.keys() if p not in after_paths]
if missing_now or not saved:
    err("VERIFY FAILED saved=%s still-missing=%s" % (saved, missing_now))
    log("RESULT=FAIL")
    raise SystemExit(1)

log("RESULT=OK registered=%d saved=True" % len(sources))
