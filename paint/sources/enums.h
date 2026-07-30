
#pragma once

typedef enum {
	SHORTCUT_TYPE_STARTED,
	SHORTCUT_TYPE_REPEAT,
	SHORTCUT_TYPE_DOWN,
	SHORTCUT_TYPE_RELEASED,
} shortcut_type_t;

typedef enum {
	BORDER_SIDE_LEFT   = 0,
	BORDER_SIDE_RIGHT  = 1,
	BORDER_SIDE_TOP    = 2,
	BORDER_SIDE_BOTTOm = 3,
} border_side_t;

typedef enum {
	BAKE_TYPE_INIT          = -1,
	BAKE_TYPE_CURVATURE     = 0,
	BAKE_TYPE_NORMAL        = 1,
	BAKE_TYPE_NORMAL_OBJECT = 2,
	BAKE_TYPE_HEIGHT        = 3,
	BAKE_TYPE_DERIVATIVE    = 4,
	BAKE_TYPE_POSITION      = 5,
	BAKE_TYPE_TEXCOORD      = 6,
	BAKE_TYPE_MATERIALID    = 7,
	BAKE_TYPE_OBJECTID      = 8,
	BAKE_TYPE_VERTEX_COLOR  = 9,
	BAKE_TYPE_OCCLUSION     = 10,
	BAKE_TYPE_LIGHTMAP      = 11,
	BAKE_TYPE_BENT_NORMAL   = 12,
	BAKE_TYPE_THICKNESS     = 13,
} bake_type_t;

typedef enum {
	SPLIT_TYPE_OBJECT   = 0,
	SPLIT_TYPE_MATERIAL = 1,
	SPLIT_TYPE_UDIM     = 2,
} split_type_t;

typedef enum {
	BAKE_AXIS_XYZ = 0,
	BAKE_AXIS_X   = 1,
	BAKE_AXIS_Y   = 2,
	BAKE_AXIS_Z   = 3,
	BAKE_AXIS_MX  = 4,
	BAKE_AXIS_MY  = 5,
	BAKE_AXIS_MZ  = 6,
} bake_axis_t;

typedef enum {
	BAKE_UP_AXIS_Z = 0,
	BAKE_UP_AXIS_Y = 1,
	BAKE_UP_AXIS_X = 2,
} bake_up_axis_t;

typedef enum {
	VIEWPORT_MODE_NONE          = -1,
	VIEWPORT_MODE_LIT           = 0,
	VIEWPORT_MODE_BASE_COLOR    = 1,
	VIEWPORT_MODE_NORMAL_MAP    = 2,
	VIEWPORT_MODE_OCCLUSION     = 3,
	VIEWPORT_MODE_ROUGHNESS     = 4,
	VIEWPORT_MODE_METALLIC      = 5,
	VIEWPORT_MODE_OPACITY       = 6,
	VIEWPORT_MODE_HEIGHT        = 7,
	VIEWPORT_MODE_EMISSION      = 8,
	VIEWPORT_MODE_SUBSURFACE    = 9,
	VIEWPORT_MODE_TEXCOORD      = 10,
	VIEWPORT_MODE_OBJECT_NORMAL = 11,
	VIEWPORT_MODE_MATERIAL_ID   = 12,
	VIEWPORT_MODE_OBJECT_ID     = 13,
	VIEWPORT_MODE_MASK          = 14,
	VIEWPORT_MODE_PATH_TRACE    = 15,
} viewport_mode_t;

typedef enum {
	WORKSPACE_PAINT_3D = 0,
	WORKSPACE_PAINT_2D = 1,
	WORKSPACE_NODES    = 2,
	WORKSPACE_SCRIPT   = 3,
	WORKSPACE_PLAYER   = 4,
} workspace_t;

typedef enum {
	WORKFLOW_PBR    = 0,
	WORKFLOW_BASE   = 1,
	WORKFLOW_SCULPT = 2,
} workflow_t;

typedef enum {
	RENDER_MODE_DEFERRED   = 0,
	RENDER_MODE_FORWARD    = 1,
	RENDER_MODE_PATH_TRACE = 2,
} render_mode_t;

typedef enum {
	EXPORT_MODE_VISIBLE       = 0,
	EXPORT_MODE_SELECTED      = 1,
	EXPORT_MODE_PER_OBJECT    = 2,
	EXPORT_MODE_PER_UDIM_TILE = 3,
} export_mode_t;

typedef enum {
	EXPORT_DESTINATION_DISK              = 0,
	EXPORT_DESTINATION_PACK_INTO_PROJECT = 1,
} export_destination_t;

typedef enum {
	PATHTRACE_MODE_FAST    = 0,
	PATHTRACE_MODE_QUALITY = 1,
} pathtrace_mode_t;

typedef enum {
	FILL_TYPE_OBJECT    = 0,
	FILL_TYPE_FACE      = 1,
	FILL_TYPE_ANGLE     = 2,
	FILL_TYPE_UV_ISLAND = 3,
} fill_type_t;

typedef enum {
	UV_TYPE_UVMAP     = 0,
	UV_TYPE_TRIPLANAR = 1,
	UV_TYPE_PROJECT   = 2,
} uv_type_t;

typedef enum {
	SCULPT_TYPE_DRAW = 0,
	SCULPT_TYPE_GRAB = 1,
} sculpt_type_t;

typedef enum {
	BLEND_TYPE_MIX          = 0,
	BLEND_TYPE_DARKEN       = 1,
	BLEND_TYPE_MULTIPLY     = 2,
	BLEND_TYPE_BURN         = 3,
	BLEND_TYPE_LIGHTEN      = 4,
	BLEND_TYPE_SCREEN       = 5,
	BLEND_TYPE_DODGE        = 6,
	BLEND_TYPE_ADD          = 7,
	BLEND_TYPE_OVERLAY      = 8,
	BLEND_TYPE_SOFT_LIGHT   = 9,
	BLEND_TYPE_LINEAR_LIGHT = 10,
	BLEND_TYPE_DIFFERENCE   = 11,
	BLEND_TYPE_SUBTRACT     = 12,
	BLEND_TYPE_DIVIDE       = 13,
	BLEND_TYPE_HUE          = 14,
	BLEND_TYPE_SATURATION   = 15,
	BLEND_TYPE_COLOR        = 16,
	BLEND_TYPE_VALUE        = 17,
} blend_type_t;

typedef enum {
	CAMERA_CONTROLS_NONE   = -1,
	CAMERA_CONTROLS_ORBIT  = 0,
	CAMERA_CONTROLS_ROTATE = 1,
	CAMERA_CONTROLS_FLY    = 2,
} camera_controls_t;

typedef enum {
	CAMERA_PIVOT_CURSOR = 0,
	CAMERA_PIVOT_CENTER = 1,
} camera_pivot_t;

typedef enum {
	CAMERA_TYPE_PERSPECTIVE  = 0,
	CAMERA_TYPE_ORTHOGRAPHIC = 1,
} camera_type_t;

typedef enum {
	TEXTURE_BITS_BITS8  = 0,
	TEXTURE_BITS_BITS16 = 1,
	TEXTURE_BITS_BITS32 = 2,
} texture_bits_t;

typedef enum {
	TEXTURE_LDR_FORMAT_PNG = 0,
	TEXTURE_LDR_FORMAT_JPG = 1,
} texture_ldr_format_t;

typedef enum {
	MESH_FORMAT_OBJ = 0,
	MESH_FORMAT_ARM = 1,
	MESH_FORMAT_GLB = 2,
} mesh_format_t;

typedef enum {
	CANVAS_TYPE_MATERIAL = 0,
	CANVAS_TYPE_BRUSH    = 1,
} canvas_type_t;

typedef enum {
	VIEW_2D_TYPE_ASSET = 0,
	VIEW_2D_TYPE_NODE  = 1,
	VIEW_2D_TYPE_FONT  = 2,
	VIEW_2D_TYPE_LAYER = 3,
	VIEW_2D_TYPE_UVMAP = 4,
} view_2d_type_t;

typedef enum {
	VIEW_2D_LAYER_MODE_VISIBLE  = 0,
	VIEW_2D_LAYER_MODE_SELECTED = 1,
} view_2d_layer_mode_t;

typedef enum {
	PAINT_TEX_BASE      = 0,
	PAINT_TEX_OPACITY   = 1,
	PAINT_TEX_NORMAL    = 2,
	PAINT_TEX_OCCLUSION = 3,
	PAINT_TEX_ROUGHNESS = 4,
	PAINT_TEX_METALLIC  = 5,
	PAINT_TEX_HEIGHT    = 6,
} paint_tex_t;

typedef enum {
	ZOOM_DIRECTION_VERTICAL                     = 0,
	ZOOM_DIRECTION_VERTICAL_INVERTED            = 1,
	ZOOM_DIRECTION_HORIZONTAL                   = 2,
	ZOOM_DIRECTION_HORIZONTAL_INVERTED          = 3,
	ZOOM_DIRECTION_VERTICAL_HORIZONTAL          = 4,
	ZOOM_DIRECTION_VERTICAL_HORIZONTAL_INVERTED = 5,
} zoom_direction_t;

typedef enum {
	LAYER_SLOT_TYPE_LAYER = 0,
	LAYER_SLOT_TYPE_MASK  = 1,
	LAYER_SLOT_TYPE_GROUP = 2,
} layer_slot_type_t;

typedef enum {
	TOOL_TYPE_BRUSH    = 0,
	TOOL_TYPE_ERASER   = 1,
	TOOL_TYPE_FILL     = 2,
	TOOL_TYPE_DECAL    = 3,
	TOOL_TYPE_TEXT     = 4,
	TOOL_TYPE_CLONE    = 5,
	TOOL_TYPE_BLUR     = 6,
	TOOL_TYPE_PARTICLE = 7,
	TOOL_TYPE_COLORID  = 8,
	TOOL_TYPE_PICKER   = 9,
	TOOL_TYPE_MATERIAL = 10,
	TOOL_TYPE_CURSOR   = 11,
	TOOL_TYPE_SELECT   = 12,
	TOOL_TYPE_BAKE     = 13, // Hidden, used by Bake Texture node
} tool_type_t;

typedef enum {
	BLUR_TYPE_BLUR   = 0,
	BLUR_TYPE_SMUDGE = 1,
} blur_type_t;

typedef enum {
	TAB_AREA_SIDEBAR0 = 0,
	TAB_AREA_SIDEBAR1 = 1,
	TAB_AREA_STATUS   = 2,
} tab_area_t;

typedef enum {
	TEXTURE_RES_RES2048  = 0,
	TEXTURE_RES_RES4096  = 1,
	TEXTURE_RES_RES8192  = 2,
	TEXTURE_RES_RES16384 = 3,
	TEXTURE_RES_CUSTOM   = 4,
} texture_res_t;

typedef enum {
	CONSOLE_MODEL_QWEN   = 0,
	CONSOLE_MODEL_CLAUDE = 1,
	CONSOLE_MODEL_GROK   = 2,
} console_model_t;

typedef enum {
	LAYOUT_SIZE_SIDEBAR_W  = 0,
	LAYOUT_SIZE_SIDEBAR_H0 = 1,
	LAYOUT_SIZE_SIDEBAR_H1 = 2,
	LAYOUT_SIZE_NODES_W    = 3,
	LAYOUT_SIZE_NODES_H    = 4,
	LAYOUT_SIZE_STATUS_H   = 5,
	LAYOUT_SIZE_HEADER     = 6, // 0 - hidden, 1 - visible
} layout_size_t;

typedef enum {
	PREFERENCES_TAB_INTERFACE = 0,
	PREFERENCES_TAB_THEME     = 1,
	PREFERENCES_TAB_USAGE     = 2,
	PREFERENCES_TAB_CAMERA    = 3,
	PREFERENCES_TAB_PEN       = 4,
	PREFERENCES_TAB_VIEWPORT  = 5,
	PREFERENCES_TAB_KEYMAP    = 6,
	PREFERENCES_TAB_NEURAL    = 7,
	PREFERENCES_TAB_PLUGINS   = 8,
} preferences_tab_t;

typedef enum {
	ICON18_EYE_ON  = 0,
	ICON18_EYE_OFF = 1,
} icon18_t;

typedef enum {
	ICON_NONE             = -1,
	ICON_BRUSH            = 0,
	ICON_ERASER           = 1,
	ICON_FILL             = 2,
	ICON_DECAL            = 3,
	ICON_TEXT             = 4,
	ICON_CLONE            = 5,
	ICON_BLUR             = 6,
	ICON_PARTICLE         = 7,
	ICON_COLOR_ID         = 8,
	ICON_PICKER           = 9,
	ICON_MATERIAL         = 10,
	ICON_CURSOR           = 11,
	ICON_DROP             = 12,
	ICON_MATERIAL_PREVIEW = 13,
	ICON_FOLDER_FULL      = 14,
	ICON_FILE             = 15,
	ICON_CHECKER          = 16,
	ICON_BRUSH_PREVIEW    = 17,
	ICON_TEXT_PREVIEW     = 18,
	ICON_PROPERTIES       = 19,
	ICON_FOLDER_OPEN      = 20,
	ICON_BAKE             = 21,
	ICON_SMUDGE           = 22,
	ICON_SELECT           = 23,
	ICON_MENU             = 24,
	ICON_FILE_NEW         = 25,
	ICON_FOLDER           = 26,
	ICON_SAVE             = 27,
	ICON_IMPORT           = 28,
	ICON_EXPORT           = 29,
	ICON_UNDO             = 30,
	ICON_REDO             = 31,
	ICON_IMAGE            = 32,
	ICON_SUN              = 33,
	ICON_CAMERA           = 34,
	ICON_HELP             = 35,
	ICON_PROJECTS         = 36,
	ICON_DELETE           = 37,
	ICON_SEARCH           = 38,
	ICON_HOME             = 39,
	ICON_CLOSE            = 40,
	ICON_COG              = 41,
	ICON_HASH             = 42,
	ICON_PLUS             = 43,
	ICON_CHEVRON_LEFT     = 44,
	ICON_CHEVRON_RIGHT    = 45,
	ICON_ARROW_LEFT       = 46,
	ICON_ARROW_RIGHT      = 47,
	ICON_ARROW_UP         = 48,
	ICON_ARROW_DOWN       = 49,
	ICON_DOWN             = 50,
	ICON_UP               = 51,
	ICON_CHECK            = 52,
	ICON_REFRESH          = 53,
	ICON_MINUS            = 54,
	ICON_NEW              = 55,
	ICON_CLEAR            = 56,
	ICON_RESIZE           = 57,
	ICON_FULLSCREEN       = 58,
	ICON_DUPLICATE        = 59,
	ICON_CHEVRON_UP       = 60,
	ICON_CHEVRON_DOWN     = 61,
	ICON_LEFT             = 62,
	ICON_RIGHT            = 63,
	ICON_MOVE             = 64,
	ICON_FILE_OPEN        = 65,
	ICON_FOLDER_NEW       = 66,
	ICON_DOWNLOADING      = 67,
	ICON_PUBLISH          = 68,
	ICON_ZOOM_OUT         = 69,
	ICON_ZOOM_IN          = 70,
	ICON_DRAFT            = 71,
	ICON_STAR             = 72,
	ICON_CUBE             = 73,
	ICON_PAINT            = 74,
	ICON_PALETTE          = 75,
	ICON_LAYER            = 76,
	ICON_LAYER_NEW        = 77,
	ICON_WINDOW           = 78,
	ICON_GESTURE          = 79,
	ICON_NODES            = 80,
	ICON_CURVE            = 81,
	ICON_PATH             = 82,
	ICON_ACCOUNT          = 83,
	ICON_ARROW_UP_LEFT    = 84,
	ICON_GIZMO            = 85,
	ICON_BLOCK            = 86,
	ICON_LANDSCAPE        = 87,
	ICON_CHAT             = 88,
	ICON_EGG              = 89,
	ICON_CLOUD            = 90,
	ICON_PICKER2          = 91,
	ICON_COPY             = 92,
	ICON_CUT              = 93,
	ICON_PASTE            = 94,
	ICON_CROP             = 95,
	ICON_EDIT             = 96,
	ICON_EXIT             = 97,
	ICON_FILTER           = 98,
	ICON_PLUGIN           = 99,
	ICON_ERASE            = 100,
	ICON_FILE_SAVE        = 101,
	ICON_FONT             = 102,
	ICON_HISTORY          = 103,
	ICON_SPHERE           = 104,
	ICON_INFO             = 105,
	ICON_SYNC             = 106,
	ICON_INVERT           = 107,
	ICON_LAYERS           = 108,
	ICON_LINK             = 109,
	ICON_LOCK             = 110,
	ICON_STICKER          = 111,
	ICON_MASK             = 112,
	ICON_DISPLAY          = 113,
	ICON_MOVIE            = 114,
	ICON_MUSIC            = 115,
	ICON_NEW2             = 116,
	ICON_OPACITY          = 117,
	ICON_MASK_WHITE       = 118,
	ICON_PAUSE            = 119,
	ICON_PHOTO            = 120,
	ICON_PLAY             = 121,
	ICON_REPLAY           = 122,
	ICON_SAVE_AS          = 123,
	ICON_TIMER            = 124,
	ICON_MASK_FILL        = 125,
	ICON_RULER            = 126,
	ICON_STOP             = 127,
	ICON_STORAGE          = 128,
	ICON_TERMINAL         = 129,
	ICON_TRANSLATE        = 130,
	ICON_UPLOAD           = 131,
	ICON_RESERVED0        = 132,
	ICON_RESERVED1        = 133,
	ICON_RESERVED2        = 134,
	ICON_RESERVED3        = 135,
	ICON_RESERVED4        = 136,
	ICON_WORKSPACE        = 137,
	ICON_UVSPHERE         = 138,
	ICON_PLANE            = 139,
	ICON_TORUS            = 140,
	ICON_CYLINDER         = 141,
	ICON_CONE             = 142,
	ICON_BOX              = 143,
} icon_t;

typedef enum {
	COLOR_SPACE_AUTO, // sRGB for base color, otherwise linear
	COLOR_SPACE_LINEAR,
	COLOR_SPACE_SRGB,
	COLOR_SPACE_DIRECTX_NORMAL_MAP,
} color_space_t;

typedef enum {
	HISTORY_ACTION_PAINT,
	HISTORY_ACTION_EDIT_NODES,
	HISTORY_ACTION_NEW_LAYER,
	HISTORY_ACTION_NEW_BLACK_MASK,
	HISTORY_ACTION_NEW_WHITE_MASK,
	HISTORY_ACTION_NEW_FILL_MASK,
	HISTORY_ACTION_NEW_GROUP,
	HISTORY_ACTION_DELETE_LAYER,
	HISTORY_ACTION_CLEAR_LAYER,
	HISTORY_ACTION_DUPLICATE_LAYER,
	HISTORY_ACTION_ORDER_LAYERS,
	HISTORY_ACTION_MERGE_LAYERS,
	HISTORY_ACTION_APPLY_MASK,
	HISTORY_ACTION_INVERT_MASK,
	HISTORY_ACTION_APPLY_FILTER,
	HISTORY_ACTION_TO_FILL_LAYER,
	HISTORY_ACTION_TO_FILL_MASK,
	HISTORY_ACTION_TO_PAINT_LAYER,
	HISTORY_ACTION_TO_PAINT_MASK,
	HISTORY_ACTION_LAYER_OPACITY,
	HISTORY_ACTION_LAYER_BLENDING,
	HISTORY_ACTION_DELETE_NODE_GROUP,
	HISTORY_ACTION_NEW_MATERIAL,
	HISTORY_ACTION_DELETE_MATERIAL,
	HISTORY_ACTION_DUPLICATE_MATERIAL,
} history_action_t;

typedef enum {
	OPACITY_MODE_ALPHA,
	OPACITY_MODE_TRANSLUC,
} opacity_mode_t;
