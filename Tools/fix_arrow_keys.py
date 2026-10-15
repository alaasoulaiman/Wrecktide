# Fix arrow-key movement in IMC_Sandbox v2 (play test 2026-07-27: right arrow moved him forward,
# up arrow moved him right). Recon: arrows are NOT on IA_Move at all - they ship on the GAS
# demo's world-axis action (IA_Move_WorldSpace), which is world-relative by design = the
# scrambled feel. Fix: REMOVE every arrow row regardless of action, then BIND each arrow to
# IA_Move with the modifier list copied from its WASD letter (Up=W Down=S Left=A Right=D).
# Verify-before-save; fail-closed on any surprise.
#   UnrealEditor-Cmd <proj> -run=pythonscript -script=<full path> \
#     "-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities" -unattended -nosplash -nullrhi
import unreal

IMC = "/Game/Input/IMC_Sandbox"
PAIRING = {"Up": "W", "Down": "S", "Left": "A", "Right": "D"}


def log(msg):
    unreal.log("WRECKTIDE-ARROWFIX %s" % msg)


def fatal(msg):
    unreal.log_error("WRECKTIDE-ARROWFIX FATAL %s" % msg)
    unreal.log_error("WRECKTIDE-ARROWFIX RESULT=FAIL (nothing saved)")
    raise SystemExit(1)


def key_name(key_struct):
    try:
        return key_struct.export_text()
    except Exception:
        return str(key_struct)


def mod_sig(mods):
    sig = []
    for m in mods or []:
        if m is None:
            sig.append("None")
            continue
        entry = m.get_class().get_name()
        try:
            entry += ":" + str(m.get_editor_property("Order"))
        except Exception:
            pass
        sig.append(entry)
    return sig


imc = unreal.EditorAssetLibrary.load_asset(IMC)
if imc is None:
    fatal("IMC_Sandbox load failed")
ia_move = unreal.EditorAssetLibrary.load_asset("/Game/Input/Actions/IA_Move")
if ia_move is None:
    # derive from the W row instead of trusting a path
    for m in imc.get_editor_property("Mappings"):
        a = m.get_editor_property("Action")
        if a and a.get_name() == "IA_Move":
            ia_move = a
            break
if ia_move is None:
    fatal("IA_Move action not found (path or W-row derivation)")

# ---- 1. evidence: every arrow row, whatever the action ----
mappings = list(imc.get_editor_property("Mappings"))
arrow_rows = []
letter_mods = {}
for m in mappings:
    a = m.get_editor_property("Action")
    k = key_name(m.get_editor_property("Key"))
    a_name = a.get_name() if a else "None"
    if k in PAIRING:
        arrow_rows.append((a, m.get_editor_property("Key"), a_name, k))
        log("ARROW ROW %s -> action=%s mods=%s" % (k, a_name, mod_sig(m.get_editor_property("Modifiers"))))
    if a_name == "IA_Move" and k in PAIRING.values():
        letter_mods[k] = m.get_editor_property("Modifiers")

for letter in PAIRING.values():
    if letter not in letter_mods:
        fatal("no IA_Move row for letter %s to copy modifiers from" % letter)
if not arrow_rows:
    log("no arrow rows exist at all; will bind fresh ones")

# ---- 2. remove every arrow row (any action) ----
if not hasattr(imc, "unmap_key"):
    fatal("unmap_key not exposed")
for a, k_struct, a_name, k in arrow_rows:
    imc.unmap_key(a, k_struct)
    log("removed %s from %s" % (k, a_name))

# ---- 3. bind arrows to IA_Move (FKey built by name) ----
if not hasattr(imc, "map_key"):
    fatal("map_key not exposed")
for arrow in PAIRING:
    key = unreal.Key()
    key.set_editor_property("KeyName", arrow)
    imc.map_key(ia_move, key)
    log("bound %s to IA_Move" % arrow)

# ---- 4. copy the letter modifiers onto the new arrow rows ----
mappings = list(imc.get_editor_property("Mappings"))
fixed = 0
for m in mappings:
    a = m.get_editor_property("Action")
    k = key_name(m.get_editor_property("Key"))
    if a and a.get_name() == "IA_Move" and k in PAIRING:
        m.set_editor_property("Modifiers", letter_mods[PAIRING[k]])
        fixed += 1
if fixed != len(PAIRING):
    fatal("expected to fix %d arrow rows, found %d" % (len(PAIRING), fixed))
imc.set_editor_property("Mappings", mappings)

# ---- 5. verify from the asset ----
final = imc.get_editor_property("Mappings")
for m in final:
    a = m.get_editor_property("Action")
    k = key_name(m.get_editor_property("Key"))
    if k in PAIRING:
        a_name = a.get_name() if a else "None"
        sig = mod_sig(m.get_editor_property("Modifiers"))
        want = mod_sig(letter_mods[PAIRING[k]])
        log("VERIFY %s action=%s sig=%s want=%s" % (k, a_name, sig, want))
        if a_name != "IA_Move" or sig != want:
            fatal("verify failed for %s" % k)

if not unreal.EditorAssetLibrary.save_loaded_asset(imc, only_if_is_dirty=False):
    fatal("save failed")
log("RESULT=OK arrows rebound to IA_Move with WASD-matched modifiers")
