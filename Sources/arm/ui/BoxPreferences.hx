package arm.ui;

import haxe.io.Bytes;
import haxe.Json;
import zui.Id;
import zui.Zui;
import iron.data.Data;
import iron.RenderPath;
import arm.node.MakeMaterial;
import arm.data.LayerSlot;
import arm.io.ImportPlugin;
import arm.io.ImportKeymap;
import arm.io.ImportTheme;
import arm.sys.Path;
import arm.sys.File;
import arm.Enums;

class BoxPreferences {

	public static var htab = Id.handle();
	public static var filesPlugin: Array<String> = null;
	public static var filesKeymap: Array<String> = null;
	public static var themeHandle: Handle;
	public static var presetHandle: Handle;
	static var locales: Array<String> = null;
	static var themes: Array<String> = null;
	static var worldColor = kha.Color.fromValue(0xff030303);

	@:access(zui.Zui)
	public static function show() {
		UIBox.showCustom(function(ui: Zui) {
			if (ui.tab(htab, tr("Interface"), true)) {

				if (locales == null) {
					locales = Translator.getSupportedLocales();
				}

				var localeHandle = Id.handle({position: locales.indexOf(Config.raw.locale)});
				ui.combo(localeHandle, locales, tr("Language"), true);
				if (localeHandle.changed) {
					var localeCode = locales[localeHandle.position];
					Config.raw.locale = localeCode;
					Translator.loadTranslations(localeCode);
					UISidebar.inst.tagUIRedraw();
				}

				var hscale = Id.handle({value: Config.raw.window_scale});
				ui.slider(hscale, tr("UI Scale"), 1.0, 4.0, true, 10);
				if (!hscale.changed && Context.hscaleWasChanged) {
					if (hscale.value == null || Math.isNaN(hscale.value)) hscale.value = 1.0;
					Config.raw.window_scale = hscale.value;
					setScale();
				}
				Context.hscaleWasChanged = hscale.changed;

				var hspeed = Id.handle({value: Config.raw.camera_speed});
				Config.raw.camera_speed = ui.slider(hspeed, tr("Camera Speed"), 0.1, 4.0, true);

				var zoomDirectionHandle = Id.handle({position: Config.raw.zoom_direction});
				ui.combo(zoomDirectionHandle, [tr("Vertical"), tr("Vertical Inverted"), tr("Horizontal"), tr("Horizontal Inverted"), tr("Vertical and Horizontal"), tr("Vertical and Horizontal Inverted")], tr("Direction to Zoom"), true);
				if (zoomDirectionHandle.changed) {
					Config.raw.zoom_direction = zoomDirectionHandle.position;
				}

				Config.raw.node_preview = ui.check(Id.handle({selected: Config.raw.node_preview}), tr("Show Node Preview"));

				#if arm_debug
				Context.cacheDraws = ui.check(Id.handle({selected: Context.cacheDraws}), tr("Cache UI Draws"));
				if (ui.isHovered) ui.tooltip(tr("Enabling may reduce GPU usage"));
				#end

				ui.changed = false;
				Config.raw.show_asset_names = ui.check(Id.handle({selected: Config.raw.show_asset_names}), tr("Show Asset Names"));
				if (ui.changed) {
					UISidebar.inst.tagUIRedraw();
				}

				// ui.text("Node Editor");
				// var gridSnap = ui.check(Id.handle({selected: false}), "Grid Snap");

				ui.endElement();
				ui.row([0.5]);
				if (ui.button(tr("Restore"))) {
					UIMenu.draw(function(ui: Zui) {
						ui.text(tr("Restore defaults?"), Right, ui.t.HIGHLIGHT_COL);
						if (ui.button(tr("Confirm"), Left)) {
							iron.App.notifyOnInit(function() {
								ui.t.ELEMENT_H = App.defaultElementH;
								Config.restore();
								setScale();
								if (filesPlugin != null) for (f in filesPlugin) Plugin.stop(f);
								filesPlugin = null;
								filesKeymap = null;
								MakeMaterial.parseMeshMaterial();
								MakeMaterial.parsePaintMaterial();
							});
						}
						if (ui.button(tr("Import..."), Left)) {
							UIFiles.show("arm", false, function(path: String) {
								Data.getBlob(path, function(b: kha.Blob) {
									var raw = Json.parse(b.toString());
									iron.App.notifyOnInit(function() {
										ui.t.ELEMENT_H = App.defaultElementH;
										Config.importFrom(raw);
										setScale();
										MakeMaterial.parseMeshMaterial();
										MakeMaterial.parsePaintMaterial();
									});
								});
							});
						}
					}, 3);
				}
			}

			if (ui.tab(htab, tr("Theme"), true)) {

				if (themes == null) {
					fetchThemes();
				}
				themeHandle = Id.handle({position: getThemeIndex()});

				ui.beginSticky();
				ui.row([1 / 4, 1 / 4, 1 / 4, 1 / 4]);

				ui.combo(themeHandle, themes, tr("Theme"));
				if (themeHandle.changed) {
					Config.raw.theme = themes[themeHandle.position] + ".json";
					Config.loadTheme(Config.raw.theme);
				}

				if (ui.button(tr("New"))) {
					UIBox.showCustom(function(ui: Zui) {
						if (ui.tab(Id.handle(), tr("New Theme"))) {
							ui.row([0.5, 0.5]);
							var themeName = ui.textInput(Id.handle({text: "new_theme"}), tr("Name"));
							if (ui.button(tr("OK")) || ui.isReturnDown) {
								var template = Json.stringify(arm.App.theme);
								if (!themeName.endsWith(".json")) themeName += ".json";
								var path = Path.data() + Path.sep + "themes" + Path.sep + themeName;
								Krom.fileSaveBytes(path, Bytes.ofString(template).getData());
								fetchThemes(); // Refresh file list
								Config.raw.theme = themeName;
								themeHandle.position = getThemeIndex();
								UIBox.show = false;
								App.redrawUI();
								BoxPreferences.htab.position = 1; // Themes
								BoxPreferences.show();
							}
						}
					});
				}

				if (ui.button(tr("Import"))) {
					UIFiles.show("json", false, function(path: String) {
						ImportTheme.run(path);
					});
				}

				if (ui.button(tr("Export"))) {
					UIFiles.show("json", true, function(path) {
						path += Path.sep + UIFiles.filename;
						if (!path.endsWith(".json")) path += ".json";
						Krom.fileSaveBytes(path, Bytes.ofString(Json.stringify(arm.App.theme)).getData());
					});
				}

				ui.endSticky();

				var i = 0;
				var theme = arm.App.theme;
				var hlist = Id.handle();

				// Viewport color
				var h = hlist.nest(i++, { color: worldColor });
				ui.row([1 / 8, 7 / 8]);
				ui.text("", 0, h.color);
				if (ui.isHovered && ui.inputReleased) {
					UIMenu.draw(function(ui) {
						ui.fill(0, 0, ui._w / ui.SCALE(), ui.t.ELEMENT_H * 9, ui.t.SEPARATOR_COL);
						ui.changed = false;
						zui.Ext.colorWheel(ui, h, false, null, false);
						if (ui.changed) UIMenu.keepOpen = true;
					}, 3);
				}
				var val = untyped h.color;
				if (val < 0) val += untyped 4294967296;
				h.text = untyped val.toString(16);
				ui.textInput(h, "VIEWPORT_COL");
				h.color = untyped parseInt(h.text, 16);

				if (worldColor != h.color) {
					worldColor = h.color;
					var b = Bytes.alloc(4);
					b.set(0, worldColor.Rb);
					b.set(1, worldColor.Gb);
					b.set(2, worldColor.Bb);
					b.set(3, 255);
					Context.emptyEnvmap = kha.Image.fromBytes(b, 1, 1);
					Context.ddirty = 2;
					if (!Context.showEnvmap) {
						iron.Scene.active.world.envmap = Context.emptyEnvmap;
					}
				}

				// Theme fields
				for (key in Reflect.fields(theme)) {
					if (key == "NAME") continue;

					var h = hlist.nest(i++);
					var val: Dynamic = untyped theme[key];
					var isHex = key.endsWith("_COL");
					if (isHex && val < 0) val += untyped 4294967296;

					if (isHex) {
						ui.row([1 / 8, 7 / 8]);
						ui.text("", 0, val);
						if (ui.isHovered && ui.inputReleased) {
							h.color = untyped theme[key];
							UIMenu.draw(function(ui) {
								ui.fill(0, 0, ui._w / ui.SCALE(), ui.t.ELEMENT_H * 9, ui.t.SEPARATOR_COL);
								ui.changed = false;
								untyped theme[key] = zui.Ext.colorWheel(ui, h, false, null, false);
								if (ui.changed) UIMenu.keepOpen = true;
							}, 3);
						}
					}

					if (Std.is(val, Bool)) {
						h.selected = val;
						untyped theme[key] = ui.check(h, key);
					}
					else if (key == "LINK_STYLE") {
						var styles = [tr("Straight"), tr("Curved")];
						h.position = val;
						untyped theme[key] = ui.combo(h, styles, key, true);
					}
					else {
						h.text = isHex ? untyped val.toString(16) : untyped val.toString();
						var res = ui.textInput(h, key);
						if (isHex) untyped theme[key] = parseInt(h.text, 16);
						else untyped theme[key] = parseInt(h.text);
					}
				}
			}

			if (ui.tab(htab, tr("Usage"), true)) {
				Context.undoHandle = Id.handle({value: Config.raw.undo_steps});
				Config.raw.undo_steps = Std.int(ui.slider(Context.undoHandle, tr("Undo Steps"), 1, 64, false, 1));
				if (Config.raw.undo_steps < 1) Config.raw.undo_steps = Std.int(Context.undoHandle.value = 1);
				if (Context.undoHandle.changed) {
					ui.g.end();
					while (History.undoLayers.length < Config.raw.undo_steps) {
						var l = new LayerSlot("_undo" + History.undoLayers.length);
						l.createMask(0, false);
						History.undoLayers.push(l);
					}
					while (History.undoLayers.length > Config.raw.undo_steps) {
						var l = History.undoLayers.pop();
						l.unload();
					}
					History.reset();
					ui.g.begin(false);
				}

				Config.raw.dilate_radius = Std.int(ui.slider(Id.handle({value: Config.raw.dilate_radius}), tr("Dilate Radius"), 0.0, 16.0, true, 1));
				if (ui.isHovered) ui.tooltip(tr("Dilate painted textures to prevent seams"));

				var dilateHandle = Id.handle({position: Config.raw.dilate});
				ui.combo(dilateHandle, [tr("Instant"), tr("Delayed")], tr("Dilate"), true);
				if (dilateHandle.changed) {
					Config.raw.dilate = dilateHandle.position;
				}

				var workspaceHandle = Id.handle({position: Config.raw.workspace});
				ui.combo(workspaceHandle, [tr("Paint"), tr("Material"), tr("Bake")], tr("Default Workspace"), true);
				if (workspaceHandle.changed) {
					Config.raw.workspace = workspaceHandle.position;
				}

				var layerResHandle = Id.handle({position: Config.raw.layer_res});
				ui.combo(layerResHandle, ["128", "256", "512", "1K", "2K", "4K", "8K"], tr("Default Layer Resolution"), true);
				if (layerResHandle.changed) {
					Config.raw.layer_res = layerResHandle.position;
				}

				var serverHandle = Id.handle({text: Config.raw.server});
				Config.raw.server = ui.textInput(serverHandle, tr("Cloud Server"));

				var materialLiveHandle = Id.handle({selected: Config.raw.material_live});
				Config.raw.material_live = ui.check(materialLiveHandle, tr("Live Material Preview"));
				if (ui.isHovered) ui.tooltip(tr("Instantly update material preview on node change"));

				var brushLiveHandle = Id.handle({selected: Config.raw.brush_live});
				Config.raw.brush_live = ui.check(brushLiveHandle, tr("Live Brush Preview"));
				if (ui.isHovered) ui.tooltip(tr("Draw live brush preview in viewport"));
				if (brushLiveHandle.changed) Context.ddirty = 2;

				var brush3dHandle = Id.handle({selected: Config.raw.brush_3d});
				Config.raw.brush_3d = ui.check(brush3dHandle, tr("3D Cursor"));
				if (brush3dHandle.changed) MakeMaterial.parsePaintMaterial();

				ui.enabled = Config.raw.brush_3d;
				var brushDepthRejectHandle = Id.handle({selected: Config.raw.brush_depth_reject});
				Config.raw.brush_depth_reject = ui.check(brushDepthRejectHandle, tr("Depth Reject"));
				if (brushDepthRejectHandle.changed) MakeMaterial.parsePaintMaterial();

				ui.row([0.5, 0.5]);

				var brushAngleRejectHandle = Id.handle({selected: Config.raw.brush_angle_reject});
				Config.raw.brush_angle_reject = ui.check(brushAngleRejectHandle, tr("Angle Reject"));
				if (brushAngleRejectHandle.changed) MakeMaterial.parsePaintMaterial();

				if (!Config.raw.brush_angle_reject) ui.enabled = false;
				var angleDotHandle = Id.handle({value: Context.brushAngleRejectDot});
				Context.brushAngleRejectDot = ui.slider(angleDotHandle, tr("Angle"), 0.0, 1.0, true);
				if (angleDotHandle.changed) {
					MakeMaterial.parsePaintMaterial();
				}
				ui.enabled = true;
			}

			if (ui.tab(htab, tr("Pen"), true)) {
				ui.text(tr("Pressure controls"));
				Config.raw.pressure_radius = ui.check(Id.handle({selected: Config.raw.pressure_radius}), tr("Brush Radius"));
				Config.raw.pressure_hardness = ui.check(Id.handle({selected: Config.raw.pressure_hardness}), tr("Brush Hardness"));
				Config.raw.pressure_opacity = ui.check(Id.handle({selected: Config.raw.pressure_opacity}), tr("Brush Opacity"));
				Config.raw.pressure_angle = ui.check(Id.handle({selected: Config.raw.pressure_angle}), tr("Brush Angle"));
				Config.raw.pressure_sensitivity = ui.slider(Id.handle({value: Config.raw.pressure_sensitivity}), tr("Sensitivity"), 0.0, 2.0, true);

				ui.endElement();
				ui.row([0.5]);
				if (ui.button(tr("Help"))) {
					File.openInStdApp("https://github.com/armory3d/armorpaint_docs#pen");
				}
			}

			Context.hssgi = Id.handle({selected: Config.raw.rp_ssgi});
			Context.hssr = Id.handle({selected: Config.raw.rp_ssr});
			Context.hbloom = Id.handle({selected: Config.raw.rp_bloom});
			Context.hsupersample = Id.handle({position: Config.getSuperSampleQuality(Config.raw.rp_supersample)});
			Context.hvxao = Id.handle({selected: Config.raw.rp_gi});
			if (ui.tab(htab, tr("Viewport"), true)) {
				#if (!arm_vr)

				#if (kha_direct3d12 || kha_vulkan)

				var hpathtracemode = Id.handle({position: Context.pathTraceMode});
				Context.pathTraceMode = ui.combo(hpathtracemode, [tr("Core"), tr("Full")], tr("Path Tracer"), true);
				if (hpathtracemode.changed) {
					arm.render.RenderPathRaytrace.ready = false;
				}

				#else

				var hrendermode = Id.handle({position: Context.renderMode});
				Context.renderMode = ui.combo(hrendermode, [tr("Full"), tr("Mobile")], tr("Renderer"), true);
				if (hrendermode.changed) {
					Context.setRenderPath();
				}

				#end

				#end

				ui.combo(Context.hsupersample, ["0.25x", "0.5x", "1.0x", "1.5x", "2.0x", "4.0x"], tr("Super Sample"), true);
				if (Context.hsupersample.changed) Config.applyConfig();

				#if arm_debug
				var vsyncHandle = Id.handle({selected: Config.raw.window_vsync});
				Config.raw.window_vsync = ui.check(vsyncHandle, tr("VSync"));
				#end

				if (Context.renderMode == RenderDeferred) {
					#if rp_voxelao
					ui.check(Context.hvxao, tr("Voxel AO"));
					if (ui.isHovered) ui.tooltip(tr("Cone-traced AO and shadows"));
					if (Context.hvxao.changed) {
						Config.applyConfig();
					}

					ui.enabled = Context.hvxao.selected;
					var h = Id.handle({value: Context.vxaoOffset});
					Context.vxaoOffset = ui.slider(h, tr("Cone Offset"), 1.0, 4.0, true);
					if (h.changed) Context.ddirty = 2;
					var h = Id.handle({value: Context.vxaoAperture});
					Context.vxaoAperture = ui.slider(h, tr("Aperture"), 1.0, 4.0, true);
					if (h.changed) Context.ddirty = 2;
					ui.enabled = true;
					#end
					ui.check(Context.hssgi, tr("SSAO"));
					if (Context.hssgi.changed) Config.applyConfig();
					ui.check(Context.hssr, tr("SSR"));
					if (Context.hssr.changed) Config.applyConfig();
					ui.check(Context.hbloom, tr("Bloom"));
					if (Context.hbloom.changed) Config.applyConfig();
				}

				var h = Id.handle({value: Config.raw.rp_vignette});
				Config.raw.rp_vignette = ui.slider(h, tr("Vignette"), 0.0, 1.0, true);
				if (h.changed) Context.ddirty = 2;

				// var h = Id.handle({value: Context.autoExposureStrength});
				// Context.autoExposureStrength = ui.slider(h, "Auto Exposure", 0.0, 2.0, true);
				// if (h.changed) Context.ddirty = 2;

				var cam = iron.Scene.active.camera;
				var camRaw = cam.data.raw;
				var near_handle = Id.handle();
				var far_handle = Id.handle();
				near_handle.value = Std.int(camRaw.near_plane * 1000) / 1000;
				far_handle.value = Std.int(camRaw.far_plane * 100) / 100;
				camRaw.near_plane = ui.slider(near_handle, tr("Clip Start"), 0.001, 1.0, true);
				camRaw.far_plane = ui.slider(far_handle, tr("Clip End"), 50.0, 100.0, true);
				if (near_handle.changed || far_handle.changed) {
					cam.buildProjection();
				}

				var dispHandle = Id.handle({value: Config.raw.displace_strength});
				Config.raw.displace_strength = ui.slider(dispHandle, tr("Displacement Strength"), 0.0, 10.0, true);
				if (dispHandle.changed) {
					Context.ddirty = 2;
					MakeMaterial.parseMeshMaterial();
				}
			}
			if (ui.tab(htab, tr("Keymap"), true)) {

				if (filesKeymap == null) {
					fetchKeymaps();
				}

				ui.beginSticky();
				ui.row([1 / 2, 1 / 4, 1 / 4]);

				presetHandle = Id.handle({position: getPresetIndex()});
				ui.combo(presetHandle, filesKeymap, tr("Preset"));
				if (presetHandle.changed) {
					Config.raw.keymap = filesKeymap[presetHandle.position] + ".json";
					Config.applyConfig();
					Config.loadKeymap();
				}

				if (ui.button(tr("Import"))) {
					UIFiles.show("json", false, function(path: String) {
						ImportKeymap.run(path);
					});
				}
				if (ui.button(tr("Export"))) {
					UIFiles.show("json", true, function(dest: String) {
						if (!UIFiles.filename.endsWith(".json")) UIFiles.filename += ".json";
						var path = Path.data() + Path.sep + "keymap_presets" + Path.sep + Config.raw.keymap;
						File.copy(path, dest + Path.sep + UIFiles.filename);
					});
				}

				ui.endSticky();

				ui.separator(8, false);

				var i = 0;
				ui.changed = false;
				for (key in Reflect.fields(Config.keymap)) {
					var h = Id.handle().nest(i++);
					h.text = Reflect.field(Config.keymap, key);
					var text = ui.textInput(h, key, Left);
					Reflect.setField(Config.keymap, key, text);
				}
				if (ui.changed) {
					Config.applyConfig();
					Config.saveKeymap();
				}
			}
			if (ui.tab(htab, tr("Plugins"), true)) {
				ui.beginSticky();
				ui.row([1 / 4, 1 / 4]);
				if (ui.button(tr("New"))) {
					UIBox.showCustom(function(ui: Zui) {
						if (ui.tab(Id.handle(), tr("New Plugin"))) {
							ui.row([0.5, 0.5]);
							var pluginName = ui.textInput(Id.handle({text: "new_plugin"}), tr("Name"));
							if (ui.button(tr("OK")) || ui.isReturnDown) {
								var template =
"let plugin = new arm.Plugin();
let h1 = new zui.Handle();
plugin.drawUI = function(ui) {
	if (ui.panel(h1, 'New Plugin')) {
		if (ui.button('Button')) {
			console.error('Hello');
		}
	}
}
";
								if (!pluginName.endsWith(".js")) pluginName += ".js";
								var path = Path.data() + Path.sep + "plugins" + Path.sep + pluginName;
								Krom.fileSaveBytes(path, Bytes.ofString(template).getData());
								filesPlugin = null; // Refresh file list
								UIBox.show = false;
								App.redrawUI();
								BoxPreferences.htab.position = 6; // Plugins
								BoxPreferences.show();
							}
						}
					});
				}
				if (ui.button(tr("Import"))) {
					UIFiles.show("js,wasm,zip", false, function(path: String) {
						ImportPlugin.run(path);
					});
				}
				ui.endSticky();

				if (filesPlugin == null) {
					fetchPlugins();
				}

				if (Config.raw.plugins == null) Config.raw.plugins = [];
				var h = Id.handle({selected: false});
				for (f in filesPlugin) {
					var isJs = f.endsWith(".js");
					var isWasm = false; //f.endsWith(".wasm");
					if (!isJs && !isWasm) continue;
					var enabled = Config.raw.plugins.indexOf(f) >= 0;
					h.selected = enabled;
					var tag = isJs ? f.split(".")[0] : f;
					ui.check(h, tag);
					if (h.changed && h.selected != enabled) {
						h.selected ? Config.enablePlugin(f) : Config.disablePlugin(f);
						App.redrawUI();
					}
					if (ui.isHovered && ui.inputReleasedR) {
						UIMenu.draw(function(ui: Zui) {
							ui.text(f, Right, ui.t.HIGHLIGHT_COL);
							var path = Path.data() + Path.sep + "plugins" + Path.sep + f;
							if (ui.button(tr("Edit in Text Editor"), Left)) {
								File.openInStdApp(path);
							}
							if (ui.button(tr("Edit in Script Tab"), Left)) {
								iron.data.Data.getBlob("plugins/" + f, function(blob: kha.Blob) {
									TabScript.hscript.text = blob.toString();
									iron.data.Data.deleteBlob("plugins/" + f);
									Console.info("Script opened");
								});

							}
							if (ui.button(tr("Export"), Left)) {
								UIFiles.show("js", true, function(dest: String) {
									if (!UIFiles.filename.endsWith(".js")) UIFiles.filename += ".js";
									File.copy(path, dest + Path.sep + UIFiles.filename);
								});
							}
							if (ui.button(tr("Delete"), Left)) {
								if (Config.raw.plugins.indexOf(f) >= 0) {
									Config.raw.plugins.remove(f);
									Plugin.stop(f);
								}
								filesPlugin.remove(f);
								File.delete(path);
							}
						}, 5);
					}
				}
			}
		}, 600, 400, function() { Config.save(); });
	}

	public static function fetchThemes() {
		themes = File.readDirectory(Path.data() + Path.sep + "themes");
		for (i in 0...themes.length) themes[i] = themes[i].substr(0, themes[i].length - 5); // Strip .json
		themes.unshift("default");
	}

	public static function fetchKeymaps() {
		filesKeymap = File.readDirectory(Path.data() + Path.sep + "keymap_presets");
		for (i in 0...filesKeymap.length) {
			filesKeymap[i] = filesKeymap[i].substr(0, filesKeymap[i].length - 5); // Strip .json
		}
	}

	public static function fetchPlugins() {
		filesPlugin = File.readDirectory(Path.data() + Path.sep + "plugins");
	}

	public static function getThemeIndex(): Int {
		return themes.indexOf(Config.raw.theme.substr(0, Config.raw.theme.length - 5)); // Strip .json
	}

	public static function getPresetIndex(): Int {
		return filesKeymap.indexOf(Config.raw.keymap.substr(0, Config.raw.keymap.length - 5)); // Strip .json
	}

	static function setScale() {
		var scale = Config.raw.window_scale;
		UISidebar.inst.ui.setScale(scale);
		Config.raw.layout[LayoutSidebarW] = Std.int(UISidebar.defaultWindowW * scale);
		UIToolbar.inst.toolbarw = Std.int(UIToolbar.defaultToolbarW * scale);
		UIHeader.inst.headerh = Std.int(UIHeader.defaultHeaderH * scale);
		Config.raw.layout[LayoutStatusH] = Std.int(UIStatus.defaultStatusH * scale);
		UIMenubar.inst.menubarw = Std.int(UIMenubar.defaultMenubarW * scale);
		UISidebar.inst.setIconScale();
		UINodes.inst.ui.setScale(scale);
		UIView2D.inst.ui.setScale(scale);
		App.uiBox.setScale(scale);
		App.uiMenu.setScale(scale);
		App.resize();
	}
}
