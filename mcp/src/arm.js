import { readFile } from 'node:fs/promises';

const UTF8 = new TextDecoder('utf-8');

export class ArmpackError extends Error {
	constructor(message) {
		super(message);
		this.name = 'ArmpackError';
	}
}

const FLAG_NULL = 0xc0;
const FLAG_FALSE = 0xc2;
const FLAG_TRUE = 0xc3;
const FLAG_F32 = 0xca;
const FLAG_I16 = 0xd1;
const FLAG_I32 = 0xd2;
const FLAG_U8 = 0xc4;
const FLAG_STRING = 0xdb;
const FLAG_MAP = 0xdf;
const FLAG_ARRAY = 0xdd;

function makeReader(buf) {
	if (!(buf instanceof Uint8Array) || buf.byteLength < 1) {
		throw new ArmpackError('expected a non-empty Uint8Array/Buffer');
	}
	const dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
	let ei = 0;
	return {
		u8() {
			const v = dv.getUint8(ei);
			ei += 1;
			return v;
		},
		i16() {
			const v = dv.getInt16(ei, true);
			ei += 2;
			return v;
		},
		u32() {
			const v = dv.getUint32(ei, true);
			ei += 4;
			return v;
		},
		i32() {
			const v = dv.getInt32(ei, true);
			ei += 4;
			return v;
		},
		f32() {
			const v = dv.getFloat32(ei, true);
			ei += 4;
			return v;
		},
		string() {
			const n = dv.getUint32(ei, true);
			ei += 4;
			if (ei + n > buf.byteLength) {
				throw new ArmpackError(`truncated string (need ${n} bytes at offset ${ei})`);
			}
			const s = UTF8.decode(buf.subarray(ei, ei + n));
			ei += n;
			return s;
		},
		bytes(n) {
			if (ei + n > buf.byteLength) {
				throw new ArmpackError(`truncated byte block (need ${n} bytes at offset ${ei})`);
			}
			const out = buf.subarray(ei, ei + n);
			ei += n;
			return out;
		},
		back1() {
			if (ei === 0) {
				throw new ArmpackError('cannot rewind past start of buffer');
			}
			ei -= 1;
		},
		offset() {
			return ei;
		},
	};
}

function decodeValue(r, flag) {
	switch (flag) {
		case FLAG_NULL: return null;
		case FLAG_FALSE: return false;
		case FLAG_TRUE: return true;
		case FLAG_F32: return r.f32();
		case FLAG_I32: return r.i32();
		case FLAG_STRING: return r.string();
		case FLAG_MAP: return decodeMap(r);
		case FLAG_ARRAY: return decodeArray(r);
		default: throw new ArmpackError(`unknown value flag 0x${flag.toString(16)} at offset ${r.offset() - 1}`);
	}
}

function decodeArray(r) {
	const count = r.u32();
	const out = [];
	if (count === 0) {
		return out;
	}
	const flag = r.u8();
	switch (flag) {
		case FLAG_F32: {
			const a = new Float32Array(count);
			for (let i = 0; i < count; i++) a[i] = r.f32();
			return a;
		}
		case FLAG_I32: {
			const a = new Int32Array(count);
			for (let i = 0; i < count; i++) a[i] = r.i32();
			return a;
		}
		case FLAG_I16: {
			const a = new Int16Array(count);
			for (let i = 0; i < count; i++) a[i] = r.i16();
			return a;
		}
		case FLAG_U8: {
			return new Uint8Array(r.bytes(count));
		}
		case FLAG_STRING: {
			r.back1();
			for (let i = 0; i < count; i++) {
				r.u8();
				out.push(r.string());
			}
			return out;
		}
		case FLAG_MAP: {
			r.back1();
			for (let i = 0; i < count; i++) {
				r.u8();
				out.push(decodeMap(r));
			}
			return out;
		}
		case FLAG_ARRAY: {
			r.back1();
			for (let i = 0; i < count; i++) {
				r.u8();
				out.push(decodeArray(r));
			}
			return out;
		}
		default: throw new ArmpackError(`unknown array element flag 0x${flag.toString(16)}`);
	}
}

function decodeMap(r) {
	const count = r.u32();
	const out = {};
	for (let i = 0; i < count; i++) {
		const flagPos = r.offset();
		r.u8(); // 0xdb string key flag
		const key = r.string();
		const flag = r.u8();
		try {
			out[key] = decodeValue(r, flag);
		} catch (e) {
			if (e instanceof ArmpackError && !String(e.message).includes('while decoding key')) {
				throw new ArmpackError(`${e.message} while decoding key "${key}" (entry ${i}/${count}, value flag 0x${flag.toString(16)} at keyflag ${flagPos})`);
			}
			throw e;
		}
	}
	return out;
}

export function decode(buf) {
	if (buf[0] !== FLAG_MAP) {
		throw new ArmpackError(`root must be a map flag (0xdf), got 0x${buf[0].toString(16)}`);
	}
	const r = makeReader(buf);
	r.u8();
	return decodeMap(r);
}

export async function decodeFile(filePath) {
	const buf = await readFile(filePath);
	return decode(buf);
}

const isInt32 = (n) => Number.isInteger(n) && n >= -2147483648 && n <= 2147483647;

export function encode(value) {
	const chunks = [];
	const push = (b) => chunks.push(b);
	const u8 = (n) => {
		const b = new Uint8Array(1);
		b[0] = n & 0xff;
		push(b);
	};
	const u32 = (n) => {
		const b = new Uint8Array(4);
		new DataView(b.buffer).setUint32(0, n >>> 0, true);
		push(b);
	};
	const i32 = (n) => {
		const b = new Uint8Array(4);
		new DataView(b.buffer).setInt32(0, Math.trunc(n), true);
		push(b);
	};
	const f32 = (n) => {
		const b = new Uint8Array(4);
		new DataView(b.buffer).setFloat32(0, n, true);
		push(b);
	};
	const str = (s) => {
		const t = new TextEncoder().encode(s);
		u8(FLAG_STRING);
		u32(t.length);
		push(t);
	};
	const typedArray = (flag, bytes) => {
		u8(FLAG_ARRAY);
		u32(bytes.length);
		if (bytes.length > 0) {
			u8(flag);
			push(new Uint8Array(bytes.buffer, bytes.byteOffset, bytes.byteLength));
		}
	};
	const encValue = (v) => {
		if (v === null || v === undefined) {
			u8(FLAG_NULL);
			return;
		}
		switch (typeof v) {
			case 'boolean':
				u8(v ? FLAG_TRUE : FLAG_FALSE);
				return;
			case 'number':
				if (isInt32(v)) {
					u8(FLAG_I32);
					i32(v);
				} else {
					u8(FLAG_F32);
					f32(v);
				}
				return;
			case 'string':
				str(v);
				return;
		}
		if (v instanceof Float32Array) {
			typedArray(FLAG_F32, v);
			return;
		}
		if (v instanceof Int32Array) {
			typedArray(FLAG_I32, v);
			return;
		}
		if (v instanceof Int16Array) {
			typedArray(FLAG_I16, v);
			return;
		}
		if (v instanceof Uint8Array) {
			typedArray(FLAG_U8, v);
			return;
		}
		if (Array.isArray(v)) {
			u8(FLAG_ARRAY);
			u32(v.length);
			for (const item of v) {
				encValue(item);
			}
			return;
		}
		if (typeof v === 'object') {
			const entries = Object.entries(v);
			u8(FLAG_MAP);
			i32(entries.length);
			for (const [k, val] of entries) {
				const t = new TextEncoder().encode(k);
				u8(FLAG_STRING);
				u32(t.length);
				push(t);
				encValue(val);
			}
			return;
		}
		throw new ArmpackError(`cannot encode value of type ${typeof v}`);
	};
	encValue(value);
	const total = chunks.reduce((n, b) => n + b.length, 0);
	const out = new Uint8Array(total);
	let o = 0;
	for (const b of chunks) {
		out.set(b, o);
		o += b.length;
	}
	return out;
}

const LARGE_ARRAY_LIMIT = 64;
const TYPED_ARRAY_NAMES = {
	Float32Array: 'f32_array',
	Int32Array: 'i32_array',
	Int16Array: 'i16_array',
	Uint8Array: 'u8_array',
};

function arrayDescriptor(arr) {
	return {
		__arrayType: TYPED_ARRAY_NAMES[arr.constructor.name] ?? 'array',
		__length: arr.length,
	};
}

export function toJSON(value, { omitLargeArrays = true } = {}) {
	if (ArrayBuffer.isView(value)) {
		if (omitLargeArrays && value.length > LARGE_ARRAY_LIMIT) {
			return arrayDescriptor(value);
		}
		return Array.from(value);
	}
	if (Array.isArray(value)) {
		return value.map((v) => toJSON(v, { omitLargeArrays }));
	}
	if (value !== null && typeof value === 'object') {
		const out = {};
		for (const [k, v] of Object.entries(value)) {
			out[k] = toJSON(v, { omitLargeArrays });
		}
		return out;
	}
	return value;
}

export function summarize(value) {
	const keys = Object.keys(value);
	const sections = {};
	const bigArrays = [];
	for (const [k, v] of Object.entries(value)) {
		if (Array.isArray(v)) {
			sections[k] = v.length;
		} else if (ArrayBuffer.isView(v)) {
			sections[k] = v.length;
			bigArrays.push({ key: k, type: TYPED_ARRAY_NAMES[v.constructor.name] ?? 'array', length: v.length });
		}
	}
	return {
		keys: keys.sort(),
		sections,
		largestArrays: bigArrays.sort((a, b) => b.length - a.length).slice(0, 5),
	};
}