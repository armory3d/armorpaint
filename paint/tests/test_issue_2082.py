"""Regression checks for locale-safe UI scale input."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(*parts: str) -> str:
	return ROOT.joinpath(*parts).read_text(encoding="utf-8")


def test_slider_normalizes_a_decimal_comma_before_evaluation() -> None:
	ui = read("base", "sources", "iron_ui.c")
	assert "ui_normalize_numeric_text" in ui
	assert "ui_normalize_numeric_text(handle->text)" in ui


def test_window_scale_is_validated_when_loaded_applied_and_saved() -> None:
	config = read("paint", "sources", "config.c")
	preferences = read("paint", "sources", "ui", "box_preferences.c")
	assert "f32 config_validate_window_scale" in config
	assert config.count("config_validate_window_scale") >= 4
	assert "g_config->window_scale = config_validate_window_scale(g_config->window_scale);" in preferences


if __name__ == "__main__":
	test_slider_normalizes_a_decimal_comma_before_evaluation()
	test_window_scale_is_validated_when_loaded_applied_and_saved()
	print("2 regression checks passed")
