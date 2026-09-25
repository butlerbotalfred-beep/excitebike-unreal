"""Creates Heatline MX content assets inside the Unreal Editor (run headless):

  UnrealEditor-Cmd HeatlineMX.uproject -run=pythonscript -script="<abs>/Tools/build_content.py" -unattended -nosplash

Materials, tuning Data Assets, NES course Data Assets (from Content/Courses/*.json), Blueprint
presentation classes and the main map. Safe to re-run: existing assets are replaced.
"""
import json
import os
import re
import unreal

ROOT = "/Game/HeatlineMX"
AT = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
LOG = []


def log(msg):
    LOG.append(msg)
    unreal.log("HEATLINE CONTENT: " + msg)


def ensure_dir(path):
    if not EAL.does_directory_exist(path):
        EAL.make_directory(path)


def fresh(name, path):
    full = f"{path}/{name}"
    if EAL.does_asset_exist(full):
        EAL.delete_asset(full)
    return full


def new_material(name, unlit=False, translucent=False, two_sided=False):
    path = ROOT + "/Materials"
    fresh(name, path)
    m = AT.create_asset(name, path, unreal.Material, unreal.MaterialFactoryNew())
    if unlit:
        m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    if translucent:
        m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("two_sided", two_sided)
    m.set_editor_property("used_with_instanced_static_meshes", True)
    return m


def x(m, cls, px, py, **props):
    e = MEL.create_material_expression(m, cls, px, py)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


def link(a, a_out, b, b_in):
    MEL.connect_material_expressions(a, a_out, b, b_in)


def out(e, e_out, prop):
    MEL.connect_material_property(e, e_out, prop)


def finish_material(m):
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    log("material " + m.get_name())


def build_materials():
    P = unreal.MaterialProperty
    # ---- M_MX_Vertex: lit, base = vertex colour, roughness from vertex alpha ----
    m = new_material("M_MX_Vertex")
    vc = x(m, unreal.MaterialExpressionVertexColor, -700, 0)
    out(vc, "", P.MP_BASE_COLOR)
    rough = x(m, unreal.MaterialExpressionLinearInterpolate, -350, 180, const_a=0.12, const_b=0.95)
    link(vc, "A", rough, "Alpha")
    out(rough, "", P.MP_ROUGHNESS)
    spec = x(m, unreal.MaterialExpressionConstant, -350, 320, r=0.35)
    out(spec, "", P.MP_SPECULAR)
    finish_material(m)

    # ---- M_MX_VertexGloss: paint / plastic / metal classes encoded in vertex alpha ----
    m = new_material("M_MX_VertexGloss")
    vc = x(m, unreal.MaterialExpressionVertexColor, -900, 0)
    out(vc, "", P.MP_BASE_COLOR)
    sub = x(m, unreal.MaterialExpressionSubtract, -650, 200, const_b=0.8)
    link(vc, "A", sub, "A")
    mul = x(m, unreal.MaterialExpressionMultiply, -500, 200, const_b=10.0)
    link(sub, "", mul, "A")
    metal = x(m, unreal.MaterialExpressionSaturate, -350, 200)
    link(mul, "", metal, "")
    out(metal, "", P.MP_METALLIC)
    r1 = x(m, unreal.MaterialExpressionLinearInterpolate, -500, 380, const_a=0.14, const_b=0.8)
    link(vc, "A", r1, "Alpha")
    r2 = x(m, unreal.MaterialExpressionLinearInterpolate, -300, 380, const_b=0.28)
    link(r1, "", r2, "A")
    link(metal, "", r2, "Alpha")
    out(r2, "", P.MP_ROUGHNESS)
    finish_material(m)

    # ---- M_MX_Emissive: unlit glow with Color / Intensity parameters ----
    m = new_material("M_MX_Emissive", unlit=True)
    col = x(m, unreal.MaterialExpressionVectorParameter, -500, 0, parameter_name="Color", default_value=unreal.LinearColor(1, 1, 1, 1))
    inten = x(m, unreal.MaterialExpressionScalarParameter, -500, 200, parameter_name="Intensity", default_value=10.0)
    mul = x(m, unreal.MaterialExpressionMultiply, -250, 80)
    link(col, "", mul, "A")
    link(inten, "", mul, "B")
    out(mul, "", P.MP_EMISSIVE_COLOR)
    finish_material(m)

    # ---- M_MX_CoolStrip: pulsing blue chevrons running down-track ----
    m = new_material("M_MX_CoolStrip", unlit=True)
    vc = x(m, unreal.MaterialExpressionVertexColor, -900, 0)
    t = x(m, unreal.MaterialExpressionTime, -1100, 200)
    tm = x(m, unreal.MaterialExpressionMultiply, -900, 200, const_b=7.0)
    link(t, "", tm, "A")
    wp = x(m, unreal.MaterialExpressionWorldPosition, -1100, 350)
    wx = x(m, unreal.MaterialExpressionComponentMask, -900, 350, r=True, g=False, b=False, a=False)
    link(wp, "", wx, "")
    wxm = x(m, unreal.MaterialExpressionMultiply, -750, 350, const_b=0.012)
    link(wx, "", wxm, "A")
    ph = x(m, unreal.MaterialExpressionSubtract, -600, 250)
    link(tm, "", ph, "A")
    link(wxm, "", ph, "B")
    sine = x(m, unreal.MaterialExpressionSine, -450, 250, period=6.2831853)
    link(ph, "", sine, "")
    amp = x(m, unreal.MaterialExpressionMultiply, -320, 250, const_b=1.6)
    link(sine, "", amp, "A")
    base = x(m, unreal.MaterialExpressionAdd, -200, 250, const_b=2.8)
    link(amp, "", base, "A")
    em = x(m, unreal.MaterialExpressionMultiply, -80, 80)
    link(vc, "", em, "A")
    link(base, "", em, "B")
    out(em, "", P.MP_EMISSIVE_COLOR)
    finish_material(m)

    # ---- M_MX_Ghost: translucent fresnel ghost bike ----
    m = new_material("M_MX_Ghost", unlit=True, translucent=True)
    fres = x(m, unreal.MaterialExpressionFresnel, -600, 150, exponent=2.5, base_reflect_fraction=0.1)
    tint = x(m, unreal.MaterialExpressionConstant3Vector, -600, 0, constant=unreal.LinearColor(0.35, 0.8, 1.0, 1.0))
    add = x(m, unreal.MaterialExpressionAdd, -420, 150, const_b=0.35)
    link(fres, "", add, "A")
    em = x(m, unreal.MaterialExpressionMultiply, -250, 60)
    link(tint, "", em, "A")
    link(add, "", em, "B")
    out(em, "", P.MP_EMISSIVE_COLOR)
    op = x(m, unreal.MaterialExpressionMultiply, -250, 220, const_b=0.45)
    link(add, "", op, "A")
    out(op, "", P.MP_OPACITY)
    finish_material(m)

    # ---- M_MX_Particle: per-instance colour/alpha, soft round sprite, billboarded per view ----
    m = new_material("M_MX_Particle", unlit=True, translucent=True, two_sided=True)
    cd = x(m, unreal.MaterialExpressionPerInstanceCustomData3Vector, -800, 0, data_index=0)
    ca = x(m, unreal.MaterialExpressionPerInstanceCustomData, -800, 150, data_index=3)
    uv = x(m, unreal.MaterialExpressionTextureCoordinate, -1100, 300)
    center = x(m, unreal.MaterialExpressionConstant2Vector, -1100, 420, r=0.5, g=0.5)
    dist = x(m, unreal.MaterialExpressionDistance, -900, 330)
    link(uv, "", dist, "A")
    link(center, "", dist, "B")
    dm = x(m, unreal.MaterialExpressionMultiply, -760, 330, const_b=2.0)
    link(dist, "", dm, "A")
    om = x(m, unreal.MaterialExpressionOneMinus, -640, 330)
    link(dm, "", om, "")
    sat = x(m, unreal.MaterialExpressionSaturate, -530, 330)
    link(om, "", sat, "")
    soft = x(m, unreal.MaterialExpressionPower, -420, 330, const_exponent=1.6)
    link(sat, "", soft, "Base")
    opa = x(m, unreal.MaterialExpressionMultiply, -250, 200)
    link(ca, "", opa, "A")
    link(soft, "", opa, "B")
    out(opa, "", P.MP_OPACITY)
    em = x(m, unreal.MaterialExpressionMultiply, -250, 0, const_b=1.1)
    link(cd, "", em, "A")
    out(em, "", P.MP_EMISSIVE_COLOR)
    # Billboard: rebuild the quad's offset from the instance centre along the camera right/up.
    wp = x(m, unreal.MaterialExpressionWorldPosition, -1300, 600)
    op_ws = x(m, unreal.MaterialExpressionObjectPositionWS, -1300, 720)
    off = x(m, unreal.MaterialExpressionSubtract, -1100, 650)
    link(wp, "", off, "A")
    link(op_ws, "", off, "B")
    offx = x(m, unreal.MaterialExpressionComponentMask, -950, 600, r=True, g=False, b=False, a=False)
    link(off, "", offx, "")
    offz = x(m, unreal.MaterialExpressionComponentMask, -950, 720, r=False, g=False, b=True, a=False)
    link(off, "", offz, "")
    right_c = x(m, unreal.MaterialExpressionConstant3Vector, -1100, 850, constant=unreal.LinearColor(1, 0, 0, 0))
    up_c = x(m, unreal.MaterialExpressionConstant3Vector, -1100, 950, constant=unreal.LinearColor(0, 1, 0, 0))
    right = x(m, unreal.MaterialExpressionTransform, -900, 850,
              transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_VIEW,
              transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    link(right_c, "", right, "")
    up = x(m, unreal.MaterialExpressionTransform, -900, 950,
           transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_VIEW,
           transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    link(up_c, "", up, "")
    rx = x(m, unreal.MaterialExpressionMultiply, -700, 850)
    link(right, "", rx, "A")
    link(offx, "", rx, "B")
    uz = x(m, unreal.MaterialExpressionMultiply, -700, 950)
    link(up, "", uz, "A")
    link(offz, "", uz, "B")
    new_off = x(m, unreal.MaterialExpressionAdd, -550, 900)
    link(rx, "", new_off, "A")
    link(uz, "", new_off, "B")
    wpo = x(m, unreal.MaterialExpressionSubtract, -400, 800)
    link(new_off, "", wpo, "A")
    link(off, "", wpo, "B")
    out(wpo, "", P.MP_WORLD_POSITION_OFFSET)
    finish_material(m)

    # ---- M_MX_Crowd: per-instance shirt colour + cheering bob ----
    m = new_material("M_MX_Crowd")
    vc = x(m, unreal.MaterialExpressionVertexColor, -1000, 0)
    cd = x(m, unreal.MaterialExpressionPerInstanceCustomData3Vector, -1000, 180, data_index=0)
    # Shirt mask: neutral grey vertices (R == B and G > 0.5).
    rb = x(m, unreal.MaterialExpressionSubtract, -800, 300)
    link(vc, "R", rb, "A")
    link(vc, "B", rb, "B")
    ab = x(m, unreal.MaterialExpressionAbs, -680, 300)
    link(rb, "", ab, "")
    abm = x(m, unreal.MaterialExpressionMultiply, -580, 300, const_b=8.0)
    link(ab, "", abm, "A")
    om = x(m, unreal.MaterialExpressionOneMinus, -470, 300)
    link(abm, "", om, "")
    s1 = x(m, unreal.MaterialExpressionSaturate, -370, 300)
    link(om, "", s1, "")
    g5 = x(m, unreal.MaterialExpressionSubtract, -800, 420, const_b=0.5)
    link(vc, "G", g5, "A")
    g10 = x(m, unreal.MaterialExpressionMultiply, -680, 420, const_b=10.0)
    link(g5, "", g10, "A")
    s2 = x(m, unreal.MaterialExpressionSaturate, -560, 420)
    link(g10, "", s2, "")
    mask = x(m, unreal.MaterialExpressionMultiply, -300, 360)
    link(s1, "", mask, "A")
    link(s2, "", mask, "B")
    col = x(m, unreal.MaterialExpressionLinearInterpolate, -150, 100)
    link(vc, "", col, "A")
    link(cd, "", col, "B")
    link(mask, "", col, "Alpha")
    out(col, "", P.MP_BASE_COLOR)
    r = x(m, unreal.MaterialExpressionConstant, -150, 260, r=0.8)
    out(r, "", P.MP_ROUGHNESS)
    phase = x(m, unreal.MaterialExpressionPerInstanceCustomData, -1000, 600, data_index=3)
    t = x(m, unreal.MaterialExpressionTime, -1000, 700)
    sp = x(m, unreal.MaterialExpressionMultiply, -850, 650, const_b=3.0)
    link(phase, "", sp, "A")
    sp2 = x(m, unreal.MaterialExpressionAdd, -720, 650, const_b=4.0)
    link(sp, "", sp2, "A")
    tt = x(m, unreal.MaterialExpressionMultiply, -600, 700)
    link(t, "", tt, "A")
    link(sp2, "", tt, "B")
    ph6 = x(m, unreal.MaterialExpressionMultiply, -720, 780, const_b=6.28)
    link(phase, "", ph6, "A")
    arg = x(m, unreal.MaterialExpressionAdd, -480, 740)
    link(tt, "", arg, "A")
    link(ph6, "", arg, "B")
    sine = x(m, unreal.MaterialExpressionSine, -360, 740, period=6.2831853)
    link(arg, "", sine, "")
    mx = x(m, unreal.MaterialExpressionMax, -250, 740, const_b=0.0)
    link(sine, "", mx, "A")
    amp = x(m, unreal.MaterialExpressionMultiply, -150, 740, const_b=9.0)
    link(mx, "", amp, "A")
    upv = x(m, unreal.MaterialExpressionConstant3Vector, -250, 860, constant=unreal.LinearColor(0, 0, 1, 0))
    wpo = x(m, unreal.MaterialExpressionMultiply, -50, 800)
    link(upv, "", wpo, "A")
    link(amp, "", wpo, "B")
    out(wpo, "", P.MP_WORLD_POSITION_OFFSET)
    finish_material(m)

    # ---- M_MX_Visor: dark glossy goggles ----
    m = new_material("M_MX_Visor")
    c = x(m, unreal.MaterialExpressionConstant3Vector, -400, 0, constant=unreal.LinearColor(0.01, 0.012, 0.02, 1))
    out(c, "", P.MP_BASE_COLOR)
    rr = x(m, unreal.MaterialExpressionConstant, -400, 150, r=0.05)
    out(rr, "", P.MP_ROUGHNESS)
    sp = x(m, unreal.MaterialExpressionConstant, -400, 250, r=1.0)
    out(sp, "", P.MP_SPECULAR)
    finish_material(m)


def new_data_asset(name, cls, path):
    fresh(name, path)
    f = unreal.DataAssetFactory()
    f.set_editor_property("data_asset_class", cls)
    a = AT.create_asset(name, path, cls, f)
    return a


def enum_name(camel):
    return re.sub(r"(?<!^)(?=[A-Z])", "_", camel).upper()


def build_data():
    path = ROOT + "/Data"
    for name, cls in [("DA_BikeTuning", unreal.MXBikeTuning), ("DA_CameraTuning", unreal.MXCameraTuning),
                      ("DA_AITuning", unreal.MXAITuning), ("DA_TrackStyle", unreal.MXTrackStyle)]:
        a = new_data_asset(name, cls, path)
        EAL.save_loaded_asset(a)
        log("data asset " + name)


def build_courses():
    path = ROOT + "/Courses"
    content = unreal.Paths.project_content_dir()
    for t in range(1, 6):
        src = os.path.join(content, "Courses", f"nes_t{t}.json")
        with open(src) as f:
            data = json.load(f)
        a = new_data_asset(f"DA_Course_NES_T{t}", unreal.MXCourseAsset, path)
        d = unreal.MXTrackDefinition()
        d.set_editor_property("id", data["id"])
        d.set_editor_property("name", f"Course {t}")
        d.set_editor_property("author", "Nintendo (1984) layout, translated")
        d.set_editor_property("built_in", True)
        d.set_editor_property("laps", data["laps"])
        d.set_editor_property("lap_length_m", data["lapLengthM"])
        d.set_editor_property("nes_track", t)
        d.set_editor_property("source_note", f"Excitebike (NES) track {t}: {data['source']['method']}")
        segs = []
        for s in data["segments"]:
            seg = unreal.MXSegment()
            seg.set_editor_property("id", s["id"])
            seg.set_editor_property("type", getattr(unreal.MXObstacleType, enum_name(s["type"])))
            seg.set_editor_property("lane_mask", sum(1 << (l - 1) for l in s["lanes"]))
            seg.set_editor_property("start_m", s["startM"])
            seg.set_editor_property("length_m", s["lengthM"])
            seg.set_editor_property("runs", s.get("runs", []))
            seg.set_editor_property("main_only", s["variant"] == "main")
            n = s["nes"]
            seg.set_editor_property("source_ref", f"NES T{t} col {n['col']} {n['piece']} ({n['letter']})")
            segs.append(seg)
        d.set_editor_property("segments", segs)
        a.set_editor_property("definition", d)
        a.set_editor_property("sort_order", t)
        EAL.save_loaded_asset(a)
        log(f"course asset DA_Course_NES_T{t} ({len(segs)} segments)")


def new_blueprint(name, parent, path):
    fresh(name, path)
    f = unreal.BlueprintFactory()
    f.set_editor_property("parent_class", parent)
    bp = AT.create_asset(name, path, None, f)
    return bp


def build_blueprints_and_map():
    path = ROOT + "/Blueprints"
    bike = new_blueprint("BP_MXBike", unreal.MXBike, path)
    EAL.save_loaded_asset(bike)
    gm = new_blueprint("BP_MXGameMode", unreal.MXGameMode, path)
    cdo = unreal.get_default_object(gm.generated_class())
    cdo.set_editor_property("bike_class", bike.generated_class())
    EAL.save_loaded_asset(gm)
    log("blueprints BP_MXBike, BP_MXGameMode")

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    map_path = ROOT + "/Maps/L_Main"
    if EAL.does_asset_exist(map_path):
        EAL.delete_asset(map_path)
    les.new_level(map_path)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    ws = world.get_world_settings()
    ws.set_editor_property("default_game_mode", gm.generated_class())
    les.save_current_level()
    log("map " + map_path)


def main():
    for d in ["", "/Materials", "/Data", "/Courses", "/Blueprints", "/Maps"]:
        ensure_dir(ROOT + d)
    for step in (build_materials, build_data, build_courses, build_blueprints_and_map):
        try:
            step()
        except Exception as e:  # keep going; report at the end
            log(f"ERROR in {step.__name__}: {e}")
    report = os.path.join(unreal.Paths.project_saved_dir(), "content_build.log")
    with open(report, "w") as f:
        f.write("\n".join(LOG) + "\n")
    unreal.log("HEATLINE CONTENT DONE: " + report)


main()
