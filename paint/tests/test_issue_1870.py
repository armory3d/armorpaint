"""Regression checks for rejecting faces that span UDIM tiles."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def read(*parts: str) -> str:
	return ROOT.joinpath(*parts).read_text(encoding="utf-8")


def test_obj_parser_marks_spanning_udim_faces_invalid() -> None:
	parser = read("base", "sources", "iron_obj.c")
	header = read("base", "sources", "iron_obj.h")
	assert "udim_split_invalid" in header
	assert "part->udim_split_invalid = true;" in parser


def test_obj_import_reports_the_invalid_udim_split() -> None:
	importer = read("paint", "sources", "io", "import_obj.c")
	assert "part->udim_split_invalid" in importer
	assert "strings_udim_face_spans_tiles()" in importer


if __name__ == "__main__":
	test_obj_parser_marks_spanning_udim_faces_invalid()
	test_obj_import_reports_the_invalid_udim_split()
	print("2 regression checks passed")
