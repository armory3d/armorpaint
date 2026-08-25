// Based on https://github.com/Kode/kmake by RobDangerous

// ██╗     ██╗██████╗
// ██║     ██║██╔══██╗
// ██║     ██║██████╔╝
// ██║     ██║██╔══██╗
// ███████╗██║██████╔╝
// ╚══════╝╚═╝╚═════╝

import * as os from "os";
import * as std from "std";

let path_sep       = "/";
let other_path_sep = "\\";
if (os_platform() === "win32") {
	path_sep       = "\\";
	other_path_sep = "/";
}

let binpath   = path_resolve(scriptArgs[0]);
let toolsdir  = binpath.substring(0, binpath.lastIndexOf(path_sep));
let makedir   = path_join(toolsdir, "..", "..");
let __dirname = makedir;
let irondir   = path_join(makedir, "..");

function get_binary_data(p) {
	return fs_readfile(irondir + "/sources/backends/data/" + p);
}

function get_text_data(p) {
	return fs_readfile(irondir + "/sources/backends/data/" + p);
}

function fs_exists(p) {
	return os.stat(p)[0] != null;
}

function fs_mkdir(p) {
	os.mkdir(p);
}

function array_remove(arr, e) {
	let i = arr.indexOf(e);
	if (i != -1) {
		arr.splice(i, 1);
	}
}

function fs_readdir(p) {
	let dirs = os.readdir(p)[0];
	array_remove(dirs, ".");
	array_remove(dirs, "..");
	return dirs;
}

function fs_copyfile(from, to) {
	let f = std.open(from, "rb");
	f.seek(0, std.SEEK_END);
	let size = f.tell();
	let u8   = new Uint8Array(size);
	f.seek(0, std.SEEK_SET);
	f.read(u8.buffer, 0, size);
	f.close();

	f = std.open(to, "wb");
	f.write(u8.buffer, 0, size);
	f.close();
}

function fs_isdir(p) {
	return (os.stat(p)[0].mode & os.S_IFMT) == os.S_IFDIR;
}

function fs_mtime(p) {
	return os.stat(p)[0].mtime;
}

function fs_readfile(p) {
	return std.loadFile(p);
}

function fs_writefile(p, data) {
	let f = std.open(p, "w");
	f.puts(data); // utf8
	f.close();
}

function parent_dir(dir) {
	return dir.substring(0, dir.lastIndexOf(path_sep));
}

function fs_ensuredir(dir) {
	if (dir != "" && !fs_exists(dir)) {
		fs_ensuredir(parent_dir(dir));
		fs_mkdir(dir);
	}
}

function fs_copydir(from, to) {
	fs_ensuredir(to);
	let files = fs_readdir(from);
	for (let file of files) {
		if (fs_isdir(path_join(from, file))) {
			fs_copydir(path_join(from, file), path_join(to, file));
		}
		else {
			fs_copyfile(path_join(from, file), path_join(to, file));
		}
	}
}

function os_popen(exe, params = [], ops = {}) {
	params.unshift(exe);
	let res = {stdout : ""};

	let cwd;
	if (ops.cwd) {
		cwd = os_cwd();
		os.chdir(ops.cwd);
	}

	let p      = std.popen(params.join(" "), "r");
	res.stdout = p.readAsString();
	p.close();

	if (ops.cwd) {
		os.chdir(cwd);
	}

	return res;
}

function os_exec(exe, params = [], ops = {}) {
	params.unshift(exe);
	let res = {status : 0};

	if (os_platform() === "win32") {
		res = amake.os_exec_win(params, ops); // { status, stdout }
	}
	else {
		res.status = os.exec(params, ops);
	}

	return res;
}

function os_platform() {
	return os.platform;
}

function os_cwd() {
	return os.getcwd()[0];
}

function os_env(s) {
	return std.getenv(s);
}

function os_argv() {
	return scriptArgs;
}

function os_cpus_length() {
	let cores = 0;
	if (os_platform() === "win32") {
		cores = parseInt(os_env("NUMBER_OF_PROCESSORS"));
	}
	else if (os_platform() === "darwin") {
		cores = parseInt(os_popen("sysctl", [ "-n", "hw.logicalcpu" ]).stdout);
	}
	else {
		let cpuinfo = fs_readfile("/proc/cpuinfo");
		if (cpuinfo != null) {
			cores = (cpuinfo.match(/^processor\s*:/gm) || []).length;
		}
	}
	if (!(cores > 0)) {
		cores = 8;
	}
	return cores;
}

function os_chmod(p, m) {
	if (os_platform() === "win32") {
		return;
	}
	os_exec("chmod", [ m, p ]);
}

function os_exit(c) {
	std.exit(c);
}

function crypto_random_uuid() {
	let u    = Date.now().toString(16) + Math.random().toString(16) + "0".repeat(16);
	let guid = [ u.substring(0, 8), u.substring(8, 12), "4000-8" + u.substring(13, 16), u.substring(16, 28) ].join("-");
	return guid;
}

// https://github.com/kawanet/sha1-uint8array
var SHA1 = "undefined" != typeof exports ? exports : {};
!function(t) {
	var r = [ 1518500249, 1859775393, -1894007588, -899497514 ], i = {sha1 : 1};
	SHA1.createHash = function(t) {
		if (t && !i[t] && !i[t.toLowerCase()])
			throw new Error("Digest method not supported");
		return new s
	};
	var n, s = function() {
		function t() {
			this.A = 1732584193, this.B = -271733879, this.C = -1732584194, this.D = 271733878, this.E = -1009589776, this.t = 0, this.i = 0,
			(!n || e >= 8e3) && (n = new ArrayBuffer(8e3), e = 0), this.h = new Uint8Array(n, e, 80), this.o = new Int32Array(n, e, 20), e += 80
		}
		return t.prototype.update = function(t) {
			if ("string" == typeof t)
				return this.u(t);
			if (null == t)
				throw new TypeError("Invalid type: " + typeof t);
			var r = t.byteOffset, i = t.byteLength, n = i / 64 | 0, s = 0;
			if (n && !(3 & r) && !(this.t % 64)) {
				for (var h = new Int32Array(t.buffer, r, 16 * n); n--;)
					this.v(h, s >> 2), s += 64;
				this.t += s
			}
			if (1 !== t.BYTES_PER_ELEMENT && t.buffer) {
				var e = new Uint8Array(t.buffer, r + s, i - s);
				return this.p(e)
			}
			return s === i ? this : this.p(t, s)
		}, t.prototype.p = function(t, r) {
			var i = this.h, n = this.o, s = t.length;
			for (r |= 0; r < s;) {
				for (var h = this.t % 64, e = h; r < s && e < 64;)
					i[e++] = t[r++];
				e >= 64 && this.v(n), this.t += e - h
			}
			return this
		}, t.prototype.u = function(t) {
			for (var r = this.h, i = this.o, n = t.length, s = this.i, h = 0; h < n;) {
				for (var e = this.t % 64, f = e; h < n && f < 64;) {
					var o = 0 | t.charCodeAt(h++);
					o < 128                  ? r[f++] = o
					: o < 2048               ? (r[f++] = 192 | o >>> 6, r[f++] = 128 | 63 & o)
					: o < 55296 || o > 57343 ? (r[f++] = 224 | o >>> 12, r[f++] = 128 | o >>> 6 & 63, r[f++] = 128 | 63 & o)
					: s ? (o = ((1023 & s) << 10) + (1023 & o) + 65536, r[f++] = 240 | o >>> 18, r[f++] = 128 | o >>> 12 & 63, r[f++] = 128 | o >>> 6 & 63,
					       r[f++] = 128 | 63 & o, s = 0)
					    : s = o
				}
				f >= 64 && (this.v(i), i[0] = i[16]), this.t += f - e
			}
			return this.i = s, this
		}, t.prototype.v = function(t, i) {
			var n = this, s = n.A, e = n.B, f = n.C, w = n.D, y = n.E, A = 0;
			for (i |= 0; A < 16;)
				h[A++] = o(t[i++]);
			for (A = 16; A < 80; A++)
				h[A] = u(h[A - 3] ^ h[A - 8] ^ h[A - 14] ^ h[A - 16]);
			for (A = 0; A < 80; A++) {
				var p = A / 20 | 0, d = a(s) + v(p, e, f, w) + y + h[A] + r[p] | 0;
				y = w, w = f, f = c(e), e = s, s = d
			}
			this.A = s + this.A | 0, this.B = e + this.B | 0, this.C = f + this.C | 0, this.D = w + this.D | 0, this.E = y + this.E | 0
		}, t.prototype.digest = function(t) {
			var r = this.h, i = this.o, n = this.t % 64 | 0;
			for (r[n++] = 128; 3 & n;)
				r[n++] = 0;
			if ((n >>= 2) > 14) {
				for (; n < 16;)
					i[n++] = 0;
				n = 0, this.v(i)
			}
			for (; n < 16;)
				i[n++] = 0;
			var s = 8 * this.t, h = (4294967295 & s) >>> 0, e = (s - h) / 4294967296;
			return e && (i[14] = o(e)), h && (i[15] = o(h)), this.v(i), "hex" === t ? this.I() : this.U()
		}, t.prototype.I = function() {
			var t = this, r = t.A, i = t.B, n = t.C, s = t.D, h = t.E;
			return f(r) + f(i) + f(n) + f(s) + f(h)
		}, t.prototype.U = function() {
			var t = this, r = t.A, i = t.B, n = t.C, s = t.D, h = t.E, e = t.h, f = t.o;
			return f[0] = o(r), f[1] = o(i), f[2] = o(n), f[3] = o(s), f[4] = o(h), e.slice(0, 20)
		}, t
	}(), h = new Int32Array(80), e = 0, f = function(t) {
		return (t + 4294967296).toString(16).substr(-8)
	}, o = 254 === new Uint8Array(new Uint16Array([ 65279 ]).buffer)[0] ? function(t) {
		return t
	} : function(t) {
		return t << 24 & 4278190080 | t << 8 & 16711680 | t >> 8 & 65280 | t >> 24 & 255
	}, u = function(t) {
		return t << 1 | t >>> 31
	}, a = function(t) {
		return t << 5 | t >>> 27
	}, c = function(t) {
		return t << 30 | t >>> 2
	};
	function v(t, r, i, n) {
		return 0 === t ? r & i | ~r & n : 2 === t ? r & i | r & n | i & n : r ^ i ^ n
	}
}();

function uuidv5(path, namespace) {
	let hash = SHA1.createHash("sha1");
	hash.update(namespace);
	hash.update(path);
	let value = hash.digest("hex");
	return value.substring(0, 8) + "-" + value.substring(8, 12) + "-" + value.substring(12, 16) + "-" + value.substring(16, 20) + "-" + value.substring(20, 32);
}

function path_join() {
	let args = Array.from(arguments);
	return path_normalize(args.join(path_sep));
}

function path_isabs(p) {
	return p[0] == "/" || p[1] == ":" || (p[0] == "\\" && p[1] == "\\");
}

function _path_resolve(base, relative) {
	let stack = base.split("/");
	let parts = relative.split("/");
	for (let i = 0; i < parts.length; i++) {
		if (parts[i] == ".") {
			continue;
		}
		if (parts[i] == "..") {
			stack.pop();
		}
		else {
			stack.push(parts[i]);
		}
	}
	return stack.join("/");
}

function path_resolve() {
	let args = Array.from(arguments);
	if (!path_isabs(args[0])) {
		args.unshift(os_cwd());
	}

	let i = args.length - 1;
	let p = args[i];
	p     = path_normalize(p);
	while (!path_isabs(p)) {
		i--;
		p = _path_resolve(args[i], p);
		p = path_normalize(p);
	}
	return p;
}

function path_relative(from, to) {
	let a = from.split(path_sep);
	let b = to.split(path_sep);

	while (a[0] == b[0]) {
		a.shift();
		b.shift();
		if (a.length == 0 || b.length == 0) {
			break;
		}
	}
	let base = "";
	for (let i = 0; i < a.length; ++i) {
		base += ".." + path_sep;
	}
	base += b.join(path_sep);
	return base;
}

function path_normalize(p) {
	p = p.replaceAll(other_path_sep, path_sep);
	while (p.indexOf(path_sep + path_sep) != -1) {
		p = p.replaceAll(path_sep + path_sep, path_sep);
	}
	if (p.endsWith(path_sep)) {
		p = p.substring(0, p.length - 1);
	}
	let ar = p.split(path_sep);
	let i  = 0;
	while (i < ar.length) {
		if (i > 0 && ar[i] == ".." && ar[i - 1] != "..") {
			ar.splice(i - 1, 2);
			i--;
		}
		else {
			i++;
		}
	}
	return ar.join(path_sep);
}

function exe_ext() {
	return os_platform() == "win32" ? ".exe" : "";
}

function path_extname(p) {
	return p.substring(p.lastIndexOf("."), p.length);
}

function path_basename(p) {
	return p.substring(p.lastIndexOf(path_sep) + 1, p.length);
}

function path_basename_noext(p) {
	return p.substring(p.lastIndexOf(path_sep) + 1, p.lastIndexOf("."));
}

function path_dirname(p) {
	return p.substring(0, p.lastIndexOf(path_sep));
}

function sys_dir() {
	if (os_platform() === "linux") {
		// if (os_arch() === "arm64") return "linux_arm64";
		return "linux_x64";
	}
	else if (os_platform() === "win32") {
		return "windows_x64";
	}
	else {
		return "macos";
	}
}

function matches(text, pattern) {
	let regexstring = pattern.replace(/\./g, "\\.").replace(/\*\*/g, ".?").replace(/\*/g, "[^/]*").replace(/\?/g, "*");
	let regex       = new RegExp("^" + regexstring + "$", "g");
	return regex.test(text);
}

function stringify(p) {
	return p.replaceAll("\\", "/");
}

// ███████╗██╗  ██╗██████╗  ██████╗ ██████╗ ████████╗███████╗██████╗ ███████╗
// ██╔════╝╚██╗██╔╝██╔══██╗██╔═══██╗██╔══██╗╚══██╔══╝██╔════╝██╔══██╗██╔════╝
// █████╗   ╚███╔╝ ██████╔╝██║   ██║██████╔╝   ██║   █████╗  ██████╔╝███████╗
// ██╔══╝   ██╔██╗ ██╔═══╝ ██║   ██║██╔══██╗   ██║   ██╔══╝  ██╔══██╗╚════██║
// ███████╗██╔╝ ██╗██║     ╚██████╔╝██║  ██║   ██║   ███████╗██║  ██║███████║
// ╚══════╝╚═╝  ╚═╝╚═╝      ╚═════╝ ╚═╝  ╚═╝   ╚═╝   ╚══════╝╚═╝  ╚═╝╚══════╝

class Exporter {
	constructor() {
		this.path    = null;
		this.outFile = null;
	}

	write_file(file) {
		this.path    = file;
		this.outFile = "";
	}

	close_file() {
		fs_writefile(this.path, this.outFile);
		this.outFile = "";
	}

	p(line = "", indent = 0) {
		let tabs = "";
		for (let i = 0; i < indent; ++i) {
			tabs += "\t";
		}
		this.outFile += tabs + line + "\n";
	}

	nice_path(from, to, filepath) {
		return filepath;
		// let absolute = path_normalize(filepath);
		// if (!path_isabs(absolute)) {
		// 	absolute = path_resolve(from, filepath);
		// }
		// return path_relative(to, absolute);
	}
}

function get_dir_from_string(file, base) {
	file = file.replace(/\\/g, '/');
	if (file.indexOf("/") >= 0) {
		let dir = file.substr(0, file.lastIndexOf("/"));
		return path_join(base, path_relative(base, dir)).replace(/\\/g, "/");
	}
	else {
		return base;
	}
}

function get_dir(file) {
	return get_dir_from_string(file.file, file.projectName);
}

class VisualStudioExporter extends Exporter {
	constructor() {
		super();
	}

	get_debug_dir(from, project) {
		let debugdir = project.get_debug_dir();
		if (path_isabs(debugdir)) {
			debugdir = debugdir.replace(/\//g, "\\");
		}
		else {
			debugdir = path_resolve(from, debugdir).replace(/\//g, "\\");
		}
		return debugdir;
	}

	export_user_file(from, to, project, platform) {
		if (project.get_debug_dir() === "")
			return;
		this.write_file(path_resolve(to, project.get_safe_name() + ".vcxproj.user"));
		this.p('<?xml version="1.0" encoding="utf-8"?>');
		this.p('<Project ToolsVersion="Current" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">');
		this.p('<PropertyGroup>', 1);
		if (platform === "windows") {
			this.p('<LocalDebuggerWorkingDirectory>' + this.get_debug_dir(from, project) + '</LocalDebuggerWorkingDirectory>', 2);
			this.p('<DebuggerFlavor>WindowsLocalDebugger</DebuggerFlavor>', 2);
			project.cmdArgs.push(this.get_debug_dir(from, project));
			if (project.cmdArgs.length > 0) {
				this.p('<LocalDebuggerCommandArguments>' + project.cmdArgs.join(' ') + '</LocalDebuggerCommandArguments>', 2);
			}
		}
		this.p('</PropertyGroup>', 1);
		this.p('</Project>');
		this.close_file();
	}

	write_project_declarations(project, solutionUuid) {
		this.p('Project("{' + solutionUuid.toUpperCase() + '}") = "' + project.get_safe_name() + '", "' + project.get_safe_name() + '.vcxproj", "{' +
		       project.get_uuid().toString().toUpperCase() + '}"');
		if (project.getSubProjects().length > 0) {
			this.p('ProjectSection(ProjectDependencies) = postProject', 1);
			for (let proj of project.getSubProjects()) {
				this.p('{' + proj.get_uuid().toString().toUpperCase() + '} = {' + proj.get_uuid().toString().toUpperCase() + '}', 2);
			}
			this.p('EndProjectSection', 1);
		}
		this.p('EndProject');
		for (let proj of project.getSubProjects())
			this.write_project_declarations(proj, solutionUuid);
	}

	get_configs() {
		return [ "Debug", "Develop", "Release" ];
	}

	get_systems() {
		return [ "x64" ];
	}

	export_solution(project) {
		let from     = path_resolve(".");
		let to       = path_resolve("build");
		let platform = goptions.target;
		this.write_file(path_resolve(to, project.get_safe_name() + '.slnx'));
		this.p('<Solution>');
		this.p('<Configurations>', 1);
		this.p('<Platform Name="x64" />', 2);
		this.p('</Configurations>', 1);
		this.p('<Project Path="' + project.get_safe_name() + '.vcxproj" Id="' + project.get_uuid().toString().toLowerCase() + '" />', 1);
		this.p('</Solution>');
		this.close_file();
		this.export_project(from, to, project, platform, false, goptions);
		this.export_filters(from, to, project, platform);
		this.export_user_file(from, to, project, platform);
		this.export_resource_script(to);
		export_ico(project.icon, path_resolve(to, 'icon.ico'), from);
	}

	export_resource_script(to) {
		this.write_file(path_resolve(to, "resources.rc"));
		this.p('107       ICON         "icon.ico"');
		this.close_file();
	}

	pretty_dir(dir) {
		let pretty_dir = dir;
		while (pretty_dir.startsWith("../")) {
			pretty_dir = pretty_dir.substring(3);
		}
		return pretty_dir.replace(/\//g, "\\");
	}

	item_group(from, to, project, type, filter) {
		let lastdir = "";
		this.p('<ItemGroup>', 1);
		for (let file of project.getFiles()) {
			let dir = get_dir(file);
			if (dir !== lastdir)
				lastdir = dir;
			if (filter(file)) {
				let filepath = "";
				if (project.noFlatten && !path_isabs(file.file)) {
					filepath = path_resolve(path_join(project.basedir, file.file));
				}
				else {
					filepath = this.nice_path(from, to, file.file);
				}
				this.p('<' + type + ' Include="' + filepath + '">', 2);
				this.p('<Filter>' + this.pretty_dir(dir) + '</Filter>', 3);
				this.p('</' + type + '>', 2);
			}
		}
		this.p('</ItemGroup>', 1);
	}

	export_filters(from, to, project, platform) {
		for (let proj of project.getSubProjects())
			this.export_filters(from, to, proj, platform);
		this.write_file(path_resolve(to, project.get_safe_name() + '.vcxproj.filters'));
		this.p('<?xml version="1.0" encoding="utf-8"?>');
		this.p('<Project ToolsVersion="Current" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">');
		let lastdir = '';
		let dirs    = [];
		for (let file of project.getFiles()) {
			let dir = get_dir(file);
			if (dir !== lastdir) {
				let subdir = dir;
				while (subdir.indexOf('/') >= 0) {
					subdir = subdir.substr(0, subdir.lastIndexOf('/'));
					if (!dirs.includes(subdir))
						dirs.push(subdir);
				}
				dirs.push(dir);
				lastdir = dir;
			}
		}
		let assets = [];
		this.p('<ItemGroup>', 1);
		for (let dir of dirs) {
			let pretty = this.pretty_dir(dir);
			if (pretty !== '..') {
				this.p('<Filter Include="' + pretty + '">', 2);
				this.p('<UniqueIdentifier>{' + crypto_random_uuid().toString().toUpperCase() + '}</UniqueIdentifier>', 3);
				this.p('</Filter>', 2);
			}
		}
		this.p('</ItemGroup>', 1);
		this.item_group(from, to, project, 'ClInclude', (file) => { return file.file.endsWith(".h"); });
		this.item_group(from, to, project, 'ClCompile',
		                (file) => { return file.file.endsWith(".cpp") || file.file.endsWith(".c") || file.file.endsWith(".cc"); });
		this.item_group(from, to, project, 'CustomBuild', (file) => { return file.file.endsWith(".hlsl"); });
		if (platform === "windows") {
			this.item_group(from, to, project, "ResourceCompile", (file) => { return file.file.endsWith(".rc"); });
		}
		this.p('</Project>');
		this.close_file();
	}

	get_platform_toolset() {
		// return 'v145';
		return 'ClangCL';
	}

	configuration(config, indent, project) {
		this.p('<PropertyGroup Condition="\'$(Configuration)\'==\'' + config + '\'" Label="Configuration">', indent);
		this.p('<ConfigurationType>Application</ConfigurationType>', indent + 1);
		this.p('<UseDebugLibraries>' + (config === "Release" ? "false" : "true") + '</UseDebugLibraries>', indent + 1);
		this.p('<PlatformToolset>' + this.get_platform_toolset() + '</PlatformToolset>', indent + 1);
		this.p('<PreferredToolArchitecture>x64</PreferredToolArchitecture>', indent + 1);
		if (config === "Release" && project.lto) {
			this.p('<WholeProgramOptimization>true</WholeProgramOptimization>', indent + 1);
		}
		this.p('<CharacterSet>Unicode</CharacterSet>', indent + 1);
		this.p('</PropertyGroup>', indent);
	}

	get_optimization(config) {
		switch (config) {
		case "Debug":
		default:
			return "Disabled";
		case "Develop":
			return "Full";
		case "Release":
			return "MaxSpeed";
		}
	}

	cStd(project) {
		switch (project.cStd.toLowerCase()) {
		case "c99":
			return "";
		case "c11":
			return "stdc11";
		case "c17":
			return "stdc17";
		case "c2x":
			return "stdc17";
		}
	}

	cppStd(project) {
		switch (project.cppStd.toLowerCase()) {
		case "c++17":
			return "stdcpp17";
		case "c++23":
			return "stdcpplatest";
		}
	}

	item_definition(config, system, includes, debugDefines, releaseDefines, indent, debuglibs, releaselibs, project) {
		this.p('<ItemDefinitionGroup Condition="\'$(Configuration)|$(Platform)\'==\'' + config + '|' + system + '\'">', indent);
		this.p('<ClCompile>', indent + 1);
		this.p('<AdditionalIncludeDirectories>' + includes + '</AdditionalIncludeDirectories>', indent + 2);

		let additionalOptions =
		    '-Wno-deprecated-declarations -Wno-c23-extensions -Wno-incompatible-pointer-types -Wno-incompatible-function-pointer-types -Wno-microsoft-enum-forward-reference /bigobj';
		if (config === 'Release') {
			additionalOptions += ' /Gw';
		}
		this.p('<AdditionalOptions>' + additionalOptions + ' %(AdditionalOptions)</AdditionalOptions>', indent + 2);

		this.p('<WarningLevel>Level3</WarningLevel>', indent + 2);
		this.p('<Optimization>' + this.get_optimization(config) + '</Optimization>', indent + 2);
		if (config === 'Release') {
			this.p('<FunctionLevelLinking>true</FunctionLevelLinking>', indent + 2);
			this.p('<IntrinsicFunctions>true</IntrinsicFunctions>', indent + 2);
			this.p('<DebugInformationFormat>None</DebugInformationFormat>', indent + 2);
		}
		this.p('<PreprocessorDefinitions>' + (config === 'Release' ? releaseDefines : debugDefines) + ((system === 'x64') ? 'SYS_64;' : '') +
		           'WIN32;_WINDOWS;%(PreprocessorDefinitions)</PreprocessorDefinitions>',
		       indent + 2);
		this.p('<RuntimeLibrary>' + (config === 'Release' ? 'MultiThreaded' : 'MultiThreadedDebug') + '</RuntimeLibrary>', indent + 2);
		this.p('<MultiProcessorCompilation>true</MultiProcessorCompilation>', indent + 2);
		this.p('<MinimalRebuild>false</MinimalRebuild>', indent + 2);
		if (config === 'Develop') {
			this.p('<BasicRuntimeChecks>Default</BasicRuntimeChecks>', indent + 2);
		}
		let cStd = this.cStd(project);
		this.p('<LanguageStandard_C>' + cStd + '</LanguageStandard_C>', indent + 2);
		let cppStd = this.cppStd(project);
		this.p('<LanguageStandard>' + cppStd + '</LanguageStandard>', indent + 2);

		this.p('</ClCompile>', indent + 1);
		this.p('<Link>', indent + 1);
		if (project.name == "amake") { // TODO
			this.p('<SubSystem>Console</SubSystem>', indent + 2);
		}
		else {
			this.p('<SubSystem>Windows</SubSystem>', indent + 2);
		}
		this.p('<GenerateDebugInformation>' + (config === 'Release' ? 'false' : 'true') + '</GenerateDebugInformation>', indent + 2);
		if (config === 'Release') {
			this.p('<EnableCOMDATFolding>true</EnableCOMDATFolding>', indent + 2);
			this.p('<OptimizeReferences>true</OptimizeReferences>', indent + 2);
		}

		let libs = config === 'Release' ? releaselibs : debuglibs;
		this.p(
		    '<AdditionalDependencies>' + libs +
		        'kernel32.lib;user32.lib;gdi32.lib;winspool.lib;comdlg32.lib;advapi32.lib;shell32.lib;ole32.lib;oleaut32.lib;uuid.lib;odbc32.lib;odbccp32.lib;%(AdditionalDependencies)</AdditionalDependencies>',
		    indent + 2);
		this.p('</Link>', indent + 1);
		this.p('<Manifest>', indent + 1);
		this.p('<EnableDpiAwareness>PerMonitorHighDPIAware</EnableDpiAwareness>', indent + 2);
		this.p('</Manifest>', indent + 1);
		this.p('</ItemDefinitionGroup>', indent);
	}

	globals(indent) {
		this.p('<VCProjectVersion>18.0</VCProjectVersion>', indent);
		this.p('<WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>', indent);
	}

	extension_settings(indent) {
		this.p('<Import Project="$(VCTargetsPath)\\BuildCustomizations\\masm.props" />', indent);
	}

	extension_targets(indent) {
		this.p('<Import Project="$(VCTargetsPath)\\BuildCustomizations\\masm.targets"/>', indent);
	}

	export_project(from, to, project, platform, cmd, options) {
		for (let proj of project.getSubProjects()) {
			this.export_project(from, to, proj, platform, cmd, options);
		}
		this.write_file(path_resolve(to, project.get_safe_name() + '.vcxproj'));
		this.p('<?xml version="1.0" encoding="utf-8"?>');
		this.p('<Project DefaultTargets="Build" ' +
		       'xmlns="http://schemas.microsoft.com/developer/msbuild/2003">');
		this.p('<ItemGroup Label="ProjectConfigurations">', 1);
		for (let system of this.get_systems()) {
			for (let config of this.get_configs()) {
				this.p('<ProjectConfiguration Include="' + config + '|' + system + '">', 2);
				this.p('<Configuration>' + config + '</Configuration>', 3);
				this.p('<Platform>' + system + '</Platform>', 3);
				this.p('</ProjectConfiguration>', 2);
			}
		}
		this.p('</ItemGroup>', 1);
		this.p('<PropertyGroup Label="Globals">', 1);
		this.p('<ProjectGuid>{' + project.get_uuid().toString().toLowerCase() + '}</ProjectGuid>', 2);
		this.globals(2);
		this.p('</PropertyGroup>', 1);
		this.p('<Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />', 1);
		for (let config of this.get_configs()) {
			for (let system of this.get_systems()) {
				this.configuration(config, 1, project);
			}
		}
		this.p('<Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />', 1);
		this.p('<ImportGroup Label="ExtensionSettings">', 1);
		this.extension_settings(2);
		this.p('</ImportGroup>', 1);
		this.p('<PropertyGroup Label="UserMacros" />', 1);
		if (project.get_executable_name()) {
			this.p('<PropertyGroup>', 1);
			this.p('<TargetName>' + project.get_executable_name() + '</TargetName>', 2);
			this.p('</PropertyGroup>', 1);
		}
		if (platform === 'windows') {
			for (let system of this.get_systems()) {
				this.p('<ImportGroup Label="PropertySheets" Condition="\'$(Platform)\'==\'' + system + '\'">', 1);
				this.p(
				    '<Import Project="$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props" Condition="exists(\'$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props\')" Label="LocalAppDataPlatform" />',
				    2);
				this.p('</ImportGroup>', 1);
			}
		}
		let debugDefines   = "_DEBUG;";
		let releaseDefines = "NDEBUG;";
		for (let define of project.getDefines()) {
			debugDefines += define + ";";
			releaseDefines += define + ";";
		}
		let incstring   = "";
		let includedirs = project.getIncludeDirs();
		for (let include of includedirs) {
			let relativized = path_relative(to, path_resolve(from, include));
			if (relativized === "") {
				relativized = ".";
			}
			incstring += relativized + ";";
		}
		if (incstring.length > 0)
			incstring = incstring.substr(0, incstring.length - 1);
		let debuglibs = "";
		for (let proj of project.getSubProjects()) {
			if (proj.noFlatten) {
				debuglibs += project.basedir + "\\build\\x64\\Debug\\" + proj.get_safe_name() + ".lib;";
			}
			else {
				debuglibs += "Debug\\" + proj.get_safe_name() + ".lib;";
			}
		}
		for (let lib of project.getLibs()) {
			if (fs_exists(path_resolve(from, lib + ".lib"))) {
				debuglibs += path_relative(to, path_resolve(from, lib)) + ".lib;";
			}
			else {
				debuglibs += lib + ".lib;";
			}
		}
		let releaselibs = "";
		for (let proj of project.getSubProjects()) {
			if (proj.noFlatten) {
				releaselibs += project.basedir + "\\build\\x64\\Release\\" + proj.get_safe_name() + ".lib;";
			}
			else {
				releaselibs += "Release\\" + proj.get_safe_name() + ".lib;";
			}
		}
		for (let proj of project.getSubProjects())
			releaselibs += "Release\\" + proj.get_safe_name() + ".lib;";
		for (let lib of project.getLibs()) {
			if (fs_exists(path_resolve(from, lib + ".lib"))) {
				releaselibs += path_relative(to, path_resolve(from, lib)) + ".lib;";
			}
			else {
				releaselibs += lib + ".lib;";
			}
		}
		for (let config of this.get_configs()) {
			for (let system of this.get_systems()) {
				this.item_definition(config, system, incstring, debugDefines, releaseDefines, 2, debuglibs, releaselibs, project);
			}
		}
		this.p('<ItemGroup>', 1);
		for (let file of project.getFiles()) {
			let filepath = "";
			if (project.noFlatten && !path_isabs(file.file)) {
				filepath = path_resolve(project.basedir + "/" + file.file);
			}
			else {
				filepath = this.nice_path(from, to, file.file);
			}
			if (file.file.endsWith(".h"))
				this.p('<ClInclude Include="' + filepath + '" />', 2);
		}
		this.p('</ItemGroup>', 1);
		this.p('<ItemGroup>', 1);
		let objects = {};
		for (let fileobject of project.getFiles()) {
			let file = fileobject.file;
			if (file.endsWith(".cpp") || file.endsWith(".c") || file.endsWith("cc")) {
				let name = file.toLowerCase();
				if (name.indexOf("/") >= 0)
					name = name.substr(name.lastIndexOf("/") + 1);
				name         = name.substr(0, name.lastIndexOf("."));
				let filepath = "";
				if (project.noFlatten && !path_isabs(file)) {
					filepath = path_resolve(project.basedir + "/" + file);
				}
				else {
					filepath = this.nice_path(from, to, file);
				}
				if (!objects[name]) {
					this.p('<ClCompile Include="' + filepath + '" />', 2);
					objects[name] = true;
				}
				else {
					while (objects[name]) {
						name = name + '_';
					}
					this.p('<ClCompile Include="' + filepath + '">', 2);
					this.p('<ObjectFileName>$(IntDir)\\' + name + '.obj</ObjectFileName>', 3);
					this.p('</ClCompile>', 2);
					objects[name] = true;
				}
			}
		}
		this.p('</ItemGroup>', 1);
		this.p('<ItemGroup>', 1);
		for (let file of project.getFiles()) {
			if (file.file.endsWith('.natvis')) {
				this.p('<Natvis Include="' + this.nice_path(from, to, file.file) + '"/>', 2);
			}
		}
		this.p('</ItemGroup>', 1);
		if (platform === "windows") {
			this.p('<ItemGroup>', 1);
			for (let file of project.customs) {
				this.p('<CustomBuild Include="' + this.nice_path(from, to, file.file) + '">', 2);
				this.p('<FileType>Document</FileType>', 2);
				this.p('<Command>' + file.command + '</Command>', 2);
				this.p('<Outputs>' + file.output + '</Outputs>', 2);
				this.p('<Message>%(Filename)%(Extension)</Message>', 2);
				this.p('</CustomBuild>', 2);
			}
			this.p('</ItemGroup>');
			this.p('<ItemGroup>', 1);
			this.p('<None Include="icon.ico" />', 2);
			this.p('</ItemGroup>', 1);
			this.p('<ItemGroup>', 1);
			this.p('<ResourceCompile Include="resources.rc" />', 2);
			for (let file of project.getFiles()) {
				if (file.file.endsWith('.rc')) {
					this.p('<ResourceCompile Include="' + this.nice_path(from, to, file.file) + '" />', 2);
				}
			}
			this.p('</ItemGroup>', 1);
		}
		this.p('<Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />', 1);
		this.p('<ImportGroup Label="ExtensionTargets">', 1);
		this.extension_targets(2);
		this.p('</ImportGroup>', 1);
		this.p('</Project>');
		this.close_file();
	}
}

class WasmExporter extends Exporter {
	constructor() {
		super();
		this.compile_commands = new CompilerCommandsExporter();
		let compiler          = "clang --target=wasm32 -nostdlib -matomics -mbulk-memory";
		let compilerFlags     = "";
		let linkerFlags =
		    '--target=wasm32 -nostdlib -matomics -mbulk-memory "-Wl,--import-memory,--shared-memory,--allow-undefined,--no-entry,--initial-memory=671088640,--max-memory=671088640,-z,stack-size=256000"';
		if (!goptions.debug) {
			linkerFlags += " -Wl,--strip-all";
		}
		this.make = new MakeExporter(compiler, compiler, compilerFlags, compilerFlags, linkerFlags, '.wasm');
	}

	export_solution(project) {
		this.make.export_solution(project);
		this.compile_commands.export_solution(project);
	}
}

function new_path_id(path) {
	return uuidv5(path, "7448ebd8-cfc8-4f45-8b3d-5df577ceea6d").toUpperCase();
}

function get_dir2(file) {
	if (file.file.indexOf("/") >= 0) {
		let dir = file.file.substr(0, file.file.lastIndexOf("/"));
		return path_join(file.projectName, path_relative(file.projectDir, dir)).replace(/\\/g, "/");
	}
	else {
		return file.projectName;
	}
}

class Directory {
	constructor(dirname) {
		this.dirname = dirname;
		this.id      = new_path_id(dirname);
	}

	getName() {
		return this.dirname;
	}

	getLastName() {
		if (this.dirname.indexOf("/") < 0)
			return this.dirname;
		return this.dirname.substr(this.dirname.lastIndexOf("/") + 1);
	}

	getId() {
		return this.id;
	}
}

class File {
	constructor(filename, dir) {
		this.filename = filename;
		this.dir      = dir;
		this.buildid  = new_path_id(dir + filename + "_buildid");
		this.fileid   = new_path_id(dir + filename + "_fileid");
	}

	getBuildId() {
		return this.buildid;
	}

	getFileId() {
		return this.fileid;
	}

	isBuildFile() {
		return this.filename.endsWith(".c") || this.filename.endsWith(".cpp") || this.filename.endsWith(".m") || this.filename.endsWith(".mm") ||
		       this.filename.endsWith(".cc") || this.filename.endsWith(".metal");
	}

	getName() {
		return this.filename;
	}

	getLastName() {
		if (this.filename.indexOf("/") < 0)
			return this.filename;
		return this.filename.substr(this.filename.lastIndexOf("/") + 1);
	}

	get_dir() {
		return this.dir;
	}

	toString() {
		return this.getName();
	}
}

class Framework {
	constructor(name) {
		this.name      = name;
		this.buildid   = new_path_id(name + "_buildid");
		this.fileid    = new_path_id(name + "_fileid");
		this.localPath = null;
	}

	toString() {
		if (this.name.indexOf(".") < 0)
			return this.name + ".framework";
		else
			return this.name;
	}

	getBuildId() {
		return this.buildid.toString().toUpperCase();
	}

	getFileId() {
		return this.fileid.toString().toUpperCase();
	}
}

function findDirectory(dirname, directories) {
	for (let dir of directories) {
		if (dir.getName() === dirname) {
			return dir;
		}
	}
	return null;
}

function addDirectory(dirname, directories) {
	let dir = findDirectory(dirname, directories);
	if (dir === null) {
		dir = new Directory(dirname);
		directories.push(dir);
		while (dirname.indexOf("/") >= 0) {
			dirname = dirname.substr(0, dirname.lastIndexOf("/"));
			addDirectory(dirname, directories);
		}
	}
	return dir;
}

class IconImage {
	constructor(idiom, size, scale) {
		this.idiom = idiom;
		this.size  = size;
		this.scale = scale;
	}
}

class XCodeExporter extends Exporter {
	constructor() {
		super();
	}

	exportWorkspace(to, project) {
		let dir = path_resolve(to, project.get_safe_name() + ".xcodeproj", "project.xcworkspace");
		fs_ensuredir(dir);
		this.write_file(path_resolve(to, project.get_safe_name() + ".xcodeproj", "project.xcworkspace", "contents.xcworkspacedata"));
		this.p('<?xml version="1.0" encoding="UTF-8"?>');
		this.p('<Workspace');
		this.p('version = "1.0">');
		this.p('<FileRef');
		this.p('location = "self:' + project.get_safe_name() + '.xcodeproj">');
		this.p('</FileRef>');
		this.p('</Workspace>');
		this.close_file();
	}

	export_solution(project) {
		let from     = path_resolve(".");
		let to       = path_resolve("build");
		let platform = goptions.target;
		let xdir     = path_resolve(to, project.get_safe_name() + ".xcodeproj");
		fs_ensuredir(xdir);
		this.exportWorkspace(to, project);
		function add_icons(icons, idiom, sizes, scales) {
			for (let i = 0; i < sizes.length; ++i) {
				icons.push(new IconImage(idiom, sizes[i], scales[i]));
			}
		}
		let icons = [];
		if (platform === "ios") {
			add_icons(icons, "iphone", [ 20, 20, 29, 29, 40, 40, 60, 60 ], [ 2, 3, 2, 3, 2, 3, 2, 3 ]);
			add_icons(icons, "ipad", [ 20, 20, 29, 29, 40, 40, 76, 76, 83.5 ], [ 1, 2, 1, 2, 1, 2, 1, 2, 2 ]);
			icons.push(new IconImage("ios-marketing", 1024, 1));
		}
		else {
			add_icons(icons, "mac", [ 16, 16, 32, 32, 128, 128, 256, 256, 512, 512 ], [ 1, 2, 1, 2, 1, 2, 1, 2, 1, 2 ]);
		}
		let iconsdir = path_resolve(to, "Images.xcassets", "AppIcon.appiconset");
		fs_ensuredir(iconsdir);
		this.write_file(path_resolve(to, "Images.xcassets", "AppIcon.appiconset", "Contents.json"));
		this.p('{');
		this.p('"images" : [', 1);
		for (let i = 0; i < icons.length; ++i) {
			let icon = icons[i];
			this.p('{', 2);
			this.p('"idiom" : "' + icon.idiom + '",', 3);
			this.p('"size" : "' + icon.size + 'x' + icon.size + '",', 3);
			this.p('"filename" : "' + icon.idiom + icon.scale + 'x' + icon.size + '.png",', 3);
			this.p('"scale" : "' + icon.scale + 'x"', 3);
			if (i === icons.length - 1)
				this.p('}', 2);
			else
				this.p('},', 2);
		}
		this.p('],', 1);
		this.p('"info" : {', 1);
		this.p('"version" : 1,', 2);
		this.p('"author" : "xcode"', 2);
		this.p('}', 1);
		this.p('}');
		this.close_file();
		for (let i = 0; i < icons.length; ++i) {
			let icon = icons[i];
			export_png_icon(project.icon, path_resolve(to, 'Images.xcassets', 'AppIcon.appiconset', icon.idiom + icon.scale + 'x' + icon.size + '.png'),
			                icon.size * icon.scale, icon.size * icon.scale, from);
		}
		let plistname   = "";
		let files       = [];
		let directories = [];
		for (let fileobject of project.getFiles()) {
			let filename = fileobject.file;
			if (filename.endsWith(".plist")) {
				plistname = filename;
			}
			let dir  = addDirectory(get_dir2(fileobject), directories);
			let file = new File(filename, dir);
			files.push(file);
		}
		if (plistname.length === 0) {
			console.log("Error: no plist found");
		}
		let frameworks = [];
		for (let lib of project.getLibs()) {
			frameworks.push(new Framework(lib));
		}

		let target_options =
		    {bundle : 'org.armory3d.' + project.getName().toLowerCase(), version : "1.0", build : "1", organizationName : "Armory3D", developmentTeam : from.startsWith("/Users/lubos/") ? "AUW6AHHL4Q" : ""};
		if (project.target_options && project.target_options.ios) {
			let userOptions = project.target_options.ios;
			if (userOptions.version)
				target_options.version = userOptions.version;
			if (userOptions.build)
				target_options.build = userOptions.build;
		}
		let projectId                = new_path_id("_projectId");
		let appFileId                = new_path_id("_appFileId");
		let frameworkBuildId         = new_path_id("_frameworkBuildId");
		let sourceBuildId            = new_path_id("_sourceBuildId");
		let frameworksGroupId        = new_path_id("_frameworksGroupId");
		let productsGroupId          = new_path_id("_productsGroupId");
		let mainGroupId              = new_path_id("_mainGroupId");
		let targetId                 = new_path_id("_targetId");
		let nativeBuildConfigListId  = new_path_id("_nativeBuildConfigListId");
		let projectBuildConfigListId = new_path_id("_projectBuildConfigListId");
		let debugId                  = new_path_id("_debugId");
		let releaseId                = new_path_id("_releaseId");
		let nativeDebugId            = new_path_id("_nativeDebugId");
		let nativeReleaseId          = new_path_id("_nativeReleaseId");
		let debugDirFileId           = new_path_id("_debugDirFileId");
		let debugDirBuildId          = new_path_id("_debugDirBuildId");
		let resourcesBuildId         = new_path_id("_resourcesBuildId");
		let iconFileId               = new_path_id("_iconFileId");
		let iconBuildId              = new_path_id("_iconBuildId");
		this.write_file(path_resolve(to, project.get_safe_name() + ".xcodeproj", "project.pbxproj"));
		this.p('// !$*UTF8*$!');
		this.p('{');
		this.p('archiveVersion = 1;', 1);
		this.p('classes = {', 1);
		this.p('};', 1);
		this.p('objectVersion = 46;', 1);
		this.p('objects = {', 1);
		this.p();
		this.p('/* Begin PBXBuildFile section */');
		for (let framework of frameworks) {
			this.p(framework.getBuildId() + ' /* ' + framework.toString() + ' in Frameworks */ = {isa = PBXBuildFile; fileRef = ' + framework.getFileId() +
			           ' /* ' + framework.toString() + ' */; };',
			       2);
		}
		this.p(debugDirBuildId + ' /* Deployment in Resources */ = {isa = PBXBuildFile; fileRef = ' + debugDirFileId + ' /* Deployment */; };', 2);
		for (let file of files) {
			if (file.isBuildFile()) {
				this.p(file.getBuildId() + ' /* ' + file.toString() + ' in Sources */ = {isa = PBXBuildFile; fileRef = ' + file.getFileId() + ' /* ' +
				           file.toString() + ' */; };',
				       2);
			}
		}
		this.p(iconBuildId + ' /* Images.xcassets in Resources */ = {isa = PBXBuildFile; fileRef = ' + iconFileId + ' /* Images.xcassets */; };', 2);
		this.p('/* End PBXBuildFile section */');
		this.p();
		this.p('/* Begin PBXFileReference section */');
		let executable_name = project.get_safe_name();
		if (project.get_executable_name()) {
			executable_name = project.get_executable_name();
		}
		this.p(appFileId + ' /* ' + project.get_safe_name() +
		           '.app */ = {isa = PBXFileReference; explicitFileType = wrapper.application; includeInIndex = 0; path = "' + executable_name +
		           '.app"; sourceTree = BUILT_PRODUCTS_DIR; };',
		       2);
		for (let framework of frameworks) {
			if (framework.toString().endsWith('.framework')) {
				// Local framework - a directory is specified
				if (framework.toString().indexOf('/') >= 0) {
					framework.localPath = path_resolve(from, framework.toString());
					this.p(framework.getFileId() + ' /* ' + framework.toString() +
					           ' */ = {isa = PBXFileReference; lastKnownFileType = wrapper.framework; name = ' + framework.toString() +
					           '; path = ' + framework.localPath + '; sourceTree = "<absolute>"; };',
					       2);
				}
				// XCode framework
				else {
					this.p(framework.getFileId() + ' /* ' + framework.toString() +
					           ' */ = {isa = PBXFileReference; lastKnownFileType = wrapper.framework; name = ' + framework.toString() +
					           '; path = System/Library/Frameworks/' + framework.toString() + '; sourceTree = SDKROOT; };',
					       2);
				}
			}
			else if (framework.toString().endsWith('.dylib')) {
				if (framework.toString().indexOf('/') >= 0) {
					framework.localPath = path_resolve(from, framework.toString());
					this.p(framework.getFileId() + ' /* ' + framework.toString() +
					           ' */ = {isa = PBXFileReference; lastKnownFileType = compiled.mach-o.dylib; name = ' + framework.toString() +
					           '; path = ' + framework.localPath + '; sourceTree = "<absolute>"; };',
					       2);
				}
				else {
					this.p(framework.getFileId() + ' /* ' + framework.toString() +
					           ' */ = {isa = PBXFileReference; lastKnownFileType = compiled.mach-o.dylib; name = ' + framework.toString() +
					           '; path = usr/lib/' + framework.toString() + '; sourceTree = SDKROOT; };',
					       2);
				}
			}
			else {
				framework.localPath = path_resolve(from, framework.toString());
				this.p(framework.getFileId() + ' /* ' + framework.toString() + ' */ = {isa = PBXFileReference; lastKnownFileType = archive.ar; name = ' +
				           framework.toString() + '; path = ' + framework.localPath + '; sourceTree = "<group>"; };',
				       2);
			}
		}
		this.p(debugDirFileId + ' /* Deployment */ = {isa = PBXFileReference; lastKnownFileType = folder; name = Deployment; path = "' +
		           path_resolve(from, project.get_debug_dir()) + '"; sourceTree = "<group>"; };',
		       2);
		for (let file of files) {
			let filetype     = "unknown";
			let fileencoding = "";
			if (file.getName().endsWith(".plist"))
				filetype = "text.plist.xml";
			if (file.getName().endsWith(".h"))
				filetype = "sourcecode.c.h";
			if (file.getName().endsWith(".m"))
				filetype = "sourcecode.c.objc";
			if (file.getName().endsWith(".c"))
				filetype = "sourcecode.c.c";
			if (file.getName().endsWith(".cpp"))
				filetype = "sourcecode.c.cpp";
			if (file.getName().endsWith(".cc"))
				filetype = "sourcecode.c.cpp";
			if (file.getName().endsWith(".mm"))
				filetype = "sourcecode.c.objcpp";
			if (file.getName().endsWith(".metal")) {
				filetype     = "sourcecode.metal";
				fileencoding = "fileEncoding = 4; ";
			}
			if (!file.getName().endsWith(".DS_Store")) {
				this.p(file.getFileId() + ' /* ' + file.toString() + ' */ = {isa = PBXFileReference; ' + fileencoding + 'lastKnownFileType = ' + filetype +
				           '; name = "' + file.getLastName() + '"; path = "' + path_resolve(from, file.toString()) + '"; sourceTree = "<group>"; };',
				       2);
			}
		}
		this.p(
		    iconFileId +
		        ' /* Images.xcassets */ = {isa = PBXFileReference; lastKnownFileType = folder.assetcatalog; path = Images.xcassets; sourceTree = "<group>"; };',
		    2);
		this.p('/* End PBXFileReference section */');
		this.p();
		this.p('/* Begin PBXFrameworksBuildPhase section */');
		this.p(frameworkBuildId + ' /* Frameworks */ = {', 2);
		this.p('isa = PBXFrameworksBuildPhase;', 3);
		this.p('buildActionMask = 2147483647;', 3);
		this.p('files = (', 3);
		for (let framework of frameworks) {
			this.p(framework.getBuildId() + ' /* ' + framework.toString() + ' in Frameworks */,', 4);
		}
		this.p(');', 3);
		this.p('runOnlyForDeploymentPostprocessing = 0;', 3);
		this.p('};', 2);
		this.p('/* End PBXFrameworksBuildPhase section */');
		this.p();
		this.p('/* Begin PBXGroup section */');
		this.p(mainGroupId + ' = {', 2);
		this.p('isa = PBXGroup;', 3);
		this.p('children = (', 3);
		this.p(iconFileId + ' /* Images.xcassets */,', 4);
		this.p(debugDirFileId + ' /* Deployment */,', 4);
		for (let dir of directories) {
			if (dir.getName().indexOf('/') < 0)
				this.p(dir.getId() + ' /* ' + dir.getName() + ' */,', 4);
		}
		this.p(frameworksGroupId + ' /* Frameworks */,', 4);
		this.p(productsGroupId + ' /* Products */,', 4);
		this.p(');', 3);
		this.p('sourceTree = "<group>";', 3);
		this.p('};', 2);
		this.p(productsGroupId + ' /* Products */ = {', 2);
		this.p('isa = PBXGroup;', 3);
		this.p('children = (', 3);
		this.p(appFileId + ' /* ' + project.get_safe_name() + '.app */,', 4);
		this.p(');', 3);
		this.p('name = Products;', 3);
		this.p('sourceTree = "<group>";', 3);
		this.p('};', 2);
		this.p(frameworksGroupId + ' /* Frameworks */ = {', 2);
		this.p('isa = PBXGroup;', 3);
		this.p('children = (', 3);
		for (let framework of frameworks) {
			this.p(framework.getFileId() + ' /* ' + framework.toString() + ' */,', 4);
		}
		this.p(');', 3);
		this.p('name = Frameworks;', 3);
		this.p('sourceTree = "<group>";', 3);
		this.p('};', 2);
		for (let dir of directories) {
			this.p(dir.getId() + ' /* ' + dir.getName() + ' */ = {', 2);
			this.p('isa = PBXGroup;', 3);
			this.p('children = (', 3);
			for (let dir2 of directories) {
				if (dir2 === dir)
					continue;
				if (dir2.getName().startsWith(dir.getName())) {
					if (dir2.getName().substr(dir.getName().length + 1).indexOf('/') < 0)
						this.p(dir2.getId() + ' /* ' + dir2.getName() + ' */,', 4);
				}
			}
			for (let file of files) {
				if (file.get_dir() === dir && !file.getName().endsWith('.DS_Store'))
					this.p(file.getFileId() + ' /* ' + file.toString() + ' */,', 4);
			}
			this.p(');', 3);
			if (dir.getName().indexOf('/') < 0) {
				this.p('path = ../;', 3);
				this.p('name = "' + dir.getLastName() + '";', 3);
			}
			else
				this.p('name = "' + dir.getLastName() + '";', 3);
			this.p('sourceTree = "<group>";', 3);
			this.p('};', 2);
		}
		this.p('/* End PBXGroup section */');
		this.p();
		this.p('/* Begin PBXNativeTarget section */');
		this.p(targetId + ' /* ' + project.get_safe_name() + ' */ = {', 2);
		this.p('isa = PBXNativeTarget;', 3);
		this.p('buildConfigurationList = ' + nativeBuildConfigListId + ' /* Build configuration list for PBXNativeTarget "' + project.get_safe_name() + '" */;',
		       3);
		this.p('buildPhases = (', 3);
		this.p(sourceBuildId + ' /* Sources */,', 4);
		this.p(frameworkBuildId + ' /* Frameworks */,', 4);
		this.p(resourcesBuildId + ' /* Resources */,', 4);
		this.p(');', 3);
		this.p('buildRules = (', 3);
		this.p(');', 3);
		this.p('dependencies = (', 3);
		this.p(');', 3);
		this.p('name = "' + project.getName() + '";', 3);
		this.p('productName = "' + project.getName() + '";', 3);
		this.p('productReference = ' + appFileId + ' /* ' + project.get_safe_name() + '.app */;', 3);
		if (project.name == "amake") { // TODO
			this.p('productType = "com.apple.product-type.tool";', 3);
		}
		else {
			this.p('productType = "com.apple.product-type.application";', 3);
		}
		this.p('};', 2);
		this.p('/* End PBXNativeTarget section */');
		this.p();
		this.p('/* Begin PBXProject section */');
		this.p(projectId + ' /* Project object */ = {', 2);
		this.p('isa = PBXProject;', 3);
		this.p('attributes = {', 3);
		this.p('LastUpgradeCheck = 1230;', 4);
		this.p('ORGANIZATIONNAME = "' + target_options.organizationName + '";', 4);
		this.p('TargetAttributes = {', 4);
		this.p(targetId + ' = {', 5);
		this.p('CreatedOnToolsVersion = 6.1.1;', 6);
		if (target_options.developmentTeam) {
			this.p('DevelopmentTeam = ' + target_options.developmentTeam + ';', 6);
			this.p('ProvisioningStyle = Automatic;', 6);
		}
		this.p('};', 5);
		this.p('};', 4);
		this.p('};', 3);
		this.p('buildConfigurationList = ' + projectBuildConfigListId + ' /* Build configuration list for PBXProject "' + project.get_safe_name() + '" */;', 3);
		this.p('compatibilityVersion = "Xcode 3.2";', 3);
		this.p('developmentRegion = en;', 3);
		this.p('hasScannedForEncodings = 0;', 3);
		this.p('knownRegions = (', 3);
		this.p('en,', 4);
		this.p('Base,', 4);
		this.p(');', 3);
		this.p('mainGroup = ' + mainGroupId + ';', 3);
		this.p('productRefGroup = ' + productsGroupId + ' /* Products */;', 3);
		this.p('projectDirPath = "";', 3);
		this.p('projectRoot = "";', 3);
		this.p('targets = (', 3);
		this.p(targetId + ' /* ' + project.get_safe_name() + ' */,', 4);
		this.p(');', 3);
		this.p('};', 2);
		this.p('/* End PBXProject section */');
		this.p();
		this.p('/* Begin PBXResourcesBuildPhase section */');
		this.p(resourcesBuildId + ' /* Resources */ = {', 2);
		this.p('isa = PBXResourcesBuildPhase;', 3);
		this.p('buildActionMask = 2147483647;', 3);
		this.p('files = (', 3);
		this.p(debugDirBuildId + ' /* Deployment in Resources */,', 4);
		this.p(iconBuildId + ' /* Images.xcassets in Resources */,', 4);
		this.p(');', 3);
		this.p('runOnlyForDeploymentPostprocessing = 0;', 3);
		this.p('};', 2);
		this.p('/* End PBXResourcesBuildPhase section */');
		this.p();
		this.p('/* Begin PBXSourcesBuildPhase section */');
		this.p(sourceBuildId + ' /* Sources */ = {', 2);
		this.p('isa = PBXSourcesBuildPhase;', 3);
		this.p('buildActionMask = 2147483647;', 3);
		this.p('files = (', 3);
		for (let file of files) {
			if (file.isBuildFile())
				this.p(file.getBuildId() + ' /* ' + file.toString() + ' in Sources */,', 4);
		}
		this.p(');', 3);
		this.p('runOnlyForDeploymentPostprocessing = 0;');
		this.p('};');
		this.p('/* End PBXSourcesBuildPhase section */');
		this.p();
		this.p('/* Begin XCBuildConfiguration section */');
		this.p(debugId + ' /* Debug */ = {', 2);
		this.p('isa = XCBuildConfiguration;', 3);
		this.p('buildSettings = {', 3);
		this.p('ALWAYS_SEARCH_USER_PATHS = NO;', 4);
		this.p('CLANG_CXX_LANGUAGE_STANDARD = "' + project.cppStd + '";', 4);
		this.p('CLANG_CXX_LIBRARY = "compiler-default";', 4);
		this.p('CLANG_ENABLE_MODULES = YES;', 4);
		this.p('CLANG_ENABLE_OBJC_ARC = YES;', 4);
		this.p('CLANG_WARN_BLOCK_CAPTURE_AUTORELEASING = YES;', 4);
		this.p('CLANG_WARN_BOOL_CONVERSION = YES;', 4);
		this.p('CLANG_WARN_COMMA = YES;', 4);
		this.p('CLANG_WARN_CONSTANT_CONVERSION = YES;', 4);
		this.p('CLANG_WARN_DEPRECATED_OBJC_IMPLEMENTATIONS = YES;', 4);
		this.p('CLANG_WARN_DIRECT_OBJC_ISA_USAGE = YES_ERROR;', 4);
		this.p('CLANG_WARN_EMPTY_BODY = YES;', 4);
		this.p('CLANG_WARN_ENUM_CONVERSION = YES;', 4);
		this.p('CLANG_WARN_INFINITE_RECURSION = YES;', 4);
		this.p('CLANG_WARN_INT_CONVERSION = YES;', 4);
		this.p('CLANG_WARN_NON_LITERAL_NULL_CONVERSION = YES;', 4);
		this.p('CLANG_WARN_OBJC_IMPLICIT_RETAIN_SELF = YES;', 4);
		this.p('CLANG_WARN_OBJC_LITERAL_CONVERSION = YES;', 4);
		this.p('CLANG_WARN_OBJC_ROOT_CLASS = YES_ERROR;', 4);
		this.p('CLANG_WARN_QUOTED_INCLUDE_IN_FRAMEWORK_HEADER = YES;', 4);
		this.p('CLANG_WARN_RANGE_LOOP_ANALYSIS = YES;', 4);
		this.p('CLANG_WARN_STRICT_PROTOTYPES = YES;', 4);
		this.p('CLANG_WARN_SUSPICIOUS_MOVE = YES;', 4);
		this.p('CLANG_WARN_UNREACHABLE_CODE = YES;', 4);
		this.p('CLANG_WARN__DUPLICATE_METHOD_MATCH = YES;', 4);
		this.p('CODE_SIGN_IDENTITY = "-";', 4);
		if (target_options.developmentTeam) {
			if (platform === 'ios') {
				this.p('"CODE_SIGN_IDENTITY[sdk=iphoneos*]" = "Apple Development";', 4);
			}
			else {
				this.p('"CODE_SIGN_IDENTITY[sdk=macosx*]" = "Apple Development";', 4);
			}
		}
		this.p('COPY_PHASE_STRIP = NO;', 4);
		this.p('ENABLE_STRICT_OBJC_MSGSEND = YES;', 4);
		this.p('ENABLE_TESTABILITY = YES;', 4);
		this.p('GCC_C_LANGUAGE_STANDARD = "' + project.cStd + '";', 4);
		this.p('GCC_DYNAMIC_NO_PIC = NO;', 4);
		this.p('GCC_NO_COMMON_BLOCKS = YES;', 4);
		this.p('GCC_OPTIMIZATION_LEVEL = 0;', 4);
		this.p('GCC_PREPROCESSOR_DEFINITIONS = (', 4);
		this.p('"DEBUG=1",', 5);
		for (let define of project.getDefines()) {
			if (define.indexOf('=') >= 0)
				this.p('"' + define.replace(/\"/g, '\\\\\\"') + '",', 5);
			else
				this.p(define + ',', 5);
		}
		this.p('"$(inherited)",', 5);
		this.p(');', 4);
		this.p('GCC_SYMBOLS_PRIVATE_EXTERN = NO;', 4);
		this.p('GCC_WARN_64_TO_32_BIT_CONVERSION = YES;', 4);
		this.p('GCC_WARN_ABOUT_RETURN_TYPE = YES_ERROR;', 4);
		this.p('GCC_WARN_UNDECLARED_SELECTOR = YES;', 4);
		this.p('GCC_WARN_UNINITIALIZED_AUTOS = YES_AGGRESSIVE;', 4);
		this.p('GCC_WARN_UNUSED_FUNCTION = YES;', 4);
		this.p('GCC_WARN_UNUSED_VARIABLE = YES;', 4);
		if (platform === 'ios') {
			this.p('IPHONEOS_DEPLOYMENT_TARGET = 18.0;', 4);
		}
		else {
			this.p('MACOSX_DEPLOYMENT_TARGET = 14.0;', 4);
		}
		this.p('MTL_ENABLE_DEBUG_INFO = YES;', 4);
		this.p('ONLY_ACTIVE_ARCH = YES;', 4);
		if (platform === 'ios') {
			this.p('SDKROOT = iphoneos;', 4);
			this.p('TARGETED_DEVICE_FAMILY = "1,2";', 4);
		}
		else {
			this.p('SDKROOT = macosx;', 4);
		}
		this.p('};', 3);
		this.p('name = Debug;', 3);
		this.p('};', 2);
		this.p(releaseId + ' /* Release */ = {', 2);
		this.p('isa = XCBuildConfiguration;', 3);
		this.p('buildSettings = {', 3);
		this.p('ALWAYS_SEARCH_USER_PATHS = NO;', 4);
		this.p('CLANG_CXX_LANGUAGE_STANDARD = "' + project.cppStd + '";', 4);
		this.p('CLANG_CXX_LIBRARY = "compiler-default";', 4);
		this.p('CLANG_ENABLE_MODULES = YES;', 4);
		this.p('CLANG_ENABLE_OBJC_ARC = YES;', 4);
		this.p('CLANG_WARN_BLOCK_CAPTURE_AUTORELEASING = YES;', 4);
		this.p('CLANG_WARN_BOOL_CONVERSION = YES;', 4);
		this.p('CLANG_WARN_COMMA = YES;', 4);
		this.p('CLANG_WARN_CONSTANT_CONVERSION = YES;', 4);
		this.p('CLANG_WARN_DEPRECATED_OBJC_IMPLEMENTATIONS = YES;', 4);
		this.p('CLANG_WARN_DIRECT_OBJC_ISA_USAGE = YES_ERROR;', 4);
		this.p('CLANG_WARN_EMPTY_BODY = YES;', 4);
		this.p('CLANG_WARN_ENUM_CONVERSION = YES;', 4);
		this.p('CLANG_WARN_INFINITE_RECURSION = YES;', 4);
		this.p('CLANG_WARN_INT_CONVERSION = YES;', 4);
		this.p('CLANG_WARN_NON_LITERAL_NULL_CONVERSION = YES;', 4);
		this.p('CLANG_WARN_OBJC_IMPLICIT_RETAIN_SELF = YES;', 4);
		this.p('CLANG_WARN_OBJC_LITERAL_CONVERSION = YES;', 4);
		this.p('CLANG_WARN_OBJC_ROOT_CLASS = YES_ERROR;', 4);
		this.p('CLANG_WARN_QUOTED_INCLUDE_IN_FRAMEWORK_HEADER = YES;', 4);
		this.p('CLANG_WARN_RANGE_LOOP_ANALYSIS = YES;', 4);
		this.p('CLANG_WARN_STRICT_PROTOTYPES = YES;', 4);
		this.p('CLANG_WARN_SUSPICIOUS_MOVE = YES;', 4);
		this.p('CLANG_WARN_UNREACHABLE_CODE = YES;', 4);
		this.p('CLANG_WARN__DUPLICATE_METHOD_MATCH = YES;', 4);
		this.p('CODE_SIGN_IDENTITY = "-";', 4);
		if (target_options.developmentTeam) {
			if (platform === 'ios') {
				this.p('"CODE_SIGN_IDENTITY[sdk=iphoneos*]" = "Apple Development";', 4);
			}
			else {
				this.p('"CODE_SIGN_IDENTITY[sdk=macosx*]" = "Apple Development";', 4);
			}
		}
		this.p('COPY_PHASE_STRIP = YES;', 4);
		if (platform === 'macos') {
			this.p('DEBUG_INFORMATION_FORMAT = "dwarf-with-dsym";', 4);
		}
		this.p('ENABLE_NS_ASSERTIONS = NO;', 4);
		this.p('ENABLE_STRICT_OBJC_MSGSEND = YES;', 4);
		this.p('GCC_C_LANGUAGE_STANDARD = "' + project.cStd + '";', 4);
		this.p('GCC_NO_COMMON_BLOCKS = YES;', 4);
		this.p('GCC_PREPROCESSOR_DEFINITIONS = (', 4);
		this.p('NDEBUG,', 5);
		for (let define of project.getDefines()) {
			if (define.indexOf('=') >= 0)
				this.p('"' + define.replace(/\"/g, '\\\\\\"') + '",', 5);
			else
				this.p(define + ',', 5);
		}
		this.p('"$(inherited)",', 5);
		this.p(');', 4);
		this.p('GCC_WARN_64_TO_32_BIT_CONVERSION = YES;', 4);
		this.p('GCC_WARN_ABOUT_RETURN_TYPE = YES_ERROR;', 4);
		this.p('GCC_WARN_UNDECLARED_SELECTOR = YES;', 4);
		this.p('GCC_WARN_UNINITIALIZED_AUTOS = YES_AGGRESSIVE;', 4);
		this.p('GCC_WARN_UNUSED_FUNCTION = YES;', 4);
		this.p('GCC_WARN_UNUSED_VARIABLE = YES;', 4);
		if (platform === 'ios') {
			this.p('IPHONEOS_DEPLOYMENT_TARGET = 18.0;', 4);
		}
		else {
			this.p('MACOSX_DEPLOYMENT_TARGET = 14.0;', 4);
		}
		this.p('MTL_ENABLE_DEBUG_INFO = NO;', 4);
		this.p('ONLY_ACTIVE_ARCH = YES;', 4);
		if (platform === 'ios') {
			this.p('SDKROOT = iphoneos;', 4);
			this.p('TARGETED_DEVICE_FAMILY = "1,2";', 4);
			this.p('VALIDATE_PRODUCT = YES;', 4);
		}
		else {
			this.p('SDKROOT = macosx;', 4);
		}
		this.p('};', 3);
		this.p('name = Release;', 3);
		this.p('};', 2);
		this.p(nativeDebugId + ' /* Debug */ = {', 2);
		this.p('isa = XCBuildConfiguration;', 3);
		this.p('buildSettings = {', 3);
		this.p('ARCHS = arm64;', 4);
		this.p('ASSETCATALOG_COMPILER_APPICON_NAME = AppIcon;', 4);
		this.p('CODE_SIGN_STYLE = Automatic;', 4);
		if (target_options.developmentTeam) {
			this.p('DEVELOPMENT_TEAM = ' + target_options.developmentTeam + ';', 4);
		}
		if (platform === 'macos') {
			this.p('COMBINE_HIDPI_IMAGES = YES;', 4);
		}
		this.p('ENABLE_HARDENED_RUNTIME = YES;', 4);
		this.p('FRAMEWORK_SEARCH_PATHS = (', 4);
		this.p('"$(inherited)",', 5);
		// Search paths to local frameworks
		for (let framework of frameworks) {
			if (framework.localPath != null)
				this.p(framework.localPath.substr(0, framework.localPath.lastIndexOf('/')) + ',', 5);
		}
		this.p(');', 4);
		this.p('HEADER_SEARCH_PATHS = (', 4);
		this.p('"$(inherited)",', 5);
		this.p('"/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/include",', 5);
		for (let projectpath of project.getIncludeDirs())
			this.p('"' + path_resolve(from, projectpath).replace(/ /g, '\\\\ ') + '",', 5);
		this.p(');', 4);
		this.p('LIBRARY_SEARCH_PATHS = (', 4);
		for (let framework of frameworks) {
			if ((framework.toString().endsWith('.dylib') || framework.toString().endsWith('.a')) && framework.localPath != null) {
				this.p(framework.localPath.substr(0, framework.localPath.lastIndexOf('/')) + ',', 5);
			}
		}
		this.p(');', 4);
		this.p('INFOPLIST_EXPAND_BUILD_SETTINGS = "YES";', 4);
		this.p('INFOPLIST_FILE = "' + path_resolve(from, plistname) + '";', 4);
		this.p('INFOPLIST_KEY_LSApplicationCategoryType = "public.app-category.graphics-design";');
		this.p('LD_RUNPATH_SEARCH_PATHS = (', 4);
		this.p('"$(inherited)",', 5);
		if (platform === 'ios') {
			this.p('"@executable_path/Frameworks",', 5);
		}
		for (let framework of frameworks) {
			if (framework.toString().endsWith('.dylib') && framework.localPath != null) {
				this.p(framework.localPath.substr(0, framework.localPath.lastIndexOf('/')) + ',', 5);
			}
		}
		this.p(');', 4);
		if (project.cFlags.length > 0) {
			this.p('OTHER_CFLAGS = (', 4);
			for (let cFlag of project.cFlags) {
				this.p('"' + cFlag + '",', 5);
			}
			this.p(');', 4);
		}
		if (project.cppFlags.length > 0) {
			this.p('OTHER_CPLUSPLUSFLAGS = (', 4);
			for (let cppFlag of project.cppFlags) {
				this.p('"' + cppFlag + '",', 5);
			}
			this.p(');', 4);
		}
		this.p('PRODUCT_BUNDLE_IDENTIFIER = "' + target_options.bundle + '";', 4);
		this.p('BUNDLE_VERSION = "' + target_options.version + '";', 4);
		this.p('BUILD_VERSION = "' + target_options.build + '";', 4);
		this.p('CODE_SIGN_IDENTITY = "-";', 4);
		if (target_options.developmentTeam) {
			if (platform === 'ios') {
				this.p('"CODE_SIGN_IDENTITY[sdk=iphoneos*]" = "Apple Development";', 4);
			}
			else {
				this.p('"CODE_SIGN_IDENTITY[sdk=macosx*]" = "Apple Development";', 4);
			}
		}
		this.p('PRODUCT_NAME = "$(TARGET_NAME)";', 4);
		this.p('};', 3);
		this.p('name = Debug;', 3);
		this.p('};', 2);
		this.p(nativeReleaseId + ' /* Release */ = {', 2);
		this.p('isa = XCBuildConfiguration;', 3);
		this.p('buildSettings = {', 3);
		this.p('ARCHS = arm64;', 4);
		this.p('ASSETCATALOG_COMPILER_APPICON_NAME = AppIcon;', 4);
		this.p('CODE_SIGN_STYLE = Automatic;', 4);
		if (target_options.developmentTeam) {
			this.p('DEVELOPMENT_TEAM = ' + target_options.developmentTeam + ';', 4);
		}
		if (platform === 'macos') {
			this.p('COMBINE_HIDPI_IMAGES = YES;', 4);
		}
		this.p('ENABLE_HARDENED_RUNTIME = YES;', 4);
		this.p('FRAMEWORK_SEARCH_PATHS = (', 4);
		this.p('"$(inherited)",', 5);
		// Search paths to local frameworks
		for (let framework of frameworks) {
			if (framework.localPath != null)
				this.p(framework.localPath.substr(0, framework.localPath.lastIndexOf('/')) + ',', 5);
		}
		this.p(');', 4);
		this.p('HEADER_SEARCH_PATHS = (', 4);
		this.p('"$(inherited)",', 5);
		this.p('"/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/include",', 5);
		for (let p of project.getIncludeDirs())
			this.p('"' + path_resolve(from, p).replace(/ /g, '\\\\ ') + '",', 5);
		this.p(');', 4);
		this.p('LIBRARY_SEARCH_PATHS = (', 4);
		for (let framework of frameworks) {
			if ((framework.toString().endsWith('.dylib') || framework.toString().endsWith('.a')) && framework.localPath != null) {
				this.p(framework.localPath.substr(0, framework.localPath.lastIndexOf('/')) + ',', 5);
			}
		}
		this.p(');', 4);
		this.p('INFOPLIST_EXPAND_BUILD_SETTINGS = "YES";', 4);
		this.p('INFOPLIST_FILE = "' + path_resolve(from, plistname) + '";', 4);
		this.p('INFOPLIST_KEY_LSApplicationCategoryType = "public.app-category.graphics-design";');
		this.p('LD_RUNPATH_SEARCH_PATHS = (', 4);
		this.p('"$(inherited)",', 5);
		if (platform === 'ios') {
			this.p('"@executable_path/Frameworks",', 5);
		}
		for (let framework of frameworks) {
			if (framework.toString().endsWith('.dylib') && framework.localPath != null) {
				this.p(framework.localPath.substr(0, framework.localPath.lastIndexOf('/')) + ',', 5);
			}
		}
		this.p(');', 4);
		if (project.cFlags.length > 0) {
			this.p('OTHER_CFLAGS = (', 4);
			for (let cFlag of project.cFlags) {
				this.p('"' + cFlag + '",', 5);
			}
			this.p(');', 4);
		}
		if (project.cppFlags.length > 0) {
			this.p('OTHER_CPLUSPLUSFLAGS = (', 4);
			for (let cppFlag of project.cppFlags) {
				this.p('"' + cppFlag + '",', 5);
			}
			this.p(');', 4);
		}
		this.p('PRODUCT_BUNDLE_IDENTIFIER = "' + target_options.bundle + '";', 4);
		this.p('BUNDLE_VERSION = "' + target_options.version + '";', 4);
		this.p('BUILD_VERSION = "' + target_options.build + '";', 4);
		this.p('CODE_SIGN_IDENTITY = "-";', 4);
		if (target_options.developmentTeam) {
			if (platform === 'ios') {
				this.p('"CODE_SIGN_IDENTITY[sdk=iphoneos*]" = "Apple Development";', 4);
			}
			else {
				this.p('"CODE_SIGN_IDENTITY[sdk=macosx*]" = "Apple Development";', 4);
			}
		}
		this.p('PRODUCT_NAME = "$(TARGET_NAME)";', 4);
		this.p('};', 3);
		this.p('name = Release;', 3);
		this.p('};', 2);
		this.p('/* End XCBuildConfiguration section */');
		this.p();
		this.p('/* Begin XCConfigurationList section */');
		this.p(projectBuildConfigListId + ' /* Build configuration list for PBXProject "' + project.get_safe_name() + '" */ = {', 2);
		this.p('isa = XCConfigurationList;', 3);
		this.p('buildConfigurations = (', 3);
		this.p(debugId + ' /* Debug */,', 4);
		this.p(releaseId + ' /* Release */,', 4);
		this.p(');', 3);
		this.p('defaultConfigurationIsVisible = 0;', 3);
		this.p('defaultConfigurationName = Release;', 3);
		this.p('};', 2);
		this.p(nativeBuildConfigListId + ' /* Build configuration list for PBXNativeTarget "' + project.get_safe_name() + '" */ = {', 2);
		this.p('isa = XCConfigurationList;', 3);
		this.p('buildConfigurations = (', 3);
		this.p(nativeDebugId + ' /* Debug */,', 4);
		this.p(nativeReleaseId + ' /* Release */,', 4);
		this.p(');', 3);
		this.p('defaultConfigurationIsVisible = 0;', 3);
		this.p('defaultConfigurationName = Release;', 3);
		this.p('};', 2);
		this.p('/* End XCConfigurationList section */');
		this.p('};', 1);
		this.p('rootObject = ' + projectId + ' /* Project object */;', 1);
		this.p('}');
		this.close_file();
	}
}

class MakeExporter extends Exporter {
	constructor(ccompiler, cppcompiler, cFlags, cppFlags, linkerFlags, outputExtension, libsLine = null) {
		super();

		if (ccompiler == "tcc") {
			let tccdir  = makedir + "/tcc"
			linkerFlags = "-lc -lm -pthread";
			ccompiler   = tccdir + "/bin/" + sys_dir() + "/tcc";
			ccompiler += " -DSTBI_NO_SIMD -DSINFL_NO_SIMD -DIRON_NOSIMD -w";
			ccompiler += " -I" + tccdir + "/include -B" + tccdir + "/bin/" + sys_dir();
			cppcompiler = ccompiler;
		}

		this.ccompiler       = ccompiler;
		this.cppcompiler     = cppcompiler;
		this.cFlags          = cFlags;
		this.cppFlags        = cppFlags;
		this.linkerFlags     = linkerFlags;
		this.outputExtension = outputExtension;
		this.cFlags += " -Wno-incompatible-pointer-types";
		if (libsLine != null) {
			this.libsLine = libsLine;
		}
	}

	libsLine(project) {
		let libs = "";
		for (let lib of project.getLibs()) {
			libs += " -l" + lib;
		}
		return libs;
	}

	export_solution(project) {
		let from        = path_resolve(".");
		let to          = path_resolve("build");
		let objects     = {};
		let ofiles      = {};
		let output_path = path_resolve(to, goptions.build_path);
		fs_ensuredir(output_path);
		for (let fileobject of project.getFiles()) {
			let file = fileobject.file;
			if (file.endsWith(".cpp") || file.endsWith(".c") || file.endsWith(".cc")) {
				let name = file.toLowerCase();
				if (name.indexOf("/") >= 0) {
					name = name.substr(name.lastIndexOf("/") + 1);
				}
				name = name.substr(0, name.lastIndexOf("."));
				if (!objects[name]) {
					objects[name] = true;
					ofiles[file]  = name;
				}
				else {
					while (objects[name]) {
						name = name + "_";
					}
					objects[name] = true;
					ofiles[file]  = name;
				}
			}
		}
		let ofilelist = "";
		for (let o in objects) {
			ofilelist += o + ".o ";
		}
		this.write_file(path_resolve(output_path, "makefile"));
		let incline = "-I./ "; // local directory to pick up the precompiled headers
		for (let inc of project.getIncludeDirs()) {
			inc = path_relative(output_path, path_resolve(from, inc));
			incline += "-I" + inc + " ";
		}
		this.p("INC=" + incline);
		this.p("LIB=" + this.linkerFlags + this.libsLine(project));
		let defline = "";
		for (let def of project.getDefines()) {
			defline += "-D" + def.replace(/\"/g, '\\"') + " ";
		}
		if (!goptions.debug) {
			defline += "-DNDEBUG ";
		}
		this.p("DEF=" + defline);
		this.p();
		let cline  = this.cFlags;
		cline     += " -std=" + project.cStd + " ";
		for (let flag of project.cFlags) {
			cline += flag + ' ';
		}
		this.p("CFLAGS=" + cline);
		let cppline  = this.cppFlags;
		cppline     += " -std=" + project.cppStd + " ";
		for (let flag of project.cppFlags) {
			cppline += flag + " ";
		}
		this.p("CPPFLAGS=" + cppline);
		let optimization = "";
		if (!goptions.debug) {
			optimization = "-O2";
		}
		else
			optimization = "-g";
		let executable_name = project.get_safe_name();
		if (project.get_executable_name()) {
			executable_name = project.get_executable_name();
		}
		this.p(executable_name + this.outputExtension + ": " + ofilelist);
		let output = '-o "' + executable_name + this.outputExtension + '"';
		this.p('\t' + this.cppcompiler + ' ' + output + ' ' + optimization + ' ' + ofilelist + ' $(LIB)');
		for (let fileobject of project.getFiles()) {
			let file = fileobject.file;
			if (file.endsWith(".c") || file.endsWith(".cpp") || file.endsWith(".cc")) {
				this.p();
				let name     = ofiles[file];
				let realfile = path_relative(output_path, path_resolve(from, file));
				this.p("-include " + name + ".d");
				this.p(name + ".o: " + realfile);
				let compiler = this.cppcompiler;
				let flags    = '$(CPPFLAGS)';
				if (file.endsWith(".c")) {
					compiler = this.ccompiler;
					flags    = '$(CFLAGS)';
				}
				this.p('\t' + compiler + ' ' + optimization + ' $(INC) $(DEF) -MD ' + flags + ' -c ' + realfile + ' -o ' + name + '.o');
			}
		}
		this.close_file();
	}
}

class LinuxExporter extends Exporter {
	constructor() {
		super();
		let compilerFlags = "";
		let linkerFlags   = "-static-libgcc -static-libstdc++ -pthread";
		if (goptions.ccompiler == "gcc") {
			compilerFlags += "-flto";
			linkerFlags += " -flto";
		}
		if (!goptions.debug) {
			compilerFlags += " -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fno-unwind-tables";
			linkerFlags += " -Wl,--gc-sections -Wl,-s";
		}
		this.make             = new MakeExporter(goptions.ccompiler, goptions.cppcompiler, compilerFlags, compilerFlags, linkerFlags, '');
		this.compile_commands = new CompilerCommandsExporter();
	}

	export_solution(project) {
		this.make.export_solution(project);
		this.compile_commands.export_solution(project);
	}
}

class AndroidExporter extends Exporter {
	constructor() {
		super();
		this.compile_commands = new CompilerCommandsExporter();
	}

	export_solution(project) {
		let from       = path_resolve(".");
		let to         = path_resolve("build");
		this.safe_name = project.get_safe_name();
		let outdir     = path_join(to.toString(), this.safe_name);
		fs_ensuredir(outdir);
		let target_options = {
			package : "org.armory3d",
			installLocation : "internalOnly",
			versionCode : 1,
			versionName : "1.0",
			compileSdkVersion : 36,
			minSdkVersion : 35, // android 15
			targetSdkVersion : 36,
			screenOrientation : "sensor",
			permissions : [],
			disableStickyImmersiveMode : false,
			metadata : [],
			abiFilters : []
		};
		if (project.target_options != null && project.target_options.android != null) {
			let userOptions = project.target_options.android;
			for (let key in userOptions) {
				if (userOptions[key] == null)
					continue;
				switch (key) {
				default:
					target_options[key] = userOptions[key];
				}
			}
		}
		fs_writefile(path_join(outdir, 'build.gradle.kts'), get_text_data('android/build.gradle.kts'));
		fs_writefile(path_join(outdir, 'gradle.properties'), get_text_data('android/gradle.properties'));
		fs_writefile(path_join(outdir, 'gradlew'), get_text_data('android/gradlew'));
		if (os_platform() !== 'win32') {
			os_chmod(path_join(outdir, 'gradlew'), "+x");
		}
		fs_writefile(path_join(outdir, 'gradlew.bat'), get_text_data('android/gradlew.bat'));
		let settings = get_text_data('android/settings.gradle.kts');
		settings     = settings.replace(/{name}/g, project.getName());
		fs_writefile(path_join(outdir, 'settings.gradle.kts'), settings);
		fs_ensuredir(path_join(outdir, 'app'));
		fs_writefile(path_join(outdir, 'app', 'proguard-rules.pro'), get_text_data('android/app/proguard-rules.pro'));
		this.write_app_gradle(project, outdir, from, target_options);
		this.write_cmake_lists(project, outdir, from);
		fs_ensuredir(path_join(outdir, 'app', 'src'));
		fs_ensuredir(path_join(outdir, 'app', 'src', 'main'));
		this.write_manifest(outdir, target_options);
		let strings = get_text_data('android/main/res/values/strings.xml');
		strings     = strings.replace(/{name}/g, project.getName());
		fs_ensuredir(path_join(outdir, 'app', 'src', 'main', 'res', 'values'));
		fs_writefile(path_join(outdir, 'app', 'src', 'main', 'res', 'values', 'strings.xml'), strings);
		this.export_icons(project.icon, outdir, from, to);
		fs_ensuredir(path_join(outdir, 'gradle', 'wrapper'));
		fs_writefile(path_join(outdir, 'gradle', 'wrapper', 'gradle-wrapper.jar'), get_binary_data('android/gradle/wrapper/gradle-wrapper.jar'));
		fs_writefile(path_join(outdir, 'gradle', 'wrapper', 'gradle-wrapper.properties'), get_text_data('android/gradle/wrapper/gradle-wrapper.properties'));
		fs_copydir(path_resolve(from, project.get_debug_dir()), path_resolve(to, this.safe_name, 'app', 'src', 'main', 'assets'));
		this.compile_commands.export_solution(project);
	}

	write_app_gradle(project, outdir, from, target_options) {
		let cflags = '';
		for (let flag of project.cFlags)
			cflags += flag + ' ';
		let cppflags = '';
		for (let flag of project.cppFlags)
			cppflags += flag + ' ';
		let gradle = get_text_data('android/app/build.gradle.kts');
		gradle     = gradle.replace(/{package}/g, target_options.package);
		gradle     = gradle.replace(/{versionCode}/g, target_options.versionCode.toString());
		gradle     = gradle.replace(/{versionName}/g, target_options.versionName);
		gradle     = gradle.replace(/{compileSdkVersion}/g, target_options.compileSdkVersion.toString());
		gradle     = gradle.replace(/{minSdkVersion}/g, target_options.minSdkVersion.toString());
		gradle     = gradle.replace(/{targetSdkVersion}/g, target_options.targetSdkVersion.toString());
		let arch   = '';
		if (target_options.abiFilters.length > 0) {
			for (let item of target_options.abiFilters) {
				if (arch.length === 0) {
					arch = '"' + item + '"';
				}
				else {
					arch = arch + ', "' + item + '"';
				}
			}
			arch = `ndk { abiFilters += listOf(${arch}) }`;
		}
		else {
			switch (goptions.arch) {
			case 'default':
				arch = '';
				break;
			case 'arm8':
				arch = 'arm64-v8a';
				break;
			}
			if (goptions.arch !== 'default') {
				arch = `ndk {abiFilters += listOf("${arch}")}`;
			}
		}
		gradle = gradle.replace(/{architecture}/g, arch);
		// Looks like these should go into CMakeLists.txt now..
		// gradle = gradle.replace(/{cflags}/g, cflags);
		// cppflags = '-frtti -fexceptions ' + cppflags;
		// cppflags = '-std=' + project.cppStd + ' ' + cppflags;
		// gradle = gradle.replace(/{cppflags}/g, cppflags);
		let javasources = '';
		for (let dir of project.getJavaDirs()) {
			javasources += '"' + path_relative(path_join(outdir, 'app'), path_resolve(from, dir)).replace(/\\/g, '/') + '", ';
		}
		javasources += '"' + path_join(irondir, 'sources', 'backends', 'data', 'android_java').replace(/\\/g, '/') + '"';
		gradle = gradle.replace(/{javasources}/g, javasources);
		fs_writefile(path_join(outdir, 'app', 'build.gradle.kts'), gradle);
	}

	write_cmake_lists(project, outdir, from) {
		let cmake        = get_text_data('android/app/CMakeLists.txt');
		let debugDefines = '';
		for (let def of project.getDefines()) {
			debugDefines += ' -D' + def.replace(/\"/g, '\\\\\\\"');
		}
		cmake              = cmake.replace(/{debug_defines}/g, debugDefines);
		let releaseDefines = '';
		for (let def of project.getDefines()) {
			releaseDefines += ' -D' + def.replace(/\"/g, '\\\\\\\"');
		}
		cmake        = cmake.replace(/{release_defines}/g, releaseDefines);
		let includes = '';
		for (let inc of project.getIncludeDirs()) {
			includes += '  "' + path_resolve(inc).replace(/\\/g, '/') + '"\n';
		}
		cmake     = cmake.replace(/{includes}/g, includes);
		let files = '';
		for (let file of project.getFiles()) {
			if (file.file.endsWith('.c') || file.file.endsWith('.cc') || file.file.endsWith('.cpp') || file.file.endsWith('.h')) {
				if (path_isabs(file.file)) {
					files += '  "' + path_resolve(file.file).replace(/\\/g, '/') + '"\n';
				}
				else {
					files += '  "' + path_resolve(path_join(from, file.file)).replace(/\\/g, '/') + '"\n';
				}
			}
		}
		cmake          = cmake.replace(/{files}/g, files);
		let libraries1 = '';
		let libraries2 = '';
		for (let lib of project.getLibs()) {
			libraries1 += 'find_library(' + lib + '-lib ' + lib + ')\n';
			libraries2 += '  ${' + lib + '-lib}\n';
		}
		cmake         = cmake.replace(/{libraries1}/g, libraries1).replace(/{libraries2}/g, libraries2);
		let cmakePath = path_join(outdir, 'app', 'CMakeLists.txt');
		fs_writefile(cmakePath, cmake);
	}

	write_manifest(outdir, target_options) {
		let manifest = get_text_data('android/main/AndroidManifest.xml');
		manifest     = manifest.replace(/{package}/g, target_options.package);
		manifest     = manifest.replace(/{installLocation}/g, target_options.installLocation);
		manifest     = manifest.replace(/{versionCode}/g, target_options.versionCode.toString());
		manifest     = manifest.replace(/{versionName}/g, target_options.versionName);
		manifest     = manifest.replace(/{screenOrientation}/g, target_options.screenOrientation);
		manifest     = manifest.replace(/{targetSdkVersion}/g, target_options.targetSdkVersion);
		manifest =
		    manifest.replace(/{permissions}/g, target_options.permissions.map((p) => { return '\n\t<uses-permission android:name="' + p + '"/>'; }).join(''));
		let metadata = target_options.disableStickyImmersiveMode ? '\n\t\t<meta-data android:name="disableStickyImmersiveMode" android:value="true"/>' : '';
		for (let meta of target_options.metadata) {
			metadata += '\n\t\t' + meta;
		}
		manifest = manifest.replace(/{metadata}/g, metadata);
		fs_ensuredir(path_join(outdir, 'app', 'src', 'main'));
		fs_writefile(path_join(outdir, 'app', 'src', 'main', 'AndroidManifest.xml'), manifest);
	}

	export_icons(icon, outdir, from, to) {
		let folders = [ 'mipmap-mdpi', 'mipmap-hdpi', 'mipmap-xhdpi', 'mipmap-xxhdpi', 'mipmap-xxxhdpi' ];
		let dpis    = [ 48, 72, 96, 144, 192 ];
		for (let i = 0; i < dpis.length; ++i) {
			let folder = folders[i];
			let dpi    = dpis[i];
			fs_ensuredir(path_join(outdir, 'app', 'src', 'main', 'res', folder));
			export_png_icon(icon, path_resolve(to, this.safe_name, 'app', 'src', 'main', 'res', folder, 'ic_launcher.png'), dpi, dpi, from);
			export_png_icon(icon, path_resolve(to, this.safe_name, 'app', 'src', 'main', 'res', folder, 'ic_launcher_round.png'), dpi, dpi, from);
		}
	}
}

class CompilerCommandsExporter extends Exporter {
	constructor() {
		super();
	}

	export_solution(project) {
		let from     = path_resolve(".");
		let to       = path_resolve("build");
		let platform = goptions.target;
		from         = path_resolve(os_cwd(), from);
		this.write_file(path_resolve(to, 'compile_commands.json'));
		let includes = [];
		for (let inc of project.getIncludeDirs()) {
			includes.push('-I');
			includes.push(path_resolve(from, inc));
		}
		let defines = [];
		for (let def of project.getDefines()) {
			defines.push('-D');
			defines.push(def.replace(/\"/g, '\\"'));
		}
		let objects = {};
		let ofiles  = {};
		for (let fileobject of project.getFiles()) {
			let file = fileobject.file;
			if (file.endsWith('.cpp') || file.endsWith('.c') || file.endsWith('.cc')) {
				let name = file.toLowerCase();
				if (name.indexOf('/') >= 0)
					name = name.substr(name.lastIndexOf('/') + 1);
				name = name.substr(0, name.lastIndexOf('.'));
				if (!objects[name]) {
					objects[name] = true;
					ofiles[file]  = name;
				}
				else {
					while (objects[name]) {
						name = name + '_';
					}
					objects[name] = true;
					ofiles[file]  = name;
				}
			}
		}

		let default_args = [];
		if (platform === 'android') {
			default_args.push('--target=aarch64-none-linux-android21');
			default_args.push('-DANDROID');
			function ndkFromSdkRoot() {
				let _a     = os_env('ANDROID_HOME');
				let sdkEnv = _a !== null ? _a : os_env('ANDROID_SDK_ROOT');
				if (!sdkEnv)
					return null;
				let ndk_dir = path_join(sdkEnv, 'ndk');
				if (!fs_exists(ndk_dir)) {
					return null;
				}
				let ndks = fs_readdir(ndk_dir);
				ndks     = ndks.filter(item => !item.startsWith("."));
				if (ndks.length < 1) {
					return null;
				}
				return path_join(ndk_dir, ndks[0]);
			}
			let _a          = os_env('ANDROID_NDK');
			let android_ndk = _a !== null ? _a : ndkFromSdkRoot();
			if (android_ndk) {
				let host_tag = '';
				switch (os_platform()) {
				case 'linux':
					host_tag = 'linux-x86_64';
					break;
				case 'darwin':
					host_tag = 'darwin-x86_64';
					break;
				case 'win32':
					host_tag = 'windows-x86_64';
					break;
				}
				let ndk_toolchain = path_join(android_ndk, `toolchains/llvm/prebuilt/${host_tag}`);
				if (host_tag !== '' && fs_exists(ndk_toolchain)) {
					default_args.push(`--gcc-toolchain=${ndk_toolchain}`);
					default_args.push(`--sysroot=${ndk_toolchain}/sysroot`);
				}
				else {
					// fallback to the first found toolchain
					let toolchains = fs_readdir(path_join(android_ndk, `toolchains/llvm/prebuilt/`));
					if (toolchains.length > 0) {
						let host_tag      = toolchains[0];
						let ndk_toolchain = path_join(android_ndk, `toolchains/llvm/prebuilt/${host_tag}`);
						default_args.push(`--gcc-toolchain=${ndk_toolchain}`);
						default_args.push(`--sysroot=${ndk_toolchain}/sysroot`);
						console.log(`Found android ndk toolchain in ${ndk_toolchain}.`);
					}
				}
			}
		}

		let commands = [];
		for (let fileobject of project.getFiles()) {
			let file = fileobject.file;
			if (file.endsWith('.c') || file.endsWith('.cpp') || file.endsWith('.cc')) {
				let args = [ '/usr/bin/clang', '-c', '-o', (goptions.debug ? 'Debug' : 'Release') + ofiles[file] + '.o' ];
				if (file.endsWith('.c')) {
					args.push('-std=c99');
				}
				args.push(...default_args);
				args.push(path_resolve(from, file));
				let command = {
					directory : from,
					file : path_resolve(from, file),
					output : path_resolve(to, ofiles[file] + '.o'),
					arguments : args.concat(includes).concat(defines)
				};
				commands.push(command);
			}
		}
		this.p(JSON.stringify(commands));
		this.close_file();
	}
}

// ███╗   ███╗ █████╗ ██╗  ██╗███████╗
// ████╗ ████║██╔══██╗██║ ██╔╝██╔════╝
// ██╔████╔██║███████║█████╔╝ █████╗
// ██║╚██╔╝██║██╔══██║██╔═██╗ ██╔══╝
// ██║ ╚═╝ ██║██║  ██║██║  ██╗███████╗
// ╚═╝     ╚═╝╚═╝  ╚═╝╚═╝  ╚═╝╚══════╝

function export_ico(icon, to, from) {
	if (!fs_exists(path_join(from, icon))) {
		from = irondir;
		icon = "icon.png";
	}
	if (fs_exists(to) && fs_mtime(to) > fs_mtime(from)) {
		return;
	}
	amake.export_ico(path_join(from, icon), to);
}

function export_png_icon(icon, to, width, height, from) {
	if (!fs_exists(path_join(from, icon))) {
		from = irondir;
		icon = "icon.png";
	}
	if (fs_exists(to) && fs_mtime(to) > fs_mtime(from)) {
		return;
	}
	amake.export_png(path_join(from, icon), to, width, height);
}

function contains_define(array, value) {
	return array.indexOf(value) > -1;
}

function contains_fancy_define(array, value) {
	let name = value.substring(0, value.indexOf("="));
	for (let element of array) {
		let index = element.indexOf("=");
		if (index >= 0) {
			let otherName = element.substring(0, index);
			if (name === otherName) {
				return true;
			}
		}
	}
	return false;
}

function load_project(directory, is_root_project) {
	__dirname = path_resolve(directory);

	if (is_root_project) {
		globalThis.platform = goptions.target;
		globalThis.graphics = goptions.graphics;
		globalThis.flags    = {
            name : "Armory",
            package : "org.armory3d",
            dirname : __dirname,
            release : os_argv().indexOf("--debug") == -1,
            embed : false,
            with_physics : false,
            with_d3dcompiler : false,
            with_nfd : false,
            with_compress : false,
            with_image_write : false,
            with_video_write : false,
            with_eval : false,
            with_plugins : false,
            with_kong : false,
            with_raytrace : false,
			with_bc7 : false,
            idle_sleep : false,
            export_version_info : false,
            export_data_list : false
		};
	}

	let project = eval("function _(){" + fs_readfile(path_resolve(directory, "project.js")) + "} _();");

	if (is_root_project) {
		try {
			export_iron_project(project, goptions);
		} catch (error) {
			console.log(error);
			os_exit(1);
		}
	}

	return project;
}

function search_files2(current_dir, pattern) {
	let result = [];
	if (!fs_exists(current_dir)) {
		return result;
	}
	current_dir = path_join(current_dir); ////
	let files   = fs_readdir(current_dir);
	for (let f in files) {
		let file = path_join(current_dir, files[f]);
		if (fs_isdir(file))
			continue;
		file = path_relative(current_dir, file);
		if (matches(stringify(file), stringify(pattern))) {
			result.push(path_join(current_dir, stringify(file)));
		}
	}
	if (pattern.endsWith("**")) {
		let dirs = fs_readdir(current_dir);
		for (let d of dirs) {
			let dir = path_join(current_dir, d);
			if (d.startsWith('.'))
				continue;
			if (!fs_isdir(dir))
				continue;
			result = result.concat(search_files2(dir, pattern));
		}
	}
	return result;
}

class AssetConverter {
	constructor(exporter, options, asset_matchers) {
		this.exporter       = exporter;
		this.options        = options;
		this.asset_matchers = asset_matchers;
	}

	static replace_pattern(pattern, value, filepath, from) {
		let base_path = from;
		let dir_value = path_relative(base_path, path_dirname(filepath));
		if (base_path.length > 0 && base_path[base_path.length - 1] === path_sep && dir_value.length > 0 && dir_value[dir_value.length - 1] !== path_sep) {
			dir_value += path_sep;
		}
		let dir_regex = dir_value === '' ? /{dir}\//g : /{dir}/g;
		return pattern.replace(/{name}/g, value).replace(dir_regex, dir_value);
	}

	static create_export_info(filepath, keep_ext, options, from) {
		let name_value  = path_basename_noext(filepath);
		let destination = path_basename_noext(filepath);
		if (keep_ext || options.noprocessing) {
			destination += path_extname(filepath);
		}
		if (options.destination) {
			destination = AssetConverter.replace_pattern(options.destination, destination, filepath, from);
		}
		if (keep_ext) {
			name_value += path_extname(filepath);
		}
		if (options.name) {
			name_value = AssetConverter.replace_pattern(options.name, name_value, filepath, from);
		}
		return {name : name_value, destination : path_normalize(destination)};
	}

	watch(match, options) {
		match       = path_normalize(match);
		let basedir = match.substring(0, match.lastIndexOf(path_sep));
		let pattern = match;
		if (path_isabs(pattern)) {
			pattern = path_relative(basedir, pattern);
		}
		let files        = search_files2(basedir, pattern);
		let self         = this;
		let parsed_files = [];

		let index = 0;
		for (let file of files) {
			console.log('Exporting asset ' + (index + 1) + ' of ' + files.length + ' (' + path_basename(file) + ').');
			let ext = path_extname(file).toLowerCase();
			switch (ext) {
			case '.png':
			case '.jpg':
			case '.hdr': {
				let export_info = AssetConverter.create_export_info(file, false, options, ".");
				let images;
				if (options.noprocessing) {
					images = self.exporter.copy_blob(file, export_info.destination, globalThis.flags.embed && !options.noembed);
				}
				else {
					images = self.exporter.copy_image(file, export_info.destination, globalThis.flags.embed && !options.noembed);
				}
				parsed_files.push({
					name : export_info.name,
					from : file,
					type : 'image',
					files : images,
					original_width : options.original_width,
					original_height : options.original_height,
					noembed : options.noembed
				});
				break;
			}
			default: {
				let export_info = AssetConverter.create_export_info(file, true, options, ".");
				let blobs       = self.exporter.copy_blob(file, export_info.destination, globalThis.flags.embed && !options.noembed);
				parsed_files.push({
					name : export_info.name,
					from : file,
					type : 'blob',
					files : blobs,
					original_width : undefined,
					original_height : undefined,
					noembed : options.noembed
				});
				break;
			}
			}

			index += 1;
		}
		return parsed_files;
	}

	run() {
		let files = [];
		for (let matcher of this.asset_matchers) {
			files = files.concat(this.watch(matcher.match, matcher.options));
		}
		return files;
	}
}

class CompiledShader {
	constructor() {
		this.files = [];
	}
}

function shader_find_type(options) {
	if (options.graphics === 'vulkan') {
		return 'spirv';
	}
	else if (options.graphics === 'metal') {
		return 'metal';
	}
	else if (options.graphics === 'direct3d12') {
		return 'hlsl';
	}
	else if (options.graphics === 'webgpu') {
		return 'wgsl';
	}
}

class ShaderCompiler {
	constructor(exporter, to, temp, options, shader_matchers) {
		this.exporter        = exporter;
		this.type            = shader_find_type(options);
		this.options         = options;
		this.to              = to;
		this.temp            = temp;
		this.shader_matchers = shader_matchers;
	}

	watch(match, options) {
		match       = path_normalize(match);
		let basedir = match.substring(0, match.lastIndexOf(path_sep));
		let pattern = match;
		if (path_isabs(pattern)) {
			pattern = path_relative(basedir, pattern);
		}

		let shaders          = search_files2(basedir, pattern);
		let self             = this;
		let compiled_shaders = [];

		let index = 0;
		for (let shader of shaders) {
			console.log('Compiling shader ' + (index + 1) + ' of ' + shaders.length + ' (' + path_basename(shader) + ').');
			let compiled_shader = null;
			try {
				compiled_shader = self.compile_shader(shader, options);
			} catch (error) {
				console.log('Compiling shader ' + (index + 1) + ' of ' + shaders.length + ' (' + path_basename(shader) + ') failed:');
				console.log(error);
			}
			if (compiled_shader === null) {
				compiled_shader = new CompiledShader();
			}
			let type = self.type;
			if (type == "hlsl") {
				type = "d3d11";
			}
			let base              = path_resolve('build', 'temp', path_basename_noext(shader));
			compiled_shader.files = [ base + '.vert.' + type, base + '.frag.' + type ];

			compiled_shader.name = AssetConverter.create_export_info(shader, false, options, ".").name;
			compiled_shaders.push(compiled_shader);
			++index;
		}

		return compiled_shaders;
	}

	run() {
		let shaders = [];
		for (let matcher of this.shader_matchers) {
			shaders = shaders.concat(this.watch(matcher.match, matcher.options));
		}
		return shaders;
	}

	shader_outputs(to) {
		let ext  = this.type === 'hlsl' ? 'd3d11' : this.type;
		let base = to.substring(0, to.lastIndexOf('.') + 1);
		return [ base + 'vert.' + ext, base + 'frag.' + ext ];
	}

	compile_shader(file, options) {
		let from = file;
		let to   = path_join(this.to, path_basename_noext(file) + '.' + this.type);

		let from_time = 0;
		let to_time;
		if (fs_exists(from))
			from_time = fs_mtime(from);
		if (fs_exists(to))
			to_time = fs_mtime(to);

		if (options.noprocessing) {
			if (!to_time || to_time < from_time) {
				fs_copyfile(from, to);
			}
			let compiled_shader = new CompiledShader();
			return compiled_shader;
		}

		let out_time;
		for (let out of this.shader_outputs(to)) {
			if (!fs_exists(out)) {
				out_time = undefined;
				break;
			}
			let t = fs_mtime(out);
			if (out_time === undefined || t < out_time) {
				out_time = t;
			}
		}

		if (!from_time || (out_time && out_time > from_time)) {
			return null;
		}
		else {
			fs_ensuredir(this.temp);

			// from = path_resolve(from);
			// to = path_resolve(to);
			// this.temp = path_resolve(this.temp);

			amake.ashader(this.type, from, to);

			let compiled_shader = new CompiledShader();
			return compiled_shader;
		}
	}
}

function export_k(from, to) {
	to += ".k";
	if (fs_exists(to) && fs_mtime(to) > fs_mtime(from)) {
		return "k";
	}
	fs_ensuredir(path_dirname(to));
	amake.export_k(from, to);
}

class IronExporter {
	constructor(project, options) {
		this.options = options;
		this.sources = [];
	}

	export() {
		fs_ensuredir(path_join("build", "out"));
	}

	copy_image(from, to, embed) {
		let to_full = path_join("build", "out", to);
		if (embed) {
			to_full = path_join("build", "temp", to);
		}
		export_k(from, to_full);
		return [ to + ".k" ];
	}

	copy_blob(from, to, embed) {
		fs_ensuredir(path_join("build", "out", path_dirname(to)));
		let to_full = path_join("build", "out", to);
		if (embed && !to.endsWith(".txt") && !to.endsWith(".md") && !to.endsWith(".json") && !to.endsWith(".c") && !to.endsWith(".html") && !to.endsWith(".js") && !to.endsWith(".ico")) {
			fs_ensuredir(path_join("build", "temp", path_dirname(to)));
			to_full = path_join("build", "temp", to);
		}
		if (!fs_exists(to_full) || fs_mtime(to_full) <= fs_mtime(from)) {
			fs_copyfile(from, to_full);
		}
		return [ to ];
	}

	add_source_directory(path) {
		this.sources.push(path);
	}
}

function export_project_files(name, options, exporter, defines) {
	exporter.export();
	return name;
}

function export_iron_project(project, options) {
	let temp = path_join("build", "temp");
	fs_ensuredir(temp);

	let exporter = new IronExporter(project, options);
	fs_ensuredir(path_join("build", "out"));

	for (let source of project.sources) {
		exporter.add_source_directory(source);
	}

	let asset_converter = new AssetConverter(exporter, options, project.asset_matchers);
	let assets          = asset_converter.run();

	let shaderdir = path_join("build", "out", "data");
	if (globalThis.flags.embed) {
		shaderdir = path_join("build", "temp");
	}
	fs_ensuredir(shaderdir);

	let exported_shaders = [];
	let shader_compiler  = new ShaderCompiler(exporter, shaderdir, temp, options, project.shader_matchers);
	exported_shaders     = shader_compiler.run();

	// Write embed.h
	if (globalThis.flags.embed) {
		let embed_files = [];
		for (let asset of assets) {
			if (asset.noembed || asset.from.endsWith(".txt") || asset.from.endsWith(".md") || asset.from.endsWith(".json") || asset.from.endsWith(".c") || asset.from.endsWith(".html") || asset.from.endsWith(".js") || asset.from.endsWith(".ico")) {
				continue;
			}
			embed_files.push(path_resolve("build", "temp", asset.files[0]));
		}
		for (let shader of exported_shaders) {
			embed_files.push(shader.files[0]);
			embed_files.push(shader.files[1]);
		}

		if (embed_files.length > 0) {
			let embed_header = "#pragma once\n";
			for (let file of embed_files) {
				embed_header += "const unsigned char " + path_basename(file).replaceAll(".", "_") + "[] = {\n"
				if (platform === "windows") {
					file = file.replaceAll("\\", "/");
				}
				embed_header += "#embed \"" + file + "\"\n";
				embed_header += "};\n"
			}
			embed_header += "char *embed_keys[] = {\n"
			for (let file of embed_files) {
				let f = file.replaceAll("\\", "/");
				let i = f.lastIndexOf("/temp/data/");
				f = i > -1 ? f.substring(i + 11) : path_basename(file);
				embed_header += "\"./data/" + f + "\",\n";
			}
			embed_header += "};\n"
			embed_header += "const unsigned char *embed_values[] = {\n"
			for (let file of embed_files) {
				embed_header += path_basename(file).replaceAll(".", "_") + ",\n";
			}
			embed_header += "};\n"
			embed_header += "const int embed_sizes[] = {\n";
			for (let file of embed_files) {
				embed_header += "sizeof(" + path_basename(file).replaceAll(".", "_") + "),\n"
			}
			embed_header += "};\n"
			embed_header += "int embed_count = " + embed_files.length + ";\n";
			fs_writefile(path_join("build", "embed.h"), embed_header);
		}
	}

	export_project_files(project.name, options, exporter, project.defines);
}

class Project {
	constructor(name) {
		this.cppStd          = "c++17";
		this.cStd            = "c11";
		this.cmdArgs         = [];
		this.cFlags          = [];
		this.cppFlags        = [];
		this.icon            = "icon.png";
		this.lto             = true;
		this.noFlatten       = true;
		this.name            = name;
		this.safe_name       = name.replace(/[^A-z0-9\-\_]/g, "-");
		this.version         = "1.0";
		this.debugdir        = "build/out";
		this.basedir         = __dirname;
		this.uuid            = crypto_random_uuid();
		this.files           = [];
		this.customs         = [];
		this.javadirs        = [];
		this.subProjects     = [];
		this.includedirs     = [];
		this.defines         = [];
		this.libs            = [];
		this.includes        = [];
		this.target_options  = {android : {}, ios : {}};
		this.executable_name = null;
		this.sources         = [];
		this.asset_matchers  = [];
		this.shader_matchers = [];
	}

	get_executable_name() {
		return this.executable_name;
	}

	flatten_subprojects() {
		for (let sub of this.subProjects) {
			sub.noFlatten = false;
			sub.flatten_subprojects();
		}
	}

	flatten() {
		this.noFlatten = false;
		this.flatten_subprojects();
	}

	internal_flatten() {
		let out = [];
		for (let sub of this.subProjects) {
			sub.internal_flatten();
		}
		for (let sub of this.subProjects) {
			if (sub.noFlatten) {
				out.push(sub);
			}
			else {
				if (!sub.lto) {
					this.lto = false;
				}
				if (sub.icon != "icon.png") {
					this.icon = sub.icon;
				}
				let subbasedir = sub.basedir;
				for (let tkey of Object.keys(sub.target_options)) {
					let target = sub.target_options[tkey];
					for (let key of Object.keys(target)) {
						let options = this.target_options[tkey];
						let option  = target[key];
						if (options[key] == null)
							options[key] = option;
						// push library properties to current array instead
						else if (Array.isArray(options[key]) && Array.isArray(option)) {
							for (let value of option) {
								if (!options[key].includes(value))
									options[key].push(value);
							}
						}
					}
				}
				for (let d of sub.defines) {
					if (d.indexOf("=") >= 0) {
						if (!contains_fancy_define(this.defines, d)) {
							this.defines.push(d);
						}
					}
					else {
						if (!contains_define(this.defines, d)) {
							this.defines.push(d);
						}
					}
				}
				for (let file of sub.files) {
					let absolute = file.file;
					if (!path_isabs(absolute)) {
						absolute = path_join(subbasedir, file.file);
					}
					this.files.push({file : absolute.replace(/\\/g, "/"), options : file.options, projectDir : subbasedir, projectName : sub.name});
				}
				for (let custom of sub.customs) {
					let absolute = custom.file;
					if (!path_isabs(absolute)) {
						absolute = path_join(subbasedir, custom.file);
					}
					this.customs.push({file : absolute.replace(/\\/g, "/"), command : custom.command, output : custom.output});
				}
				for (let i of sub.includedirs)
					if (!this.includedirs.includes(path_resolve(subbasedir, i)))
						this.includedirs.push(path_resolve(subbasedir, i));
				for (let j of sub.javadirs)
					if (!this.javadirs.includes(path_resolve(subbasedir, j)))
						this.javadirs.push(path_resolve(subbasedir, j));
				for (let lib of sub.libs) {
					if (!this.libs.includes(lib))
						this.libs.push(lib);
				}
				for (let flag of sub.cFlags) {
					if (!this.cFlags.includes(flag)) {
						this.cFlags.push(flag);
					}
				}
				for (let flag of sub.cppFlags) {
					if (!this.cppFlags.includes(flag)) {
						this.cppFlags.push(flag);
					}
				}
			}
		}
		this.subProjects = out;
	}

	getName() {
		return this.name;
	}

	get_safe_name() {
		return this.safe_name;
	}

	get_uuid() {
		return this.uuid;
	}

	addCFlag(flag) {
		this.cFlags.push(flag);
	}

	addCFlags() {
		for (let i = 0; i < arguments.length; ++i) {
			if (typeof arguments[i] === "string") {
				this.addCFlag(arguments[i]);
			}
		}
	}

	add_file_for_real(file, options) {
		for (let index in this.files) {
			if (this.files[index].file === file) {
				this.files[index] = {file : file, options : options, projectDir : this.basedir, projectName : this.name};
				return;
			}
		}
		this.files.push({file : file, options : options, projectDir : this.basedir, projectName : this.name});
	}

	search_files(current) {
		if (current === undefined) {
			for (let sub of this.subProjects)
				sub.search_files(undefined);
			this.search_files(this.basedir);
			for (let includeobject of this.includes) {
				if (path_isabs(includeobject.file) && includeobject.file.includes("**")) {
					let starIndex = includeobject.file.indexOf("**");
					let endIndex  = includeobject.file.substring(0, starIndex).replace(/\\/g, "/").lastIndexOf("/");
					this.search_files(includeobject.file.substring(0, endIndex));
				}
				if (includeobject.file.startsWith("../")) {
					let start = "../";
					while (includeobject.file.startsWith(start)) {
						start += "../";
					}
					this.search_files(path_resolve(this.basedir, start));
				}
			}
			return;
		}
		let files = fs_readdir(current);
		for (let f in files) {
			let file   = path_join(current, files[f]);
			let follow = true;
			try {
				if (fs_isdir(file)) {
					follow = false;
				}
			} catch (err) {
				follow = false;
			}
			if (!follow) {
				continue;
			}

			file = path_relative(this.basedir, file);
			for (let includeobject of this.includes) {
				let include = includeobject.file;
				if (path_isabs(include)) {
					let inc = include;
					inc     = path_relative(this.basedir, inc);
					include = inc;
				}
				if (matches(stringify(file), stringify(include))) {
					this.add_file_for_real(stringify(file), includeobject.options);
				}
			}
		}
		let dirs = fs_readdir(current);
		for (let d of dirs) {
			let dir = path_join(current, d);
			if (d.startsWith("."))
				continue;
			let follow = true;
			try {
				if (!fs_isdir(dir)) {
					follow = false;
				}
			} catch (err) {
				follow = false;
			}
			if (!follow) {
				continue;
			}
			this.search_files(dir);
		}
	}

	add_cfiles(file, options) {
		this.includes.push({file : file, options : options});
	}

	add_define(define) {
		if (contains_define(this.defines, define)) {
			return;
		}
		this.defines.push(define);
	}

	add_include_dir(include) {
		if (this.includedirs.includes(include))
			return;
		this.includedirs.push(include);
	}

	add_lib(lib) {
		this.libs.push(lib);
	}

	add_assets(match, options) {
		if (!options)
			options = {};
		if (!path_isabs(match)) {
			let base = stringify(path_resolve(this.basedir));
			if (!base.endsWith('/')) {
				base += '/';
			}
			match = base + match.replace(/\\/g, '/');
		}
		this.asset_matchers.push({match : match, options : options});
	}

	add_shaders(match, options) {
		if (!options)
			options = {};
		if (!path_isabs(match)) {
			let base = stringify(path_resolve(this.basedir));
			if (!base.endsWith('/')) {
				base += '/';
			}
			match = base + match.replace(/\\/g, '/');
		}
		this.shader_matchers.push({match : match, options : options});
	}

	getFiles() {
		return this.files;
	}

	getJavaDirs() {
		return this.javadirs;
	}

	getSubProjects() {
		return this.subProjects;
	}

	getIncludeDirs() {
		return this.includedirs;
	}

	getDefines() {
		return this.defines;
	}

	getLibs() {
		return this.libs;
	}

	get_debug_dir() {
		return this.debugdir;
	}

	add_project(directory) {
		let from = path_isabs(directory) ? directory : path_join(this.basedir, directory);
		if (!fs_exists(from)) {
			return;
		}
		let project = load_project(from, false);
		this.subProjects.push(project);
		this.asset_matchers  = this.asset_matchers.concat(project.asset_matchers);
		this.sources         = this.sources.concat(project.sources);
		this.shader_matchers = this.shader_matchers.concat(project.shader_matchers);
		this.defines         = this.defines.concat(project.defines);
		return project;
	}

	static create(directory) {
		let project = load_project(path_resolve(directory), true);
		let defines = [];
		for (let define of defines) {
			project.add_define(define);
		}
		return project;
	}
}

function export_amake_project() {
	console.log('Creating ' + goptions.target + ' project files.');

	let project = Project.create(".");
	if (goptions.graphics === "metal") {
		project.add_cfiles(path_join("build", 'sources', '*'), {});
	}
	project.search_files(undefined);
	project.internal_flatten();
	fs_ensuredir("build");

	let exporter = null;
	if (goptions.ccompiler === "tcc") {
		let cFlags = goptions.debug ? "" : "-fno-asynchronous-unwind-tables";
		exporter   = new MakeExporter(goptions.ccompiler, goptions.cppcompiler, cFlags, cFlags, "", "");
	}
	else if (goptions.target === 'ios' || goptions.target === 'macos') {
		exporter = new XCodeExporter();
	}
	else if (goptions.target === 'android') {
		exporter = new AndroidExporter();
	}
	else if (goptions.target === 'wasm') {
		exporter = new WasmExporter();
	}
	else if (goptions.target === 'linux') {
		exporter = new LinuxExporter();
	}
	else {
		exporter = new VisualStudioExporter();
	}

	exporter.export_solution(project);
	return project;
}

function compile_project(make, project) {
	if (make.status != 0) {
		os_exit(1);
	}
	let executable_name = project.get_safe_name();
	if (project.get_executable_name()) {
		executable_name = project.get_executable_name();
	}
	if (goptions.target === "linux") {
		let from = path_resolve(path_join("build", goptions.build_path), executable_name);
		let to   = path_resolve(".", project.get_debug_dir(), executable_name);
		fs_copyfile(from, to);
		os_chmod(to, "+x");
	}
	else if (goptions.target === "windows") {
		let from = path_join("x64", goptions.debug ? "Debug" : "Release", executable_name + ".exe");
		let to   = path_resolve("..", project.get_debug_dir(), executable_name + ".exe");
		fs_copyfile(from, to);
	}
	else if (goptions.target === "wasm") {
		let from = path_resolve(path_join("build", goptions.build_path), executable_name + ".wasm");
		let to   = path_resolve(".", project.get_debug_dir(), "start.wasm");
		fs_copyfile(from, to);
	}
	if (goptions.run) {
		if (goptions.target === "macos") {
			os_exec("build/" + (goptions.debug ? "Debug" : "Release") + "/" + project.name + ".app/Contents/MacOS/" + project.name, [], {cwd : "build"});
		}
		else if (goptions.target === "linux") {
			os_exec(path_resolve(".", project.get_debug_dir(), executable_name), [], {cwd : path_resolve(".", project.get_debug_dir())});
		}
		else if (goptions.target === "windows") {
			os_exec(path_resolve("..", project.get_debug_dir(), executable_name), [], {cwd : path_resolve(".", project.get_debug_dir())});
		}
	}
}

function main() {
	console.log('Using Iron from ' + irondir);
	goptions.build_path = goptions.debug ? 'Debug' : 'Release';
	let project         = export_amake_project();

	let project_name = project.get_safe_name();
	if (goptions.compile && project_name !== '') {
		console.log('Compiling...');
		let make = null;
		if (goptions.target == 'linux' || goptions.target == 'wasm' || goptions.ccompiler == "tcc") {
			let cores = os_cpus_length();
			make      = os_exec('make', [ '-j', cores.toString() ], {cwd : path_join("build", goptions.build_path)});
		}
		else if (goptions.target == 'macos' || goptions.target == 'ios') {
			let xcode_options = [ '-configuration', goptions.debug ? 'Debug' : 'Release', '-project', project_name + '.xcodeproj' ];
			make              = os_exec('xcodebuild', xcode_options, {cwd : "build"});
		}
		else if (goptions.target == 'windows') {
			let vswhere = path_join(os_env('ProgramFiles(x86)'), 'Microsoft Visual Studio', 'Installer', 'vswhere.exe');
			let vsvars  = os_exec(vswhere, [ '-products', '*', '-latest', '-find', 'VC\\Auxiliary\\Build\\vcvars64.bat' ]).stdout.trim();
			fs_writefile(path_join("build", 'build.bat'), '@call "' + vsvars + '"\n' +
			                                                  '@MSBuild.exe "' + path_resolve("build", project_name + '.vcxproj') +
			                                                  '" /m /clp:ErrorsOnly /p:Configuration=' + (goptions.debug ? 'Debug' : 'Release') +
			                                                  ',Platform=x64');
			make = os_exec('build.bat', [], {cwd : "build"});
		}
		else if (goptions.target == 'android') {
			let gradlew = (os_platform() === 'win32') ? 'gradlew.bat' : 'bash';
			let args    = (os_platform() === 'win32') ? [] : [ 'gradlew' ];
			args.push('assemble' + (goptions.debug ? 'Debug' : 'Release'));
			make = os_exec(gradlew, args, {cwd : path_join("build", project_name)});
		}
		if (make !== null) {
			compile_project(make, project);
		}
	}
}

function default_target() {
	if (os_platform() === 'linux') {
		return 'linux';
	}
	else if (os_platform() === 'win32') {
		return 'windows';
	}
	else {
		return 'macos';
	}
}

let goptions = {
	target : default_target(),
	graphics : 'default',
	visualstudio : 'vs2026',
	compile : false,
	run : false,
	debug : false,
	ccompiler : 'clang',
	cppcompiler : 'clang++',
	arch : 'default',
	js : false,
	ashader : false,
};

let args = scriptArgs;
for (let i = 1; i < args.length; ++i) {
	let arg = args[i];
	if (arg.startsWith("--")) {
		let name  = arg.substring(2);
		let value = true;
		if (i < args.length - 1 && !args[i + 1].startsWith("--")) {
			++i;
			value = args[i];
		}
		goptions[name] = value;
	}
}

if (goptions.js) {
	globalThis.std          = std;
	globalThis.fs_readfile  = fs_readfile;
	globalThis.fs_writefile = fs_writefile;
	globalThis.fs_exists    = fs_exists;
	globalThis.fs_readdir   = fs_readdir;
	(1, eval)(fs_readfile(goptions.js));
	std.exit();
}

if (goptions.ashader) {
	let type = args[3];
	let from = args[4];
	let to   = args[5];
	amake.ashader(type, from, to);
	std.exit();
}

if (goptions.run) {
	goptions.compile = true;
}

if (goptions.graphics === "default") {
	if (goptions.target === "wasm") {
		goptions.graphics = "webgpu";
	}
	else if (os_platform() === "win32") {
		goptions.graphics = "direct3d12";
	}
	else if (os_platform() === "darwin") {
		goptions.graphics = "metal";
	}
	else {
		goptions.graphics = "vulkan";
	}
}

let start = Date.now();
main();
console.log("Done in " + (Date.now() - start) + "ms.");
