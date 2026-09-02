
#include "../global.h"

int tab_stages_selected = 0;

stage_t *tab_stages_create_stage(char *name) {
	stage_t *s = ALLOC_INIT(stage_t, {0});
	s->name    = name;
	s->objects = string_array_create(0);
	s->layers  = string_array_create(0);
	s->hidden  = string_array_create(0);
	return s;
}

bool tab_stages_is_hidden(stage_t *stage, char *name) {
	return stage->hidden != NULL && string_array_index_of(stage->hidden, name) >= 0;
}

void tab_stages_set_hidden(stage_t *stage, char *name, bool hidden) {
	if (stage->hidden == NULL) {
		stage->hidden = string_array_create(0);
	}
	i32 idx = string_array_index_of(stage->hidden, name);
	if (hidden && idx < 0) {
		string_array_push(stage->hidden, name);
	}
	else if (!hidden && idx >= 0) {
		array_splice(stage->hidden, idx, 1);
	}
}

stage_t *tab_stages_get_stage() {
	if (g_project->stages == NULL || g_project->stages->length == 0) {
		return NULL;
	}
	if (tab_stages_selected < 0 || tab_stages_selected >= g_project->stages->length) {
		return NULL;
	}
	return g_project->stages->buffer[tab_stages_selected];
}

void tab_stages_apply(stage_t *stage) {
	mesh_object_t_array_t *visibles = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *p = g_project->_->paint_objects->buffer[i];
		p->base->visible = string_array_index_of(stage->objects, p->base->name) >= 0 && !tab_stages_is_hidden(stage, p->base->name);
		if (p->base->visible) {
			any_array_push(visibles, p);
		}
	}
	util_mesh_merge(visibles);
	g_context->ddirty = 2;
}

void tab_stages_apply_visible(mesh_object_t *o) {
	stage_t *stage = tab_stages_get_stage();
	if (stage != NULL) {
		tab_stages_set_hidden(stage, o->base->name, !o->base->visible);
	}
	util_mesh_visibility_changed();
}

void tab_stages_add_object(char *name) {
	stage_t *stage = tab_stages_get_stage();
	if (stage != NULL && string_array_index_of(stage->objects, name) < 0) {
		string_array_push(stage->objects, name);
	}
}

void tab_stages_add_layer(char *name) {
	stage_t *stage = tab_stages_get_stage();
	if (stage != NULL && string_array_index_of(stage->layers, name) < 0) {
		string_array_push(stage->layers, name);
	}
}

void tab_stages_rename_object(char *old_name, char *new_name) {
	if (g_project->stages == NULL || string_equals(old_name, new_name)) {
		return;
	}
	for (i32 i = 0; i < g_project->stages->length; ++i) {
		stage_t *s = g_project->stages->buffer[i];
		for (i32 j = 0; j < s->objects->length; ++j) {
			if (string_equals(s->objects->buffer[j], old_name)) {
				s->objects->buffer[j] = string_copy(new_name);
			}
		}
		if (s->hidden == NULL) {
			continue;
		}
		for (i32 j = 0; j < s->hidden->length; ++j) {
			if (string_equals(s->hidden->buffer[j], old_name)) {
				s->hidden->buffer[j] = string_copy(new_name);
			}
		}
	}
}

void tab_stages_rename_layer(char *old_name, char *new_name) {
	if (g_project->stages == NULL || string_equals(old_name, new_name)) {
		return;
	}
	for (i32 i = 0; i < g_project->stages->length; ++i) {
		stage_t *s = g_project->stages->buffer[i];
		for (i32 j = 0; j < s->layers->length; ++j) {
			if (string_equals(s->layers->buffer[j], old_name)) {
				s->layers->buffer[j] = string_copy(new_name);
			}
		}
	}
}

void tab_stages_prune() {
	if (g_project->stages == NULL) {
		return;
	}
	for (i32 i = 0; i < g_project->stages->length; ++i) {
		stage_t *s = g_project->stages->buffer[i];

		for (i32 j = s->objects->length - 1; j >= 0; --j) {
			bool found = false;
			for (i32 k = 0; k < g_project->_->paint_objects->length; ++k) {
				if (string_equals(g_project->_->paint_objects->buffer[k]->base->name, s->objects->buffer[j])) {
					found = true;
					break;
				}
			}
			if (!found) {
				array_splice(s->objects, j, 1);
			}
		}

		for (i32 j = s->hidden != NULL ? s->hidden->length - 1 : -1; j >= 0; --j) {
			if (string_array_index_of(s->objects, s->hidden->buffer[j]) < 0) {
				array_splice(s->hidden, j, 1);
			}
		}

		for (i32 j = s->layers->length - 1; j >= 0; --j) {
			bool found = false;
			for (i32 k = 0; k < g_project->_->layers->length; ++k) {
				if (string_equals(g_project->_->layers->buffer[k]->name, s->layers->buffer[j])) {
					found = true;
					break;
				}
			}
			if (!found) {
				array_splice(s->layers, j, 1);
			}
		}
	}
}
