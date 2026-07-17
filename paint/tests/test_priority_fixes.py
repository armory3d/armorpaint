"""Source-level regression checks for the priority issue fixes.

The checkout does not contain a native unit-test runner for the paint target,
so these checks protect the exact defensive boundaries while the generated
native project is used for compile-time verification.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(*parts: str) -> str:
    return (ROOT.joinpath(*parts)).read_text(encoding="utf-8")


def test_config_scale_is_validated_at_boundaries() -> None:
    config = read("paint", "sources", "config.c")
    preferences = read("paint", "sources", "ui", "box_preferences.c")
    assert "f32 config_validate_window_scale" in config
    assert config.count("config_validate_window_scale") >= 4
    assert "g_config->window_scale = config_validate_window_scale(g_config->window_scale);" in preferences


def test_opacity_is_clamped_after_pressure_multipliers() -> None:
    uniforms = read("paint", "sources", "uniforms.c")
    paint = read("paint", "sources", "render", "render_path_paint.c")
    assert "return fminf(fmaxf(val, 0.0), 1.0);" in uniforms
    assert "fminf(fmaxf(opacity, 0.0), 1.0)" in paint


def test_blender_import_stops_when_temp_obj_is_missing() -> None:
    source = read("paint", "sources", "io", "import_blend_mesh.c")
    assert "iron_file_exists(save)" in source
    assert "import_obj_run(save, replace_existing);" in source


def test_pack_and_preset_exports_validate_missing_data() -> None:
    arm = read("paint", "sources", "io", "export_arm.c")
    preset = read("paint", "sources", "ui", "box_export.c")
    texture = read("paint", "sources", "io", "export_texture.c")
    assert "bool export_arm_pack_assets" in arm
    assert "if (image == NULL)" in arm
    assert "if (!export_arm_pack_assets" in arm
    assert "if (blob == NULL)" in preset
    assert "layers->length == 0" in texture
    assert "No valid export preset available" in texture


def test_layer_creation_and_2d_view_are_null_safe() -> None:
    layers = read("paint", "sources", "util", "util_layer.c")
    slot_layer = read("paint", "sources", "slot_layer.c")
    view = read("paint", "sources", "ui", "ui_view2d.c")
    assert ">= layers_max_layers" in layers
    assert layers.count("if (l == NULL)") >= 3
    assert "raw == NULL || g_context->material == NULL" in slot_layer
    assert "if (tex != NULL)" in view


def test_history_mask_restore_and_redo_scan_are_bounded() -> None:
    history = read("paint", "sources", "history.c")
    assert "history_undo_delete_layer_masks" in history
    assert "active + n < history_steps->length" in history


if __name__ == "__main__":
    tests = [value for name, value in globals().items() if name.startswith("test_") and callable(value)]
    for test in tests:
        test()
    print(f"{len(tests)} regression checks passed")
