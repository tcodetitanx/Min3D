"""
Min3D content bootstrap: two unlit translucent parent materials + the empty Main map.
Everything else is generated at runtime in C++.

Run headless:
  UnrealEditor-Cmd.exe <project> -run=pythonscript -script="<this file>" -unattended -nosplash
"""
import unreal

GEN_PATH = "/Game/Gen"
MAPS_PATH = "/Game/Maps"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
mat_lib = unreal.MaterialEditingLibrary
asset_lib = unreal.EditorAssetLibrary


def ensure_dir(path):
    if not asset_lib.does_directory_exist(path):
        asset_lib.make_directory(path)


def make_sprite_material():
    """Tex * Tint -> emissive; Tex.a * Tint.a * Opacity -> opacity. Unlit translucent."""
    name = "M_MinSprite"
    full = f"{GEN_PATH}/{name}"
    if asset_lib.does_asset_exist(full):
        asset_lib.delete_asset(full)
    mat = asset_tools.create_asset(name, GEN_PATH, unreal.Material, unreal.MaterialFactoryNew())

    tex = mat_lib.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -700, -150)
    tex.set_editor_property("parameter_name", "Tex")
    white = unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture")
    if white:
        tex.set_editor_property("texture", white)

    tint = mat_lib.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -700, 120)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))

    opacity = mat_lib.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -700, 330)
    opacity.set_editor_property("parameter_name", "Opacity")
    opacity.set_editor_property("default_value", 1.0)

    mul_rgb = mat_lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -380, -60)
    mat_lib.connect_material_expressions(tex, "RGB", mul_rgb, "A")
    mat_lib.connect_material_expressions(tint, "", mul_rgb, "B")

    mul_a1 = mat_lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -380, 200)
    mat_lib.connect_material_expressions(tex, "A", mul_a1, "A")
    mat_lib.connect_material_expressions(tint, "A", mul_a1, "B")
    mul_a2 = mat_lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -200, 260)
    mat_lib.connect_material_expressions(mul_a1, "", mul_a2, "A")
    mat_lib.connect_material_expressions(opacity, "", mul_a2, "B")

    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("two_sided", True)
    mat_lib.connect_material_property(mul_rgb, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mat_lib.connect_material_property(mul_a2, "", unreal.MaterialProperty.MP_OPACITY)
    mat_lib.recompile_material(mat)
    asset_lib.save_loaded_asset(mat)
    unreal.log(f"[bootstrap] created {full}")


def make_vertex_material():
    """VertexColor * Tint -> emissive; VertexColor.a * Opacity -> opacity. Unlit translucent."""
    name = "M_MinVertex"
    full = f"{GEN_PATH}/{name}"
    if asset_lib.does_asset_exist(full):
        asset_lib.delete_asset(full)
    mat = asset_tools.create_asset(name, GEN_PATH, unreal.Material, unreal.MaterialFactoryNew())

    vc = mat_lib.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -700, -100)

    tint = mat_lib.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -700, 150)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))

    opacity = mat_lib.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -700, 350)
    opacity.set_editor_property("parameter_name", "Opacity")
    opacity.set_editor_property("default_value", 1.0)

    mul_rgb = mat_lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -380, -40)
    mat_lib.connect_material_expressions(vc, "", mul_rgb, "A")
    mat_lib.connect_material_expressions(tint, "", mul_rgb, "B")

    mul_a = mat_lib.create_material_expression(mat, unreal.MaterialExpressionMultiply, -380, 250)
    mat_lib.connect_material_expressions(vc, "A", mul_a, "A")
    mat_lib.connect_material_expressions(opacity, "", mul_a, "B")

    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("two_sided", True)
    mat_lib.connect_material_property(mul_rgb, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mat_lib.connect_material_property(mul_a, "", unreal.MaterialProperty.MP_OPACITY)
    mat_lib.recompile_material(mat)
    asset_lib.save_loaded_asset(mat)
    unreal.log(f"[bootstrap] created {full}")


def make_main_map():
    full = f"{MAPS_PATH}/Main"
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if asset_lib.does_asset_exist(full):
        unreal.log(f"[bootstrap] {full} already exists")
        return
    les.new_level(full)
    les.save_current_level()
    unreal.log(f"[bootstrap] created {full}")


ensure_dir(GEN_PATH)
ensure_dir(MAPS_PATH)
make_sprite_material()
make_vertex_material()
make_main_map()
asset_lib.save_directory("/Game", recursive=True)
unreal.log("[bootstrap] done")
