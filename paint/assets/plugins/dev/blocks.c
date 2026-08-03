
int COLS = 10;
int ROWS = 20;
int cur_type;
int cur_rot;
int cur_x;
int cur_y;
float fall_timer;
float fall_speed;
float cell_size;
float block_scale;
slot_material_t *block_mat;
i32_array_t *board;
i32_array_t *masks;
any_array_t *cells;
any_array_t *active;

float type_hue(int type) {
    return type * 0.142857;
}

void setup_colors() {
    block_mat = script_material_create("blocks");
    ui_node_t *info = script_material_create_node_at("OBJECT_INFO", -600.0, 0.0);
    ui_node_t *hsv = script_material_create_node_at("HUE_SAT", -300.0, 0.0);
    ui_node_t *out = script_material_get_node("OUTPUT_MATERIAL_PBR");
    script_material_set_color(hsv, 1, 4, 1.0, 0.0, 0.0, 1.0);
    script_material_connect(info, 3, hsv, 0);
    script_material_connect(hsv, 0, out, 0);
    script_material_update();
}

int mask_get(int type, int rot) {
    return masks->buffer[type * 4 + rot];
}

bool piece_fits(int type, int rot, int px, int py) {
    int m = mask_get(type, rot);
    for (int i = 0; i < 16; ++i) {
        if ((m & (32768 >> i)) == 0) continue;
        int cx = px + (i % 4);
        int cy = py + (i / 4);
        if (cx < 0 || cx > COLS - 1 || cy > ROWS - 1) return false;
        if (cy < 0) continue;
        if (board->buffer[cy * COLS + cx] != 0) return false;
    }
    return true;
}

void board_clear() {
    for (int i = 0; i < ROWS * COLS; ++i) board->buffer[i] = 0;
}

void spawn_piece() {
    cur_type = iron_random_get_max(6);
    cur_rot = 0;
    cur_x = 3;
    cur_y = 0;
    if (!piece_fits(cur_type, cur_rot, cur_x, cur_y)) board_clear();
}

void clear_lines() {
    int y = ROWS - 1;
    while (y > -1) {
        int full = 1;
        for (int x = 0; x < COLS; ++x) {
            if (board->buffer[y * COLS + x] == 0) full = 0;
        }
        if (full == 0) {
            y = y - 1;
        }
        else {
            for (int yy = y; yy > 0; --yy) {
                for (int x = 0; x < COLS; ++x) {
                    board->buffer[yy * COLS + x] = board->buffer[(yy - 1) * COLS + x];
                }
            }
            for (int x = 0; x < COLS; ++x) board->buffer[x] = 0;
        }
    }
}

void lock_piece() {
    int m = mask_get(cur_type, cur_rot);
    for (int i = 0; i < 16; ++i) {
        if ((m & (32768 >> i)) == 0) continue;
        int cy = cur_y + (i / 4);
        if (cy < 0) continue;
        board->buffer[cy * COLS + cur_x + (i % 4)] = cur_type + 1;
    }
    clear_lines();
    spawn_piece();
}

void set_block(object_t *o, int cx, int cy, int visible) {
    transform_t *t = o->transform;
    o->visible = visible;
    t->loc.x = (cx - COLS * 0.5) * cell_size;
    t->loc.y = 0.0;
    t->loc.z = (ROWS * 0.5 - cy) * cell_size;
    t->loc.w = 1.0;
    t->rot.x = 0.0;
    t->rot.y = 0.0;
    t->rot.z = 0.0;
    t->rot.w = 1.0;
    t->scale.x = block_scale;
    t->scale.y = block_scale;
    t->scale.z = block_scale;
    t->scale.w = 1.0;
    t->scale_world = 1.0;
    transform_build_matrix(t);
}

void refresh() {
    for (int i = 0; i < ROWS * COLS; ++i) {
        object_t *o = cells->buffer[i];
        int v = board->buffer[i];
        o->visible = v != 0;
        if (v != 0) o->urandom = type_hue(v - 1);
    }

    int m = mask_get(cur_type, cur_rot);
    int n = 0;
    for (int j = 0; j < 16; ++j) {
        if ((m & (32768 >> j)) == 0) continue;
        object_t *a = active->buffer[n];
        a->urandom = type_hue(cur_type);
        set_block(a, cur_x + (j % 4), cur_y + (j / 4), 1);
        n = n + 1;
    }
}

void try_move(int dx) {
    if (piece_fits(cur_type, cur_rot, cur_x + dx, cur_y)) cur_x = cur_x + dx;
}

bool rotate_to(int rot, int x) {
    if (!piece_fits(cur_type, rot, x, cur_y)) return false;
    cur_rot = rot;
    cur_x = x;
    return true;
}

void try_rotate() {
    int r = (cur_rot + 1) % 4;
    if (rotate_to(r, cur_x)) return;
    if (rotate_to(r, cur_x - 1)) return;
    rotate_to(r, cur_x + 1);
}

void step_down() {
    if (piece_fits(cur_type, cur_rot, cur_x, cur_y + 1)) cur_y = cur_y + 1;
    else lock_piece();
}

void update() {
    if (keyboard_started("left")) try_move(-1);
    if (keyboard_started("right")) try_move(1);
    if (keyboard_started("up")) try_rotate();
    if (keyboard_started("space")) {
        while (piece_fits(cur_type, cur_rot, cur_x, cur_y + 1)) cur_y = cur_y + 1;
        lock_piece();
    }

    float speed = fall_speed;
    if (keyboard_down("down")) speed = 0.05;
    fall_timer = fall_timer + sys_delta();
    if (fall_timer > speed) {
        fall_timer = 0.0;
        step_down();
    }
    refresh();
}

void main() {
    cell_size = 0.25;
    block_scale = 0.11;
    fall_speed = 0.5;
    fall_timer = 0.0;

    board = i32_array_create(ROWS * COLS);
    board_clear();

    masks = i32_array_create(28);
    masks->buffer[0] = 0x0F00; masks->buffer[1] = 0x2222; masks->buffer[2] = 0x00F0; masks->buffer[3] = 0x4444;
    masks->buffer[4] = 0x8E00; masks->buffer[5] = 0x6440; masks->buffer[6] = 0x0E20; masks->buffer[7] = 0x44C0;
    masks->buffer[8] = 0x2E00; masks->buffer[9] = 0x4460; masks->buffer[10] = 0x0E80; masks->buffer[11] = 0xC440;
    masks->buffer[12] = 0x6600; masks->buffer[13] = 0x6600; masks->buffer[14] = 0x6600; masks->buffer[15] = 0x6600;
    masks->buffer[16] = 0x6C00; masks->buffer[17] = 0x4620; masks->buffer[18] = 0x06C0; masks->buffer[19] = 0x8C40;
    masks->buffer[20] = 0x4E00; masks->buffer[21] = 0x4640; masks->buffer[22] = 0x0E40; masks->buffer[23] = 0x4C40;
    masks->buffer[24] = 0xC600; masks->buffer[25] = 0x2640; masks->buffer[26] = 0x0C60; masks->buffer[27] = 0x4C80;

    setup_colors();

    object_t *proto = script_shape_add("box");
    mesh_object_t *proto_mo = proto->ext;
    mesh_data_t *shared_data = proto_mo->data;
    material_data_t *shared_mat = proto_mo->material;

    cells = any_array_create(ROWS * COLS);
    for (int i = 0; i < ROWS * COLS; ++i) {
        object_t *o = proto;
        if (i > 0) {
            mesh_object_t *mo = scene_add_mesh_object(shared_data, shared_mat, NULL);
            o = mo->base;
        }
        cells->buffer[i] = o;
        script_object_set_material(o, block_mat);
        set_block(o, i % COLS, i / COLS, 0);
    }

    active = any_array_create(4);
    for (int j = 0; j < 4; ++j) {
        mesh_object_t *mo = scene_add_mesh_object(shared_data, shared_mat, NULL);
        active->buffer[j] = mo->base;
        script_object_set_material(mo->base, block_mat);
        set_block(mo->base, 0, 0, 0);
    }
    spawn_piece();
    refresh();
    script_notify_on_update(update);
}
