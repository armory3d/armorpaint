#pragma once

#include "iron_armpack.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct raw_mesh {
	struct i16_array *posa;
	struct i16_array *nora;
	struct i16_array *texa;
	struct i16_array *texa1;
	struct i16_array *cola;
	struct u32_array *inda;
	int               vertex_count;
	int               index_count;
	float             scale_pos;
	float             scale_tex;
	char             *name;
	bool              has_next; // File contains multiple objects
	uint64_t          pos;
	struct any_array *udims;   // u32_array_t[] - Indices split per udim tile
	int               udims_u; // Number of horizontal udim tiles
	int               udims_v;
	void             *vertex_arrays; // vertex_array_t[]
	struct u32_array *index_array;
	struct u8_array  *blob; // Source file bytes
} raw_mesh_t;

raw_mesh_t *obj_parse(buffer_t *file_bytes, char split_code, uint64_t start_pos, bool udim);
void        obj_destroy(raw_mesh_t *part);
