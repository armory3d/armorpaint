import { mkdir, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { encode } from '../src/arm.js';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const OUT = path.resolve(__dirname, '..', '..', 'paint', 'assets', 'scenes', 'gold_cube.arm');

const GOLD = [1.0, 0.84, 0.0, 1.0];        // AARRGGBB 0xffffd700
const ROUGH = 0.15;
const METAL = 1.0;

// ---- material canvas (mirrors default_material.arm, gold metallic) ----

const f32 = (...xs) => Float32Array.from(xs);

function socket({ id, nodeId, name, type, color, defaultValue, min = 0, max = 1, precision = 100 }) {
	return {
		id,
		node_id: nodeId,
		name,
		type,
		color,
		default_value: f32(...defaultValue),
		min,
		max,
		precision,
		display: 0,
	};
}

function button({ name, type, output, defaultValue }) {
	return {
		name,
		type,
		output,
		default_value: f32(...defaultValue),
		data: null,
		min: 0,
		max: 1,
		precision: 100,
		height: 0,
	};
}

// node id 1: color source
const colorNode = {
	id: 1,
	name: 'Color',
	type: 'RGB',
	x: 122,
	y: 100,
	color: -5025958,
	inputs: [],
	outputs: [
		socket({ id: 0, nodeId: 1, name: 'Color', type: 'RGBA', color: -3684567, defaultValue: GOLD }),
	],
	buttons: [button({ name: 'default_value', type: 'RGBA', output: 0, defaultValue: GOLD })],
	width: 0,
	flags: 0,
};

// node id 0: output
const outputNode = {
	id: 0,
	name: 'Material Output',
	type: 'OUTPUT_MATERIAL_PBR',
	x: 386,
	y: 100,
	color: -5025958,
	inputs: [
		socket({ id: 0, nodeId: 0, name: 'Base Color', type: 'RGBA', color: -3684567, defaultValue: GOLD }),
		socket({ id: 1, nodeId: 0, name: 'Opacity', type: 'VALUE', color: -6184543, defaultValue: [1] }),
		socket({ id: 2, nodeId: 0, name: 'Occlusion', type: 'VALUE', color: -6184543, defaultValue: [1] }),
		socket({ id: 3, nodeId: 0, name: 'Roughness', type: 'VALUE', color: -6184543, defaultValue: [ROUGH] }),
		socket({ id: 4, nodeId: 0, name: 'Metallic', type: 'VALUE', color: -6184543, defaultValue: [METAL] }),
		socket({ id: 5, nodeId: 0, name: 'Normal Map', type: 'VECTOR', color: -10238109, defaultValue: [0.5, 0.5, 1] }),
		socket({ id: 6, nodeId: 0, name: 'Emission', type: 'VALUE', color: -6184543, defaultValue: [0] }),
		socket({ id: 7, nodeId: 0, name: 'Height', type: 'VALUE', color: -6184543, defaultValue: [0] }),
		socket({ id: 8, nodeId: 0, name: 'Subsurface', type: 'VALUE', color: -6184543, defaultValue: [0] }),
	],
	outputs: [],
	buttons: [],
	width: 0,
	flags: 0,
};

const goldCanvas = {
	name: 'Material',
	nodes: [colorNode, outputNode],
	links: [{ id: 0, from_id: 1, from_socket: 0, to_id: 0, to_socket: 0 }],
};

// ---- cube mesh (matches base/sources/engine.c mesh_data format) ----
// pos: short4norm vec4 (x, y, z, normal.z), world = i16 * scale_pos / 32767
// nor: short2norm vec2 (normal.x, normal.y); shader reassembles
//      normal = normalize(float3(nor.x, nor.y, pos.w))  (parser_material.c)
// tex: short2norm vec2 (u, v)

function buildCubeMesh() {
	const i16pos = (x) => Math.round(x * 32767);
	const pos = [];
	const nor = [];
	const tex = [];
	const idx = [];

	const faces = [
		{ axis: 'x', faceCoord: 1 },
		{ axis: 'x', faceCoord: -1 },
		{ axis: 'y', faceCoord: 1 },
		{ axis: 'y', faceCoord: -1 },
		{ axis: 'z', faceCoord: 1 },
		{ axis: 'z', faceCoord: -1 },
	];
	for (const fc of faces) {
		const axes = ['x', 'y', 'z'].filter((a) => a !== fc.axis);
		const [t1, t2] = axes;
		const n = { x: 0, y: 0, z: 0 };
		n[fc.axis] = fc.faceCoord;
		const base = pos.length / 4;
		for (let b2 = -1; b2 <= 1; b2 += 2) {
			for (let b1 = 1; b1 >= -1; b1 -= 2) {
				const p = { x: 0, y: 0, z: 0 };
				p[fc.axis] = fc.faceCoord;
				p[t1] = b1;
				p[t2] = b2;
				const u = b1 === 1 ? 1 : 0;
				const v = b2 === 1 ? 1 : 0;
				pos.push(i16pos(p.x), i16pos(p.y), i16pos(p.z), i16pos(n.z));
				nor.push(i16pos(n.x), i16pos(n.y));
				tex.push(i16pos(u), i16pos(v));
			}
		}
		idx.push(base, base + 1, base + 2, base, base + 2, base + 3);
	}

	return {
		name: 'Box',
		scale_pos: 1,
		scale_tex: 1,
		vertex_arrays: [
			{ attrib: 'pos', data: 'short4norm', values: Int16Array.from(pos) },
			{ attrib: 'nor', data: 'short2norm', values: Int16Array.from(nor) },
			{ attrib: 'tex', data: 'short2norm', values: Int16Array.from(tex) },
		],
		index_array: Int32Array.from(idx),
	};
}

// ---- project file (mirrors util_encode.c map(39) save format) ----

const LAYER_RES = 512;
const TEXPAINT = new Uint8Array(LAYER_RES * LAYER_RES * 4);

const project = {
	version: '15',
	assets: [],
	is_bgra: false,
	packed_assets: null,
	envmap: '',
	envmap_strength: 1.0,
	envmap_angle: 0,
	envmap_blur: true,
	camera_world: f32(
		1, 0, 0, 0,
		0, -1, 0, 0,
		0, 0, -1, 0,
		0, 0, 0, 1,
	),
	camera_origin: f32(0, 0, 4),
	camera_fov: 60,
	swatches: [
		{
			base: 0xffffd700,       // gold (AARRGGBB)
			opacity: 1,
			occlusion: 1,
			roughness: ROUGH,
			metallic: METAL,
			normal: 0xffffffff,
			emission: 0,
			height: 0,
			subsurface: 0,
		},
	],
	brush_nodes: null,
	brush_icons: null,
	material_nodes: [goldCanvas],
	material_groups: null,
	material_icons: null,
	material_datas: [
		{
			paint_base: true,
			paint_opac: true,
			paint_occ: true,
			paint_rough: true,
			paint_met: true,
			paint_nor: true,
			paint_height: true,
			paint_emis: true,
			paint_subs: true,
			opac_mode: 0,
		},
	],
	font_assets: [],
	sound_assets: [],
	layer_datas: [
		{
			name: 'Layer 1',
			res: LAYER_RES,
			bpp: 32,
			texpaint: TEXPAINT,
			uv_scale: 1,
			uv_rot: 0,
			uv_type: 0,
			decal_mat: f32(),
			opacity_mask: 1,
			fill_material: 0,
			object_mask: 0,
			blending: 0,
			parent: 0,
			visible: true,
			texpaint_nor: null,
			texpaint_pack: null,
			texpaint_sculpt: null,
			paint_base: true,
			paint_opac: true,
			paint_occ: true,
			paint_rough: true,
			paint_met: true,
			paint_nor: true,
			paint_nor_blend: true,
			paint_height: true,
			paint_height_blend: true,
			paint_emis: true,
			paint_subs: true,
			uv_map: 0,
			path_points: f32(),
			path_points_world: f32(),
			path_points_camera: f32(),
			path_points_parent: Int32Array.from([]),
			path_tool: 0,
			path_curved: false,
			path_material: 0,
			path_text: false,
		},
	],
	mesh_datas: [buildCubeMesh()],
	mesh_assets: ['Box'],
	mesh_icons: null,
	mesh_transforms: null,
	mesh_materials: Int32Array.from([0]),
	mesh_parents: Int32Array.from([-1]),
	mesh_physics_shapes: Int32Array.from([0]),
	mesh_physics_masses: f32(0),
	mesh_skins: null,
	atlas_objects: Int32Array.from([]),
	atlas_names: [],
	script_datas: [],
	script_names: [],
	timeline_frame_rate: 24,
	timeline_max_frames: 1,
	timeline_layers: null,
	timeline_meshes: null,
	stages: null,
};

await mkdir(path.dirname(OUT), { recursive: true });
const buf = encode(project);
await writeFile(OUT, buf);
console.log('wrote', OUT, `(${buf.length} bytes)`);