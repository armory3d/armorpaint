
#include "../global.h"

#define DOCK_MIN_SIZE   32
#define DOCK_MAX_LEAVES 64

static dock_node_t *ui_dock_root         = NULL;
static dock_node_t *ui_dock_menubar_leaf = NULL;  // Tab names are moved to make space for the menubar
static bool         ui_dock_menubar_row  = false; // Menubar takes the full row above the dock
static dock_node_t *ui_dock_resizing     = NULL;
static i32          dock_view3d_w        = 1;
static i32          dock_view3d_h        = 1;
// Tab drop waiting for the frame to end
static int *ui_dock_drop_to            = NULL;
static i32  ui_dock_drop_to_position   = 0;
static int *ui_dock_drop_from          = NULL;
static i32  ui_dock_drop_from_position = 0;
static i32  ui_dock_context_tab        = -1;
static bool ui_dock_preset             = false; // Workspace is being set up, not a change made by the user
static f32  ui_dock_resizing_size      = 0;
// Tab resize waiting for the next frame
static i32 ui_dock_resize_tab = -1;
static i32 ui_dock_resize_w   = 0;
static i32 ui_dock_resize_h   = 0;

static bool dock_is_leaf(dock_node_t *n) {
	return n->a == NULL;
}

static bool dock_tab_is_view(i32 id) {
	return id == TAB_VIEW3D || id == TAB_VIEW2D || id == TAB_NODES;
}

static dock_node_t *dock_leaf_create() {
	dock_node_t *n = calloc(1, sizeof(dock_node_t));
	n->hwnd        = ui_window_create();
	return n;
}

static dock_node_t *dock_split_create(dock_node_t *a, dock_node_t *b, bool vertical, i32 fixed, f32 size, i32 role) {
	dock_node_t *n = calloc(1, sizeof(dock_node_t));
	n->a           = a;
	n->b           = b;
	n->vertical    = vertical;
	n->fixed       = fixed;
	n->size        = size;
	n->role        = role;
	a->parent      = n;
	b->parent      = n;
	return n;
}

static void dock_free(dock_node_t *n) {
	if (n == NULL) {
		return;
	}
	if (dock_is_leaf(n)) {
		ui_window_destroy(n->hwnd);
	}
	else {
		dock_free(n->a);
		dock_free(n->b);
	}
	free(n);
}

static i32 dock_collect_leaves(dock_node_t *n, dock_node_t **out, i32 count) {
	if (n == NULL) {
		return count;
	}
	if (dock_is_leaf(n)) {
		if (count < DOCK_MAX_LEAVES) {
			out[count++] = n;
		}
		return count;
	}
	count = dock_collect_leaves(n->a, out, count);
	return dock_collect_leaves(n->b, out, count);
}

static bool dock_tab_open(i32 id) {
	tab_draw_t *tab = ui_base_get_tab(id);
	return tab != NULL && !tab->closed;
}

static i32 dock_leaf_open_count(dock_node_t *l) {
	i32 count = 0;
	for (i32 i = 0; i < l->tab_count; ++i) {
		if (dock_tab_open(l->tabs[i])) {
			count++;
		}
	}
	return count;
}

// Tab id of the open tab at position, -1 when out of range
static i32 dock_leaf_open_tab(dock_node_t *l, i32 position) {
	for (i32 i = 0; i < l->tab_count; ++i) {
		if (dock_tab_open(l->tabs[i]) && position-- == 0) {
			return l->tabs[i];
		}
	}
	return -1;
}

static i32 dock_leaf_open_index(dock_node_t *l, i32 id) {
	i32 position = 0;
	for (i32 i = 0; i < l->tab_count; ++i) {
		if (l->tabs[i] == id) {
			return dock_tab_open(id) ? position : -1;
		}
		if (dock_tab_open(l->tabs[i])) {
			position++;
		}
	}
	return -1;
}

static i32 dock_leaf_active_tab(dock_node_t *l) {
	return dock_leaf_open_tab(l, l->active);
}

static dock_node_t *dock_find_leaf(dock_node_t *n, i32 id) {
	if (n == NULL) {
		return NULL;
	}
	if (dock_is_leaf(n)) {
		for (i32 i = 0; i < n->tab_count; ++i) {
			if (n->tabs[i] == id) {
				return n;
			}
		}
		return NULL;
	}
	dock_node_t *l = dock_find_leaf(n->a, id);
	return l != NULL ? l : dock_find_leaf(n->b, id);
}

static dock_node_t *dock_find_handle(dock_node_t *n, int *handle) {
	if (n == NULL) {
		return NULL;
	}
	if (dock_is_leaf(n)) {
		return &n->active == handle ? n : NULL;
	}
	dock_node_t *l = dock_find_handle(n->a, handle);
	return l != NULL ? l : dock_find_handle(n->b, handle);
}

static dock_node_t *dock_find_role(dock_node_t *n, i32 role) {
	if (n == NULL || dock_is_leaf(n)) {
		return NULL;
	}
	if (n->role == role) {
		return n;
	}
	dock_node_t *s = dock_find_role(n->a, role);
	return s != NULL ? s : dock_find_role(n->b, role);
}

static dock_node_t *dock_first_visible_leaf(dock_node_t *n) {
	if (n == NULL || !n->visible) {
		return NULL;
	}
	if (dock_is_leaf(n)) {
		return n;
	}
	dock_node_t *l = dock_first_visible_leaf(n->a);
	return l != NULL ? l : dock_first_visible_leaf(n->b);
}

// Leaf at the bottom with the height kept, minimized to the tab names with a click
static dock_node_t *dock_status_leaf() {
	dock_node_t *s = dock_find_role(ui_dock_root, DOCK_ROLE_STATUS);
	return s != NULL && dock_is_leaf(s->b) ? s->b : NULL;
}

// ████████╗██████╗ ███████╗███████╗
// ╚══██╔══╝██╔══██╗██╔════╝██╔════╝
//    ██║   ██████╔╝█████╗  █████╗
//    ██║   ██╔══██╗██╔══╝  ██╔══╝
//    ██║   ██║  ██║███████╗███████╗
//    ╚═╝   ╚═╝  ╚═╝╚══════╝╚══════╝

static void dock_leaf_add(dock_node_t *l, i32 id, i32 at) {
	if (l->tab_count >= TAB_COUNT) {
		return;
	}
	if (at < 0 || at > l->tab_count) {
		at = l->tab_count;
	}
	for (i32 i = l->tab_count; i > at; --i) {
		l->tabs[i] = l->tabs[i - 1];
	}
	l->tabs[at] = id;
	l->tab_count++;
}

static void dock_leaf_remove(dock_node_t *l, i32 id) {
	for (i32 i = 0; i < l->tab_count; ++i) {
		if (l->tabs[i] == id) {
			for (i32 j = i; j < l->tab_count - 1; ++j) {
				l->tabs[j] = l->tabs[j + 1];
			}
			l->tab_count--;
			return;
		}
	}
}

static void dock_leaf_clamp_active(dock_node_t *l) {
	i32 count = dock_leaf_open_count(l);
	if (l->active > count - 1) {
		l->active = count - 1;
	}
	if (l->active < 0) {
		l->active = 0;
	}
}

// Leaf with no tabs left, the other child takes the place of the split
static void dock_prune(dock_node_t *l) {
	dock_node_t *p = l->parent;
	if (l->tab_count > 0 || p == NULL) {
		return;
	}
	dock_node_t *other = p->a == l ? p->b : p->a;
	other->parent      = p->parent;
	if (p->parent == NULL) {
		ui_dock_root = other;
	}
	else if (p->parent->a == p) {
		p->parent->a = other;
	}
	else {
		p->parent->b = other;
	}
	ui_window_destroy(l->hwnd);
	free(l);
	free(p);
}

// Split the leaf in half, the new leaf is placed on the given side
static dock_node_t *dock_split_leaf(dock_node_t *target, dock_zone_t zone) {
	dock_node_t *parent   = target->parent;
	dock_node_t *l        = dock_leaf_create();
	bool         first    = zone == DOCK_ZONE_LEFT || zone == DOCK_ZONE_TOP;
	bool         vertical = zone == DOCK_ZONE_TOP || zone == DOCK_ZONE_BOTTOM;
	dock_node_t *s        = dock_split_create(first ? l : target, first ? target : l, vertical, 0, 0.5, DOCK_ROLE_NONE);
	s->parent             = parent;
	if (parent == NULL) {
		ui_dock_root = s;
	}
	else if (parent->a == target) {
		parent->a = s;
	}
	else {
		parent->b = s;
	}
	return l;
}

// Move the tab into the leaf, or next to the leaf when a side is given
static void dock_move_tab(i32 id, dock_node_t *target, dock_zone_t zone, i32 open_position) {
	dock_node_t *source = dock_find_leaf(ui_dock_root, id);
	if (source == NULL || target == NULL) {
		return;
	}
	if (source == target && zone != DOCK_ZONE_CENTER && source->tab_count == 1) {
		return;
	}

	// Position in between the open tabs
	i32 at = -1;
	if (zone == DOCK_ZONE_CENTER && open_position > -1) {
		i32 before = dock_leaf_open_tab(target, open_position);
		if (source == target && dock_leaf_open_index(target, id) < open_position) {
			before = dock_leaf_open_tab(target, open_position + 1);
		}
		dock_leaf_remove(source, id);
		for (i32 i = 0; i < target->tab_count; ++i) {
			if (target->tabs[i] == before) {
				at = i;
			}
		}
	}
	else {
		dock_leaf_remove(source, id);
	}

	if (zone != DOCK_ZONE_CENTER) {
		target = dock_split_leaf(target, zone);
	}
	dock_leaf_add(target, id, at);
	target->active = dock_leaf_open_index(target, id);
	if (source != target) {
		dock_leaf_clamp_active(source);
		dock_prune(source);
	}
}

static void dock_default_leaf(dock_node_t *l, i32 id) {
	tab_draw_t *tab = ui_base_get_tab(id);
	if (tab != NULL) {
		tab->closed = tab->closed_default;
		dock_leaf_add(l, id, -1);
	}
}

void ui_dock_reset() {
	dock_free(ui_dock_root);
	ui_dock_resizing = NULL;
	ui_dock_drop_to  = NULL;

	dock_node_t *view3d = dock_leaf_create();
	dock_default_leaf(view3d, TAB_VIEW3D);
	dock_node_t *view2d = dock_leaf_create();
	dock_default_leaf(view2d, TAB_VIEW2D);
	dock_node_t *nodes = dock_leaf_create();
	dock_default_leaf(nodes, TAB_NODES);
	dock_node_t *status = dock_leaf_create();
	dock_default_leaf(status, TAB_BROWSER);
	dock_default_leaf(status, TAB_TEXTURES);
	dock_default_leaf(status, TAB_FONTS);
	dock_default_leaf(status, TAB_SOUNDS);
	dock_default_leaf(status, TAB_SWATCHES);
	dock_default_leaf(status, TAB_TIMELINE);
	dock_default_leaf(status, TAB_CONSOLE);
	dock_node_t *sidebar0 = dock_leaf_create();
	dock_default_leaf(sidebar0, TAB_LAYERS);
	dock_default_leaf(sidebar0, TAB_MESHES);
	dock_default_leaf(sidebar0, TAB_SCRIPTS);
	dock_default_leaf(sidebar0, TAB_DEBUG);
	dock_node_t *sidebar1 = dock_leaf_create();
	dock_default_leaf(sidebar1, TAB_MATERIALS);
	dock_default_leaf(sidebar1, TAB_BRUSHES);
	dock_default_leaf(sidebar1, TAB_PLUGINS);

	f32 scale     = g_config->window_scale;
	i32 sidebar_w = math_floor(ui_sidebar_default_w * scale);
	i32 status_h  = math_floor(ui_statusbar_default_h * scale);
	// Views take the space next to the sidebar and the toolbar
	i32 views_w = iron_window_width() - sidebar_w - math_floor(ui_toolbar_default_w * scale);
#if defined(IRON_IOS) || defined(IRON_ANDROID)
	i32 nodes_w = math_floor(views_w * 0.473);
#else
	i32 nodes_w = math_floor(views_w * 0.515); // Align with ui header controls
#endif
	i32 nodes_h = math_floor((iron_window_height() - status_h) / 2.0);

	dock_node_t *views_inner = dock_split_create(view2d, nodes, true, 2, nodes_h, DOCK_ROLE_VIEWS_INNER);
	dock_node_t *views       = dock_split_create(view3d, views_inner, false, 2, nodes_w, DOCK_ROLE_VIEWS);
	dock_node_t *left        = dock_split_create(views, status, true, 2, status_h, DOCK_ROLE_STATUS);
	dock_node_t *sidebar     = dock_split_create(sidebar0, sidebar1, true, 0, 0.5, DOCK_ROLE_SIDEBAR_INNER);
	ui_dock_root             = dock_split_create(left, sidebar, false, 2, sidebar_w, DOCK_ROLE_SIDEBAR);
	ui_dock_layout();
}

// Sizes of the default splits, splits removed by docking are skipped
void ui_dock_set_size(i32 role, f32 size) {
	dock_node_t *s = dock_find_role(ui_dock_root, role);
	if (s != NULL) {
		s->size = size;
	}
}

f32 ui_dock_get_size(i32 role) {
	dock_node_t *s = dock_find_role(ui_dock_root, role);
	return s != NULL ? s->size : 0;
}

// ███████╗████████╗ ██████╗ ██████╗ ███████╗
// ██╔════╝╚══██╔══╝██╔═══██╗██╔══██╗██╔════╝
// ███████╗   ██║   ██║   ██║██████╔╝█████╗
// ╚════██║   ██║   ██║   ██║██╔══██╗██╔══╝
// ███████║   ██║   ╚██████╔╝██║  ██║███████╗
// ╚══════╝   ╚═╝    ╚═════╝ ╚═╝  ╚═╝╚══════╝

// Stored in config as the root node
// split {-1, vertical, fixed, size, collapsed, role, node a, node b}, leaf {tab count, active, tab ids}
static void dock_save_node(i32_array_t *a, dock_node_t *n) {
	if (dock_is_leaf(n)) {
		i32_array_push(a, n->tab_count);
		i32_array_push(a, n->active);
		for (i32 i = 0; i < n->tab_count; ++i) {
			i32_array_push(a, n->tabs[i] + (dock_tab_open(n->tabs[i]) ? 0 : 1000));
		}
		return;
	}
	i32_array_push(a, -1);
	i32_array_push(a, n->vertical);
	i32_array_push(a, n->fixed);
	i32_array_push(a, n->fixed == 0 ? math_floor(n->size * 10000) : math_floor(n->size));
	i32_array_push(a, n->collapsed);
	i32_array_push(a, n->role);
	dock_save_node(a, n->a);
	dock_save_node(a, n->b);
}

// Not stored while a workspace preset is being set up or the player runs
void ui_dock_save() {
	if (ui_dock_root == NULL || base_player || ui_dock_preset || base_workspace_pending) {
		return;
	}
	if (g_config->layout_tabs == NULL) {
		g_config->layout_tabs = i32_array_create_from_raw((i32[]){}, 0);
	}
	i32_array_t *a = g_config->layout_tabs;
	a->length      = 0;
	dock_save_node(a, ui_dock_root);
	config_save();
}

void ui_dock_set_preset(bool b) {
	ui_dock_preset = b;
}

static dock_node_t *dock_load_node(i32_array_t *a, i32 *pos, bool *docked, i32 depth) {
	if (*pos >= a->length || depth > 32) {
		return NULL;
	}
	i32 count = a->buffer[(*pos)++];
	if (count == -1) {
		if (*pos + 5 > a->length) {
			return NULL;
		}
		bool vertical  = a->buffer[(*pos)++] != 0;
		i32  fixed     = a->buffer[(*pos)++];
		f32  size      = a->buffer[(*pos)++];
		bool collapsed = a->buffer[(*pos)++] != 0;
		i32  role      = a->buffer[(*pos)++];
		if (fixed < 0 || fixed > 2) {
			return NULL;
		}
		dock_node_t *na = dock_load_node(a, pos, docked, depth + 1);
		dock_node_t *nb = dock_load_node(a, pos, docked, depth + 1);
		if (na == NULL || nb == NULL) {
			dock_free(na);
			dock_free(nb);
			return NULL;
		}
		dock_node_t *s = dock_split_create(na, nb, vertical, fixed, fixed == 0 ? size / 10000.0 : size, role);
		s->collapsed   = collapsed;
		return s;
	}
	if (count < 0 || count > TAB_COUNT || *pos + 1 + count > a->length) {
		return NULL;
	}
	dock_node_t *l = dock_leaf_create();
	l->active      = a->buffer[(*pos)++];
	for (i32 i = 0; i < count; ++i) {
		i32         id     = a->buffer[(*pos)++];
		bool        closed = id >= 1000;
		tab_draw_t *tab    = ui_base_get_tab(id % 1000);
		if (tab != NULL && !docked[tab->id]) {
			docked[tab->id] = true;
			tab->closed     = closed;
			dock_leaf_add(l, tab->id, -1);
		}
	}
	return l;
}

void ui_dock_load() {
	i32_array_t *a   = g_config->layout_tabs;
	i32          pos = 0;
	if (a == NULL || a->length == 0) {
		ui_dock_reset();
		return;
	}
	bool         docked[TAB_COUNT] = {0};
	dock_node_t *root              = dock_load_node(a, &pos, docked, 0);
	if (root == NULL) {
		ui_dock_reset();
		return;
	}
	dock_free(ui_dock_root);
	ui_dock_root = root;

	// Tabs added after the config was saved
	dock_node_t *leaves[DOCK_MAX_LEAVES];
	dock_collect_leaves(ui_dock_root, leaves, 0);
	for (i32 i = 0; i < TAB_COUNT; ++i) {
		tab_draw_t *tab = ui_base_get_tab(i);
		if (tab != NULL && !docked[i]) {
			tab->closed = tab->closed_default;
			dock_leaf_add(leaves[0], i, -1);
		}
	}
	ui_dock_layout();
}

// ██╗      █████╗ ██╗   ██╗ ██████╗ ██╗   ██╗████████╗
// ██║     ██╔══██╗╚██╗ ██╔╝██╔═══██╗██║   ██║╚══██╔══╝
// ██║     ███████║ ╚████╔╝ ██║   ██║██║   ██║   ██║
// ██║     ██╔══██║  ╚██╔╝  ██║   ██║██║   ██║   ██║
// ███████╗██║  ██║   ██║   ╚██████╔╝╚██████╔╝   ██║
// ╚══════╝╚═╝  ╚═╝   ╚═╝    ╚═════╝  ╚═════╝    ╚═╝

static void dock_hide(dock_node_t *n) {
	n->visible = false;
	if (!dock_is_leaf(n)) {
		dock_hide(n->a);
		dock_hide(n->b);
	}
}

static void dock_update_visible(dock_node_t *n) {
	if (dock_is_leaf(n)) {
		dock_leaf_clamp_active(n);
		i32 active = dock_leaf_active_tab(n);
		// Distract free mode draws the views only
		n->visible = active != -1 && (ui_base_show || dock_tab_is_view(active));
		return;
	}
	dock_update_visible(n->a);
	dock_update_visible(n->b);
	if (n->collapsed && ui_base_show) {
		dock_hide(n->fixed == 1 ? n->a : n->b);
	}
	n->visible = n->a->visible || n->b->visible;
}

static i32 dock_split_min_size(dock_node_t *n) {
	if (n->role == DOCK_ROLE_STATUS) {
		return math_floor(ui_statusbar_default_h * g_config->window_scale);
	}
	return DOCK_MIN_SIZE;
}

static void dock_layout(dock_node_t *n, i32 x, i32 y, i32 w, i32 h) {
	n->x = x;
	n->y = y;
	n->w = n->visible ? w : 0;
	n->h = n->visible ? h : 0;
	if (dock_is_leaf(n)) {
		return;
	}
	if (!n->a->visible || !n->b->visible) {
		dock_layout(n->a, x, y, w, h);
		dock_layout(n->b, x, y, w, h);
		return;
	}

	i32 total = n->vertical ? h : w;
	i32 size  = n->fixed == 1 ? n->size : n->fixed == 2 ? total - n->size : total * n->size;
	i32 min   = dock_split_min_size(n);
	if (size > total - min) {
		size = total - min;
	}
	if (size < DOCK_MIN_SIZE) {
		size = DOCK_MIN_SIZE;
	}
	if (n->vertical) {
		dock_layout(n->a, x, y, w, size);
		dock_layout(n->b, x, y + size, w, h - size);
	}
	else {
		dock_layout(n->a, x, y, size, h);
		dock_layout(n->b, x + size, y, w - size, h);
	}
}

static dock_node_t *dock_view_leaf(i32 id) {
	dock_node_t *l = dock_find_leaf(ui_dock_root, id);
	return l != NULL && l->visible && dock_leaf_active_tab(l) == id ? l : NULL;
}

// The tab names of the 3D view are hidden when the header is not pinned, unless there are more tabs to pick from
static i32 dock_view3d_strip_h(dock_node_t *l) {
	if (!ui_base_show) {
		return 0;
	}
	bool pinned = g_config->header_pinned;
	return pinned || dock_leaf_open_count(l) > 1 ? ui_header_h : 0;
}

static bool dock_leaf_has_tab_names(dock_node_t *l) {
	i32 active = dock_leaf_active_tab(l);
	if (active == TAB_VIEW3D) {
		return dock_view3d_strip_h(l) > 0;
	}
	if (dock_tab_is_view(active)) {
		return !g_config->touch_ui && ui_base_show;
	}
	return l->w > ui_sidebar_w_mini;
}

static void dock_update_views() {
	dock_node_t *view3d = dock_view_leaf(TAB_VIEW3D);
	dock_node_t *view2d = dock_view_leaf(TAB_VIEW2D);
	dock_node_t *nodes  = dock_view_leaf(TAB_NODES);
	base_view3d_show    = view3d != NULL;
	ui_view2d_show      = view2d != NULL;
	ui_nodes_show       = nodes != NULL;

	if (view2d != NULL) {
		ui_view2d_wx = view2d->x;
		ui_view2d_wy = view2d->y;
		ui_view2d_ww = view2d->w;
		ui_view2d_wh = view2d->h;
	}
	if (nodes != NULL) {
		ui_nodes_wx = nodes->x;
		ui_nodes_wy = nodes->y;
		ui_nodes_ww = nodes->w;
		ui_nodes_wh = nodes->h;
	}

	if (view3d != NULL) {
		bool pinned    = g_config->header_pinned;
		i32  toolbar_w = ui_base_show ? ui_toolbar_w(true) : 0;
		i32  header_h  = dock_view3d_strip_h(view3d) + (ui_base_show && pinned ? ui_header_h : 0);
		dock_view3d_w  = view3d->w - toolbar_w;
		dock_view3d_h  = view3d->h - header_h;
		base_appx      = view3d->x + toolbar_w;
		base_appy      = view3d->y + header_h;
	}
	else {
		dock_view3d_w = 1;
		dock_view3d_h = 1;
		base_appx     = -1;
		base_appy     = 0;
	}
}

void ui_dock_layout() {
	if (ui_dock_root == NULL) {
		return;
	}
	dock_update_visible(ui_dock_root);
	dock_layout(ui_dock_root, 0, 0, iron_window_width(), iron_window_height());
	dock_update_views();

	// The menubar floats in the 3D view when the header is not pinned
	ui_dock_menubar_leaf = NULL;
	ui_dock_menubar_row  = false;
	bool floating        = !g_config->header_pinned && base_view3d_show;
	bool hidden          = !ui_base_show || (g_config->touch_ui && !base_view3d_show);
	if (!floating && !hidden) {
		dock_node_t *l = dock_first_visible_leaf(ui_dock_root);
		if (l != NULL && dock_leaf_has_tab_names(l) && l->w > ui_menubar_w + 100 * UI_SCALE()) {
			ui_dock_menubar_leaf = l;
		}
		else {
			ui_dock_menubar_row = true;
			dock_layout(ui_dock_root, 0, ui_header_h, iron_window_width(), iron_window_height() - ui_header_h);
			dock_update_views();
		}
	}
}

i32 ui_dock_view3d_w() {
	if (ui_dock_root == NULL) { // Not docked yet
		return iron_window_width() - ui_sidebar_default_w - ui_toolbar_default_w;
	}
	return dock_view3d_w;
}

i32 ui_dock_view3d_h() {
	if (ui_dock_root == NULL) {
		return iron_window_height() - ui_header_default_h * 2 - ui_statusbar_default_h;
	}
	return dock_view3d_h;
}

bool ui_dock_is_empty() {
	return ui_dock_root == NULL || !ui_dock_root->visible;
}

bool ui_dock_is_menubar_row() {
	return ui_dock_menubar_row;
}

// Width the menubar can take
i32 ui_dock_menubar_max_w() {
	if (ui_dock_menubar_leaf != NULL) {
		return ui_dock_menubar_leaf->w;
	}
	if (base_view3d_show && !ui_dock_menubar_row) {
		return base_view_w();
	}
	return iron_window_width();
}

// ████████╗ █████╗ ██████╗ ███████╗
// ╚══██╔══╝██╔══██╗██╔══██╗██╔════╝
//    ██║   ███████║██████╔╝███████╗
//    ██║   ██╔══██║██╔══██╗╚════██║
//    ██║   ██║  ██║██████╔╝███████║
//    ╚═╝   ╚═╝  ╚═╝╚═════╝ ╚══════╝

bool ui_dock_tab_open(i32 id) {
	return dock_tab_open(id);
}

// Open the tab and bring it to front
void ui_base_show_tab(i32 id) {
	tab_draw_t  *tab = ui_base_get_tab(id);
	dock_node_t *l   = dock_find_leaf(ui_dock_root, id);
	if (tab == NULL || l == NULL) {
		return;
	}
	bool opened  = tab->closed;
	bool changed = opened || dock_leaf_active_tab(l) != id;
	tab->closed  = false;
	l->active    = dock_leaf_open_index(l, id);
	if (changed) {
		l->hwnd->redraws = 2;
		ui_dock_layout();
		ui_dock_save();
	}
}

void ui_base_close_tab(i32 id) {
	tab_draw_t  *tab = ui_base_get_tab(id);
	dock_node_t *l   = dock_find_leaf(ui_dock_root, id);
	if (tab == NULL || l == NULL || tab->closed) {
		return;
	}
	i32 index   = dock_leaf_open_index(l, id);
	tab->closed = true;
	if (l->active > index) {
		l->active--;
	}
	dock_leaf_clamp_active(l);
	l->hwnd->redraws = 2;
	ui_dock_layout();
	ui_dock_save();
}

// Show or hide one of the views
void ui_dock_show_view(i32 id, bool show) {
	show ? ui_base_show_tab(id) : ui_base_close_tab(id);
}

// Redraw the tabs which are docked in the given tab area by default
void ui_dock_redraw(i32 area) {
	dock_node_t *leaves[DOCK_MAX_LEAVES];
	i32          count = dock_collect_leaves(ui_dock_root, leaves, 0);
	for (i32 i = 0; i < count; ++i) {
		dock_node_t *l = leaves[i];
		for (i32 j = 0; j < l->tab_count; ++j) {
			tab_draw_t *tab = ui_base_get_tab(l->tabs[j]);
			if (tab != NULL && (area == -1 || tab->area == area)) {
				l->hwnd->redraws = 2;
				break;
			}
		}
	}
}

// Draw the tab names of the leaf the view is docked in, the view draws the window on its own
void ui_dock_view_tabs(i32 id) {
	dock_node_t *l = dock_find_leaf(ui_dock_root, id);
	if (l == NULL) {
		return;
	}
	if (l == ui_dock_menubar_leaf) {
		g_ui->tab_offset_x = ui_menubar_w;
	}
	for (i32 i = 0; i < l->tab_count; ++i) {
		if (dock_tab_open(l->tabs[i])) {
			ui_tab(&l->active, tr(ui_base_get_tab(l->tabs[i])->name), false, -1, false);
		}
	}
}

void tab_view3d_draw(i32 *htab) {
	ui_tab(htab, tr("3D View"), false, -1, false);
}

void tab_view2d_draw(i32 *htab) {
	ui_tab(htab, tr("2D View"), false, -1, false);
}

void tab_nodes_draw(i32 *htab) {
	ui_tab(htab, tr("Nodes"), false, -1, false);
}

bool ui_dock_on_tab_dock(int *handle) {
	return dock_find_handle(ui_dock_root, handle) != NULL;
}

// Called while the windows are being drawn, dock the tab once the frame is done
void ui_dock_on_tab_drop(int *to, i32 to_position, int *from, i32 from_position) {
	ui_dock_drop_to            = to;
	ui_dock_drop_to_position   = to_position;
	ui_dock_drop_from          = from;
	ui_dock_drop_from_position = from_position;
}

static f32 ui_dock_resize_box_w = 0;
static f32 ui_dock_resize_box_h = 0;

static void ui_dock_resize_box_draw() {
	ui_slider(&ui_dock_resize_box_w, tr("Width"), 1, 3840, false, 1, true, UI_ALIGN_RIGHT, true);
	ui_slider(&ui_dock_resize_box_h, tr("Height"), 1, 2160, false, 1, true, UI_ALIGN_RIGHT, true);
	ui_end_element();
	ui_row2();
	if (ui_icon_button(tr("Cancel"), ICON_CLOSE, UI_ALIGN_CENTER)) {
		ui_box_hide();
	}
	if (ui_icon_button(tr("OK"), ICON_CHECK, UI_ALIGN_CENTER) || g_ui->is_return_down) {
		ui_dock_resize_tab = ui_dock_context_tab;
		ui_dock_resize_w   = math_floor(ui_dock_resize_box_w);
		ui_dock_resize_h   = math_floor(ui_dock_resize_box_h);
		ui_box_hide();
	}
}

static void ui_dock_tab_context_menu_draw() {
	if (ui_menu_button(tr("Close"), "", ICON_CLOSE)) {
		ui_base_close_tab(ui_dock_context_tab);
	}
	if (ui_menu_button(tr("Resize..."), "", ICON_NONE)) {
		dock_node_t *l       = dock_find_leaf(ui_dock_root, ui_dock_context_tab);
		bool         view3d  = ui_dock_context_tab == TAB_VIEW3D;
		ui_dock_resize_box_w = view3d ? dock_view3d_w : l->w;
		ui_dock_resize_box_h = view3d ? dock_view3d_h : l->h;
		ui_box_show_custom(&ui_dock_resize_box_draw, 400, 200, NULL, true, tr("Resize"));
	}
}

static i32 dock_split_resize_child(dock_node_t *s, dock_node_t *n, i32 delta) {
	i32 total = s->vertical ? s->h : s->w;
	i32 a     = s->vertical ? s->a->h : s->a->w;
	i32 to    = s->a == n ? a + delta : a - delta;
	i32 min   = dock_split_min_size(s);
	if (to > total - min) {
		to = total - min;
	}
	if (to < DOCK_MIN_SIZE) {
		to = DOCK_MIN_SIZE;
	}
	s->size = s->fixed == 1 ? to : s->fixed == 2 ? total - to : to / (f32)total;
	return s->a == n ? to - a : a - to;
}

static void dock_resize_leaf(dock_node_t *l, i32 id, i32 w, i32 h) {
	bool view3d = id == TAB_VIEW3D;
	i32  dw     = w - (view3d ? dock_view3d_w : l->w);
	i32  dh     = h - (view3d ? dock_view3d_h : l->h);
	for (dock_node_t *n = l; n->parent != NULL; n = n->parent) {
		dock_node_t *s = n->parent;
		if (!s->a->visible || !s->b->visible) {
			continue;
		}
		if (s->vertical && dh != 0) {
			dh -= dock_split_resize_child(s, n, dh);
		}
		else if (!s->vertical && dw != 0) {
			dw -= dock_split_resize_child(s, n, dw);
		}
		dock_layout(ui_dock_root, ui_dock_root->x, ui_dock_root->y, ui_dock_root->w, ui_dock_root->h);
	}
}

void ui_dock_on_tab_context_menu(int *handle, i32 position) {
	dock_node_t *l  = dock_find_handle(ui_dock_root, handle);
	i32          id = l != NULL ? dock_leaf_open_tab(l, position) : -1;
	if (id == -1) {
		return;
	}
	ui_dock_context_tab = id;
	ui_menu_draw(&ui_dock_tab_context_menu_draw, -1, -1);
}

// Status bar toggles in between tab names only and the full height
bool ui_dock_status_minimized() {
	dock_node_t *l = dock_status_leaf();
	return l != NULL && l->visible && l->h <= ui_statusbar_default_h * g_config->window_scale;
}

void ui_base_toggle_browser() {
	dock_node_t *s = dock_find_role(ui_dock_root, DOCK_ROLE_STATUS);
	if (s == NULL) {
		return;
	}
	bool minimized = s->size <= ui_statusbar_default_h * g_config->window_scale;
	s->size        = math_floor((minimized ? 240 : ui_statusbar_default_h) * g_config->window_scale);
	base_resize();
	ui_dock_save();
}

void ui_sidebar_show(bool b) {
	dock_node_t *s = dock_find_role(ui_dock_root, DOCK_ROLE_SIDEBAR);
	if (s != NULL && s->collapsed == b) {
		s->collapsed = !b;
		ui_dock_layout();
		ui_dock_save();
	}
}

// ██████╗ ███████╗███╗   ██╗██████╗ ███████╗██████╗
// ██╔══██╗██╔════╝████╗  ██║██╔══██╗██╔════╝██╔══██╗
// ██████╔╝█████╗  ██╔██╗ ██║██║  ██║█████╗  ██████╔╝
// ██╔══██╗██╔══╝  ██║╚██╗██║██║  ██║██╔══╝  ██╔══██╗
// ██║  ██║███████╗██║ ╚████║██████╔╝███████╗██║  ██║
// ╚═╝  ╚═╝╚══════╝╚═╝  ╚═══╝╚═════╝ ╚══════╝╚═╝  ╚═╝

static void ui_dock_draw_version_tab(dock_node_t *l) {
	if (!g_config->touch_ui) {
		g_ui->enabled = false;
		ui_tab(&l->active, manifest_version, false, -1, false);
		g_ui->enabled = true;
	}
}

static void ui_dock_render_leaf(dock_node_t *l) {
	dock_node_t *status      = dock_status_leaf();
	dock_node_t *sidebar     = dock_find_role(ui_dock_root, DOCK_ROLE_SIDEBAR);
	dock_node_t *sidebar_top = sidebar != NULL ? dock_first_visible_leaf(sidebar->b) : NULL;
	bool         mini        = l->w <= ui_sidebar_w_mini;
	i32          count       = dock_leaf_open_count(l);

	i32 _SCROLL_W = g_theme->SCROLL_W;
	if (mini) {
		g_theme->SCROLL_W = g_theme->SCROLL_MINI_W;
	}

	bool drawn = ui_window(l->hwnd, l->x, l->y, l->w, l->h, false);
	if (drawn) {
		if (l == ui_dock_menubar_leaf) {
			g_ui->tab_offset_x = ui_menubar_w;
		}
		if (l == status) {
			g_ui->_y += 2;
			draw_set_color(g_theme->SEPARATOR_COL);
			draw_filled_rect(0, 0, 1, g_ui->_window_h);
			draw_filled_rect(g_ui->_window_w - 1, 0, 1, g_ui->_window_h);
		}

		for (i32 i = 0; i < l->tab_count; ++i) {
			tab_draw_t *tab = ui_base_get_tab(l->tabs[i]);
			if (dock_tab_open(l->tabs[i])) {
				tab->f(&l->active);
				if (mini) {
					break;
				}
			}
		}

		// Buttons drawn next to the tabs
		if (l == sidebar_top && !g_config->touch_ui && !mini) {
			ui_tab(&l->active, ">", false, -2, false);
		}
		if (l == status) {
			ui_dock_draw_version_tab(l);
			if (!g_config->touch_ui) {
				ui_tab(&l->active, ui_dock_status_minimized() ? "<" : ">", false, -2, false);
			}
		}
	}
	// Tabs are picked and the scrollbar is drawn when the window ends
	ui_end_window();
	g_theme->SCROLL_W = _SCROLL_W;

	if (drawn) {
		bool button = l->active >= count;
		if (button) {
			l->active = l->last_active;
		}
		l->last_active = l->active;

		// Collapse the sidebar
		if (l == sidebar_top && button) {
			ui_sidebar_show(false);
			base_resize();
		}
		// Minimize or maximize the status bar
		if (l == status) {
			if (ui_tab_changed(l->hwnd, &l->active) && (button || l->active == g_context->last_status_position || ui_dock_status_minimized())) {
				ui_base_toggle_browser();
			}
			g_context->last_status_position = l->active;
		}
	}
}

// Tab names above the 3D view
static void ui_dock_render_view3d_tabs(dock_node_t *l) {
	i32 strip_h = dock_view3d_strip_h(l);
	if (strip_h == 0) {
		return;
	}
	i32 item_w = ui_toolbar_w(true);
	i32 w      = l->w;

	// Split view, the second 3D view gets its own tab header
	ui_window_sync_redraws(ui_menubar_hwnd, ui_menubar_split_hwnd, base_is_split());
	if (base_is_split()) {
		i32 split_x = base_view_x(1) - item_w;
		w           = split_x - l->x;
		if (ui_window(ui_menubar_split_hwnd, split_x, l->y, l->x + l->w - split_x, strip_h, false)) {
			if (g_config->touch_ui) {
				ui_fill(0, 0, g_ui->_window_w, g_ui->_window_h + 4, g_theme->SEPARATOR_COL);
			}
			else {
				static i32 split_tab = 0;
				ui_tab(&split_tab, tr("3D View"), false, -1, false);
			}
		}
	}

	if (ui_window(ui_menubar_hwnd, l->x, l->y, w, strip_h, false)) {
		if (g_config->touch_ui) {
			ui_fill(0, 0, g_ui->_window_w, g_ui->_window_h + 4, g_theme->SEPARATOR_COL);
		}
		else {
			ui_dock_view_tabs(TAB_VIEW3D);
		}
	}
}

static void ui_dock_render_sidebar_buttons() {
	dock_node_t *s = dock_find_role(ui_dock_root, DOCK_ROLE_SIDEBAR);
	if (s == NULL) {
		return;
	}

	// Expand button
	if (s->collapsed) {
		i32 width = math_floor(draw_string_width(g_font, g_ui->font_size, "<") + 25 * UI_SCALE());
		if (ui_window(ui_sidebar_hminimized, iron_window_width() - width, -1, width, math_floor(UI_ELEMENT_H() + 4 * UI_SCALE()), false)) {
			ui_fill(0, 0, g_ui->_window_w, g_ui->_window_h + 1, g_theme->SEPARATOR_COL);
			g_ui->_w            = width;
			i32 _BUTTON_H       = g_theme->BUTTON_H;
			i32 _BUTTON_COL     = g_theme->BUTTON_COL;
			i32 _TEXT_COL       = g_theme->TEXT_COL;
			g_theme->BUTTON_H   = g_theme->ELEMENT_H;
			g_theme->BUTTON_COL = g_theme->SEPARATOR_COL;
			g_theme->TEXT_COL   = g_theme->HOVER_COL;

			if (ui_button("<", UI_ALIGN_CENTER, "")) {
				ui_sidebar_show(true);
				base_resize();
			}
			g_theme->BUTTON_H   = _BUTTON_H;
			g_theme->BUTTON_COL = _BUTTON_COL;
			g_theme->TEXT_COL   = _TEXT_COL;
		}
		return;
	}

	// Collapse / expand button for mini sidebar
	if (g_config->touch_ui && s->b->visible) {
		bool               mini   = s->b->w <= ui_sidebar_w_mini;
		i32                width  = s->b->w;
		i32                height = math_floor(UI_ELEMENT_H() + UI_ELEMENT_OFFSET());
		static ui_window_t window = {0};
		if (ui_window(&window, s->b->x, s->b->y + s->b->h - height, width, height + 1, false)) {
			g_ui->_w            = width;
			i32 _BUTTON_H       = g_theme->BUTTON_H;
			i32 _BUTTON_COL     = g_theme->BUTTON_COL;
			g_theme->BUTTON_H   = g_theme->ELEMENT_H;
			g_theme->BUTTON_COL = g_theme->WINDOW_BG_COL;
			if (ui_button(mini ? "<" : ">", UI_ALIGN_CENTER, "")) {
				s->size = math_floor((mini ? ui_sidebar_default_w_full : ui_sidebar_default_w_mini) * UI_SCALE());
				base_resize();
				ui_dock_save();
			}
			g_theme->BUTTON_H   = _BUTTON_H;
			g_theme->BUTTON_COL = _BUTTON_COL;
		}
	}
}

void ui_dock_render_ui() {
	dock_node_t *leaves[DOCK_MAX_LEAVES];
	i32          count = dock_collect_leaves(ui_dock_root, leaves, 0);
	for (i32 i = 0; i < count; ++i) {
		dock_node_t *l = leaves[i];
		if (!l->visible) {
			continue;
		}
		i32 active = dock_leaf_active_tab(l);
		if (active == TAB_VIEW3D) {
			ui_dock_render_view3d_tabs(l);
		}
		else if (!dock_tab_is_view(active)) {
			ui_dock_render_leaf(l);
		}
		if (ui_tab_changed(l->hwnd, &l->active) || (active == TAB_VIEW3D && ui_tab_changed(ui_menubar_hwnd, &l->active))) {
			ui_dock_save();
		}
	}
	ui_dock_render_sidebar_buttons();
}

// ██████╗ ██████╗  █████╗  ██████╗
// ██╔══██╗██╔══██╗██╔══██╗██╔════╝
// ██║  ██║██████╔╝███████║██║  ███╗
// ██║  ██║██╔══██╗██╔══██║██║   ██║
// ██████╔╝██║  ██║██║  ██║╚██████╔╝
// ╚═════╝ ╚═╝  ╚═╝╚═╝  ╚═╝ ╚═════╝

static i32 dock_leaf_tab_names_h(dock_node_t *l) {
	return dock_leaf_has_tab_names(l) ? ui_header_h : 0;
}

// Tab is being dragged over the docked tabs
static dock_node_t *dock_drag_leaf() {
	if (g_ui == NULL || g_ui->drag_tab_handle == NULL) {
		return NULL;
	}
	f32 dx = g_ui->input_x - g_ui->input_started_x;
	f32 dy = g_ui->input_y - g_ui->input_started_y;
	if (math_abs(dx) < 8 * UI_SCALE() && math_abs(dy) < 8 * UI_SCALE()) {
		return NULL;
	}
	return dock_find_handle(ui_dock_root, g_ui->drag_tab_handle);
}

// Leaf under the mouse with the side the tab is going to be docked at, tab names take the drop on their own
static dock_node_t *dock_drop_target(dock_zone_t *zone, i32 *x, i32 *y, i32 *w, i32 *h) {
	dock_node_t *leaves[DOCK_MAX_LEAVES];
	i32          count = dock_collect_leaves(ui_dock_root, leaves, 0);
	for (i32 i = 0; i < count; ++i) {
		dock_node_t *l       = leaves[i];
		i32          names_h = dock_leaf_tab_names_h(l);
		i32          by      = l->y + names_h;
		i32          bh      = l->h - names_h;
		if (!l->visible || bh <= 0 || mouse_x < l->x || mouse_x >= l->x + l->w || mouse_y < by || mouse_y >= by + bh) {
			continue;
		}
		f32 fx = (mouse_x - l->x) / (f32)l->w;
		f32 fy = (mouse_y - by) / (f32)bh;
		*x     = l->x;
		*y     = by;
		*w     = l->w;
		*h     = bh;
		*zone  = DOCK_ZONE_CENTER;
		if (fx < 0.25 && fx < fy && fx < 1.0 - fy) {
			*zone = DOCK_ZONE_LEFT;
			*w    = l->w / 2;
		}
		else if (fx > 0.75 && 1.0 - fx < fy && 1.0 - fx < 1.0 - fy) {
			*zone = DOCK_ZONE_RIGHT;
			*x    = l->x + l->w / 2;
			*w    = l->w - l->w / 2;
		}
		else if (fy < 0.25) {
			*zone = DOCK_ZONE_TOP;
			*h    = bh / 2;
		}
		else if (fy > 0.75) {
			*zone = DOCK_ZONE_BOTTOM;
			*y    = by + bh / 2;
			*h    = bh - bh / 2;
		}
		return l;
	}
	return NULL;
}

bool ui_dock_is_dragging() {
	return dock_drag_leaf() != NULL;
}

// Highlight the place the dragged tab is going to be docked at
void ui_dock_render_drag() {
	if (dock_drag_leaf() == NULL) {
		return;
	}
	dock_zone_t  zone;
	i32          x, y, w, h;
	dock_node_t *l = dock_drop_target(&zone, &x, &y, &w, &h);
	if (l != NULL) {
		draw_begin(NULL, false, 0);
		draw_set_color((g_theme->HIGHLIGHT_COL & 0x00ffffff) | 0x55000000);
		draw_filled_rect(x, y, w, h);
		draw_set_color(g_theme->HIGHLIGHT_COL);
		draw_rect(x + 1, y + 1, w - 2, h - 2, 2.0);
		draw_set_color(0xffffffff);
		draw_end();
	}
	// Draw over the highlight of the last frame
	base_redraw_ui();
}

static dock_node_t *dock_hovered_split(dock_node_t *n) {
	if (n == NULL || dock_is_leaf(n) || !n->visible) {
		return NULL;
	}
	if (n->a->visible && n->b->visible) {
		i32 grip = 4;
		if (n->vertical && mouse_x >= n->x && mouse_x < n->x + n->w && math_abs(mouse_y - n->b->y) <= grip) {
			return n;
		}
		if (!n->vertical && mouse_y >= n->y && mouse_y < n->y + n->h && math_abs(mouse_x - n->b->x) <= grip) {
			return n;
		}
	}
	dock_node_t *s = dock_hovered_split(n->a);
	return s != NULL ? s : dock_hovered_split(n->b);
}

static void ui_dock_update_resize() {
	if (ui_dock_resizing != NULL) {
		dock_node_t *n = ui_dock_resizing;
		if (!mouse_down("left")) {
			ui_dock_resizing = NULL;
			base_is_resizing = false;
			if (n->size != ui_dock_resizing_size) {
				ui_dock_save();
			}
			return;
		}
		i32 total = n->vertical ? n->h : n->w;
		i32 size  = n->vertical ? mouse_y - n->y : mouse_x - n->x;
		i32 min   = dock_split_min_size(n);
		if (size > total - min) {
			size = total - min;
		}
		if (size < DOCK_MIN_SIZE) {
			size = DOCK_MIN_SIZE;
		}
		f32 last = n->size;
		n->size  = n->fixed == 1 ? size : n->fixed == 2 ? total - size : size / (f32)total;
        n->vertical ? iron_mouse_set_cursor(IRON_CURSOR_SIZENS) : iron_mouse_set_cursor(IRON_CURSOR_SIZEWE);
		if (last != n->size) {
			base_resize();
		}
		return;
	}

	if (!base_ui_enabled || !ui_base_show || base_is_dragging || g_ui->drag_tab_handle != NULL) {
		return;
	}
	dock_node_t *n = dock_hovered_split(ui_dock_root);
	if (n == NULL || (mouse_down("left") && !mouse_started("left"))) {
		return;
	}
	n->vertical ? iron_mouse_set_cursor(IRON_CURSOR_SIZENS) : iron_mouse_set_cursor(IRON_CURSOR_SIZEWE);
	if (mouse_started("left")) {
		ui_dock_resizing      = n;
		ui_dock_resizing_size = n->size;
		base_is_resizing      = true;
	}
}

// Runs before the windows of the frame are drawn
void ui_dock_update() {
	if (ui_dock_root == NULL) {
		return;
	}

	// Tab dropped on the tab names
	if (ui_dock_drop_to != NULL) {
		dock_node_t *to   = dock_find_handle(ui_dock_root, ui_dock_drop_to);
		dock_node_t *from = dock_find_handle(ui_dock_root, ui_dock_drop_from);
		i32          id   = from != NULL ? dock_leaf_open_tab(from, ui_dock_drop_from_position) : -1;
		bool         same = to == from && ui_dock_drop_to_position == ui_dock_drop_from_position;
		ui_dock_drop_to   = NULL;
		if (to != NULL && id != -1 && !same) {
			dock_move_tab(id, to, DOCK_ZONE_CENTER, ui_dock_drop_to_position);
			base_resize();
			ui_dock_save();
		}
	}

	// Tab dropped into the tab contents
	dock_node_t *from = dock_drag_leaf();
	if (from != NULL && g_ui->input_released) {
		dock_zone_t  zone;
		i32          x, y, w, h;
		dock_node_t *to = dock_drop_target(&zone, &x, &y, &w, &h);
		i32          id = dock_leaf_open_tab(from, g_ui->drag_tab_position);
		if (to != NULL && id != -1 && !(to == from && zone == DOCK_ZONE_CENTER)) {
			dock_move_tab(id, to, zone, -1);
			base_resize();
			ui_dock_save();
		}
	}

	// Tab resized from the context menu
	if (ui_dock_resize_tab != -1) {
		dock_node_t *l     = dock_find_leaf(ui_dock_root, ui_dock_resize_tab);
		i32          id    = ui_dock_resize_tab;
		ui_dock_resize_tab = -1;
		if (l != NULL && l->visible) {
			ui_dock_layout();
			dock_resize_leaf(l, id, ui_dock_resize_w, ui_dock_resize_h);
			base_resize();
			ui_dock_save();
		}
	}

	ui_dock_update_resize();
	ui_dock_layout();
}
