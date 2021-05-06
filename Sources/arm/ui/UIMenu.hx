package arm.ui;

import haxe.io.Bytes;
import haxe.Json;
import kha.System;
import kha.Image;
import zui.Zui;
import zui.Id;
import zui.Ext;
import iron.Scene;
import iron.RenderPath;
import iron.system.Input;
import arm.util.ViewportUtil;
import arm.util.UVUtil;
import arm.util.BuildMacros;
import arm.sys.Path;
import arm.sys.File;
import arm.node.MakeMaterial;
import arm.io.ImportAsset;
import arm.render.RenderPathDeferred;
import arm.render.RenderPathForward;
import arm.Enums;

@:access(zui.Zui)
class UIMenu {

	public static var show = false;
	public static var menuCategory = 0;
	public static var menuX = 0;
	public static var menuY = 0;
	public static var menuElements = 0;
	public static var keepOpen = false;
	public static var menuCommands: Zui->Void = null;
	static var changeStarted = false;
	static var showMenuFirst = true;
	static var hideMenu = false;
	static var askToResetLayout = false;

	public static function render(g: kha.graphics2.Graphics) {
		var ui = App.uiMenu;
		var menuW = menuCommands != null ? Std.int(App.defaultElementW * App.uiMenu.SCALE() * 2.0) : Std.int(ui.ELEMENT_W() * 2.0);
		var _BUTTON_COL = ui.t.BUTTON_COL;
		ui.t.BUTTON_COL = ui.t.SEPARATOR_COL;
		var _ELEMENT_OFFSET = ui.t.ELEMENT_OFFSET;
		ui.t.ELEMENT_OFFSET = 0;
		var _ELEMENT_H = ui.t.ELEMENT_H;
		ui.t.ELEMENT_H = 28;

		ui.beginRegion(g, menuX, menuY, menuW);

		if (menuCommands != null) {
			ui.fill(0, 0, ui._w / ui.SCALE(), ui.t.ELEMENT_H * menuElements, ui.t.SEPARATOR_COL);
			menuCommands(ui);
		}
		else {
			if (menuCategory == MenuFile) {
				if (menuButton(ui, tr("New Project..."), Config.keymap.file_new)) Project.projectNewBox();
				if (menuButton(ui, tr("Open..."), Config.keymap.file_open)) Project.projectOpen();

				#if !(krom_android || krom_ios)
				if (menuButton(ui, tr("Open Recent..."), Config.keymap.file_open_recent)) Project.projectOpenRecentBox();
				#end

				if (menuButton(ui, tr("Save"), Config.keymap.file_save)) Project.projectSave();

				#if !(krom_android || krom_ios)
				if (menuButton(ui, tr("Save As..."), Config.keymap.file_save_as)) Project.projectSaveAs();
				#end

				menuSeparator(ui);
				if (menuButton(ui, tr("Import Texture..."), Config.keymap.file_import_assets)) Project.importAsset(Path.textureFormats.join(","), false);
				if (menuButton(ui, tr("Import Envmap..."))) {
					UIFiles.show("hdr", false, function(path: String) {
						if (!path.endsWith(".hdr")) {
							Console.error("Error: .hdr file expected");
							return;
						}
						ImportAsset.run(path);
					});
				}
				if (menuButton(ui, tr("Import Font..."))) Project.importAsset("ttf,ttc,otf");
				if (menuButton(ui, tr("Import Material..."))) Project.importMaterial();
				if (menuButton(ui, tr("Import Brush..."))) Project.importBrush();
				if (menuButton(ui, tr("Import Swatches..."))) Project.importAsset("arm");
				if (menuButton(ui, tr("Import Mesh..."))) Project.importMesh();
				if (menuButton(ui, tr("Reimport Mesh"), Config.keymap.file_reimport_mesh)) Project.reimportMesh();
				if (menuButton(ui, tr("Reimport Textures"), Config.keymap.file_reimport_textures)) Project.reimportTextures();
				menuSeparator(ui);
				if (menuButton(ui, tr("Export Textures..."), Config.keymap.file_export_textures_as)) {
					Context.layersExport = ExportVisible;
					BoxExport.showTextures();
				}
				if (menuButton(ui, tr("Export Swatches..."))) Project.exportSwatches();
				if (menuButton(ui, tr("Export Mesh..."))) BoxExport.showMesh();
				if (menuButton(ui, tr("Bake Material..."))) BoxExport.showBakeMaterial();

				#if !(krom_android || krom_ios)
				menuSeparator(ui);
				if (menuButton(ui, tr("Exit"))) System.stop();
				#end
			}
			else if (menuCategory == MenuEdit) {
				var stepUndo = "";
				var stepRedo = "";
				if (History.undos > 0) {
					stepUndo = History.steps[History.steps.length - 1 - History.redos].name;
				}
				if (History.redos > 0) {
					stepRedo = History.steps[History.steps.length - History.redos].name;
				}
				ui.enabled = History.undos > 0;
				if (menuButton(ui, tr("Undo {step}", ["step" => stepUndo]), Config.keymap.edit_undo)) History.undo();
				ui.enabled = History.redos > 0;
				if (menuButton(ui, tr("Redo {step}", ["step" => stepRedo]), Config.keymap.edit_redo)) History.redo();
				ui.enabled = true;
				menuSeparator(ui);
				if (menuButton(ui, tr("Reset Layout"))) askToResetLayout = true;
				if (menuButton(ui, tr("Preferences..."), Config.keymap.edit_prefs)) BoxPreferences.show();
			}
			else if (menuCategory == MenuViewport) {
				if (menuButton(ui, tr("Distract Free"), Config.keymap.view_distract_free)) {
					UISidebar.inst.toggleDistractFree();
					UISidebar.inst.ui.isHovered = false;
				}

				#if !(krom_android || krom_ios)
				if (menuButton(ui, tr("Toggle Fullscreen"), "alt+enter")) {
					App.toggleFullscreen();
				}
				#end

				ui.changed = false;

				menuFill(ui);
				var p = Scene.active.world.probe;
				var envHandle = Id.handle();
				envHandle.value = p.raw.strength;
				ui.row([1 / 8, 7 / 8]);
				ui.endElement();
				p.raw.strength = ui.slider(envHandle, tr("Environment"), 0.0, 8.0, true);
				if (envHandle.changed) Context.ddirty = 2;

				if (Scene.active.lights.length > 0) {
					var light = Scene.active.lights[0];

					menuFill(ui);
					var lhandle = Id.handle();
					var scale = 1333;
					lhandle.value = light.data.raw.strength / scale;
					lhandle.value = Std.int(lhandle.value * 100) / 100;
					ui.row([1 / 8, 7 / 8]);
					ui.endElement();
					light.data.raw.strength = ui.slider(lhandle, tr("Light"), 0.0, 4.0, true) * scale;
					if (lhandle.changed) Context.ddirty = 2;

					menuFill(ui);
					var sxhandle = Id.handle();
					sxhandle.value = light.data.raw.size;
					ui.row([1 / 8, 7 / 8]);
					ui.endElement();
					light.data.raw.size = ui.slider(sxhandle, tr("Light Size"), 0.0, 4.0, true);
					if (sxhandle.changed) Context.ddirty = 2;
				}

				menuFill(ui);
				var splitViewHandle = Id.handle({selected: Context.splitView});
				Context.splitView = ui.check(splitViewHandle, " " + tr("Split View"));
				if (splitViewHandle.changed) {
					App.resize();
				}

				menuFill(ui);
				var cullHandle = Id.handle({selected: Context.cullBackfaces});
				Context.cullBackfaces = ui.check(cullHandle, " " + tr("Cull Backfaces"));
				if (cullHandle.changed) {
					MakeMaterial.parseMeshMaterial();
				}

				menuFill(ui);
				var filterHandle = Id.handle({selected: Context.textureFilter});
				Context.textureFilter = ui.check(filterHandle, " " + tr("Filter Textures"));
				if (filterHandle.changed) {
					MakeMaterial.parsePaintMaterial();
					MakeMaterial.parseMeshMaterial();
				}

				menuFill(ui);
				Context.drawWireframe = ui.check(Context.wireframeHandle, " " + tr("Wireframe"));
				if (Context.wireframeHandle.changed) {
					ui.g.end();
					UVUtil.cacheUVMap();
					ui.g.begin(false);
					MakeMaterial.parseMeshMaterial();
				}

				menuFill(ui);
				Context.drawTexels = ui.check(Context.texelsHandle, " " + tr("Texels"));
				if (Context.texelsHandle.changed) {
					MakeMaterial.parseMeshMaterial();
				}

				menuFill(ui);
				var compassHandle = Id.handle({selected: Context.showCompass});
				Context.showCompass = ui.check(compassHandle, " " + tr("Compass"));
				if (compassHandle.changed) Context.ddirty = 2;

				menuFill(ui);
				Context.showEnvmap = ui.check(Context.showEnvmapHandle, " " + tr("Envmap"));
				if (Context.showEnvmapHandle.changed) {
					Context.loadEnvmap();
					Context.ddirty = 2;
				}

				menuFill(ui);
				Context.showEnvmapBlur = ui.check(Context.showEnvmapBlurHandle, " " + tr("Blur Envmap"));
				if (Context.showEnvmapBlurHandle.changed) Context.ddirty = 2;
				if (Context.showEnvmap) {
					Scene.active.world.envmap = Context.showEnvmapBlur ? Scene.active.world.probe.radianceMipmaps[0] : Context.savedEnvmap;
				}
				else {
					Scene.active.world.envmap = Context.emptyEnvmap;
				}

				if (ui.changed) keepOpen = true;
			}
			else if (menuCategory == MenuMode) {
				var modeHandle = Id.handle();
				modeHandle.position = Context.viewportMode;
				var modes = [
					tr("Lit"),
					tr("Base Color"),
					tr("Normal"),
					tr("Occlusion"),
					tr("Roughness"),
					tr("Metallic"),
					tr("Opacity"),
					tr("Height"),
					tr("TexCoord"),
					tr("Object Normal"),
					tr("Material ID"),
					tr("Object ID"),
					tr("Mask")
				];
				#if (kha_direct3d12 || kha_vulkan)
				modes.push(tr("Path Traced"));
				#end
				for (i in 0...modes.length) {
					menuFill(ui);
					ui.radio(modeHandle, i, modes[i]);
				}

				Context.viewportMode = modeHandle.position;
				if (modeHandle.changed) {
					var deferred = Context.renderMode != RenderForward && (Context.viewportMode == ViewLit || Context.viewportMode == ViewPathTrace);
					if (deferred) {
						RenderPath.active.commands = RenderPathDeferred.commands;
					}
					// else if (Context.viewportMode == ViewPathTrace) {
					// }
					else {
						if (RenderPathForward.path == null) {
							RenderPathForward.init(RenderPath.active);
						}
						RenderPath.active.commands = RenderPathForward.commands;
					}
					var _workspace = UIHeader.inst.worktab.position;
					UIHeader.inst.worktab.position = SpacePaint;
					MakeMaterial.parseMeshMaterial();
					UIHeader.inst.worktab.position = _workspace;
				}
			}
			else if (menuCategory == MenuCamera) {
				if (menuButton(ui, tr("Reset"), Config.keymap.view_reset)) {
					ViewportUtil.resetViewport();
					ViewportUtil.scaleToBounds();
				}
				menuSeparator(ui);
				if (menuButton(ui, tr("Front"), Config.keymap.view_front)) {
					ViewportUtil.setView(0, -1, 0, Math.PI / 2, 0, 0);
				}
				if (menuButton(ui, tr("Back"), Config.keymap.view_back)) {
					ViewportUtil.setView(0, 1, 0, Math.PI / 2, 0, Math.PI);
				}
				if (menuButton(ui, tr("Right"), Config.keymap.view_right)) {
					ViewportUtil.setView(1, 0, 0, Math.PI / 2, 0, Math.PI / 2);
				}
				if (menuButton(ui, tr("Left"), Config.keymap.view_left)) {
					ViewportUtil.setView(-1, 0, 0, Math.PI / 2, 0, -Math.PI / 2);
				}
				if (menuButton(ui, tr("Top"), Config.keymap.view_top)) {
					ViewportUtil.setView(0, 0, 1, 0, 0, 0);
				}
				if (menuButton(ui, tr("Bottom"), Config.keymap.view_bottom)) {
					ViewportUtil.setView(0, 0, -1, Math.PI, 0, Math.PI);
				}
				menuSeparator(ui);

				ui.changed = false;

				if (menuButton(ui, tr("Orbit Left"), Config.keymap.view_orbit_left)) {
					ViewportUtil.orbit(-Math.PI / 12, 0);
				}
				if (menuButton(ui, tr("Orbit Right"), Config.keymap.view_orbit_right)) {
					ViewportUtil.orbit(Math.PI / 12, 0);
				}
				if (menuButton(ui, tr("Orbit Up"), Config.keymap.view_orbit_up)) {
					ViewportUtil.orbit(0, -Math.PI / 12);
				}
				if (menuButton(ui, tr("Orbit Down"), Config.keymap.view_orbit_down)) {
					ViewportUtil.orbit(0, Math.PI / 12);
				}
				if (menuButton(ui, tr("Orbit Opposite"), Config.keymap.view_orbit_opposite)) {
					ViewportUtil.orbitOpposite();
				}
				if (menuButton(ui, tr("Zoom In"), Config.keymap.view_zoom_in)) {
					ViewportUtil.zoom(0.2);
				}
				if (menuButton(ui, tr("Zoom Out"), Config.keymap.view_zoom_out)) {
					ViewportUtil.zoom(-0.2);
				}
				// menuSeparator(ui);

				menuFill(ui);
				var cam = Scene.active.camera;
				Context.fovHandle = Id.handle({value: Std.int(cam.data.raw.fov * 100) / 100});
				ui.row([1 / 8, 7 / 8]);
				ui.endElement();
				cam.data.raw.fov = ui.slider(Context.fovHandle, tr("FoV"), 0.3, 2.0, true);
				if (Context.fovHandle.changed) {
					ViewportUtil.updateCameraType(Context.cameraType);
				}

				menuFill(ui);
				ui.row([1 / 8, 7 / 8]);
				ui.endElement();
				Context.cameraControls = Ext.inlineRadio(ui, Id.handle({position: Context.cameraControls}), [tr("Orbit"), tr("Rotate"), tr("Fly")], Left);

				menuFill(ui);
				ui.row([1 / 8, 7 / 8]);
				ui.endElement();
				Context.cameraType = Ext.inlineRadio(ui, Context.camHandle, [tr("Perspective"), tr("Orthographic")], Left);
				if (ui.isHovered) ui.tooltip(tr("Camera Type") + ' (${Config.keymap.view_camera_type})');
				if (Context.camHandle.changed) {
					ViewportUtil.updateCameraType(Context.cameraType);
				}

				if (ui.changed) keepOpen = true;

			}
			else if (menuCategory == MenuHelp) {
				if (menuButton(ui, tr("Manual"))) {
					File.openInStdApp("https://armorpaint.org/manual");
				}
				if (menuButton(ui, tr("What's New"))) {
					File.openInStdApp("https://armorpaint.org/notes");
				}
				if (menuButton(ui, tr("Issue Tracker"))) {
					File.openInStdApp("https://github.com/armory3d/armorpaint/issues");
				}
				if (menuButton(ui, tr("Report Bug"))) {
					var url = "https://github.com/armory3d/armorpaint/issues/new?labels=bug&template=bug_report.md&body=*ArmorPaint%20" + Main.version + "-" + Main.sha + ",%20" + System.systemId + "*%0A%0A**Issue description:**%0A%0A**Steps to reproduce:**%0A%0A";
					File.openInStdApp(url);
				}
				if (menuButton(ui, tr("Request Feature"))) {
					var url = "https://github.com/armory3d/armorpaint/issues/new?labels=feature%20request&template=feature_request.md&body=*ArmorPaint%20" + Main.version + "-" + Main.sha + ",%20" + System.systemId + "*%0A%0A**Feature description:**%0A%0A";
					File.openInStdApp(url);
				}
				menuSeparator(ui);

				#if !(krom_android || krom_ios)
				if (menuButton(ui, tr("Check for Updates..."))) {
					// Retrieve latest version number
					var url = "'https://luboslenco.gitlab.io/armorpaint/index.html'";
					var blob = File.downloadBytes(url);
					if (blob != null)  {
						// Compare versions
						var update = Json.parse(blob.toString());
						var updateVersion = Std.int(update.version);
						if (updateVersion > 0) {
							var date = BuildMacros.date().split(" ")[0].substr(2); // 2019 -> 19
							var dateInt = Std.parseInt(date.replace("-", ""));
							if (updateVersion > dateInt) {
								UIBox.showMessage(tr("Update"), tr("Update is available!\nPlease visit armorpaint.org to download."));
							}
							else {
								UIBox.showMessage(tr("Update"), tr("You are up to date!"));
							}
						}
					}
					else {
						UIBox.showMessage(tr("Update"), tr("Unable to check for updates.\nPlease visit armorpaint.org."));
					}
				}
				#end

				if (menuButton(ui, tr("About..."))) {
					#if kha_direct3d11
					var gapi = "Direct3D11";
					#elseif kha_direct3d12
					var gapi = "Direct3D12";
					#elseif kha_metal
					var gapi = "Metal";
					#elseif kha_vulkan
					var gapi = "Vulkan";
					#else
					var gapi = "OpenGL";
					#end
					var msg = "ArmorPaint.org - v" + Main.version + " (" + Main.date + ") - " + Main.sha + "\n";
					msg += System.systemId + " - " + gapi;

					#if krom_windows
					var save = (Path.isProtected() ? Krom.savePath() : Path.data()) + Path.sep + "tmp.txt";
					Krom.sysCommand('wmic path win32_VideoController get name > "' + save + '"');
					var bytes = haxe.io.Bytes.ofData(Krom.loadBlob(save));
					var gpu = "";
					for (i in 30...Std.int(bytes.length / 2)) {
						var c = String.fromCharCode(bytes.get(i * 2));
						if (c == "\n") continue;
						gpu += c;
					}
					msg += '\n$gpu';
					#else
					// { lshw -C display }
					#end

					UIBox.showMessage(tr("About"), msg, true);
				}
			}
		}

		var first = showMenuFirst;
		hideMenu = ui.comboSelectedHandle == null && !changeStarted && !keepOpen && !first && (ui.changed || ui.inputReleased || ui.inputReleasedR || ui.isEscapeDown);
		showMenuFirst = false;
		keepOpen = false;
		if (ui.inputReleased) changeStarted = false;

		ui.t.BUTTON_COL = _BUTTON_COL;
		ui.t.ELEMENT_OFFSET = _ELEMENT_OFFSET;
		ui.t.ELEMENT_H = _ELEMENT_H;
		ui.endRegion();
	}

	public static function update() {
		//var ui = App.uiMenu;
		if (hideMenu) {
			show = false;
			App.redrawUI();
			showMenuFirst = true;
			menuCommands = null;
		}

		if (askToResetLayout) {
			askToResetLayout = false;
			UIMenu.draw(function(ui: Zui) {
				ui.text(tr("Reset layout?"), Right, ui.t.HIGHLIGHT_COL);
				if (ui.button(tr("Confirm"), Left)) {
					Config.initLayout();
					Config.save();
				}
			}, 2);
		}
	}

	public static function draw(commands: Zui->Void = null, elements: Int, x = -1, y = -1) {
		show = true;
		menuCommands = commands;
		menuElements = elements;
		menuX = x > -1 ? x : Std.int(Input.getMouse().x);
		menuY = y > -1 ? y : Std.int(Input.getMouse().y);
		var menuW = App.defaultElementW * App.uiMenu.SCALE() * 2.0;
		if (menuX + menuW > System.windowWidth()) {
			menuX = Std.int(System.windowWidth() - menuW);
		}
		var menuH = menuElements * 28; // ui.t.ELEMENT_H
		if (menuY + menuH > System.windowHeight()) {
			menuY = System.windowHeight() - menuH;
			menuX += 1; // Move out of mouse focus
		}
	}

	static function menuFill(ui: Zui) {
		ui.g.color = ui.t.SEPARATOR_COL;
		ui.g.fillRect(ui._x, ui._y, ui._w, ui.ELEMENT_H());
		ui.g.color = 0xffffffff;
	}

	static function menuSeparator(ui: Zui) {
		ui._y++;
		ui.fill(0, 0, ui._w / ui.SCALE(), 1, ui.t.ACCENT_SELECT_COL);
	}

	static function menuButton(ui: Zui, text: String, label = ""): Bool {
		menuFill(ui);
		return ui.button("      " + text, Left, label);
	}
}
