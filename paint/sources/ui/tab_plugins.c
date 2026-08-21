
#include "../global.h"

void tab_plugins_draw(ui_handle_t *htab) {
	if (ui_tab(htab, tr("Plugins"), false, -1, false)) {

		ui_begin_sticky();

		f32_array_t *row = f32_array_create_from_raw_tmp(
		    (f32[]){
		        -100,
		    },
		    1);
		ui_row(row);

		if (ui_icon_button(tr("Preferences"), ICON_COG, UI_ALIGN_CENTER)) {
			box_preferences_htab->i = PREFERENCES_TAB_PLUGINS;
			box_preferences_show();
		}
		ui_end_sticky();

		// Draw plugins
		string_array_t *keys = map_keys(g_plugins);
		for (i32 i = 0; i < keys->length; ++i) {
			plugin_t *p = any_map_get(g_plugins, keys->buffer[i]);
			if (p->on_ui != NULL) {
				minic_ctx_call_fn(p->ctx, p->on_ui, NULL, 0);
			}
		}
		array_free(keys);
		free(keys);
	}
}

#ifdef WITH_PLUGINS

void proc_uv_unwrap(void *mesh);

void plugin_uv_unwrap_button() {
	util_mesh_merge(g_project->_->paint_objects);
	if (g_context->merged_object == NULL) {
		return;
	}

	mesh_data_t *mmd         = g_context->merged_object->data;
	i16_array_t *merged_posa = mmd->vertex_arrays->buffer[0]->values;
	i16_array_t *merged_nora = mmd->vertex_arrays->buffer[1]->values;
	u32_array_t *merged_inda = mmd->index_array;

	i16_array_t *posa = malloc(sizeof(i16_array_t));
	posa->length = posa->capacity = merged_posa->length;
	posa->buffer                  = malloc(merged_posa->length * sizeof(i16));
	memcpy(posa->buffer, merged_posa->buffer, merged_posa->length * sizeof(i16));

	i16_array_t *nora = malloc(sizeof(i16_array_t));
	nora->length = nora->capacity = merged_nora->length;
	nora->buffer                  = malloc(merged_nora->length * sizeof(i16));
	memcpy(nora->buffer, merged_nora->buffer, merged_nora->length * sizeof(i16));

	u32_array_t *inda = malloc(sizeof(u32_array_t));
	inda->length = inda->capacity = merged_inda->length;
	inda->buffer                  = malloc(merged_inda->length * sizeof(u32));
	memcpy(inda->buffer, merged_inda->buffer, merged_inda->length * sizeof(u32));

	raw_mesh_t *mesh = ALLOC_INIT(raw_mesh_t, {.posa = posa, .nora = nora, .texa = NULL, .inda = inda});
	proc_uv_unwrap(mesh);

	i32 ioff      = 0;
	f32 max_scale = mmd->scale_pos;
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_data_t *md      = g_project->_->paint_objects->buffer[i]->data;
		i32          ilen    = md->index_array->length;
		f32          rescale = max_scale / md->scale_pos;

		i16_array_t *new_posa = i16_array_create(ilen * 4);
		i16_array_t *new_nora = i16_array_create(ilen * 2);
		i16_array_t *new_texa = i16_array_create(ilen * 2);
		u32_array_t *new_inda = u32_array_create(ilen);

		for (i32 j = 0; j < ilen; ++j) {
			i32 src                     = (ioff + j) * 4;
			new_posa->buffer[j * 4]     = (i16)math_floor(mesh->posa->buffer[src] * rescale);
			new_posa->buffer[j * 4 + 1] = (i16)math_floor(mesh->posa->buffer[src + 1] * rescale);
			new_posa->buffer[j * 4 + 2] = (i16)math_floor(mesh->posa->buffer[src + 2] * rescale);
			new_posa->buffer[j * 4 + 3] = mesh->posa->buffer[src + 3];
		}
		for (i32 j = 0; j < ilen * 2; ++j) {
			new_nora->buffer[j] = mesh->nora->buffer[ioff * 2 + j];
		}
		for (i32 j = 0; j < ilen * 2; ++j) {
			new_texa->buffer[j] = mesh->texa->buffer[ioff * 2 + j];
		}
		for (i32 j = 0; j < ilen; ++j) {
			new_inda->buffer[j] = (u32)j;
		}

		md->vertex_arrays->buffer[0]->values = new_posa;
		md->vertex_arrays->buffer[1]->values = new_nora;
		md->vertex_arrays->buffer[2]->values = new_texa;
		md->index_array                      = new_inda;

		mesh_data_build(md);
		ioff += ilen;
	}

	free(mesh->posa->buffer);
	free(mesh->posa);
	free(mesh->nora->buffer);
	free(mesh->nora);
	free(mesh->texa->buffer);
	free(mesh->texa);
	free(mesh->inda->buffer);
	free(mesh->inda);

	util_mesh_merge(NULL);
	util_uv_uvmap_cached = false;
}

void plugin_uv_unwrap_per_object_button(mesh_object_t *mo) {
	mesh_data_t *md      = mo->data;
	i16_array_t *md_posa = md->vertex_arrays->buffer[0]->values;
	i16_array_t *md_nora = md->vertex_arrays->buffer[1]->values;
	u32_array_t *md_inda = md->index_array;

	i16_array_t *posa = malloc(sizeof(i16_array_t));
	posa->length = posa->capacity = md_posa->length;
	posa->buffer                  = malloc(md_posa->length * sizeof(i16));
	memcpy(posa->buffer, md_posa->buffer, md_posa->length * sizeof(i16));

	i16_array_t *nora = malloc(sizeof(i16_array_t));
	nora->length = nora->capacity = md_nora->length;
	nora->buffer                  = malloc(md_nora->length * sizeof(i16));
	memcpy(nora->buffer, md_nora->buffer, md_nora->length * sizeof(i16));

	u32_array_t *inda = malloc(sizeof(u32_array_t));
	inda->length = inda->capacity = md_inda->length;
	inda->buffer                  = malloc(md_inda->length * sizeof(u32));
	memcpy(inda->buffer, md_inda->buffer, md_inda->length * sizeof(u32));

	raw_mesh_t *mesh = ALLOC_INIT(raw_mesh_t, {.posa = posa, .nora = nora, .texa = NULL, .inda = inda});

	proc_uv_unwrap(mesh);
	md->vertex_arrays->buffer[0]->values = mesh->posa;
	md->vertex_arrays->buffer[1]->values = mesh->nora;
	md->vertex_arrays->buffer[2]->values = mesh->texa;
	md->index_array                      = mesh->inda;
	mesh_data_build(md);
	util_uv_uvmap_cached = false;
}

#endif
