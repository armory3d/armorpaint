
#include "../global.h"

static i32 import_arm_progress_unpacked = 0;

void import_arm_run_project_on_next_frame(void *_) {
	// Once envmap is imported
	scene_world->strength       = g_project->envmap_strength;
	g_context->envmap_angle     = g_project->envmap_angle;
	g_context->show_envmap_blur = g_project->envmap_blur;
	if (g_context->show_envmap_blur) {
		scene_world->_->envmap = scene_world->_->radiance_mipmaps->buffer[0];
	}
}

void import_arm_run_mesh_on_next_frame(void *_) {
	layers_init();
}

static buffer_t *import_arm_get_mesh_skin(project_t *project, i32 i) {
	if (project->mesh_skins == NULL || i >= project->mesh_skins->length) {
		return NULL;
	}
	buffer_t *blob = project->mesh_skins->buffer[i];
	return (blob != NULL && blob->length > 0) ? blob : NULL; // Empty buffer = no skin
}

static mesh_data_t_array_t *import_arm_get_mesh_datas(project_t *project, string_array_t *mesh_names) {
	mesh_data_t_array_t *mesh_datas = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < project->mesh_datas->length; ++i) {
		mesh_data_t *raw = project->mesh_datas->buffer[i];
		i32          source_index;
		char        *object_name;
		if (i > 0 && util_mesh_link_parse(raw->name, &source_index, &object_name)) {
			any_array_push(mesh_datas, NULL);
			string_array_push(mesh_names, object_name);
		}
		else {
			mesh_data_t *md   = mesh_data_create(raw);
			md->_->skin_blob  = import_arm_get_mesh_skin(project, i);
			md->_->skin_frame = -1;
			any_array_push(mesh_datas, md);
			string_array_push(mesh_names, md->name);
		}
	}
	for (i32 i = 1; i < mesh_datas->length; ++i) {
		if (mesh_datas->buffer[i] != NULL) {
			continue;
		}
		i32   source_index;
		char *object_name;
		util_mesh_link_parse(((mesh_data_t *)project->mesh_datas->buffer[i])->name, &source_index, &object_name);
		bool linked           = source_index >= 0 && source_index < mesh_datas->length && mesh_datas->buffer[source_index] != NULL;
		mesh_datas->buffer[i] = linked ? mesh_datas->buffer[source_index] : mesh_datas->buffer[0];
	}
	return mesh_datas;
}

void import_arm_run_mesh(project_t *raw) {
	g_project->_->paint_objects = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < raw->mesh_datas->length; ++i) {
		mesh_data_t   *md     = mesh_data_create(raw->mesh_datas->buffer[i]);
		mesh_object_t *object = NULL;
		if (i == 0) {
			mesh_object_set_data(g_context->paint_object, md);
			object = g_context->paint_object;
		}
		else {
			object             = scene_add_mesh_object(md, g_context->paint_object->material, g_context->paint_object->base);
			object->base->name = md->name;
		}
		object->base->transform->scale = (vec4_t){1, 1, 1, 1.0};
		transform_build_matrix(object->base->transform);
		object->base->name = md->name;
		any_array_push(g_project->_->paint_objects, object);
		util_mesh_merge(NULL);
		viewport_scale_to_bounds(2.0);
	}
	sys_notify_on_next_frame(&import_arm_run_mesh_on_next_frame, NULL);
	history_reset();
}

// 0xdf map, i32 count, 0xdb string, i32 len, then the first key, 'v' of "version" when the file has one
#define ARM_VERSION_OFFSET 10

project_t *import_arm_decode_project(buffer_t *b) {
	if (b == NULL || b->length <= ARM_VERSION_OFFSET) { // Missing or too short to be an arm file
		return NULL;
	}
	if (!import_arm_has_version(b)) { // Mesh only
		scene_t *scene = armpack_decode(b);
		return ALLOC_INIT(project_t, {.mesh_datas = scene->mesh_datas});
	}
	if (import_arm_is_old(b)) {
		return import_arm_from_old(b);
	}
	return armpack_decode(b);
}

static bool import_arm_is_selected(i32_array_t *selected, i32 i) {
	return selected == NULL || (i < selected->length && selected->buffer[i] != 0);
}

static bool import_arm_has_selected(i32_array_t *selected) {
	if (selected == NULL) {
		return true;
	}
	for (i32 i = 0; i < selected->length; ++i) {
		if (selected->buffer[i] != 0) {
			return true;
		}
	}
	return false;
}

static i32 import_arm_object_name_index(char *name) {
	if (name == NULL || g_project->_->paint_objects == NULL) {
		return -1;
	}
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *o = g_project->_->paint_objects->buffer[i];
		if (o->base->name != NULL && string_equals(o->base->name, name)) {
			return i;
		}
	}
	return -1;
}

bool import_arm_object_name_exists(char *name) {
	return import_arm_object_name_index(name) >= 0;
}

i32 import_arm_material_name_index(char *name) {
	if (name == NULL) {
		return -1;
	}
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		slot_material_t *m = g_project->_->materials->buffer[i];
		if (m->canvas->name != NULL && string_equals(m->canvas->name, name)) {
			return i;
		}
	}
	return -1;
}

static i32 import_arm_mesh_parent_index(project_t *project, i32 mesh_i) {
	if (project->mesh_parents != NULL && mesh_i < project->mesh_parents->length) {
		return project->mesh_parents->buffer[mesh_i];
	}
	return mesh_i > 0 ? 0 : -1;
}

static mat4_t import_arm_mesh_world_matrix(project_t *project, i32 mesh_i) {
	f32_array_t_array_t *transforms = project->mesh_transforms;
	mat4_t               m          = mat4_from_f32_array(transforms->buffer[mesh_i], 0);
	i32                  parent     = import_arm_mesh_parent_index(project, mesh_i);
	for (i32 guard = 0; guard < transforms->length && parent >= 0 && parent < transforms->length; ++guard) {
		m      = mat4_mult_mat3x4(m, mat4_from_f32_array(transforms->buffer[parent], 0));
		parent = import_arm_mesh_parent_index(project, parent);
	}
	return m;
}

static i32 import_arm_mesh_material_index(project_t *project, i32 mesh_i) {
	if (project->mesh_materials == NULL || mesh_i < 0 || mesh_i >= project->mesh_materials->length) {
		return -1;
	}
	return project->mesh_materials->buffer[mesh_i];
}

void import_arm_run_material_from_project_on_next_frame(slot_material_t_array_t *imported) {
	for (i32 i = 0; i < imported->length; ++i) {
		slot_material_t *m = imported->buffer[i];
		context_set_material(m);
		make_material_parse_paint_material(true);
		util_render_make_material_preview();
	}
}

static bool import_arm_group_exists_in(node_group_t_array_t *dest_groups, char *name) {
	for (i32 i = 0; dest_groups != NULL && i < dest_groups->length; ++i) {
		node_group_t *g = dest_groups->buffer[i];
		if (string_equals(g->canvas->name, name)) {
			return true;
		}
	}
	return false;
}

static bool import_arm_is_unique_group_name_in(node_group_t_array_t *dest_groups, char *name, ui_node_canvas_t_array_t *groups) {
	if (import_arm_group_exists_in(dest_groups, name)) {
		return false;
	}
	for (i32 i = 0; i < groups->length; ++i) {
		if (string_equals(groups->buffer[i]->name, name)) {
			return false;
		}
	}
	return true;
}

static char *import_arm_unique_group_name_in(node_group_t_array_t *dest_groups, char *name, ui_node_canvas_t_array_t *groups) {
	char *base;
	i32   i   = strings_split_number_ext(name, &base);
	char *res = string_tmp("%s%s", base, strings_number_ext(++i));
	while (!import_arm_is_unique_group_name_in(dest_groups, res, groups)) {
		res = string_tmp("%s%s", base, strings_number_ext(++i));
	}
	return res;
}

static void import_arm_rename_group_canvases(char *name, char *new_name, ui_node_canvas_t_array_t *canvases, ui_node_canvas_t_array_t *groups) {
	for (i32 i = 0; canvases != NULL && i < canvases->length; ++i) {
		ui_node_canvas_t *c = canvases->buffer[i];
		for (i32 j = 0; j < c->nodes->length; ++j) {
			ui_node_t *n = c->nodes->buffer[j];
			if (string_equals(n->type, "GROUP") && string_equals(n->name, name)) {
				n->name = string_copy(new_name);
			}
		}
	}
	for (i32 i = 0; groups != NULL && i < groups->length; ++i) {
		ui_node_canvas_t *c = groups->buffer[i];
		for (i32 j = 0; j < c->nodes->length; ++j) {
			ui_node_t *n = c->nodes->buffer[j];
			if (string_equals(n->type, "GROUP") && string_equals(n->name, name)) {
				n->name = string_copy(new_name);
			}
		}
	}
}

static ui_node_canvas_t_array_t *import_arm_collect_groups(ui_node_canvas_t_array_t *src_groups, ui_node_canvas_t_array_t *roots) {
	ui_node_canvas_t_array_t *used = any_array_create(0);
	if (src_groups == NULL || roots == NULL) {
		return used;
	}
	for (i32 r = 0; r < roots->length; ++r) {
		ui_node_canvas_t *c = roots->buffer[r];
		for (i32 i = 0; c != NULL && i < c->nodes->length; ++i) {
			ui_node_t *n = c->nodes->buffer[i];
			if (string_equals(n->type, "GROUP")) {
				ui_node_canvas_t *g = ui_nodes_get_group(src_groups, n->name);
				if (g != NULL && array_index_of(used, g) == -1) {
					any_array_push(used, g);
				}
			}
		}
	}
	for (i32 k = 0; k < used->length; ++k) {
		ui_node_canvas_t *c = used->buffer[k];
		for (i32 i = 0; i < c->nodes->length; ++i) {
			ui_node_t *n = c->nodes->buffer[i];
			if (string_equals(n->type, "GROUP")) {
				ui_node_canvas_t *g = ui_nodes_get_group(src_groups, n->name);
				if (g != NULL && array_index_of(used, g) == -1) {
					any_array_push(used, g);
				}
			}
		}
	}
	return used;
}

void import_arm_make_pink(char *abs) {
	console_error(string("%s %s", strings_could_not_locate_texture(), abs));
	u8_array_t *b       = u8_array_create(4);
	b->buffer[0]        = 255;
	b->buffer[1]        = 0;
	b->buffer[2]        = 255;
	b->buffer[3]        = 255;
	gpu_texture_t *pink = gpu_create_texture_from_bytes(b, 1, 1, GPU_TEXTURE_FORMAT_RGBA32);
	array_free(b);
	free(b);
	any_map_set(data_cached_textures, abs, pink);
}

static bool import_arm_has_alpha(buffer_t *pixels, i32 format) {
	if (format == GPU_TEXTURE_FORMAT_RGBA64) {
		u16 *p = (u16 *)pixels->buffer;
		for (u32 i = 3; i < pixels->length / 2; i += 4) {
			if (p[i] < 0x3c00) { // Half float 1.0
				return true;
			}
		}
	}
	else {
		for (u32 i = 3; i < pixels->length; i += 4) {
			if (pixels->buffer[i] < 255) {
				return true;
			}
		}
	}
	return false;
}

// Image nodes are saved by asset name, the saved index only picks between assets sharing a name
static i32 import_arm_image_asset_index(ui_node_button_t *but) {
	char *name  = u8_array_to_string(but->data);
	i32   saved = but->default_value->buffer[0];
	if (saved >= 0 && saved < g_project->_->assets->length && string_equals(g_project->_->assets->buffer[saved]->name, name)) {
		return saved;
	}
	for (i32 i = 0; i < g_project->_->assets->length; ++i) {
		if (string_equals(g_project->_->assets->buffer[i]->name, name)) {
			return i;
		}
	}
	return 9999; // Texture deleted, use pink now
}

void import_arm_init_nodes(ui_node_t_array_t *nodes) {
	for (i32 i = 0; i < nodes->length; ++i) {
		ui_node_t *node = nodes->buffer[i];
		if (string_equals(node->type, "TEX_IMAGE")) {
			node->buttons->buffer[0]->default_value = f32_array_create_x(import_arm_image_asset_index(node->buttons->buffer[0]));
			node->buttons->buffer[0]->data          = u8_array_create_from_string("");
		}
	}
}

static packed_asset_t *import_arm_take_packed_asset(project_t *project, char *abs, char *file, bool copy) {
	if (g_project->packed_assets == NULL) {
		g_project->packed_assets = any_array_create_from_raw((void *[]){}, 0);
	}
	for (i32 i = 0; i < project->packed_assets->length; ++i) {
		packed_asset_t *pa = project->packed_assets->buffer[i];
#ifdef IRON_WINDOWS
		pa->name = string_copy(string_replace_all(pa->name, "/", "\\"));
#else
		pa->name = string_copy(string_replace_all(pa->name, "\\", "/"));
#endif
		pa->name     = string_copy(path_normalize(pa->name));
		bool matches = string_equals(pa->name, file) || string_equals(pa->name, abs);
		if (!matches) {
			char *pa_name   = substring(pa->name, string_last_index_of(pa->name, PATH_SEP) + 1, string_length(pa->name));
			char *file_name = substring(file, string_last_index_of(file, PATH_SEP) + 1, string_length(file));
			matches         = string_equals(pa_name, file_name);
		}
		if (matches) {
			pa->name = string_copy(abs);
			if (!project_packed_asset_exists(g_project->packed_assets, pa->name)) {

				if (copy) {
					packed_asset_t *pa_copy = ALLOC_INIT(packed_asset_t, {.name  = string_copy(pa->name),
					                                                      .bytes = u8_array_create_from_array(pa->bytes)}); // Project data is temporary
					pa                      = pa_copy;
				}

				any_array_push(g_project->packed_assets, pa);
			}
			return pa;
		}
	}
	return NULL;
}

void import_arm_unpack_asset(project_t *project, char *abs, char *file, bool copy) {
	packed_asset_t *pa = import_arm_take_packed_asset(project, abs, file, copy);
	if (pa == NULL) {
		return;
	}
	import_arm_progress_unpacked += ((buffer_t *)pa->bytes)->length;
	gpu_texture_t *image = gpu_create_texture_from_encoded_bytes(pa->bytes, pa->name);
	any_map_set(data_cached_textures, abs, image);
}

void import_arm_unpack_sound(project_t *project, char *abs, char *file, bool copy) {
	packed_asset_t *pa = import_arm_take_packed_asset(project, abs, file, copy);
	if (pa == NULL) {
		return;
	}
	import_arm_progress_unpacked += ((buffer_t *)pa->bytes)->length;
	sound_t *sound = iron_load_sound_from_bytes(pa->bytes, path_has_ext(pa->name, "wav") ? ".wav" : ".ogg");
	if (data_cached_sounds == NULL) {
		data_cached_sounds = any_map_create();
	}
	any_map_set(data_cached_sounds, abs, sound);
}

typedef struct import_arm_image {
	char               *abs;
	buffer_t           *bytes; // Encoded
	char               *format;
	bool                packed;
	bool                from_disk; // Read when its batch is decoded
	bool                decoded;
	iron_image_pixels_t pixels;
} import_arm_image_t;

typedef struct import_arm_decode {
	import_arm_image_t *images;
	i32                 end;
	volatile int32_t    next;
} import_arm_decode_t;

#if defined(IRON_ANDROID) || defined(IRON_IOS) || defined(IRON_WASM)
#define IMPORT_ARM_DECODE_BUDGET ((i64)256 * 1024 * 1024)
#else
#define IMPORT_ARM_DECODE_BUDGET ((i64)1024 * 1024 * 1024)
#endif
#define IMPORT_ARM_LAYER_SLOTS    4
#define IMPORT_ARM_DECODE_THREADS 16

typedef struct import_arm_layer_job {
	buffer_t            *compressed;
	gpu_texture_t       *target;
	i32                  res;
	u32                  size;
	gpu_texture_format_t format;
	bool                 check_alpha;
	bool                 is_sculpt;
	bool                 has_alpha;
} import_arm_layer_job_t;

typedef struct import_arm_layer_slot {
	uint8_t         *pixels;
	volatile int32_t state; // 0 = free, 1 = ready for upload, 2 = done
	i32              job_index;
} import_arm_layer_slot_t;

typedef struct import_arm_layer_decode {
	import_arm_layer_job_t  *jobs;
	i32                      count;
	volatile int32_t         next;
	import_arm_layer_slot_t *slots;
} import_arm_layer_decode_t;

typedef struct import_arm_layer_worker_arg {
	import_arm_layer_decode_t *d;
	i32                        slot;
} import_arm_layer_worker_arg_t;

static void import_arm_layer_worker(void *arg) {
	import_arm_layer_worker_arg_t *a = arg;
	import_arm_layer_decode_t     *d = a->d;
	import_arm_layer_slot_t       *s = &d->slots[a->slot];
	for (;;) {
		while (!iron_atomic_compare_exchange(&s->state, 0, 0)) {
		}
		i32 j = iron_atomic_increment(&d->next);
		if (j >= d->count) {
			iron_atomic_exchange(&s->state, 2);
			break;
		}
		import_arm_layer_job_t *job = &d->jobs[j];
		lz4_decode_to(job->compressed, s->pixels, job->size);
		if (job->check_alpha) {
			buffer_t b     = {.buffer = s->pixels, .length = job->size, .capacity = job->size};
			job->has_alpha = import_arm_has_alpha(&b, job->format);
		}
		s->job_index = j;
		iron_atomic_exchange(&s->state, 1);
	}
}

static void import_arm_upload_layer_job(import_arm_layer_job_t *job, uint8_t *pixels) {
	buffer_t       b    = {.buffer = pixels, .length = job->size, .capacity = job->size};
	gpu_texture_t *temp = gpu_create_texture_from_bytes_raw(&b, job->res, job->res, job->format);
	draw_begin(job->target, false, 0);
	draw_set_pipeline(job->is_sculpt ? pipes_copy128 : g_project->is_bgra ? pipes_copy_bgra : pipes_copy);
	draw_image(temp, 0, 0);
	draw_set_pipeline(NULL);
	draw_end();
	gpu_delete_texture(temp);
	if (job->check_alpha && job->has_alpha) {
		g_project->_->translucency = true;
	}
}

static void import_arm_load_layer_textures(import_arm_layer_job_t *jobs, i32 count, u32 max_size) {
	if (count == 0) {
		return;
	}
	u32 alloc_size = max_size > 0 ? max_size : 1;
	i32 slot_count = iron_hardware_threads();
	if (slot_count > IMPORT_ARM_LAYER_SLOTS) {
		slot_count = IMPORT_ARM_LAYER_SLOTS;
	}
	if (slot_count > count) {
		slot_count = count;
	}
	if (slot_count <= 1) {
		uint8_t *pixels = malloc(alloc_size);
		for (i32 j = 0; j < count; ++j) {
			lz4_decode_to(jobs[j].compressed, pixels, jobs[j].size);
			if (jobs[j].check_alpha) {
				buffer_t b        = {.buffer = pixels, .length = jobs[j].size, .capacity = jobs[j].size};
				jobs[j].has_alpha = import_arm_has_alpha(&b, jobs[j].format);
			}
			import_arm_upload_layer_job(&jobs[j], pixels);
		}
		free(pixels);
		return;
	}

	import_arm_layer_slot_t       slots[IMPORT_ARM_LAYER_SLOTS];
	import_arm_layer_worker_arg_t args[IMPORT_ARM_LAYER_SLOTS];
	iron_thread_t                 threads[IMPORT_ARM_LAYER_SLOTS];
	import_arm_layer_decode_t     d = {.jobs = jobs, .count = count, .next = 0, .slots = slots};
	for (i32 w = 0; w < slot_count; ++w) {
		slots[w].pixels    = malloc(alloc_size);
		slots[w].state     = 0;
		slots[w].job_index = -1;
		args[w].d          = &d;
		args[w].slot       = w;
		iron_thread_init(&threads[w], import_arm_layer_worker, &args[w]);
	}

	i32 uploaded = 0;
	while (uploaded < count) {
		for (i32 w = 0; w < slot_count; ++w) {
			if (iron_atomic_compare_exchange(&slots[w].state, 1, 1)) {
				import_arm_upload_layer_job(&jobs[slots[w].job_index], slots[w].pixels);
				iron_atomic_exchange(&slots[w].state, 0);
				uploaded++;
			}
		}
	}

	for (i32 w = 0; w < slot_count; ++w) {
		iron_thread_wait_and_destroy(&threads[w]);
		free(slots[w].pixels);
	}
}

static bool import_arm_is_decodable(char *abs) {
	char *ext = path_ext(abs);
	if (any_map_get(import_texture_importers, ext) != NULL) {
		return false;
	}
	return string_equals(ext, "jpg") || string_equals(ext, "jpeg") || string_equals(ext, "png") || string_equals(ext, "hdr");
}

static void import_arm_decode_thread(void *arg) {
	import_arm_decode_t *d = arg;
	for (;;) {
		i32 i = iron_atomic_increment(&d->next);
		if (i >= d->end) {
			break;
		}
		import_arm_image_t *im = &d->images[i];
		if (im->bytes != NULL) {
			im->decoded = iron_decode_image(im->bytes, im->format, &im->pixels);
		}
	}
}

static i32 import_arm_decode_images(import_arm_image_t *images, i32 start, i32 count) {
	i64 pixel_bytes = 0;
	i32 end         = start;
	while (end < count) {
		import_arm_image_t *im = &images[end];
		if (im->from_disk && im->bytes == NULL) {
			im->bytes = iron_load_blob(data_resolve_path(im->abs));
		}
		buffer_t *b    = im->bytes;
		i64       size = 0;
		i32       w;
		i32       h;
		i32       comp;
		if (b != NULL && stbi_info_from_memory(b->buffer, b->length, &w, &h, &comp)) {
			size = (i64)w * h * (stbi_is_hdr_from_memory(b->buffer, b->length) ? 16 : 4);
		}
		if (end > start && pixel_bytes + size > IMPORT_ARM_DECODE_BUDGET) {
			break;
		}
		pixel_bytes += size;
		end++;
	}

	import_arm_decode_t d            = {.images = images, .end = end, .next = start};
	i32                 thread_count = iron_hardware_threads();
	if (thread_count > IMPORT_ARM_DECODE_THREADS) {
		thread_count = IMPORT_ARM_DECODE_THREADS;
	}
	if (thread_count > end - start) {
		thread_count = end - start;
	}
	if (thread_count <= 1) {
		import_arm_decode_thread(&d);
		return end;
	}
	iron_thread_t threads[IMPORT_ARM_DECODE_THREADS];
	for (i32 i = 0; i < thread_count; ++i) {
		iron_thread_init(&threads[i], import_arm_decode_thread, &d);
	}
	for (i32 i = 0; i < thread_count; ++i) {
		iron_thread_wait_and_destroy(&threads[i]);
	}
	return end;
}

static bool import_arm_canvases_have_script(ui_node_canvas_t_array_t *canvases) {
	if (canvases == NULL) {
		return false;
	}
	for (i32 i = 0; i < canvases->length; ++i) {
		ui_node_canvas_t *c = canvases->buffer[i];
		for (i32 j = 0; c != NULL && j < c->nodes->length; ++j) {
			if (string_equals(c->nodes->buffer[j]->type, "SCRIPT_CPU")) {
				return true;
			}
		}
	}
	return false;
}

static void import_arm_trust_scripts_on_next_frame(void *_) {
	slot_material_t *current = g_context->material;
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		g_context->material = g_project->_->materials->buffer[i];
		util_render_make_material_preview();
	}
	g_context->material = current;
	make_material_parse_paint_material(true);
	layers_update_fill_layers();
	nodes_mesh_update_scripts();
	g_context->ddirty = 2;
}

static void import_arm_trust_scripts_box_draw() {
	ui_text(tr("Trust project scripts?"), UI_ALIGN_LEFT, 0x00000000);
	ui_end_element();
	ui_row2();
	if (ui_icon_button(tr("No"), ICON_CLOSE, UI_ALIGN_CENTER)) {
		ui_box_hide();
	}
	if (ui_icon_button(tr("Yes"), ICON_CHECK, UI_ALIGN_CENTER)) {
		ui_box_hide();
		project_scripts_trusted = true;
		sys_notify_on_next_frame(&import_arm_trust_scripts_on_next_frame, NULL);
	}
}

static bool import_arm_is_cloud(char *path) {
	char *root = path_is_protected() ? string("%scloud", iron_internal_save_path()) : string("%s%scloud", iron_internal_files_location(), PATH_SEP);
	return starts_with(path, root);
}

static void import_arm_check_canvases_scripts(ui_node_canvas_t_array_t *mat_a, ui_node_canvas_t_array_t *mat_b, ui_node_canvas_t_array_t *mesh_a,
                                              ui_node_canvas_t_array_t *mesh_b, char *path) {
	if (args_player) {
		return;
	}
	if (g_config->auto_run_scripts || import_arm_is_cloud(path)) {
		project_scripts_trusted = true;
		return;
	}
	if (!import_arm_canvases_have_script(mat_a) && !import_arm_canvases_have_script(mat_b) && !nodes_mesh_canvases_have_script(mesh_a) &&
	    !nodes_mesh_canvases_have_script(mesh_b)) {
		return;
	}
	project_scripts_trusted = false;
	ui_box_show_custom(&import_arm_trust_scripts_box_draw, 400, 200, NULL, true, tr("Script Nodes"));
}

static void import_arm_check_scripts(project_t *project, char *path, bool meshes) {
	import_arm_check_canvases_scripts(project->material_nodes, project->material_groups, meshes ? project->mesh_nodes : NULL,
	                                  meshes ? project->mesh_groups : NULL, path);
}

static char *import_arm_asset_name(char *file) {
	i32 i = string_last_index_of(file, "/");
	i32 j = string_last_index_of(file, "\\");
	if (j > i) {
		i = j;
	}
	return i >= 0 ? substring(file, i + 1, string_length(file)) : file;
}

static i32 import_arm_find_project_asset(project_t *project, ui_node_button_t *but) {
	if (project->assets == NULL || but == NULL || but->data == NULL || but->data->length == 0) {
		return -1;
	}
	char *name = u8_array_to_string(but->data);
	if (string_equals(name, "")) {
		return -1;
	}
	i32 saved = but->default_value != NULL && but->default_value->length > 0 ? (i32)but->default_value->buffer[0] : -1;
	if (saved >= 0 && saved < project->assets->length && string_equals(import_arm_asset_name(project->assets->buffer[saved]), name)) {
		return saved;
	}
	for (i32 i = 0; i < project->assets->length; ++i) {
		if (string_equals(import_arm_asset_name(project->assets->buffer[i]), name)) {
			return i;
		}
	}
	return -1;
}

static void import_arm_mark_canvas_assets(project_t *project, ui_node_canvas_t_array_t *canvases, i32_array_t *needed) {
	if (canvases == NULL) {
		return;
	}
	for (i32 i = 0; i < canvases->length; ++i) {
		ui_node_canvas_t *c = canvases->buffer[i];
		for (i32 j = 0; c != NULL && j < c->nodes->length; ++j) {
			ui_node_t *n = c->nodes->buffer[j];
			if (string_equals(n->type, "TEX_IMAGE") && n->buttons->length > 0) {
				i32 idx = import_arm_find_project_asset(project, n->buttons->buffer[0]);
				if (idx >= 0 && idx < needed->length) {
					needed->buffer[idx] = 1;
				}
			}
		}
	}
}

static i32_array_t *import_arm_import_referenced_assets(project_t *project, char *path, ui_node_canvas_t_array_t *canvases_a,
                                                        ui_node_canvas_t_array_t *canvases_b) {
	if (project->assets == NULL || project->assets->length == 0) {
		return NULL;
	}
	i32_array_t *needed = i32_array_create(project->assets->length);
	import_arm_mark_canvas_assets(project, canvases_a, needed);
	import_arm_mark_canvas_assets(project, canvases_b, needed);

	i32_array_t *src_to_dest = i32_array_create(project->assets->length);
	char        *base        = path_base_dir(path);
	for (i32 i = 0; i < project->assets->length; ++i) {
		src_to_dest->buffer[i] = -1;
		if (needed->buffer[i] == 0) {
			continue;
		}
		char *file = project->assets->buffer[i];
#ifdef IRON_WINDOWS
		file = string_copy(string_replace_all(file, "/", "\\"));
#else
		file = string_copy(string_replace_all(file, "\\", "/"));
#endif
		// Convert image path from relative to absolute
		char *abs = data_is_abs(file) ? file : string("%s%s", base, file);
		if (project->packed_assets != NULL) {
			abs = string_copy(path_normalize(abs));
			if (any_map_get(data_cached_textures, abs) == NULL) {
				import_arm_unpack_asset(project, abs, file, true);
			}
			else {
				import_arm_take_packed_asset(project, abs, file, true);
			}
		}
		if (any_map_get(data_cached_textures, abs) == NULL && !iron_file_exists(abs)) {
			import_arm_make_pink(abs);
		}
		import_texture_run(abs, false);
		for (i32 j = 0; j < g_project->_->assets->length; ++j) {
			if (string_equals(g_project->_->assets->buffer[j]->file, abs)) {
				src_to_dest->buffer[i] = j;
				break;
			}
		}
	}
	array_delete(needed);
	return src_to_dest;
}

static void import_arm_init_nodes_mapped(project_t *project, ui_node_t_array_t *nodes, i32_array_t *src_to_dest_asset) {
	for (i32 i = 0; i < nodes->length; ++i) {
		ui_node_t *node = nodes->buffer[i];
		if (string_equals(node->type, "TEX_IMAGE") && node->buttons->length > 0) {
			i32 src_idx  = import_arm_find_project_asset(project, node->buttons->buffer[0]);
			i32 dest_idx = (src_to_dest_asset != NULL && src_idx >= 0 && src_idx < src_to_dest_asset->length && src_to_dest_asset->buffer[src_idx] >= 0)
			                   ? src_to_dest_asset->buffer[src_idx]
			                   : import_arm_image_asset_index(node->buttons->buffer[0]);
			node->buttons->buffer[0]->default_value = f32_array_create_x(dest_idx);
			node->buttons->buffer[0]->data          = u8_array_create_from_string("");
		}
	}
}

static void import_arm_remap_mesh_object_nodes(ui_node_t_array_t *nodes, i32_array_t *src_to_dest_obj) {
	for (i32 i = 0; i < nodes->length; ++i) {
		ui_node_t *n = nodes->buffer[i];
		if ((string_equals(n->type, "MESH_OBJECT") || string_equals(n->type, "MESH_SCATTER")) && n->buttons->length > 0) {
			ui_node_button_t *b        = n->buttons->buffer[0];
			i32               src_idx  = b->default_value != NULL && b->default_value->length > 0 ? (i32)b->default_value->buffer[0] : -1;
			i32               dest_idx = (src_to_dest_obj != NULL && src_idx >= 0 && src_idx < src_to_dest_obj->length) ? src_to_dest_obj->buffer[src_idx] : -1;
			b->default_value           = f32_array_create_xyz(dest_idx, -1, dest_idx);
		}
	}
}

static void import_arm_import_groups(project_t *project, ui_node_canvas_t_array_t *used_groups, ui_node_canvas_t_array_t *root_canvases,
                                     node_group_t_array_t *dest_groups, i32_array_t *src_to_dest_asset, i32_array_t *src_to_dest_obj) {
	if (used_groups == NULL || used_groups->length == 0) {
		return;
	}
	ui_node_canvas_t_array_t *cloned = any_array_create(0);
	for (i32 i = 0; i < used_groups->length; ++i) {
		any_array_push(cloned, util_clone_canvas(used_groups->buffer[i]));
	}
	for (i32 i = 0; i < cloned->length; ++i) {
		ui_node_canvas_t *c = cloned->buffer[i];
		if (import_arm_group_exists_in(dest_groups, c->name)) { // Ensure unique group name
			char *old_name = c->name;
			c->name        = string_copy(import_arm_unique_group_name_in(dest_groups, old_name, cloned));
			import_arm_rename_group_canvases(old_name, c->name, root_canvases, cloned);
		}
		import_arm_init_nodes_mapped(project, c->nodes, src_to_dest_asset);
		if (src_to_dest_obj != NULL) {
			import_arm_remap_mesh_object_nodes(c->nodes, src_to_dest_obj);
		}
		node_group_t *ng = ALLOC_INIT(node_group_t, {.canvas = c, .nodes = ui_nodes_create()});
		any_array_push(dest_groups, ng);
	}
	array_delete(cloned);
}

static void import_arm_import_materials(project_t *project, char *path, i32_array_t *selected, bool delete_blob) {
	if (project->material_nodes == NULL || project->material_nodes->length == 0) {
		if (delete_blob) {
			data_delete_blob(path);
		}
		return;
	}

	ui_node_canvas_t_array_t *mat_roots = any_array_create(0);
	for (i32 i = 0; i < project->material_nodes->length; ++i) {
		if (import_arm_is_selected(selected, i)) {
			any_array_push(mat_roots, project->material_nodes->buffer[i]);
		}
	}
	if (mat_roots->length == 0) {
		array_delete(mat_roots);
		if (delete_blob) {
			data_delete_blob(path);
		}
		return;
	}

	ui_node_canvas_t_array_t *mat_groups = import_arm_collect_groups(project->material_groups, mat_roots);
	import_arm_check_canvases_scripts(mat_roots, mat_groups, NULL, NULL, path);
	i32_array_t *src_to_dest_asset = import_arm_import_referenced_assets(project, path, mat_roots, mat_groups);

	shader_data_t            *m0           = data_get_shader("Scene", "Material");
	slot_material_t_array_t  *imported     = any_array_create_from_raw((void *[]){}, 0);
	ui_node_canvas_t_array_t *mat_canvases = any_array_create(0);

	for (i32 i = 0; i < project->material_nodes->length; ++i) {
		if (!import_arm_is_selected(selected, i)) {
			continue;
		}
		ui_node_canvas_t *c = util_clone_canvas(project->material_nodes->buffer[i]); // Project data is temporary
		import_arm_init_nodes_mapped(project, c->nodes, src_to_dest_asset);
		g_context->material = slot_material_create(m0, c);
		if (project->material_datas != NULL && i < project->material_datas->length) {
			material_data2_t *md              = project->material_datas->buffer[i];
			g_context->material->paint_base   = md->paint_base;
			g_context->material->paint_opac   = md->paint_opac;
			g_context->material->paint_occ    = md->paint_occ;
			g_context->material->paint_rough  = md->paint_rough;
			g_context->material->paint_met    = md->paint_met;
			g_context->material->paint_nor    = md->paint_nor;
			g_context->material->paint_height = md->paint_height;
			g_context->material->paint_emis   = md->paint_emis;
			g_context->material->paint_subs   = md->paint_subs;
		}
		any_array_push(g_project->_->materials, g_context->material);
		any_array_push(imported, g_context->material);
		any_array_push(mat_canvases, c);
		history_new_material();
	}

	import_arm_import_groups(project, mat_groups, mat_canvases, g_project->_->material_groups, src_to_dest_asset, NULL);

	if (src_to_dest_asset != NULL) {
		array_delete(src_to_dest_asset);
	}
	array_delete(mat_canvases);
	array_delete(mat_groups);
	array_delete(mat_roots);

	sys_notify_on_next_frame(&import_arm_run_material_from_project_on_next_frame, imported);
	ui_nodes_group_stack = any_array_create_from_raw((void *[]){}, 0);
	ui_dock_redraw(TAB_AREA_SIDEBAR1);
	if (delete_blob) {
		data_delete_blob(path);
	}
}

static void import_arm_run_mesh_append_from_project(project_t *project, char *path, i32_array_t *selected, i32_array_t *src_to_dest_mat) {
	if (project->mesh_datas == NULL || project->mesh_datas->length == 0) {
		return;
	}

	string_array_t        *mesh_names       = string_array_create(0);
	mesh_data_t_array_t   *mesh_datas       = import_arm_get_mesh_datas(project, mesh_names);
	i32_array_t           *src_to_dest_obj  = i32_array_create(mesh_datas->length);
	mesh_object_t_array_t *appended_objects = any_array_create(mesh_datas->length);

	i32            appended = 0;
	bool           assigned = false;
	mesh_object_t *first    = NULL;
	for (i32 i = 0; i < mesh_datas->length; ++i) {
		i32 existing                = import_arm_object_name_index(mesh_names->buffer[i]);
		appended_objects->buffer[i] = NULL;
		if (!import_arm_is_selected(selected, i) || existing >= 0) {
			src_to_dest_obj->buffer[i] = existing;
			continue;
		}
		mesh_data_t   *md     = mesh_datas->buffer[i];
		mesh_object_t *object = scene_add_mesh_object(md, g_context->paint_object->material, NULL);
		object->base->name    = mesh_names->buffer[i];

		if (project->mesh_transforms != NULL && i < project->mesh_transforms->length) {
			transform_set_matrix(object->base->transform, import_arm_mesh_world_matrix(project, i));
		}
		else {
			object->base->transform->scale = (vec4_t){1, 1, 1, 1.0};
			transform_build_matrix(object->base->transform);
		}

		any_array_push(g_project->_->paint_objects, object);
		src_to_dest_obj->buffer[i]  = g_project->_->paint_objects->length - 1;
		appended_objects->buffer[i] = object;
		tab_stages_add_object(object->base->name);
		appended++;
		if (first == NULL) {
			first = object;
		}

		if (src_to_dest_mat != NULL) {
			i32 src_mat = import_arm_mesh_material_index(project, i);
			if (src_mat >= 0 && src_mat < src_to_dest_mat->length) {
				i32 dest_mat = src_to_dest_mat->buffer[src_mat];
				if (dest_mat >= 0) {
					tab_meshes_set_override(object, dest_mat);
					assigned = true;
				}
			}
		}
	}

	if (appended == 0) {
		array_delete(src_to_dest_obj);
		array_delete(appended_objects);
		return;
	}

	if (project->mesh_nodes != NULL && project->mesh_nodes_objects != NULL) {
		ui_node_canvas_t_array_t *mesh_roots        = any_array_create(0);
		mesh_object_t_array_t    *mesh_node_targets = any_array_create(0);
		for (i32 i = 0; i < project->mesh_nodes->length && i < project->mesh_nodes_objects->length; ++i) {
			i32 src_idx = project->mesh_nodes_objects->buffer[i];
			if (src_idx >= 0 && src_idx < appended_objects->length && appended_objects->buffer[src_idx] != NULL) {
				any_array_push(mesh_roots, project->mesh_nodes->buffer[i]);
				any_array_push(mesh_node_targets, appended_objects->buffer[src_idx]);
			}
		}
		if (mesh_roots->length > 0) {
			ui_node_canvas_t_array_t *mesh_groups = import_arm_collect_groups(project->mesh_groups, mesh_roots);
			import_arm_check_canvases_scripts(NULL, NULL, mesh_roots, mesh_groups, path);
			i32_array_t *src_to_dest_asset = import_arm_import_referenced_assets(project, path, mesh_roots, mesh_groups);

			ui_node_canvas_t_array_t *cloned_roots = any_array_create(0);
			for (i32 i = 0; i < mesh_roots->length; ++i) {
				ui_node_canvas_t *c = util_clone_canvas(mesh_roots->buffer[i]);
				import_arm_init_nodes_mapped(project, c->nodes, src_to_dest_asset);
				import_arm_remap_mesh_object_nodes(c->nodes, src_to_dest_obj);
				any_array_push(cloned_roots, c);
			}

			if (g_project->_->mesh_groups == NULL) {
				g_project->_->mesh_groups = any_array_create(0);
			}
			import_arm_import_groups(project, mesh_groups, cloned_roots, g_project->_->mesh_groups, src_to_dest_asset, src_to_dest_obj);

			for (i32 i = 0; i < mesh_node_targets->length; ++i) {
				mesh_object_t *o    = mesh_node_targets->buffer[i];
				mesh_nodes_t  *slot = nodes_mesh_get(o, true);
				slot->canvas        = cloned_roots->buffer[i];
			}
			// Scene mesh nodes need all the canvases and object uids in place
			for (i32 i = 0; i < mesh_node_targets->length; ++i) {
				nodes_mesh_get(mesh_node_targets->buffer[i], false);
			}
			for (i32 i = 0; i < mesh_node_targets->length; ++i) {
				nodes_mesh_update(mesh_node_targets->buffer[i], true);
			}

			if (src_to_dest_asset != NULL) {
				array_delete(src_to_dest_asset);
			}
			array_delete(cloned_roots);
			array_delete(mesh_groups);
			ui_nodes_hwnd->redraws = 2;
		}
		array_delete(mesh_roots);
		array_delete(mesh_node_targets);
	}

	array_delete(src_to_dest_obj);
	array_delete(appended_objects);

	if (assigned) {
		g_project->mesh_materials = i32_array_create(0);
	}

	if (g_project->_->paint_objects->length > 1) {
		util_mesh_merge(NULL);
		if (g_context->merged_object != NULL) {
			g_context->merged_object->base->visible = true;
		}
	}
	context_select_paint_object(first);

	make_material_parse_paint_material(true);
	make_material_parse_mesh_material();
	tab_meshes_reset_preview_map();
	base_update_tools();

	util_uv_uvmap_cached       = false;
	util_uv_trianglemap_cached = false;
	util_uv_seammap_cached     = false;
	util_uv_dilatemap_cached   = false;
	g_context->ddirty          = 4;
	ui_dock_redraw(TAB_AREA_SIDEBAR0);
}

void import_arm_run_mesh_append(char *path) {
	buffer_t *b = data_get_blob(path);
	if (b == NULL) {
		console_error(string("Could not open file %s.", path));
		return;
	}
	project_t *project = import_arm_decode_project(b);
	if (project == NULL) { // Unknown version
		console_error(string("Unsupported project version in %s.", path));
		data_delete_blob(path);
		return;
	}
	import_arm_run_mesh_append_from_project(project, path, NULL, NULL);
	data_delete_blob(path);
}

void import_arm_run_material_from_project(project_t *project, char *path) {
	import_arm_import_materials(project, path, NULL, true);
}

void import_arm_append(project_t *project, char *path, i32_array_t *mesh_selected, i32_array_t *material_selected) {
	i32_array_t *src_to_dest_mat = NULL;
	i32          mat_base        = g_project->_->materials->length;

	if (project->material_nodes != NULL && project->material_nodes->length > 0) {
		i32_array_t *to_import = i32_array_create(project->material_nodes->length);
		src_to_dest_mat        = i32_array_create(project->material_nodes->length);
		i32 dest               = mat_base;
		for (i32 i = 0; i < src_to_dest_mat->length; ++i) {
			ui_node_canvas_t *c        = project->material_nodes->buffer[i];
			i32               existing = import_arm_material_name_index(c != NULL ? c->name : NULL);
			if (existing >= 0) {
				to_import->buffer[i]       = 0;
				src_to_dest_mat->buffer[i] = existing;
			}
			else if (import_arm_is_selected(material_selected, i)) {
				to_import->buffer[i]       = 1;
				src_to_dest_mat->buffer[i] = dest++;
			}
			else {
				to_import->buffer[i]       = 0;
				src_to_dest_mat->buffer[i] = -1;
			}
		}
		if (import_arm_has_selected(to_import)) {
			import_arm_import_materials(project, path, to_import, false);
		}
		array_delete(to_import);
	}

	if (project->mesh_datas != NULL && import_arm_has_selected(mesh_selected)) {
		import_arm_run_mesh_append_from_project(project, path, mesh_selected, src_to_dest_mat);
	}
	if (src_to_dest_mat != NULL) {
		array_delete(src_to_dest_mat);
	}
	data_delete_blob(path);
}

void import_arm_run_swatches_from_project(project_t *project, char *path, bool replace_existing) {
	history_replace_swatches(tr("Import Swatches"));

	if (replace_existing) {
		g_project->swatches = any_array_create_from_raw((void *[]){}, 0);

		if (project->swatches == NULL) { // No swatches contained
			any_array_push(g_project->swatches, project_make_swatch(0xffffffff));
		}
	}

	if (project->swatches != NULL) {
		for (i32 i = 0; i < project->swatches->length; ++i) {
			swatch_color_t *s = util_clone_swatch_color(project->swatches->buffer[i]);
			any_array_push(g_project->swatches, s);
		}
	}
	ui_dock_redraw(TAB_AREA_STATUS);
	data_delete_blob(path);
}

static void import_arm_sculpt_init(void *_) {
	if (history_undo_layers == NULL) {
		return;
	}
	sculpt_init();
	sys_remove_update(import_arm_sculpt_init);
}

void import_arm_run_project(char *path) {
	bool keep_script_trust       = import_arm_keep_script_trust;
	bool was_trusted             = project_scripts_trusted;
	import_arm_keep_script_trust = false;
	ui_header_draw_progress(0.0);
	buffer_t *b = data_get_blob(path);
	if (b == NULL) {
		console_error(string("Could not open file %s.", path));
		ui_header_draw_progress(1.0);
		return;
	}
	if (b->length <= ARM_VERSION_OFFSET) { // Too short to be an arm file
		console_error(string("Unsupported project version in %s.", path));
		data_delete_blob(path);
		ui_header_draw_progress(1.0);
		return;
	}
	project_t *project;
	bool       import_as_mesh = false;
	if (!import_arm_has_version(b)) {
		import_as_mesh     = true;
		scene_t *scene_raw = armpack_decode(b);
		project            = ALLOC_INIT(project_t, {.mesh_datas = scene_raw->mesh_datas});
	}
	else if (import_arm_is_old(b)) {
		project = import_arm_from_old(b);
		if (project == NULL) { // Unknown version
			console_error(string("Unsupported project version in %s.", path));
			data_delete_blob(path);
			ui_header_draw_progress(1.0);
			return;
		}
	}
	else {
		project = armpack_decode(b);
	}

	if (project->version != NULL && project->layer_datas == NULL) {
		// Import as material
		if (project->material_nodes != NULL) {
			import_arm_run_material_from_project(project, path);
		}
		// Import as brush
		else if (project->brush_nodes != NULL) {
			import_arm_run_brush_from_project(project, path);
		}
		// Import as swatches
		else if (project->swatches != NULL) {
			import_arm_run_swatches_from_project(project, path, false);
		}
		return;
	}

	g_context->layers_preview_dirty = true;
	g_context->layer_filter         = 0;
	ui_header_draw_progress(0.1);

	i32 progress_assets = project->assets->length + (project->sound_assets != NULL ? project->sound_assets->length : 0);
	i32 progress_loaded = 0;
	i32 progress_bytes  = 1;
	if (project->packed_assets != NULL) {
		for (i32 i = 0; i < project->packed_assets->length; ++i) {
			packed_asset_t *pa = project->packed_assets->buffer[i];
			progress_bytes += ((buffer_t *)pa->bytes)->length;
		}
	}
	import_arm_progress_unpacked = 0;

	project_new(import_as_mesh);
	g_project->_->filepath = string_copy(path);
	ui_files_filename      = string_copy(substring(path, string_last_index_of(path, PATH_SEP) + 1, string_last_index_of(path, ".")));
#if defined(IRON_ANDROID) || defined(IRON_IOS)
	sys_title_set(ui_files_filename);
#else
	sys_title_set(string("%s - %s", ui_files_filename, manifest_title));
#endif

	// Import as mesh instead
	if (import_as_mesh) {
		import_arm_run_mesh(project);
		return;
	}

// Save to recent
#ifdef IRON_IOS
	char *recent_path = substring(path, string_last_index_of(path, "/") + 1, string_length(path));
#else
	char *recent_path = path;
#endif
#ifdef IRON_WINDOWS
	recent_path = string_copy(string_replace_all(recent_path, "\\", "/"));
#endif
	string_array_t *recent = g_config->recent_projects;
	string_array_remove(recent, recent_path);
	array_insert(recent, 0, recent_path);
	config_save();

	project->_ = g_project->_; // Carry over runtime arrays set up by project_new
	g_project  = project;
	if (keep_script_trust) {
		project_scripts_trusted = was_trusted;
	}
	else {
		import_arm_check_scripts(project, path, true);
	}
	layer_data_t *l0                     = g_project->layer_datas->buffer[0];
	base_res                             = config_get_texture_res_pos(l0->res);
	base_res_x                           = (f32)l0->res;
	base_res_y                           = (f32)l0->res;
	texture_bits_t bits_pos              = l0->bpp == 8 ? TEXTURE_BITS_BITS8 : TEXTURE_BITS_BITS16;
	base_bits                            = bits_pos;
	i32                  bytes_per_pixel = math_floor(l0->bpp / 8.0);
	gpu_texture_format_t format          = l0->bpp == 8 ? GPU_TEXTURE_FORMAT_RGBA32 : GPU_TEXTURE_FORMAT_RGBA64;

	char *base = path_base_dir(path);
	if (g_project->envmap != NULL) {
		g_project->envmap = data_is_abs(g_project->envmap) ? g_project->envmap : string("%s%s", base, g_project->envmap);
#ifdef IRON_WINDOWS
		g_project->envmap = string_copy(string_replace_all(g_project->envmap, "/", "\\"));
#else
		g_project->envmap = string_copy(string_replace_all(g_project->envmap, "\\", "/"));
#endif
		g_project->envmap = path_normalize(g_project->envmap);
	}

	if (g_project->camera_world != NULL) {
		scene_camera->base->transform->local = mat4_from_f32_array(g_project->camera_world, 0);
		transform_decompose(scene_camera->base->transform);
		scene_camera->data->fov             = g_project->camera_fov;
		scene_camera->data->near_plane      = g_project->camera_near;
		scene_camera->data->far_plane       = g_project->camera_far;
		scene_camera->data->frustum_culling = g_project->camera_frustum_culling;
		camera_object_build_proj(scene_camera, -1.0);
		f32_array_t *origin = g_project->camera_origin;
		camera_origins[0]   = (vec4_t){origin->buffer[0], origin->buffer[1], origin->buffer[2], 1.0};
	}

	// Gather the encoded images first, so they can be decoded in parallel
	i32                 image_count = g_project->assets->length;
	import_arm_image_t *images      = calloc(image_count > 0 ? image_count : 1, sizeof(import_arm_image_t));
	for (i32 i = 0; i < image_count; ++i) {
		char *file = g_project->assets->buffer[i];
#ifdef IRON_WINDOWS
		file = string_copy(string_replace_all(file, "/", "\\"));
#else
		file = string_copy(string_replace_all(file, "\\", "/"));
#endif
		// Convert image path from relative to absolute
		char *abs = data_is_abs(file) ? file : string("%s%s", base, file);
		if (g_project->packed_assets != NULL) {
			abs                = string_copy(path_normalize(abs));
			packed_asset_t *pa = import_arm_take_packed_asset(g_project, abs, file, false);
			if (pa != NULL) {
				images[i].bytes  = pa->bytes;
				images[i].format = pa->name;
				images[i].packed = true;
			}
		}
		if (images[i].bytes == NULL && any_map_get(data_cached_textures, abs) == NULL && import_arm_is_decodable(abs) && iron_file_exists(abs)) {
			images[i].from_disk = true;
			images[i].format    = abs;
		}
		images[i].abs = abs;
	}

	i32 decoded_end = 0;
	for (i32 i = 0; i < image_count; ++i) {
		if (i == decoded_end) {
			decoded_end = import_arm_decode_images(images, i, image_count);
		}
		import_arm_image_t *im  = &images[i];
		char               *abs = im->abs;
		if (im->decoded) {
			if (any_map_get(data_cached_textures, abs) == NULL) {
				any_map_set(data_cached_textures, abs, gpu_create_texture_from_pixels(&im->pixels));
			}
			else { // Listed twice
				free(im->pixels.data);
			}
		}
		if (im->packed) {
			import_arm_progress_unpacked += im->bytes->length;
		}
		else if (im->bytes != NULL) {
			iron_delete_blob(im->bytes);
		}
		if (any_map_get(data_cached_textures, abs) == NULL && !iron_file_exists(abs)) {
			import_arm_make_pink(abs);
		}
		bool hdr_as_envmap = path_has_ext(abs, "hdr") && g_project->envmap != NULL && string_equals(g_project->envmap, path_normalize(abs));
		import_texture_run(abs, hdr_as_envmap);
		progress_loaded++;
		ui_header_draw_progress(0.1 + 0.7 * (0.8 * import_arm_progress_unpacked / (f32)progress_bytes + 0.2 * progress_loaded / (f32)progress_assets));
	}
	free(images);

	if (g_project->font_assets != NULL) {
		for (i32 i = 0; i < g_project->font_assets->length; ++i) {
			char *file = g_project->font_assets->buffer[i];
#ifdef IRON_WINDOWS
			file = string_copy(string_replace_all(file, "/", "\\"));
#else
			file = string_copy(string_replace_all(file, "\\", "/"));
#endif
			// Convert font path from relative to absolute
			char *abs = data_is_abs(file) ? file : string("%s%s", base, file);
			if (iron_file_exists(abs)) {
				import_font_run(abs);
			}
		}
	}

	if (g_project->sound_assets != NULL) {
		for (i32 i = 0; i < g_project->sound_assets->length; ++i) {
			char *file = g_project->sound_assets->buffer[i];
#ifdef IRON_WINDOWS
			file = string_copy(string_replace_all(file, "/", "\\"));
#else
			file = string_copy(string_replace_all(file, "\\", "/"));
#endif
			// Convert sound path from relative to absolute
			char *abs = data_is_abs(file) ? file : string("%s%s", base, file);
			if (g_project->packed_assets != NULL) {
				abs = string_copy(path_normalize(abs));
				import_arm_unpack_sound(g_project, abs, file, false);
			}
			if (iron_file_exists(abs) || (data_cached_sounds != NULL && any_map_get(data_cached_sounds, abs) != NULL)) {
				import_sound_run(abs);
			}
			progress_loaded++;
			ui_header_draw_progress(0.1 + 0.7 * (0.8 * import_arm_progress_unpacked / (f32)progress_bytes + 0.2 * progress_loaded / (f32)progress_assets));
		}
	}

	string_array_t      *mesh_names = string_array_create(0);
	mesh_data_t_array_t *mesh_datas = import_arm_get_mesh_datas(g_project, mesh_names);

	ui_header_draw_progress(0.8);
	mesh_data_t *md = mesh_datas->buffer[0];

	mesh_object_set_data(g_context->paint_object, md);
	g_context->paint_object->base->transform->scale = (vec4_t){1, 1, 1, 1.0};
	transform_build_matrix(g_context->paint_object->base->transform);
	g_context->paint_object->base->name = mesh_names->buffer[0];
	g_project->_->paint_objects         = any_array_create_from_raw(
	    (void *[]){
	        g_context->paint_object,
	    },
	    1);

	for (i32 i = 1; i < mesh_datas->length; ++i) {
		mesh_object_t *object = scene_add_mesh_object(mesh_datas->buffer[i], g_context->paint_object->material, g_context->paint_object->base);
		object->base->name    = mesh_names->buffer[i];
		any_array_push(g_project->_->paint_objects, object);
	}

	transform_set_matrix(g_context->paint_object->base->transform, mat4_from_f32_array(g_project->mesh_transforms->buffer[0], 0));
	for (i32 i = 1; i < g_project->mesh_datas->length; ++i) {
		mesh_object_t *o = g_project->_->paint_objects->buffer[i];
		transform_set_matrix(o->base->transform, mat4_from_f32_array(g_project->mesh_transforms->buffer[i], 0));
	}

	if (g_project->mesh_assets != NULL && g_project->mesh_assets->length > 0) {
		char *file             = g_project->mesh_assets->buffer[0];
		char *abs              = data_is_abs(file) ? file : string("%s%s", base, file);
		g_project->mesh_assets = any_array_create_from_raw(
		    (void *[]){
		        abs,
		    },
		    1);
	}

	context_select_paint_object(context_main_object());

	gpu_texture_t *tex = g_project->_->layers->buffer[0]->texpaint;
	if (tex->width != config_get_texture_res_x() || tex->height != config_get_texture_res_y()) {
		layers_resize_targets();
	}

	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		slot_layer_unload(l);
	}
	g_project->_->layers                = any_array_create_from_raw((void *[]){}, 0);
	i32                     layer_count = g_project->layer_datas->length;
	import_arm_layer_job_t *layer_jobs  = calloc(layer_count > 0 ? layer_count * 4 : 1, sizeof(import_arm_layer_job_t));
	i32                     job_count   = 0;
	u32                     max_size    = 0;
	for (i32 i = 0; i < layer_count; ++i) {
		layer_data_t *ld       = g_project->layer_datas->buffer[i];
		bool          is_group = ld->texpaint == NULL;
		bool          is_mask  = ld->texpaint != NULL && ld->texpaint_nor == NULL;
		slot_layer_t *l        = slot_layer_create("", is_group ? LAYER_SLOT_TYPE_GROUP : is_mask ? LAYER_SLOT_TYPE_MASK : LAYER_SLOT_TYPE_LAYER, NULL);
		if (ld->name != NULL) {
			l->name = string_copy(ld->name);
		}
		l->visible = ld->visible;
		any_array_push(g_project->_->layers, l);
		if (!is_group) {
			if (is_mask) {
				u32 sz                  = (u32)ld->res * ld->res * 4;
				layer_jobs[job_count++] = (import_arm_layer_job_t){
				    .compressed = ld->texpaint, .target = l->texpaint, .res = ld->res, .size = sz, .format = GPU_TEXTURE_FORMAT_RGBA32};
				if (sz > max_size)
					max_size = sz;
			}
			else { // Layer
				u32 sz                  = (u32)ld->res * ld->res * 4 * bytes_per_pixel;
				layer_jobs[job_count++] = (import_arm_layer_job_t){
				    .compressed = ld->texpaint, .target = l->texpaint, .res = ld->res, .size = sz, .format = format, .check_alpha = i == 0};
				layer_jobs[job_count++] =
				    (import_arm_layer_job_t){.compressed = ld->texpaint_nor, .target = l->texpaint_nor, .res = ld->res, .size = sz, .format = format};
				layer_jobs[job_count++] =
				    (import_arm_layer_job_t){.compressed = ld->texpaint_pack, .target = l->texpaint_pack, .res = ld->res, .size = sz, .format = format};
				if (sz > max_size)
					max_size = sz;

				if (ld->texpaint_sculpt != NULL) {
					render_target_t *t = render_target_create();
					t->name            = string("texpaint_sculpt%s", l->ext);
					t->width           = ld->res;
					t->height          = ld->res;
					t->format          = "RGBA128";
					l->texpaint_sculpt = render_path_create_render_target(t)->_image;

					u32 ssz                 = (u32)ld->res * ld->res * 4 * 4;
					layer_jobs[job_count++] = (import_arm_layer_job_t){.compressed = ld->texpaint_sculpt,
					                                                   .target     = l->texpaint_sculpt,
					                                                   .res        = ld->res,
					                                                   .size       = ssz,
					                                                   .format     = GPU_TEXTURE_FORMAT_RGBA128,
					                                                   .is_sculpt  = true};
					if (ssz > max_size)
						max_size = ssz;
				}
			}

			l->scale   = ld->uv_scale;
			l->angle   = ld->uv_rot;
			l->uv_type = ld->uv_type;
			l->uv_map  = ld->uv_map;
			if (ld->decal_mat != NULL) {
				l->decal_mat = mat4_from_f32_array(ld->decal_mat, 0);
			}
			l->mask_opacity = ld->opacity_mask;
			if (i == 0 && l->mask_opacity < 1.0) {
				g_project->_->translucency = true;
			}
			l->object_mask = ld->object_mask;
			l->blending    = ld->blending;

			l->paint_base         = ld->paint_base;
			l->paint_opac         = ld->paint_opac;
			l->paint_occ          = ld->paint_occ;
			l->paint_rough        = ld->paint_rough;
			l->paint_met          = ld->paint_met;
			l->paint_nor          = ld->paint_nor;
			l->paint_nor_blend    = ld->paint_nor_blend;
			l->paint_height       = ld->paint_height;
			l->paint_height_blend = ld->paint_height_blend;
			l->paint_emis         = ld->paint_emis;
			l->paint_subs         = ld->paint_subs;
			l->path_points        = ld->path_points;
			l->path_points_world  = ld->path_points_world;
			l->path_points_camera = ld->path_points_camera;
			l->path_points_parent = ld->path_points_parent;
			l->path_tool          = ld->path_tool;
			l->path_curved        = ld->path_curved;
			l->path_text          = ld->path_text;
		}
	}
	import_arm_load_layer_textures(layer_jobs, job_count, max_size);
	free(layer_jobs);

	ui_header_draw_progress(0.85);

	// Assign parents to groups and masks
	for (i32 i = 0; i < g_project->layer_datas->length; ++i) {
		layer_data_t *ld = g_project->layer_datas->buffer[i];
		if (ld->parent >= 0) {
			g_project->_->layers->buffer[i]->parent = g_project->_->layers->buffer[ld->parent];
		}
	}

	// Layer
	g_context->layer        = g_project->_->layers->buffer[0];
	ui_view2d_hwnd->redraws = 2;
	if (slot_layer_get_object_mask(g_context->layer) > 0 || g_context->layer_filter > 0) {
		layers_set_object_mask();
	}
	make_material_parse_mesh_material();

	// Materials
	shader_data_t *m0       = data_get_shader("Scene", "Material");
	g_project->_->materials = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->material_nodes->length; ++i) {
		ui_node_canvas_t *n = g_project->material_nodes->buffer[i];
		import_arm_init_nodes(n->nodes);
		g_context->material = slot_material_create(m0, n);

		material_data2_t *md              = g_project->material_datas->buffer[i];
		g_context->material->paint_base   = md->paint_base;
		g_context->material->paint_opac   = md->paint_opac;
		g_context->material->paint_occ    = md->paint_occ;
		g_context->material->paint_rough  = md->paint_rough;
		g_context->material->paint_met    = md->paint_met;
		g_context->material->paint_nor    = md->paint_nor;
		g_context->material->paint_height = md->paint_height;
		g_context->material->paint_emis   = md->paint_emis;
		g_context->material->paint_subs   = md->paint_subs;

		any_array_push(g_project->_->materials, g_context->material);
	}

	ui_nodes_hwnd->redraws        = 2;
	ui_nodes_group_stack          = any_array_create_from_raw((void *[]){}, 0);
	g_project->_->material_groups = any_array_create_from_raw((void *[]){}, 0);
	g_project->_->mesh_groups     = any_array_create(0);
	if (g_project->material_groups != NULL) {
		for (i32 i = 0; i < g_project->material_groups->length; ++i) {
			ui_node_canvas_t *g = g_project->material_groups->buffer[i];
			import_arm_init_nodes(g->nodes);
			node_group_t *ng = ALLOC_INIT(node_group_t, {.canvas = g, .nodes = ui_nodes_create()});
			any_array_push(g_project->_->material_groups, ng);
		}
	}

	bool make_previews = !base_player;

	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		g_context->material = g_project->_->materials->buffer[i];
		if (make_previews) {
			make_material_bake_node_previews();
			util_render_make_material_preview();
		}
	}
	make_material_parse_paint_material(!make_previews);

	g_project->_->brushes = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->brush_nodes->length; ++i) {
		ui_node_canvas_t *n = g_project->brush_nodes->buffer[i];
		import_arm_init_nodes(n->nodes);
		g_context->brush = slot_brush_create(n);
		any_array_push(g_project->_->brushes, g_context->brush);
		make_material_parse_brush();
		brush_output_node_parse_inputs();
		if (make_previews) {
			util_render_make_brush_preview();
		}
	}

	// Fill layers and path layers materials
	for (i32 i = 0; i < g_project->layer_datas->length; ++i) {
		layer_data_t *ld       = g_project->layer_datas->buffer[i];
		slot_layer_t *l        = g_project->_->layers->buffer[i];
		bool          is_group = ld->texpaint == NULL;
		if (!is_group) {
			l->fill_material = ld->fill_material > -1 ? g_project->_->materials->buffer[ld->fill_material] : NULL;
			l->path_material = ld->path_material > -1 ? g_project->_->materials->buffer[ld->path_material] : NULL;
		}
	}

	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		if (g_project->_->layers->buffer[i]->texpaint_sculpt != NULL) {
			sys_remove_update(import_arm_sculpt_init);
			sys_notify_on_update(import_arm_sculpt_init, NULL);
			break;
		}
	}

	ui_header_draw_progress(0.9);
	if (g_project->mesh_materials != NULL) {
		i32             mat_count = g_project->_->materials->length;
		shader_data_t **mat_cache = calloc(mat_count, sizeof(shader_data_t *));
		shader_compile_batch_begin();
		for (i32 i = 0; i < g_project->_->paint_objects->length && i < g_project->mesh_materials->length; ++i) {
			i32 mat_index = g_project->mesh_materials->buffer[i];
			if (mat_index >= 0) {
				mesh_object_t *o      = g_project->_->paint_objects->buffer[i];
				bool           cached = mat_index < mat_count;
				tab_meshes_set_override_data(o, mat_index, cached ? mat_cache[mat_index] : NULL);
				if (cached) {
					mat_cache[mat_index] = o->material;
				}
			}
		}
		shader_compile_batch_end();
		free(mat_cache);
	}

	if (g_project->mesh_parents != NULL) {
		for (i32 i = 0; i < g_project->_->paint_objects->length && i < g_project->mesh_parents->length; ++i) {
			i32       parent_index = g_project->mesh_parents->buffer[i];
			object_t *parent       = parent_index >= 0 ? g_project->_->paint_objects->buffer[parent_index]->base : NULL;
			object_set_parent(g_project->_->paint_objects->buffer[i]->base, parent);
		}
		transform_build_matrix(_scene_root->transform);
	}

	if (g_project->mesh_physics_shapes != NULL) {
		for (i32 i = 0; i < g_project->_->paint_objects->length && i < g_project->mesh_physics_shapes->length; ++i) {
			i32 shape = g_project->mesh_physics_shapes->buffer[i];
			if (shape < 0) {
				continue; // No physics
			}
			f32 mass = g_project->mesh_physics_masses != NULL && i < g_project->mesh_physics_masses->length ? g_project->mesh_physics_masses->buffer[i] : 0.0;
			util_physics_store(g_project->_->paint_objects->buffer[i]->base, shape, mass);
		}
	}

	nodes_mesh_import();
	util_path_tool_import();

	ui_header_draw_progress(0.95);
	tab_meshes_sort_hierarchy();

	tab_stages_init();
	tab_timeline_import(g_project);

	// Select the first stage
	tab_stages_selected = 0;
	tab_stages_apply(g_project->stages->buffer[0]);

	if (g_context->merged_object == NULL) {
		util_mesh_merge(NULL);
	}
	if (slot_layer_get_object_mask(g_context->layer) > 0 || g_context->layer_filter > 0) {
		layers_set_object_mask();
	}
	else {
		context_select_paint_object(context_main_object());
		if (g_context->merged_object != NULL) {
			g_context->merged_object->base->visible = true;
		}
	}

	ui_header_draw_progress(1.0);
	sys_notify_on_next_frame(&import_arm_run_project_on_next_frame, NULL);

	base_update_tools();
	ui_dock_redraw(TAB_AREA_SIDEBAR0);
	ui_dock_redraw(TAB_AREA_SIDEBAR1);
	g_context->ddirty = 4;
	data_delete_blob(path);
}

void import_arm_run_material(char *path) {
	buffer_t *b = data_get_blob(path);
	if (b == NULL) {
		console_error(string("Could not open file %s.", path));
		return;
	}
	project_t *project;
	if (import_arm_is_old(b)) {
		project = import_arm_from_old(b);
	}
	else {
		project = armpack_decode(b);
	}

	if (project == NULL) { // Unknown version
		console_error(string("Unsupported project version in %s.", path));
		data_delete_blob(path);
		return;
	}
	if (project->version == NULL) {
		data_delete_blob(path);
		return;
	}
	import_arm_run_material_from_project(project, path);
}

void import_arm_run_brush(char *path) {
	buffer_t *b = data_get_blob(path);
	if (b == NULL) {
		console_error(string("Could not open file %s.", path));
		return;
	}
	project_t *project = armpack_decode(b);
	if (project->version == NULL) {
		data_delete_blob(path);
		return;
	}
	import_arm_run_brush_from_project(project, path);
}

void import_arm_run_brush_from_project(project_t *project, char *path) {
	char *base = path_base_dir(path);
	for (i32 i = 0; i < project->assets->length; ++i) {
		char *file = project->assets->buffer[i];
#ifdef IRON_WINDOWS
		file = string_copy(string_replace_all(file, "/", "\\"));
#else
		file = string_copy(string_replace_all(file, "\\", "/"));
#endif
		// Convert image path from relative to absolute
		char *abs = data_is_abs(file) ? file : string("%s%s", base, file);
		if (project->packed_assets != NULL) {
			abs = string_copy(path_normalize(abs));
			import_arm_unpack_asset(project, abs, file, true);
		}
		if (any_map_get(data_cached_textures, abs) == NULL && !iron_file_exists(abs)) {
			import_arm_make_pink(abs);
		}
		import_texture_run(abs, true);
	}

	slot_brush_t_array_t *imported = any_array_create_from_raw((void *[]){}, 0);

	for (i32 i = 0; i < project->brush_nodes->length; ++i) {
		ui_node_canvas_t *c = util_clone_canvas(project->brush_nodes->buffer[i]);
		import_arm_init_nodes(c->nodes);
		g_context->brush = slot_brush_create(c);
		any_array_push(g_project->_->brushes, g_context->brush);
		any_array_push(imported, g_context->brush);
		make_material_parse_brush();
		brush_output_node_parse_inputs();
		util_render_make_brush_preview();
	}

	ui_dock_redraw(TAB_AREA_SIDEBAR1);
	data_delete_blob(path);
}

void import_arm_run_swatches(char *path, bool replace_existing) {
	buffer_t *b = data_get_blob(path);
	if (b == NULL) {
		console_error(string("Could not open file %s.", path));
		return;
	}
	project_t *project = armpack_decode(b);
	if (project->version == NULL) {
		data_delete_blob(path);
		return;
	}
	import_arm_run_swatches_from_project(project, path, replace_existing);
}

bool import_arm_has_version(buffer_t *b) {
	bool has_version = b->length > ARM_VERSION_OFFSET && b->buffer[ARM_VERSION_OFFSET] == 118; // 'v';
	return has_version;
}
