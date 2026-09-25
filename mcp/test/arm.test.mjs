import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { decode, decodeFile, summarize, toJSON } from '../src/arm.js';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, '..', '..');
const asset = (rel) => path.join(ROOT, 'paint', 'assets', rel);

// ---- manual armpack encoder (mirrors base/sources/iron_armpack.c) ----

function u8(n) {
	return new Uint8Array([n]);
}
function u16(n) {
	const b = new Uint8Array(2);
	new DataView(b.buffer).setUint16(0, n, true);
	return b;
}
function i16(n) {
	const b = new Uint8Array(2);
	new DataView(b.buffer).setInt16(0, n, true);
	return b;
}
function u32(n) {
	const b = new Uint8Array(4);
	new DataView(b.buffer).setUint32(0, n, true);
	return b;
}
function i32(n) {
	const b = new Uint8Array(4);
	new DataView(b.buffer).setInt32(0, n, true);
	return b;
}
function f32(n) {
	const b = new Uint8Array(4);
	new DataView(b.buffer).setFloat32(0, n, true);
	return b;
}
function concat(...parts) {
	const total = parts.reduce((n, p) => n + p.length, 0);
	const out = new Uint8Array(total);
	let o = 0;
	for (const p of parts) {
		out.set(p, o);
		o += p.length;
	}
	return out;
}
function encStr(s) {
	const t = new TextEncoder().encode(s);
	return concat(u8(0xdb), u32(t.length), t);
}
// entries: [[key, fullyEncodedValueBytes], ...]; value bytes include their flag
function encMap(entries) {
	return concat(u8(0xdf), u32(entries.length), ...entries.map(([k, v]) => concat(u8(0xdb), u32(k.length), new TextEncoder().encode(k), v)));
}
function encTypedArray(elemFlag, elems) {
	return concat(u8(0xdd), u32(elems.length), u8(elemFlag), ...elems);
}
function encArray(encoded) {
	return concat(u8(0xdd), u32(encoded.length), encoded);
}
// C writes dd count, then each element is a self-contained encoded value (incl. its own flag)
function encStringArray(parts) {
	return concat(u8(0xdd), u32(parts.length), ...parts.map(encStr));
}
function encMapArray(maps) {
	return concat(u8(0xdd), u32(maps.length), ...maps);
}

test('decodes a hand-built map covering every value flag', () => {
	const innerMap = encMap([
		['pi', concat(u8(0xca), f32(3.25))],
		['count', concat(u8(0xd2), i32(-7))],
		['msg', encStr('hi')],
		['nil', u8(0xc0)],
		['yes', u8(0xc3)],
		['no', u8(0xc2)],
	]);
	const root = encMap([
		['name', encStr('root')],
		['meta', innerMap],
		['nums', encTypedArray(0xca, [f32(1), f32(2), f32(3)])],
		['ints', encTypedArray(0xd2, [i32(10), i32(20)])],
		['shorts', encTypedArray(0xd1, [i16(-3), i16(300)])],
		['bytes', encTypedArray(0xc4, [u8(0), u8(255)])],
		['strings', encStringArray(['abc', 'z'])],
		['submaps', encMapArray([innerMap])],
		['nested', concat(u8(0xdd), u32(1), u8(0xdd), concat(u32(1), u8(0xd2), i32(42)))],
		['empty', concat(u8(0xdd), u32(0))],
	]);

	const out = decode(root);
	assert.equal(out.name, 'root');
	assert.equal(out.meta.pi, 3.25);
	assert.equal(out.meta.count, -7);
	assert.equal(out.meta.msg, 'hi');
	assert.equal(out.meta.nil, null);
	assert.equal(out.meta.yes, true);
	assert.equal(out.meta.no, false);
	assert.deepEqual([...out.nums], [1, 2, 3]);
	assert.ok(out.nums instanceof Float32Array);
	assert.deepEqual([...out.ints], [10, 20]);
	assert.ok(out.ints instanceof Int32Array);
	assert.deepEqual([...out.shorts], [-3, 300]);
	assert.ok(out.shorts instanceof Int16Array);
	assert.deepEqual([...out.bytes], [0, 255]);
	assert.ok(out.bytes instanceof Uint8Array);
	assert.deepEqual(out.strings, ['abc', 'z']);
	assert.equal(out.submaps[0].pi, 3.25);
	assert.deepEqual([...out.nested[0]], [42]);
	assert.deepEqual(out.empty, []);
});

test('decodes the real default_material.arm fixture', async () => {
	const buf = await readFile(asset('default_material.arm'));
	const out = decode(buf);
	assert.equal(out.name, 'Material');
	assert.ok(Array.isArray(out.nodes));
	assert.equal(out.nodes.length, 2);
	const first = out.nodes[0];
	for (const k of ['name', 'type', 'color', 'inputs', 'outputs']) {
		assert.ok(k in first, `nodes[0] should have key ${k}`);
	}
	assert.equal(first.type, 'RGB');
});

test('decodes the real cube.arm fixture (scene with mesh_datas)', async () => {
	const buf = await readFile(asset('meshes/cube.arm'));
	const out = decode(buf);
	assert.equal(out.name, 'Scene');
	assert.ok(Array.isArray(out.mesh_datas));
	assert.ok(out.mesh_datas.length >= 1);
	const md = out.mesh_datas[0];
	assert.ok('vertex_arrays' in md);
	const va = md.vertex_arrays;
	assert.ok(Array.isArray(va));
	const first = va[0];
	if (ArrayBuffer.isView(first)) {
		assert.ok(first instanceof Float32Array || first instanceof Uint8Array || first instanceof Int16Array || first instanceof Int32Array, 'vertex array should be typed');
	}
});

test('decode rejects a buffer whose root is not a map flag', () => {
	assert.throws(() => decode(u8(0xca)), /root.*map/i);
	assert.throws(() => decode(new Uint8Array([0xdf, 0])), /truncated|offset/i);
});

test('decodeFile reads from disk', async () => {
	const out = await decodeFile(asset('default_brush.arm'));
	assert.equal(typeof out, 'object');
	assert.ok(out != null);
});

test('toJSON converts typed arrays and can omit large arrays', async () => {
	const buf = await readFile(asset('meshes/cube.arm'));
	const out = decode(buf);
	const json = toJSON(out, { omitLargeArrays: true });
	assert.equal(json.name, 'Scene');
	// some large typed array somewhere gets replaced by a descriptor
	const hasDescriptor = JSON.stringify(json).includes('__arrayType');
	assert.ok(hasDescriptor, 'large arrays become descriptors');
	for (const va of json.mesh_datas[0].vertex_arrays) {
		if (va && va.__arrayType !== undefined) {
			assert.ok(va.__arrayType.startsWith('array_'), 'descriptor names start with array_');
			assert.equal(typeof va.__length, 'number');
		}
	}
	const full = toJSON(out, { omitLargeArrays: false });
	// small arrays stay JSON-safe arrays
	assert.ok(Array.isArray(full.mesh_datas));
	assert.ok(!JSON.stringify(full).includes('__arrayType'), 'no descriptors when not omitting');
});

test('summarize reports top-level keys and section counts', async () => {
	const buf = await readFile(asset('meshes/cube.arm'));
	const s = summarize(decode(buf));
	assert.ok(Array.isArray(s.keys));
	assert.ok(s.keys.includes('mesh_datas'));
	assert.ok(s.keys.includes('objects'));
	assert.equal(typeof s.sections.mesh_datas, 'number');
	assert.ok(s.sections.mesh_datas >= 1);
});