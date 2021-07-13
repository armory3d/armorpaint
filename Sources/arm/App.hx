package arm;

import haxe.io.Bytes;
import kha.graphics2.truetype.StbTruetype;
import kha.Image;
import kha.Font;
import kha.System;
import zui.Zui;
import zui.Themes;
import zui.Nodes;
import iron.Scene;
import iron.data.Data;
import iron.system.Input;
import arm.ui.UISidebar;
import arm.ui.UIToolbar;
import arm.ui.UINodes;
import arm.ui.UIView2D;
import arm.ui.UIMenu;
import arm.ui.UIBox;
import arm.ui.UIFiles;
import arm.ui.UIHeader;
import arm.ui.UIStatus;
import arm.ui.UIMenubar;
import arm.ui.TabSwatches;
import arm.ui.TabLayers;
import arm.ui.BoxExport;
import arm.io.ImportAsset;
import arm.io.ExportMesh;
import arm.io.ExportTexture;
import arm.sys.File;
import arm.sys.Path;
import arm.util.RenderUtil;
import arm.Viewport;
import arm.data.MaterialSlot;
import arm.data.LayerSlot;
import arm.data.ConstData;
import arm.node.MakeMaterial;
import arm.Camera;
import arm.Enums;
import arm.ProjectFormat;
import arm.Res;

class App {

	public static var uiEnabled = true;
	public static var isDragging = false;
	public static var isResizing = false;
	public static var dragMaterial: MaterialSlot = null;
	public static var dragLayer: LayerSlot = null;
	public static var dragAsset: TAsset = null;
	public static var dragSwatch: TSwatchColor = null;
	public static var dragFile: String = null;
	public static var dragTint = 0xffffffff;
	public static var dragSize = -1;
	public static var dragRect: TRect = null;
	public static var dragOffX = 0.0;
	public static var dragOffY = 0.0;
	public static var dropX = 0.0;
	public static var dropY = 0.0;
	public static var font: Font = null;
	public static var theme: TTheme;
	public static var colorWheel: Image;
	public static var uiBox: Zui;
	public static var uiMenu: Zui;
	public static var defaultElementW = 100;
	public static var defaultElementH = 28;
	public static var defaultFontSize = 13;
	public static var resHandle = new Handle();
	public static var bitsHandle = new Handle();
	static var dropPaths: Array<String> = [];
	static var appx = 0;
	static var appy = 0;
	static var lastWindowWidth = 0;
	static var lastWindowHeight = 0;

	public function new() {
		Console.init();
		lastWindowWidth = System.windowWidth();
		lastWindowHeight = System.windowHeight();

		#if arm_resizable
		iron.App.onResize = onResize;
		#end

		System.notifyOnDropFiles(function(dropPath: String) {
			#if krom_linux
			dropPath = untyped decodeURIComponent(dropPath);
			dropPaths = dropPath.split("file://");
			for (i in 0...dropPaths.length) dropPaths[i] = dropPaths[i].rtrim();
			#else
			dropPath = dropPath.rtrim();
			dropPaths.push(dropPath);
			#end
			#if krom_ios
			// Import immediately while access to resource is unlocked
			handleDropPaths();
			#end
		});

		System.notifyOnApplicationState(
			function() { // Foreground
				Context.foregroundEvent = true;
				Context.lastPaintX = -1;
				Context.lastPaintY = -1;
			},
			function() {}, // Resume
			function() {}, // Pause
			function() { // Background
				// Release keys after alt-tab / win-tab
				@:privateAccess Input.getKeyboard().upListener(kha.input.KeyCode.Alt);
				@:privateAccess Input.getKeyboard().upListener(kha.input.KeyCode.Win);
			},
			function() {} // Shutdown
		);

		Krom.setSaveAndQuitCallback(saveAndQuitCallback);

		Data.getFont("font.ttf", function(f: Font) {
			Data.getImage("color_wheel.k", function(image: Image) {

				font = f;
				Config.loadTheme(Config.raw.theme, false);
				defaultElementW = theme.ELEMENT_W;
				defaultFontSize = theme.FONT_SIZE;
				Translator.loadTranslations(Config.raw.locale);
				UIFiles.filename = tr("untitled");

				// Precompiled font for fast startup
				if (Config.raw.locale == "en") {
					var kimg: kha.Font.KravurImage = js.lib.Object.create(untyped kha.Font.KravurImage.prototype);
					@:privateAccess kimg.mySize = 13;
					@:privateAccess kimg.width = 128;
					@:privateAccess kimg.height = 128;
					@:privateAccess kimg.baseline = 10;
					var chars = new haxe.ds.Vector(ConstData.font_x0.length);
					kha.graphics2.Graphics.fontGlyphs = [for (i in 32...127) i];
					for (i in 0...95) chars[i] = new Stbtt_bakedchar();
					for (i in 0...ConstData.font_x0.length) chars[i].x0 = ConstData.font_x0[i];
					for (i in 0...ConstData.font_y0.length) chars[i].y0 = ConstData.font_y0[i];
					for (i in 0...ConstData.font_x1.length) chars[i].x1 = ConstData.font_x1[i];
					for (i in 0...ConstData.font_y1.length) chars[i].y1 = ConstData.font_y1[i];
					for (i in 0...ConstData.font_xoff.length) chars[i].xoff = ConstData.font_xoff[i];
					for (i in 0...ConstData.font_yoff.length) chars[i].yoff = ConstData.font_yoff[i];
					for (i in 0...ConstData.font_xadvance.length) chars[i].xadvance = ConstData.font_xadvance[i];
					@:privateAccess kimg.chars = chars;
					Data.getBlob("font13.bin", function(fontbin: kha.Blob) {
						@:privateAccess kimg.texture = Image.fromBytes(fontbin.toBytes(), 128, 128, kha.graphics4.TextureFormat.L8);
						@:privateAccess cast(font, kha.Font).images.set(130095, kimg);
					});
				}

				colorWheel = image;
				Nodes.enumTexts = enumTexts;
				Nodes.tr = tr;
				uiBox = new Zui({ theme: App.theme, font: f, scaleFactor: Config.raw.window_scale, color_wheel: colorWheel });
				uiMenu = new Zui({ theme: App.theme, font: f, scaleFactor: Config.raw.window_scale, color_wheel: colorWheel });
				defaultElementH = uiMenu.t.ELEMENT_H;

				// Init plugins
				if (Config.raw.plugins != null) {
					for (plugin in Config.raw.plugins) {
						Plugin.start(plugin);
					}
				}

				Args.parse();

				iron.App.notifyOnUpdate(update);
				new UISidebar();
				new UINodes();
				new UIView2D();
				new Camera();
				iron.App.notifyOnRender2D(UIView2D.inst.render);
				iron.App.notifyOnUpdate(UIView2D.inst.update);
				iron.App.notifyOnRender2D(UISidebar.inst.renderCursor);
				iron.App.notifyOnUpdate(UINodes.inst.update);
				iron.App.notifyOnRender2D(UINodes.inst.render);
				iron.App.notifyOnUpdate(UISidebar.inst.update);
				iron.App.notifyOnRender2D(UISidebar.inst.render);
				iron.App.notifyOnRender2D(render);
				appx = UIToolbar.inst.toolbarw;
				appy = UIHeader.inst.headerh * 2;
				var cam = Scene.active.camera;
				cam.data.raw.fov = Std.int(cam.data.raw.fov * 100) / 100;
				cam.buildProjection();

				Args.run();
			});
		});
	}

	static function saveAndQuitCallback(save: Bool) {
		saveWindowRect();
		if (save) Project.projectSave(true);
		else System.stop();
	}

	public static function w(): Int {
		// Draw material preview
		if (UISidebar.inst != null && Context.materialPreview) {
			return RenderUtil.matPreviewSize;
		}

		// Drawing decal preview
		if (UISidebar.inst != null && Context.decalPreview) {
			return RenderUtil.decalPreviewSize;
		}

		var res = 0;
		if (UINodes.inst == null || UISidebar.inst == null) {
			var sidebarw = Config.raw.layout == null ? UISidebar.defaultWindowW : Config.raw.layout[LayoutSidebarW];
			res = System.windowWidth() - sidebarw - UIToolbar.defaultToolbarW;
		}
		else if (UINodes.inst.show || UIView2D.inst.show) {
			res = System.windowWidth() - Config.raw.layout[LayoutSidebarW] - Config.raw.layout[LayoutNodesW] - UIToolbar.inst.toolbarw;
		}
		else if (UISidebar.inst.show) {
			res = System.windowWidth() - Config.raw.layout[LayoutSidebarW] - UIToolbar.inst.toolbarw;
		}
		else { // Distract free
			res = System.windowWidth();
		}
		if (UISidebar.inst != null && Context.viewIndex > -1) {
			res = Std.int(res / 2);
		}
		if (Context.paint2dView) {
			res = UIView2D.inst.ww;
		}

		return res > 0 ? res : 1; // App was minimized, force render path resize
	}

	public static function h(): Int {
		// Draw material preview
		if (UISidebar.inst != null && Context.materialPreview) {
			return RenderUtil.matPreviewSize;
		}

		// Drawing decal preview
		if (UISidebar.inst != null && Context.decalPreview) {
			return RenderUtil.decalPreviewSize;
		}

		var res = System.windowHeight();
		if (UISidebar.inst == null) {
			res -= UIHeader.defaultHeaderH * 2 + UIStatus.defaultStatusH;
		}
		else if (UISidebar.inst != null && UISidebar.inst.show && res > 0) {
			var statush = Config.raw.layout[LayoutStatusH];
			res -= Std.int(UIHeader.defaultHeaderH * 2 * Config.raw.window_scale) + statush;
		}

		return res > 0 ? res : 1; // App was minimized, force render path resize
	}

	public static function x(): Int {
		return Context.viewIndex == 1 ? appx + w() : appx;
	}

	public static function y(): Int {
		return appy;
	}

	#if arm_resizable
	static function onResize() {
		if (System.windowWidth() == 0 || System.windowHeight() == 0) return;

		var ratioW = System.windowWidth() / lastWindowWidth;
		lastWindowWidth = System.windowWidth();
		var ratioH = System.windowHeight() / lastWindowHeight;
		lastWindowHeight = System.windowHeight();

		Config.raw.layout[LayoutNodesW] = Std.int(Config.raw.layout[LayoutNodesW] * ratioW);
		Config.raw.layout[LayoutSidebarH0] = Std.int(Config.raw.layout[LayoutSidebarH0] * ratioH);
		Config.raw.layout[LayoutSidebarH1] = Std.int(Config.raw.layout[LayoutSidebarH1] * ratioH);
		Config.raw.layout[LayoutSidebarH2] = System.windowHeight() - Config.raw.layout[LayoutSidebarH0] - Config.raw.layout[LayoutSidebarH1];

		resize();

		#if (krom_linux || krom_darwin)
		saveWindowRect();
		#end
	}
	#end

	static function saveWindowRect() {
		#if (krom_windows || krom_linux || krom_darwin)
		Config.raw.window_w = System.windowWidth();
		Config.raw.window_h = System.windowHeight();
		Config.raw.window_x = kha.Window.get(0).x;
		Config.raw.window_y = kha.Window.get(0).y;
		Config.save();
		#end
	}

	public static function resize() {
		if (System.windowWidth() == 0 || System.windowHeight() == 0) return;

		var cam = Scene.active.camera;
		if (cam.data.raw.ortho != null) {
			cam.data.raw.ortho[2] = -2 * (iron.App.h() / iron.App.w());
			cam.data.raw.ortho[3] =  2 * (iron.App.h() / iron.App.w());
		}
		cam.buildProjection();

		if (Context.cameraType == CameraOrthographic) {
			Viewport.updateCameraType(Context.cameraType);
		}

		Context.ddirty = 2;

		if (UISidebar.inst.show) {
			appx = UIToolbar.inst.toolbarw;
			appy = UIHeader.inst.headerh * 2;
		}
		else {
			appx = 0;
			appy = 0;
		}

		if (UINodes.inst.grid != null) {
			var _grid = UINodes.inst.grid;
			function _next() {
				_grid.unload();
			}
			App.notifyOnNextFrame(_next);
			UINodes.inst.grid = null;
		}

		redrawUI();
	}

	public static function redrawUI() {
		UISidebar.inst.hwnd0.redraws = 2;
		UISidebar.inst.hwnd1.redraws = 2;
		UISidebar.inst.hwnd2.redraws = 2;
		UIHeader.inst.headerHandle.redraws = 2;
		UIToolbar.inst.toolbarHandle.redraws = 2;
		UIStatus.inst.statusHandle.redraws = 2;
		UIMenubar.inst.menuHandle.redraws = 2;
		UIMenubar.inst.workspaceHandle.redraws = 2;
		UINodes.inst.hwnd.redraws = 2;
		UIView2D.inst.hwnd.redraws = 2;
		if (Context.ddirty < 0) Context.ddirty = 0; // Redraw viewport
		if (Context.splitView) Context.ddirty = 1;
	}

	static function update() {
		var mouse = Input.getMouse();

		if (mouse.movementX != 0 || mouse.movementY != 0) {
			Krom.setMouseCursor(0); // Arrow
		}

		var hasDrag = dragAsset != null || dragMaterial != null || dragLayer != null || dragFile != null || dragSwatch != null;
		if (hasDrag && (mouse.movementX != 0 || mouse.movementY != 0)) {
			isDragging = true;
		}
		if (mouse.released() && hasDrag) {
			var mx = mouse.x;
			var my = mouse.y;
			var inViewport = Context.paintVec.x < 1 && Context.paintVec.x > 0 &&
							 Context.paintVec.y < 1 && Context.paintVec.y > 0;
			var inLayers = UISidebar.inst.htab0.position == 0 &&
						   mx > UISidebar.inst.tabx && my < Config.raw.layout[LayoutSidebarH0];
			var in2dView = UIView2D.inst.show && UIView2D.inst.type == View2DLayer &&
						   mx > UIView2D.inst.wx && mx < UIView2D.inst.wx + UIView2D.inst.ww &&
						   my > UIView2D.inst.wy && my < UIView2D.inst.wy + UIView2D.inst.wh;
			var inNodes = UINodes.inst.show &&
						  mx > UINodes.inst.wx && mx < UINodes.inst.wx + UINodes.inst.ww &&
						  my > UINodes.inst.wy && my < UINodes.inst.wy + UINodes.inst.wh;
			if (dragAsset != null) {
				if (inNodes) { // Create image texture
					UINodes.inst.acceptAssetDrag(Project.assets.indexOf(dragAsset));
				}
				else if (inLayers || in2dView) { // Create mask
					Layers.createImageMask(dragAsset);
				}
				else if (inViewport) {
					if (dragAsset.file.toLowerCase().endsWith(".hdr")) {
						var image = Project.getImage(dragAsset);
						arm.io.ImportEnvmap.run(dragAsset.file, image);
					}
				}
				dragAsset = null;
			}
			else if (dragSwatch != null) {
				if (inNodes) { // Create RGB node
					UINodes.inst.acceptSwatchDrag(Project.raw.swatches.indexOf(dragSwatch));
				}
				dragSwatch = null;
			}
			else if (dragMaterial != null) {
				materialDropped(inViewport, inLayers, inNodes);
			}
			else if (dragLayer != null) {
				if (inNodes) {
					UINodes.inst.acceptLayerDrag(Project.layers.indexOf(dragLayer));
				}
				else if (inLayers && isDragging) {
					dragLayer.move(Context.dragDestination);
					MakeMaterial.parseMeshMaterial();
				}
				dragLayer = null;
			}
			else if (dragFile != null) {
				var statush = Config.raw.layout[LayoutStatusH];
				var inBrowser =
					mx > iron.App.x() && mx < iron.App.x() + (System.windowWidth() - UIToolbar.inst.toolbarw - Config.raw.layout[LayoutSidebarW]) &&
					my > System.windowHeight() - statush;
				if (!inBrowser) {
					dropX = mouse.x;
					dropY = mouse.y;
					var materialCount = Project.materials.length;
					ImportAsset.run(dragFile, dropX, dropY);
					// Asset was material
					if (Project.materials.length > materialCount) {
						dragMaterial = Context.material;
						materialDropped(inViewport, inLayers, inNodes);
					}
				}
				dragFile = null;
			}
			Krom.setMouseCursor(0); // Arrow
			isDragging = false;
		}

		handleDropPaths();

		if (UIBox.show) UIBox.update();
		if (UIMenu.show) UIMenu.update();

		var decal = Context.tool == ToolDecal || Context.tool == ToolText;
		var isPicker = Context.tool == ToolPicker;
		#if krom_windows
		Zui.alwaysRedrawWindow = !Context.cacheDraws ||
			UIMenu.show ||
			UIBox.show ||
			isDragging ||
			isPicker ||
			decal ||
			UIView2D.inst.show ||
			!Config.raw.brush_3d ||
			Context.frame < 3;
		#end
		if (Zui.alwaysRedrawWindow && Context.ddirty < 0) Context.ddirty = 0;
	}

	static function materialDropped(inViewport: Bool, inLayers: Bool, inNodes: Bool) {
		// Material drag and dropped onto viewport or layers tab
		if (inViewport || inLayers) {
			Context.setMaterial(dragMaterial);
			var uvType = Input.getKeyboard().down("control") ? UVProject : UVMap;
			var decalMat = uvType == UVProject ? RenderUtil.getDecalMat() : null;
			Layers.createFillLayer(uvType, decalMat);
		}
		else if (inNodes) {
			UINodes.inst.acceptMaterialDrag(Project.materials.indexOf(dragMaterial));
		}
		dragMaterial = null;
	}

	static function handleDropPaths() {
		if (dropPaths.length > 0) {
			var mouse = Input.getMouse();
			#if (krom_linux || krom_darwin)
			var wait = !mouse.moved; // Mouse coords not updated during drag
			#else
			var wait = false;
			#end
			if (!wait) {
				dropX = mouse.x;
				dropY = mouse.y;
				var dropPath = dropPaths.shift();
				ImportAsset.run(dropPath, dropX, dropY);
			}
		}
	}

	static function getDragBackground(): TRect {
		var icons = Res.get("icons.k");
		if (dragLayer != null && !dragLayer.isGroup() && dragLayer.fill_layer == null) {
			return Res.tile50(icons, 4, 1);
		}
		return null;
	}

	static function getDragImage(): kha.Image {
		dragTint = 0xffffffff;
		dragSize = -1;
		dragRect = null;
		if (dragAsset != null) {
			return Project.getImage(dragAsset);
		}
		if (dragSwatch != null) {
			dragTint = dragSwatch.base;
			dragSize = 26;
			return TabSwatches.empty;
		}
		if (dragMaterial != null) {
			return dragMaterial.imageIcon;
		}
		if (dragLayer != null && dragLayer.isGroup()) {
			var icons = Res.get("icons.k");
			var folderClosed = Res.tile50(icons, 2, 1);
			var folderOpen = Res.tile50(icons, 8, 1);
			dragRect = dragLayer.show_panel ? folderOpen : folderClosed;
			dragTint = UISidebar.inst.ui.t.LABEL_COL - 0x00202020;
			return icons;
		}
		if (dragLayer != null && dragLayer.isMask() && dragLayer.fill_layer == null) {
			TabLayers.makeMaskPreviewRgba32(dragLayer);
			return TabLayers.maskPreviewRgba32;
		}
		if (dragLayer != null) {
			return dragLayer.fill_layer != null ? dragLayer.fill_layer.imageIcon : dragLayer.texpaint_preview;
		}
		if (dragFile != null) {
			var icons = Res.get("icons.k");
			dragRect = dragFile.indexOf(".") > 0 ? Res.tile50(icons, 3, 1) : Res.tile50(icons, 2, 1);
			dragTint = UISidebar.inst.ui.t.HIGHLIGHT_COL;
			return icons;
		}
		return null;
	}

	static function render(g: kha.graphics2.Graphics) {
		if (System.windowWidth() == 0 || System.windowHeight() == 0) return;

		if (Context.frame == 2) {
			RenderUtil.makeMaterialPreview();
			UISidebar.inst.hwnd1.redraws = 2;
			MakeMaterial.parseMeshMaterial();
			MakeMaterial.parsePaintMaterial();
			Context.ddirty = 0;
			if (History.undoLayers == null) {
				History.undoLayers = [];
				for (i in 0...Config.raw.undo_steps) {
					var l = new LayerSlot("_undo" + History.undoLayers.length);
					History.undoLayers.push(l);
				}
			}
			// Default workspace
			if (Config.raw.workspace != 0) {
				UIHeader.inst.worktab.position = Config.raw.workspace;
				UIMenubar.inst.workspaceHandle.redraws = 2;
				if (UIHeader.inst.worktab.position == SpaceBake) {
					Context.selectTool(ToolBake);
				}
				else {
					Context.selectTool(ToolGizmo);
				}
				if (UIHeader.inst.worktab.position == SpaceMaterial) {
					Layers.updateFillLayers();
					UINodes.inst.show = true;
				}
			}
		}
		else if (Context.frame == 3) {
			Context.ddirty = Context.renderMode == RenderForward ? 3 : 1;
		}
		Context.frame++;

		var mouse = Input.getMouse();
		if (isDragging) {
			Krom.setMouseCursor(1); // Hand
			var img = getDragImage();
			var size = (dragSize == -1 ? 50 : dragSize) * UISidebar.inst.ui.ops.scaleFactor;
			var ratio = size / img.width;
			var h = img.height * ratio;
			#if (kha_direct3d11 || kha_direct3d12 || kha_metal || kha_vulkan)
			var inv = 0;
			#else
			var inv = (dragMaterial != null || (dragLayer != null && dragLayer.fill_layer != null)) ? h : 0;
			#end
			g.color = dragTint;
			var bgRect = getDragBackground();
			if (bgRect != null) g.drawScaledSubImage(Res.get("icons.k"), bgRect.x, bgRect.y, bgRect.w, bgRect.h, mouse.x + dragOffX, mouse.y + dragOffY + inv, size, h - inv * 2);
			dragRect == null ?
				g.drawScaledImage(img, mouse.x + dragOffX, mouse.y + dragOffY + inv, size, h - inv * 2) :
				g.drawScaledSubImage(img, dragRect.x, dragRect.y, dragRect.w, dragRect.h, mouse.x + dragOffX, mouse.y + dragOffY + inv, size, h - inv * 2);
			g.color = 0xffffffff;
		}

		var usingMenu = UIMenu.show && mouse.y > UIHeader.inst.headerh;
		uiEnabled = !UIBox.show && !usingMenu;
		if (UIBox.show) UIBox.render(g);
		if (UIMenu.show) UIMenu.render(g);

		// Save last pos for continuos paint
		Context.lastPaintVecX = Context.paintVec.x;
		Context.lastPaintVecY = Context.paintVec.y;

		#if (krom_android || krom_ios)
		// No mouse move events for touch, re-init last paint position on touch start
		if (!mouse.down()) {
			Context.lastPaintX = -1;
			Context.lastPaintY = -1;
		}
		#end
	}

	public static function enumTexts(nodeType: String): Array<String> {
		if (nodeType == "TEX_IMAGE") {
			return Project.assetNames.length > 0 ? Project.assetNames : [""];
		}
		else if (nodeType == "LAYER" || nodeType == "LAYER_MASK") {
			var layerNames: Array<String> = [];
			for (l in Project.layers) layerNames.push(l.name);
			return layerNames;
		}
		else if (nodeType == "MATERIAL") {
			var materialNames: Array<String> = [];
			for (m in Project.materials) materialNames.push(m.canvas.name);
			return materialNames;
		}
		return null;
	}

	public static function getAssetIndex(fileName: String): Int {
		var i = Project.assetNames.indexOf(fileName);
		return i >= 0 ? i : 0;
	}

	public static function notifyOnNextFrame(f: Void->Void) {
		function _render(_) {
			iron.App.notifyOnInit(function() {
				function _update() {
					iron.App.notifyOnInit(f);
					iron.App.removeUpdate(_update);
				}
				iron.App.notifyOnUpdate(_update);
			});
			iron.App.removeRender(_render);
		}
		iron.App.notifyOnRender(_render);
	}

	public static function toggleFullscreen() {
		if (kha.Window.get(0).mode == kha.WindowMode.Windowed) {
			#if (krom_windows || krom_linux || krom_darwin)
			Config.raw.window_w = System.windowWidth();
			Config.raw.window_h = System.windowHeight();
			Config.raw.window_x = kha.Window.get(0).x;
			Config.raw.window_y = kha.Window.get(0).y;
			#end
			kha.Window.get(0).mode = kha.WindowMode.Fullscreen;
		}
		else {
			kha.Window.get(0).mode = kha.WindowMode.Windowed;
			kha.Window.get(0).resize(Config.raw.window_w, Config.raw.window_h);
			kha.Window.get(0).move(Config.raw.window_x, Config.raw.window_y);
		}
	}

	public static function isScrolling(): Bool {
		return UISidebar.inst.ui.isScrolling;
	}

	public static function getUIs(): Array<Zui> {
		return [App.uiBox, App.uiMenu, arm.ui.UISidebar.inst.ui, arm.ui.UINodes.inst.ui, arm.ui.UIView2D.inst.ui];
	}

	public static function isDecalLayer(): Bool {
		var isPaint = UIHeader.inst.worktab.position == SpacePaint;
		return isPaint && Context.layer.fill_layer != null && Context.layer.uvType == UVProject;
	}

	public static function redrawStatus() {
		if (arm.ui.UIStatus.inst != null) {
			arm.ui.UIStatus.inst.statusHandle.redraws = 2;
		}
	}

	public static function redrawConsole() {
		var statush = Config.raw.layout[LayoutStatusH];
		if (arm.ui.UIStatus.inst != null && arm.ui.UISidebar.inst != null && arm.ui.UISidebar.inst.ui != null && statush > arm.ui.UIStatus.defaultStatusH * arm.ui.UISidebar.inst.ui.SCALE()) {
			arm.ui.UIStatus.inst.statusHandle.redraws = 2;
		}
	}

	public static function initLayout() {
		var show2d = (UINodes.inst != null && UINodes.inst.show) || (UIView2D.inst != null && UIView2D.inst.show);
		var raw = Config.raw;
		raw.layout = [
			Std.int(UISidebar.defaultWindowW * raw.window_scale),
			Std.int(kha.System.windowHeight() / 3),
			Std.int(kha.System.windowHeight() / 3),
			Std.int(kha.System.windowHeight() / 3),
			show2d ? Std.int((iron.App.w() + raw.layout[LayoutNodesW]) / 2) : Std.int(iron.App.w() / 2),
			Std.int(iron.App.h() / 2),
			Std.int(UIStatus.defaultStatusH * raw.window_scale)
		];
	}

	public static function initConfig() {
		var raw = Config.raw;
		raw.recent_projects = [];
		raw.bookmarks = [];
		raw.plugins = [];
		raw.keymap = "default.json";
		raw.theme = "default.json";
		raw.server = "https://armorpaint.fra1.digitaloceanspaces.com";
		raw.undo_steps = 4;
		raw.pressure_radius = true;
		raw.pressure_hardness = true;
		raw.pressure_angle = false;
		raw.pressure_opacity = false;
		raw.pressure_sensitivity = 1.0;
		#if kha_vulkan
		raw.material_live = false;
		#else
		raw.material_live = true;
		#end
		raw.brush_3d = true;
		raw.brush_depth_reject = true;
		raw.brush_angle_reject = true;
		raw.brush_live = false;
		raw.camera_speed = 1.0;
		raw.zoom_direction = ZoomVertical;
		raw.displace_strength = 0.0;
		raw.show_asset_names = false;
		raw.node_preview = true;
		raw.workspace = 0;
		raw.layer_res = Res2048;
		raw.dilate = DilateInstant;
		raw.dilate_radius = 2;
	}
}
