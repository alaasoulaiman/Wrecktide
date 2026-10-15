"""look_lighting.py - brighter readable daylight + water that reads as water (2026-09-06, lane L2).

Two beta complaints, one level pass, because both live in the same actors:

  C6 "too dark at times in the game, especially the upper half of the screen where it wasn't sand."
     build_island_level.py:161-178 left the sun at 3.0 lux, the SkyLight at its default intensity,
     the fog at density 0.015, and NO PostProcessVolume anywhere. With a 30 stop auto exposure
     window (-10..20 EV100 is the UE 5.6 default once ExtendDefaultLuminanceRange is on, see
     Engine\\Source\\Runtime\\Engine\\Private\\Scene.cpp:467) the bright sand drives the whole frame
     down and everything above the horizon sits in the dark part of the curve.

  C5 "I can't even see the water when I am swimming in it. It's like it's not even there."
     The ocean is a flat plane: no WaterWaves asset was ever assigned (grep of every Tools\\*.py
     finds only the two place_class lines). A mirror-flat Single Layer Water surface at a grazing
     angle is pure specular, so it renders the sky back at you and reads as nothing. Gerstner waves
     give the surface normals to break that up and to catch a sun highlight.

Levers, all data on the command line so a re-tune is one run and no code edit:
  sun intensity + colour, sky light intensity, fog density / falloff / inscattering colour, an
  UNBOUND PostProcessVolume with a narrow EV100 window, and an instanced UGerstnerWaterWaves on the
  ocean. The underwater post process material slot is filled too (5.6 gates it on
  bGenerateOverlapEvents, not the deprecated bGenerateCollisions).

The sea PhysicsVolume, KillZ and the surf walls are NOT touched: swimming is island_sea_volume.py's
contract and it is re-verified after this runs.

Usage (through tools\\run_py_args.cmd, WRECKTIDE_REPO set to this worktree):
    look_lighting.py verify
    look_lighting.py apply [--sun 9.0] [--sky 2.2] [--fog 0.005] [--ev 4.0] [--evwin 1.0]
                           [--bias 0.4] [--waveamp 20] [--nowaves]
Markers: WRECKTIDE-LOOK. Nothing is saved unless every required actor was found and every write read
back with the value it was given.
"""
import sys
import unreal

M = "WRECKTIDE-LOOK"
MAP = "/Game/Lvl_Island"
Z_WATER = -120.0
PLANE_MESH = "/Engine/BasicShapes/Plane"
STYLE_WATER_MAT = "/Game/PolygonPirates/Materials/MI_PolygonPirates_Water"
STYLE_PLANE_LABEL = "LOOK_stylised_sea"
PPV_LABEL = "LOOK_exposure"
UNDERWATER_MAT = "/Water/Materials/PostProcessing/M_UnderWater_PostProcess_Volume"

FAILED = []


def log(msg):
    unreal.log_warning("%s %s" % (M, msg))


def err(msg):
    FAILED.append(msg)
    unreal.log_error("%s %s" % (M, msg))


def fatal(msg):
    unreal.log_error("%s FATAL %s" % (M, msg))
    unreal.log_error("%s RESULT=FAIL (nothing saved)" % M)
    raise SystemExit(1)


def argf(name, default):
    """--name value on the commandlet line. run_py_args.cmd forwards everything after the script."""
    for i, a in enumerate(sys.argv):
        if a == name and i + 1 < len(sys.argv):
            return float(sys.argv[i + 1])
    return default


VERIFY_ONLY = any(a.lower() == "verify" for a in sys.argv[1:])
NO_WAVES = any(a.lower() == "--nowaves" for a in sys.argv[1:])
FOG_TINT = any(a.lower() == "--fogtint" for a in sys.argv[1:])
# Sky light lower hemisphere: black by default, which is why raising Intensity did nothing for the
# shadow sides. --lowerblack puts the engine default back so the experiment stays repeatable.
LOWER_BLACK = any(a.lower() == "--lowerblack" for a in sys.argv[1:])
# Round 3 water experiments (see apply_water). STYLISED hides the Water plugin surface and lays a
# flat Synty-material plane on it instead; NOSTYLISED puts the plugin surface back.
STYLISED = any(a.lower() == "--stylised" for a in sys.argv[1:])
NOSTYLISED = any(a.lower() == "--nostylised" for a in sys.argv[1:])

# MEASURED, not assumed. Spec C6 asked for the sun at 8 to 10 lux; every variant of that measured
# WORSE than the 3.0 lux the level shipped with, at the camp spot:
#     shipped                        sun 3.0 sky 1.0  upper 0.675  black 3.8%   (look_camp_before)
#     spec numbers + 1 stop EV clamp sun 9.0 sky 2.2  upper 0.294  black 64.9%  (look_camp_probe)
#     fill ratio fixed               sun 5.0 sky 5.0  upper 0.296  black 32.0%  (look_camp_probe2)
#     same, default EV window        sun 5.0 sky 5.0  upper 0.552  black  8.8%  (look_camp_ev0)
# Raising the KEY light in an auto-exposed scene buys nothing: the meter simply stops down by the
# same amount, and everything the sun does NOT reach goes darker in the process. The sun therefore
# stays where it shipped. Only fill and exposure compensation are left raised.
SUN_INTENSITY = argf("--sun", 3.0)
SUN_COLOUR = (1.0, 1.0, 1.0)                # left as shipped: a warm tint was never measured as a win
# Also left as shipped, and for the same measured reason as the sun. Sky light 3.0 did lift the
# shaded surfaces, but it lifted the metered average with them and the auto exposure stopped down by
# more than it gained: across the six spots at sun 3.0 / sky 3.0 / bias +0.5 the upper half came out
# DARKER than shipped at five of six (navy 0.668 -> 0.438, grave 0.680 -> 0.548), and only the pier
# improved (black 8.6% -> 4.6%). In an auto exposed scene with no bounce, adding light of any kind
# is close to a no-op; the thing that actually moves every pixel is the exposure compensation below.
SKY_INTENSITY = argf("--sky", 1.0)
# Left at the level's original 0.015 on purpose. Thinning the fog took light OUT of the frame
# rather than putting it in, because with DynamicGlobalIlluminationMethod=0 the atmosphere-driven
# fog is one of only two fill terms in the whole level (the other is the sky light).
FOG_DENSITY = argf("--fog", 0.015)
FOG_FALLOFF = argf("--fogfall", 0.20)       # left at the level's original value: see FOG_START
FOG_START = argf("--fogstart", 0.0)         # never raise this, it is shadow fill (see apply_lighting)
# The EV window is left at the engine default (-10..20). Spec C6 asked for a roughly 1 stop clamp;
# pinning an absolute EV100 without an in-editor preview is a guess, and the guess measured as the
# single worst change of the whole pass (camp black 3.8% -> 64.9%). Exposure COMPENSATION does the
# same job safely: it rides on top of whatever the meter finds instead of overriding it.
EV_CENTRE = argf("--ev", 5.0)
EV_WINDOW = argf("--evwin", 30.0)           # 30 = the engine default -10..20, i.e. no clamp
EV_BIAS = argf("--bias", 0.5)               # exposure compensation, spec C6 range +0.3..+0.8
WAVE_AMP_MAX = argf("--waveamp", 18.0)      # spec C5: amplitude 8 to 20 uu
WAVE_AMP_MIN = argf("--waveampmin", 8.0)
TESS_FACTOR = int(argf("--tess", 6.0))   # UWaterMeshComponent::TessellationFactor, engine default 6
LOWER_R = argf("--lowerr", 0.45)            # warm sand bounce, the coordinator's numbers
LOWER_G = argf("--lowerg", 0.40)
LOWER_B = argf("--lowerb", 0.32)
WAVE_LEN_MIN = argf("--wavelenmin", 400.0)  # spec C5: wavelength 400 to 2500 uu
WAVE_LEN_MAX = argf("--wavelenmax", 2500.0)
# spec C5: 4 to 6 waves. MaxWaveHeight is the SUM of the amplitudes, so 4 waves at 8..18 uu tops
# out near 52 uu worst case against the 80 uu the beach ring stands above the sea. 6 waves at the
# spec's upper amplitude would have crests washing over the beach tier.
WAVE_COUNT = int(argf("--waves", 4.0))


def get_world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def all_actors():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()


def first_of(cls):
    for a in all_actors():
        if isinstance(a, cls):
            return a
    return None


def try_set(obj, names, value, label):
    """set_editor_property with a list of candidate spellings (UE renames these across versions:
    FogInscatteringColor became FogInscatteringLuminance, bUnbound is exposed as 'unbound').
    Returns the name that took, or None. Read-back is the caller's proof, not this."""
    for n in names:
        try:
            obj.set_editor_property(n, value)
            return n
        except Exception:
            continue
    err("could not set %s (tried %s)" % (label, ",".join(names)))
    return None


def try_get(obj, names, default=None):
    for n in names:
        try:
            return obj.get_editor_property(n)
        except Exception:
            continue
    return default


def near(a, b, tol=1e-3):
    return abs(float(a) - float(b)) <= tol


# ------------------------------------------------------------------------------- report the state
def report():
    sun = first_of(unreal.DirectionalLight)
    sky = first_of(unreal.SkyLight)
    fog = first_of(unreal.ExponentialHeightFog)
    ocean = first_of(unreal.WaterBodyOcean)
    ppv = None
    for a in all_actors():
        if isinstance(a, unreal.PostProcessVolume) and a.get_actor_label() == PPV_LABEL:
            ppv = a
            break

    if sun:
        c = sun.get_editor_property("LightComponent")
        log("sun intensity=%.3f colour=%s" % (c.get_editor_property("Intensity"), c.get_editor_property("LightColor")))
    else:
        log("sun MISSING")
    if sky:
        c = sky.get_editor_property("LightComponent")
        # Full state, because "the sky light is set to 3.0" says nothing about whether it is
        # CONTRIBUTING. Three separate things can leave it dark at full intensity: a stale/black
        # captured cubemap, bLowerHemisphereIsBlack (which zeroes fill on every vertical face), and
        # a SourceType pointing at a cubemap asset that is not there.
        log("skylight intensity=%.3f realtime=%s source=%s lower_black=%s lower_colour=%s cubemap_res=%s dist=%s" % (
            c.get_editor_property("Intensity"),
            try_get(c, ["bRealTimeCapture", "real_time_capture"], "?"),
            try_get(c, ["SourceType", "source_type"], "?"),
            try_get(c, ["bLowerHemisphereIsBlack", "lower_hemisphere_is_black"], "?"),
            try_get(c, ["LowerHemisphereColor", "lower_hemisphere_color"], "?"),
            try_get(c, ["CubemapResolution", "cubemap_resolution"], "?"),
            try_get(c, ["SkyDistanceThreshold", "sky_distance_threshold"], "?")))
    else:
        log("skylight MISSING")
    if fog:
        c = fog.get_editor_property("Component")
        log("fog density=%.4f falloff=%.3f inscatter=%s" % (
            c.get_editor_property("FogDensity"), c.get_editor_property("FogHeightFalloff"),
            try_get(c, ["FogInscatteringLuminance", "FogInscatteringColor"], "?")))
    else:
        log("fog MISSING")
    if ppv:
        s = ppv.get_editor_property("Settings")
        log("ppv unbound=%s minEV=%.2f maxEV=%.2f bias=%.2f method=%s" % (
            try_get(ppv, ["unbound", "bUnbound"], "?"),
            s.get_editor_property("auto_exposure_min_brightness"),
            s.get_editor_property("auto_exposure_max_brightness"),
            s.get_editor_property("auto_exposure_bias"),
            s.get_editor_property("auto_exposure_method")))
    else:
        log("ppv %s MISSING" % PPV_LABEL)
    if ocean:
        waves = try_get(ocean, ["WaterWaves"], None)
        comp = ocean.get_water_body_component()
        log("ocean waves=%s n=%s maxheight=%s" % (
            waves.get_class().get_name() if waves else None,
            len(try_get(waves, ["GerstnerWaves"], []) or []) if waves else 0,
            try_get(waves, ["MaxWaveHeight"], "n/a") if waves else "n/a"))
        log("ocean overlap_events=%s underwater_mat=%s" % (
            try_get(comp, ["bGenerateOverlapEvents", "generate_overlap_events"], "?"),
            (try_get(comp, ["UnderwaterPostProcessMaterial"], None) or "NONE")))
    else:
        log("ocean MISSING")
    return sun, sky, fog, ocean, ppv


# ------------------------------------------------------------------------------- lights and fog
def apply_lighting(sun, sky, fog):
    sun_c = sun.get_editor_property("LightComponent")
    sun_c.set_editor_property("Intensity", SUN_INTENSITY)
    sun_c.set_light_color(unreal.LinearColor(SUN_COLOUR[0], SUN_COLOUR[1], SUN_COLOUR[2], 1.0))
    back = sun_c.get_editor_property("Intensity")
    if not near(back, SUN_INTENSITY, 0.01):
        err("sun intensity read back %.3f, wanted %.3f" % (back, SUN_INTENSITY))
    log("sun intensity %.2f lux, warm colour" % back)

    sky_c = sky.get_editor_property("LightComponent")
    sky_c.set_editor_property("Intensity", SKY_INTENSITY)
    # DO NOT switch bRealTimeCapture off here. Turning it off and baking a cubemap instead cost the
    # scene every scrap of shadow fill: run_py_args.cmd runs the commandlet with -nullrhi, so
    # recapture_sky() has nothing to render and serialises a BLACK cubemap. With
    # DynamicGlobalIlluminationMethod=0 the sky light is the ONLY fill light in the level, so the
    # awning at the camp went from a readable shadow to 61 percent black pixels
    # (_logs\look_camp_after_view1.png, first attempt). This is the same "stale empty capture" trap
    # build_island_level.py:162-164 already warns about, reached from the other side.
    # It is set explicitly rather than just left alone because one run of this script DID save it
    # off, and a re-run has to be able to put the level back.
    try_set(sky_c, ["bRealTimeCapture", "real_time_capture"], True, "skylight realtime capture on")
    # bLowerHemisphereIsBlack is ON by default and it is a hard zero, not a dim value: every ray the
    # sky light would gather from below the horizon returns black. On an island that is most of what
    # a VERTICAL face sees, so the shadow side of every crate, gravestone and hull gets no fill at
    # all no matter how high Intensity goes. Turning it off with a warm sand bounce colour is the
    # cheapest stand-in for the bounce light r.DynamicGlobalIlluminationMethod=0 removed.
    try_set(sky_c, ["bLowerHemisphereIsBlack", "lower_hemisphere_is_black"], LOWER_BLACK,
            "skylight lower hemisphere black")
    try_set(sky_c, ["LowerHemisphereColor", "lower_hemisphere_color"],
            unreal.LinearColor(LOWER_R, LOWER_G, LOWER_B, 1.0), "skylight lower hemisphere colour")
    lb = try_get(sky_c, ["bLowerHemisphereIsBlack", "lower_hemisphere_is_black"], None)
    if lb != LOWER_BLACK:
        err("skylight lower hemisphere black read back %s, wanted %s" % (lb, LOWER_BLACK))
    log("skylight lower hemisphere black=%s colour=(%.2f,%.2f,%.2f)" % (lb, LOWER_R, LOWER_G, LOWER_B))
    rtc = try_get(sky_c, ["bRealTimeCapture", "real_time_capture"], None)
    if rtc is not True:
        err("skylight real time capture read back %s, wanted True" % rtc)
    log("skylight real time capture = %s" % rtc)
    back = sky_c.get_editor_property("Intensity")
    if not near(back, SKY_INTENSITY, 0.01):
        err("skylight intensity read back %.3f, wanted %.3f" % (back, SKY_INTENSITY))
    log("skylight intensity %.2f" % back)

    fog_c = fog.get_editor_property("Component")
    fog_c.set_editor_property("FogDensity", FOG_DENSITY)
    fog_c.set_editor_property("FogHeightFalloff", FOG_FALLOFF)
    # DO NOT write a fog inscattering colour. The level's value is BLACK (0,0,0), and in UE 5 that
    # is not "no fog colour": a black FogInscatteringLuminance is the signal that the fog takes its
    # colour from the SkyAtmosphere, which is bright daylight haze. Writing a literal warm colour
    # replaced that with a fixed 0.62 cd/m2 veil and dropped the camp frame from 0.675 to 0.296
    # upper luminance with 32 percent black pixels (_logs\look_camp_probe2_view1.png). The two
    # writes below are kept behind an off-by-default flag purely so the experiment is repeatable.
    tint = unreal.LinearColor(0.62, 0.70, 0.82, 1.0) if FOG_TINT else unreal.LinearColor(0.0, 0.0, 0.0, 1.0)
    dir_tint = unreal.LinearColor(1.0, 0.86, 0.62, 1.0) if FOG_TINT else unreal.LinearColor(0.0, 0.0, 0.0, 1.0)
    # Written every run, not just when tinting: an earlier run of this script DID tint the fog and
    # saved it, so "stop setting it" is not the same as "put it back". Every lever this script owns
    # has to be able to restore the level, or a bad experiment is permanent.
    try_set(fog_c, ["FogInscatteringLuminance", "FogInscatteringColor"], tint, "fog inscattering")
    try_set(fog_c, ["DirectionalInscatteringLuminance", "DirectionalInscatteringColor"], dir_tint,
            "fog directional inscattering")
    # StartDistance MUST stay 0. Setting it to 800 uu cut the fog out of everything inside 8 m,
    # and the near-field fog was carrying the shadow fill: the camp awning went to 65 percent black
    # pixels with it on (_logs\look_camp_probe_view1.png). Fog is a lighting term in this level, not
    # just a distance haze, because DynamicGlobalIlluminationMethod=0 leaves no bounce at all.
    try_set(fog_c, ["StartDistance"], FOG_START, "fog start distance")
    back = fog_c.get_editor_property("FogDensity")
    if not near(back, FOG_DENSITY, 1e-5):
        err("fog density read back %.5f, wanted %.5f" % (back, FOG_DENSITY))
    log("fog density %.4f falloff %.2f tint=%s" % (back, fog_c.get_editor_property("FogHeightFalloff"), FOG_TINT))


# ------------------------------------------------------------------------------- exposure volume
def apply_ppv(existing):
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    ppv = existing
    if ppv is None:
        # spawn_actor_from_class gives a volume an EMPTY brush (the trap that cost the island its
        # navmesh and its swimming, see WrecktideEditorTools.h). That is harmless here and only
        # here: an UNBOUND post process volume ignores its own shape and applies to the whole
        # world, so there is no brush to get wrong. It must stay unbound for that reason.
        ppv = eas.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 0))
        if ppv is None:
            err("PostProcessVolume spawn failed")
            return None
        ppv.set_actor_label(PPV_LABEL)
        ppv.set_folder_path("Look")
    try_set(ppv, ["unbound", "bUnbound"], True, "ppv unbound")
    try_set(ppv, ["enabled", "bEnabled"], True, "ppv enabled")
    ppv.set_editor_property("priority", 1.0)
    ppv.set_editor_property("blend_weight", 1.0)

    s = ppv.get_editor_property("Settings")
    lo = EV_CENTRE - EV_WINDOW * 0.5
    hi = EV_CENTRE + EV_WINDOW * 0.5
    pairs = [
        ("override_auto_exposure_method", True), ("auto_exposure_method", unreal.AutoExposureMethod.AEM_HISTOGRAM),
        ("override_auto_exposure_min_brightness", True), ("auto_exposure_min_brightness", lo),
        ("override_auto_exposure_max_brightness", True), ("auto_exposure_max_brightness", hi),
        ("override_auto_exposure_bias", True), ("auto_exposure_bias", EV_BIAS),
        # a slow adapt across a 1 stop window would only ever be visible as a lag, so pin it fast
        ("override_auto_exposure_speed_up", True), ("auto_exposure_speed_up", 8.0),
        ("override_auto_exposure_speed_down", True), ("auto_exposure_speed_down", 8.0),
        # the corners that read black at the wreck are contact AO, not shadow: halve it
        ("override_ambient_occlusion_intensity", True), ("ambient_occlusion_intensity", 0.25),
    ]
    for name, value in pairs:
        try:
            s.set_editor_property(name, value)
        except Exception as e:
            err("ppv setting %s failed: %s" % (name, e))
    ppv.set_editor_property("Settings", s)

    back = ppv.get_editor_property("Settings")
    got_lo = back.get_editor_property("auto_exposure_min_brightness")
    got_hi = back.get_editor_property("auto_exposure_max_brightness")
    got_bias = back.get_editor_property("auto_exposure_bias")
    if not (near(got_lo, lo, 0.01) and near(got_hi, hi, 0.01) and near(got_bias, EV_BIAS, 0.01)):
        err("ppv exposure read back min=%.2f max=%.2f bias=%.2f, wanted %.2f/%.2f/%.2f" % (
            got_lo, got_hi, got_bias, lo, hi, EV_BIAS))
    log("ppv unbound exposure EV100 %.2f..%.2f bias %+.2f" % (got_lo, got_hi, got_bias))
    return ppv


# ------------------------------------------------------------------------------- ocean waves
def apply_water(ocean):
    comp = ocean.get_water_body_component()
    if comp is None:
        err("ocean has no water body component")
        return

    if NO_WAVES:
        # Escape hatch, and it must WRITE, not just skip: once waves are saved into the map, "do not
        # set them" leaves them there. Clearing the property is the only way back to a flat ocean.
        ocean.set_water_waves(None)
        left = try_get(ocean, ["WaterWaves"], None)
        if left is not None:
            err("waves still present after clear: %s" % left)
        else:
            log("ocean waves cleared (flat surface)")
    if not NO_WAVES:
        # Instanced on the actor, NOT a new .uasset: AWaterBody::WaterWaves is an Instanced
        # UPROPERTY, so the wave object serialises into the map and cooks with it. No new asset
        # means no new staging rule and nothing for the cooker to drop.
        waves = unreal.new_object(unreal.GerstnerWaterWaves, outer=ocean)
        gen = waves.get_editor_property("GerstnerWaveGenerator")
        if gen is None:
            gen = unreal.new_object(unreal.GerstnerWaterWaveGeneratorSimple, outer=waves)
        gen.set_editor_property("NumWaves", WAVE_COUNT)
        gen.set_editor_property("Seed", 7)
        gen.set_editor_property("MinWavelength", WAVE_LEN_MIN)
        gen.set_editor_property("MaxWavelength", WAVE_LEN_MAX)
        gen.set_editor_property("WavelengthFalloff", 2.0)
        gen.set_editor_property("MinAmplitude", WAVE_AMP_MIN)
        gen.set_editor_property("MaxAmplitude", WAVE_AMP_MAX)
        gen.set_editor_property("AmplitudeFalloff", 2.0)
        gen.set_editor_property("WindAngleDeg", 35.0)          # same yaw as the sun, so the glint lines up
        gen.set_editor_property("DirectionAngularSpreadDeg", 60.0)
        gen.set_editor_property("SmallWaveSteepness", 0.5)
        gen.set_editor_property("LargeWaveSteepness", 0.3)
        # Setting the generator BACK on the waves object is what fires UGerstnerWaterWaves::
        # PostEditChangeProperty -> RecomputeWaves (GerstnerWaterWaves.cpp:54-59). RecomputeWaves is
        # WATER_API but not a UFUNCTION, so this notify is the only way to reach it from python.
        # notify_mode=ALWAYS is load bearing: on the default mode the write was silent, the
        # constructor's own RecomputeWaves(false) stood, and the ocean got 16 DEFAULT waves
        # (maxheight 508 uu, taller than the beach tier) instead of the tuned set.
        waves.set_editor_property("GerstnerWaveGenerator", gen,
                                  notify_mode=unreal.PropertyAccessChangeNotifyMode.ALWAYS)
        ocean.set_water_waves(waves)

        got = try_get(ocean, ["WaterWaves"], None)
        made = list(try_get(got, ["GerstnerWaves"], []) or []) if got else []
        height = float(try_get(got, ["MaxWaveHeight"], 0.0) or 0.0) if got else 0.0
        if got is None or len(made) == 0 or height <= 0.0:
            err("ocean waves did not take (waves=%s generated=%d maxheight=%.2f)" % (got, len(made), height))
        else:
            log("ocean gerstner waves generated=%d amp %.0f..%.0f len %.0f..%.0f maxheight=%.1f" % (
                len(made), WAVE_AMP_MIN, WAVE_AMP_MAX, WAVE_LEN_MIN, WAVE_LEN_MAX, height))

    # 5.6 moved the underwater material's EditCondition onto bGenerateOverlapEvents; a body with an
    # empty material slot is skipped by UWaterSubsystem::ComputeUnderwaterPostProcess entirely.
    try_set(comp, ["bGenerateOverlapEvents", "generate_overlap_events"], True, "ocean overlap events")
    # ---- water mesh tessellation (round 3 experiment 2) ----------------------------------------
    # Verts on a tile side are (2^TessellationFactor)+1, so 6 -> 65 and 10 -> 1025. The wireframe
    # capture showed the near camera quads stretched into near vertical slivers; with vertex wave
    # displacement on top, each long triangle's displaced near vertex becomes a picket in a fence.
    # More verts per tile is the direct fix for that silhouette.
    for zone in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if not isinstance(zone, unreal.WaterZone):
            continue
        mesh = try_get(zone, ["WaterMesh", "water_mesh"], None)
        if mesh is None:
            err("water zone has no WaterMesh component")
            continue
        try_set(mesh, ["TessellationFactor", "tessellation_factor"], TESS_FACTOR, "water tessellation")
        got = try_get(mesh, ["TessellationFactor", "tessellation_factor"], None)
        if got != TESS_FACTOR:
            err("water tessellation read back %s, wanted %s" % (got, TESS_FACTOR))
        # visibility is the stylised experiment's switch: hide the plugin surface, keep the actor so
        # the underwater post process and the water info texture still exist
        try_set(mesh, ["bVisible", "visible"], not STYLISED, "water mesh visible")
        log("water zone extent=%s tessellation=%s mesh_visible=%s" % (
            try_get(zone, ["ZoneExtent", "zone_extent"], "?"), got, not STYLISED))

    # ---- stylised flat plane (round 3 experiment 3) --------------------------------------------
    apply_stylised_plane()

    have = try_get(comp, ["UnderwaterPostProcessMaterial"], None)
    if have is not None:
        log("ocean underwater post process material already set: %s (left alone)" % have.get_name())
    else:
        mat = unreal.EditorAssetLibrary.load_asset(UNDERWATER_MAT)
        if mat is None:
            err("underwater post process material not found at %s" % UNDERWATER_MAT)
        else:
            try:
                comp.set_underwater_post_process_material(mat)
            except Exception:
                try_set(comp, ["UnderwaterPostProcessMaterial"], mat, "underwater material")
            back = try_get(comp, ["UnderwaterPostProcessMaterial"], None)
            if back is None:
                err("underwater post process material slot still empty")
            else:
                log("ocean underwater post process material = %s" % back.get_name())


def _no_collision_body(component):
    """FBodyInstance with collision off, taken from the component so nothing else in it is lost."""
    body = component.get_editor_property("BodyInstance")
    try:
        body.set_editor_property("collision_enabled", unreal.CollisionEnabled.NO_COLLISION)
    except Exception as e:
        err("body instance collision_enabled not settable: %s" % e)
    try:
        body.set_editor_property("collision_profile_name", "NoCollision")
    except Exception:
        pass
    return body


def apply_stylised_plane():
    """The Synty look: one flat engine plane at Z_WATER+1 wearing MI_PolygonPirates_Water, with the
    Water plugin surface hidden behind it. It has NO vertex displacement, so it cannot produce the
    picket fence silhouette at a grazing angle at all. Swimming is untouched either way: that is the
    sea APhysicsVolume (island_sea_volume.py), not this mesh, and the plane carries no collision."""
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    existing = [a for a in eas.get_all_level_actors()
                if a.get_actor_label().startswith(STYLE_PLANE_LABEL)]
    if not STYLISED:
        for a in existing:
            eas.destroy_actor(a)
        if existing:
            log("stylised plane removed (%d actor(s))" % len(existing))
        return

    for a in existing:
        eas.destroy_actor(a)
    plane_mesh = unreal.EditorAssetLibrary.load_asset(PLANE_MESH)
    water_mi = unreal.EditorAssetLibrary.load_asset(STYLE_WATER_MAT)
    if plane_mesh is None or water_mi is None:
        err("stylised plane assets missing plane=%s mat=%s" % (plane_mesh, water_mi))
        return
    # /Engine/BasicShapes/Plane is 100 uu square, so 512 covers the 51200 uu water zone in one draw.
    actor = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0.0, 0.0, Z_WATER + 1.0))
    if actor is None:
        err("stylised plane spawn failed")
        return
    actor.set_actor_label(STYLE_PLANE_LABEL)
    actor.set_folder_path("Look")
    actor.set_actor_scale3d(unreal.Vector(512.0, 512.0, 1.0))
    smc = actor.static_mesh_component
    smc.set_static_mesh(plane_mesh)
    smc.set_material(0, water_mi)
    # NEVER COLLIDE. set_collision_enabled alone did NOT survive the save: the first stylised build
    # left the pirate STANDING on the plane at z=-31 instead of swimming, and -SwimProbe caught it
    # (WRECKTIDE-SWIM-PROBE float BAD swimming=0 z=-31, band -260..-20). The collision PROFILE is the
    # property that serialises, so set that first and keep the enum call as a belt-and-braces second.
    # The ACTOR level flag is the one that reliably survives a headless save. The component setters
    # below did not: two builds in a row left the pirate standing on the sea at z=-31 instead of
    # swimming. bActorEnableCollision is a plain serialised UPROPERTY, not a runtime setter.
    actor.set_actor_enable_collision(False)
    smc.set_editor_property("BodyInstance", _no_collision_body(smc))
    smc.set_collision_profile_name("NoCollision")
    smc.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    smc.set_generate_overlap_events(False)
    try_set(smc, ["bCanEverAffectNavigation", "can_ever_affect_navigation"], False, "plane navigation")
    smc.set_editor_property("CastShadow", False)
    back = smc.get_material(0)
    if back is None:
        err("stylised plane material did not take")
    # fail closed on collision: a colliding sea plane silently breaks swimming, which is the one
    # thing this lane is not allowed to touch
    prof = smc.get_collision_profile_name()
    enabled = smc.get_collision_enabled()
    actor_coll = actor.get_editor_property("bActorEnableCollision")
    if str(prof) != "NoCollision" or enabled != unreal.CollisionEnabled.NO_COLLISION or actor_coll:
        err("stylised plane still collides profile=%s enabled=%s actor_collision=%s" % (
            prof, enabled, actor_coll))
    log("stylised plane at z=%.0f scale 512 material=%s profile=%s enabled=%s actor_collision=%s" % (
        Z_WATER + 1.0, back.get_name() if back else None, prof, enabled, actor_coll))


def main():
    log("mode=%s sun=%.2f sky=%.2f fog=%.4f ev=%.2f+-%.2f bias=%.2f" % (
        "verify" if VERIFY_ONLY else "apply", SUN_INTENSITY, SKY_INTENSITY, FOG_DENSITY,
        EV_CENTRE, EV_WINDOW * 0.5, EV_BIAS))
    # A -run=pythonscript commandlet starts on an EMPTY editor world, not on the startup map, so
    # every actor lookup comes back None until the level is loaded (island_sea_volume.py:81 does the
    # same thing for the same reason). Without this the script found nothing and failed closed.
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not les.load_level(MAP):
        fatal("load_level failed for %s" % MAP)
    sun, sky, fog, ocean, ppv = report()
    if VERIFY_ONLY:
        log("RESULT=VERIFY (nothing written)")
        return

    if sun is None or sky is None or fog is None or ocean is None:
        fatal("missing actor sun=%s sky=%s fog=%s ocean=%s" % (
            sun is not None, sky is not None, fog is not None, ocean is not None))

    apply_lighting(sun, sky, fog)
    apply_ppv(ppv)
    apply_water(ocean)

    # No recapture_sky() here on purpose: see the note in apply_lighting. Under -nullrhi it writes a
    # black cubemap, and the movable sky light re-captures at runtime anyway.

    if FAILED:
        unreal.log_error("%s %d issue(s): %s" % (M, len(FAILED), FAILED))
        unreal.log_error("%s RESULT=FAIL (nothing saved)" % M)
        raise SystemExit(1)

    if not les.save_current_level():
        fatal("save_current_level failed")
    log("level saved")
    report()
    log("RESULT=OK")


main()
