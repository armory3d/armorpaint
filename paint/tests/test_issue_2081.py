"""Regression checks for UDIM-aware face-fill triangle maps."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(*parts: str) -> str:
	return ROOT.joinpath(*parts).read_text(encoding="utf-8")


def test_filtered_udim_triangle_map_uses_the_selected_paint_object() -> None:
	uv = read("paint", "sources", "util", "util_uv.c")
	assert "context_layer_filter_used() || context_object_mask_used()" in uv
	assert "g_context->paint_object->data" in uv


if __name__ == "__main__":
	test_filtered_udim_triangle_map_uses_the_selected_paint_object()
	print("1 regression check passed")
