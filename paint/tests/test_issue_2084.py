"""Regression checks for bounded brush opacity."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(*parts: str) -> str:
	return ROOT.joinpath(*parts).read_text(encoding="utf-8")


def test_brush_opacity_is_clamped_after_all_multipliers() -> None:
	uniforms = read("paint", "sources", "uniforms.c")
	assert "return fminf(fmaxf(val, 0.0), 1.0);" in uniforms


def test_decal_cursor_uses_the_same_opacity_range() -> None:
	render = read("paint", "sources", "render", "render_path_paint.c")
	assert "opacity     = fminf(fmaxf(opacity, 0.0), 1.0);" in render


if __name__ == "__main__":
	test_brush_opacity_is_clamped_after_all_multipliers()
	test_decal_cursor_uses_the_same_opacity_range()
	print("2 regression checks passed")
