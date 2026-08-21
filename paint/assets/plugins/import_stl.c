#include "global.h"

void *plugin;

void *import_stl(char *path) {
	buffer_t *b = data_get_blob(path);
	int pos = 80; // skip header
	int faces = buffer_get_i32(b, pos);
	pos += 4;

	f32_array_t *pos_temp = f32_array_create(faces * 9);
	f32_array_t *nor_temp = f32_array_create(faces * 3);

	for (int i = 0; i < faces; ++i) {
		int i3 = i * 3;
		nor_temp->buffer[i3    ] = buffer_get_f32(b, pos); pos += 4;
		nor_temp->buffer[i3 + 1] = buffer_get_f32(b, pos); pos += 4;
		nor_temp->buffer[i3 + 2] = buffer_get_f32(b, pos); pos += 4;
		int i9 = i * 9;
		pos_temp->buffer[i9    ] = buffer_get_f32(b, pos); pos += 4;
		pos_temp->buffer[i9 + 1] = buffer_get_f32(b, pos); pos += 4;
		pos_temp->buffer[i9 + 2] = buffer_get_f32(b, pos); pos += 4;
		pos_temp->buffer[i9 + 3] = buffer_get_f32(b, pos); pos += 4;
		pos_temp->buffer[i9 + 4] = buffer_get_f32(b, pos); pos += 4;
		pos_temp->buffer[i9 + 5] = buffer_get_f32(b, pos); pos += 4;
		pos_temp->buffer[i9 + 6] = buffer_get_f32(b, pos); pos += 4;
		pos_temp->buffer[i9 + 7] = buffer_get_f32(b, pos); pos += 4;
		pos_temp->buffer[i9 + 8] = buffer_get_f32(b, pos); pos += 4;
		pos += 2; // skip attribute
	}

	float scale_pos = 0.0;
	for (int i = 0; i < pos_temp->length; ++i) {
		float f = pos_temp->buffer[i];
		if (f < 0.0) f = -f;
		if (scale_pos < f) scale_pos = f;
	}
	float inv = 32767.0 * (1.0 / scale_pos);

	int verts = faces * 3;
	i16_array_t *posa = i16_array_create(verts * 4);
	i16_array_t *nora = i16_array_create(verts * 2);
	u32_array_t *inda = u32_array_create(verts);

	for (int i = 0; i < verts; ++i) {
		int f = (i / 3) * 3; // face normal base index

		// posa->buffer[i * 4    ] = (int)( pos_temp->buffer[i * 3    ] * inv);
		// posa->buffer[i * 4 + 1] = (int)(-pos_temp->buffer[i * 3 + 2] * inv);
		// posa->buffer[i * 4 + 2] = (int)( pos_temp->buffer[i * 3 + 1] * inv);
		// nora->buffer[i * 2    ] = (int)( nor_temp->buffer[f    ] * 32767.0);
		// nora->buffer[i * 2 + 1] = (int)(-nor_temp->buffer[f + 2] * 32767.0);
		// posa->buffer[i * 4 + 3] = (int)( nor_temp->buffer[f + 1] * 32767.0);
        buffer_set_i16(posa, (i * 4) * 2, pos_temp->buffer[i * 3    ] * inv);
        buffer_set_i16(posa, (i * 4 + 1) * 2, -pos_temp->buffer[i * 3 + 2] * inv);
        buffer_set_i16(posa, (i * 4 + 2) * 2, pos_temp->buffer[i * 3 + 1] * inv);
        buffer_set_i16(nora, (i * 2) * 2, nor_temp->buffer[f    ] * 32767.0);
        buffer_set_i16(nora, (i * 2 + 1) * 2, -nor_temp->buffer[f + 2] * 32767.0);
        buffer_set_i16(posa, (i * 4 + 3) * 2, nor_temp->buffer[f + 1] * 32767.0);

		inda->buffer[i] = i;
	}

	data_delete_blob(path);

	string_array_t *a = string_split(path, "\\\\");
	char *s = array_pop(a);
	a = string_split(s, "/");
	s = array_pop(a);
	a = string_split(s, ".");
	char *name = a->buffer[0];

	return plugin_make_raw_mesh(name, posa, nora, inda, scale_pos);
}

void on_delete() {
	plugin_unregister_mesh("stl");
}

void main() {
	plugin = plugin_create();
	plugin_notify_on_delete(plugin, on_delete);
	plugin_register_mesh("stl", import_stl);
}
