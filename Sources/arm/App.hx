package arm;

import zui.*;
import zui.Zui.State;
import zui.Canvas;

class App extends iron.Trait {

	public static var uienabled = true;
	public static var isDragging = false;
	public static var dragAsset:TAsset = null;
	public static var showFiles = false;
	public static var foldersOnly = false;
	public static var showFilename = false;
	public static var whandle = new Zui.Handle();
	public static var filenameHandle = new Zui.Handle({text: "untitled"});
	public static var filesDone:String->Void;
	public static var dropPath = "";
	public static var dropX = 0.0;
	public static var dropY = 0.0;
	public static var font:kha.Font = null;
	public static var theme:zui.Themes.TTheme;
	public static var color_wheel:kha.Image;
	public static var uimodal:Zui;
	public static var path = '/';
	static var modalW = 625;
	static var modalH = 545;
	static var appx = 0;
	static var appy = 0;

	public static function getEnumTexts():Array<String> {
		return UITrait.inst.assetNames.length > 0 ? UITrait.inst.assetNames : [""];
	}

	public static function mapEnum(s:String):String {
		for (a in UITrait.inst.assets) if (a.name == s) return a.file;
		return "";
	}

	public function new() {
		super();

		#if arm_resizable
		iron.App.onResize = resize;
		#end

		// Set base dir for file browser
		zui.Ext.dataPath = iron.data.Data.dataPath;

		kha.System.notifyOnDropFiles(function(filePath:String) {
			dropPath = StringTools.rtrim(filePath);
			var mouse = iron.system.Input.getMouse();
			dropX = mouse.x;
			dropY = mouse.y;
		});

		iron.data.Data.getFont("font_default.ttf", function(f:kha.Font) {
			iron.data.Data.getBlob("theme.arm", function(b:kha.Blob) {
				iron.data.Data.getImage('color_wheel.png', function(image:kha.Image) {
					theme = haxe.Json.parse(b.toString());
					theme.WINDOW_BG_COL = Std.parseInt(cast theme.WINDOW_BG_COL);
					theme.WINDOW_TINT_COL = Std.parseInt(cast theme.WINDOW_TINT_COL);
					theme.ACCENT_COL = Std.parseInt(cast theme.ACCENT_COL);
					theme.ACCENT_HOVER_COL = Std.parseInt(cast theme.ACCENT_HOVER_COL);
					theme.ACCENT_SELECT_COL = Std.parseInt(cast theme.ACCENT_SELECT_COL);
					theme.PANEL_BG_COL = Std.parseInt(cast theme.PANEL_BG_COL);
					theme.PANEL_TEXT_COL = Std.parseInt(cast theme.PANEL_TEXT_COL);
					theme.BUTTON_COL = Std.parseInt(cast theme.BUTTON_COL);
					theme.BUTTON_TEXT_COL = Std.parseInt(cast theme.BUTTON_TEXT_COL);
					theme.BUTTON_HOVER_COL = Std.parseInt(cast theme.BUTTON_HOVER_COL);
					theme.BUTTON_PRESSED_COL = Std.parseInt(cast theme.BUTTON_PRESSED_COL);
					theme.TEXT_COL = Std.parseInt(cast theme.TEXT_COL);
					theme.LABEL_COL = Std.parseInt(cast theme.LABEL_COL);
					theme.ARROW_COL = Std.parseInt(cast theme.ARROW_COL);
					theme.SEPARATOR_COL = Std.parseInt(cast theme.SEPARATOR_COL);
					font = f;
					color_wheel = image;
					zui.Nodes.getEnumTexts = getEnumTexts;
					zui.Nodes.mapEnum = mapEnum;
					uimodal = new Zui({ font: f, scaleFactor: armory.data.Config.raw.window_scale });
					
					iron.App.notifyOnInit(function() {
						// #if arm_debug
						// iron.Scene.active.sceneParent.getTrait(armory.trait.internal.DebugConsole).visible = false;
						// #end
						iron.App.notifyOnUpdate(update);
						var root = iron.Scene.active.root;
						root.addTrait(new UITrait());
						root.addTrait(new UINodes());
						root.addTrait(new UIView2D());
						root.addTrait(new arm.trait.FlyCamera());
						root.addTrait(new arm.trait.OrbitCamera());
						root.addTrait(new arm.trait.ArcBallCamera());
						iron.App.notifyOnInit(function() {
							iron.App.notifyOnRender2D(render); // Draw on top
						});
						appx = UITrait.inst.C.ui_layout == 0 ? UITrait.inst.toolbarw : UITrait.inst.windowW + UITrait.inst.toolbarw;
						appy = UITrait.inst.headerh * 2;
					});
				});
			});
		});
	}

	public static function w():Int {
		// Draw material preview
		if (UITrait.inst != null && UITrait.inst.materialPreview) return 100;

		// Drawing sticker preview
		if (UITrait.inst != null && UITrait.inst.stickerPreview) return 512;
		
		var res = 0;
		if (UINodes.inst == null || UITrait.inst == null) {
			res = kha.System.windowWidth() - UITrait.defaultWindowW;
		}
		else if (UINodes.inst.show || UIView2D.inst.show) {
			res = Std.int((kha.System.windowWidth() - UITrait.inst.windowW) / 2);
			res -= UITrait.inst.toolbarw;
		}
		else if (UITrait.inst.show) {
			res = kha.System.windowWidth() - UITrait.inst.windowW;
			res -= UITrait.inst.toolbarw;
		}
		else {
			res = kha.System.windowWidth();
		}
		
		return res > 0 ? res : 1; // App was minimized, force render path resize
	}

	public static function h():Int {
		// Draw material preview
		if (UITrait.inst != null && UITrait.inst.materialPreview) return 100;

		// Drawing sticker preview
		if (UITrait.inst != null && UITrait.inst.stickerPreview) return 512;

		var res = 0;
		res = kha.System.windowHeight();
		if (UITrait.inst != null && UITrait.inst.show && res > 0) res -= UITrait.inst.headerh * 3;
		
		return res > 0 ? res : 1; // App was minimized, force render path resize
	}

	public static function resize() {
		iron.Scene.active.camera.buildProjection();
		UITrait.inst.ddirty = 2;

		var lay = UITrait.inst.C.ui_layout;
		
		appx = lay == 0 ? UITrait.inst.toolbarw : UITrait.inst.windowW + UITrait.inst.toolbarw;
		if (lay == 1 && (UINodes.inst.show || UIView2D.inst.show)) {
			appx += iron.App.w() + UITrait.inst.toolbarw;
		}

		appy = UITrait.inst.headerh * 2;

		if (!UITrait.inst.show) {
			appx = 0;
			appy = 0;
		}

		if (UINodes.inst.grid != null) {
			UINodes.inst.grid.unload();
			UINodes.inst.grid = null;
		}
	}

	public static function getAssetIndex(f:String):Int {
		for (i in 0...UITrait.inst.assets.length) {
			if (UITrait.inst.assets[i].file == f) {
				return i;
			}
		}
		return 0;
	}

	static function update() {
		var mouse = iron.system.Input.getMouse();
		var kb = iron.system.Input.getKeyboard();

		isDragging = dragAsset != null;
		if (mouse.released() && isDragging) {
			var x = mouse.x + iron.App.x();
			var y = mouse.y + iron.App.y();
			if (UINodes.inst.show && x > UINodes.inst.wx && y > UINodes.inst.wy) {
				var index = 0;
				for (i in 0...UITrait.inst.assets.length) {
					if (UITrait.inst.assets[i] == dragAsset) {
						index = i;
						break;
					}
				}
				UINodes.inst.acceptDrag(index);
			}
			dragAsset = null;
		}

		if (dropPath != "") {
			Importer.importFile(dropPath, dropX, dropY);
			dropPath = "";
		}

		if (showFiles) updateFiles();
	}

	static function updateFiles() {
		var mouse = iron.system.Input.getMouse();
		if (mouse.released()) {
			var appw = kha.System.windowWidth();
			var apph = kha.System.windowHeight();
			var left = appw / 2 - modalW / 2;
			var right = appw / 2 + modalW / 2;
			var top = apph / 2 - modalH / 2;
			var bottom = apph / 2 + modalH / 2;
			var mx = mouse.x + iron.App.x();
			var my = mouse.y + iron.App.y();
			if (mx < left || mx > right || my < top || my > bottom) {
				showFiles = false;
			}
		}
	}

	static function render(g:kha.graphics2.Graphics) {
		if (arm.App.realw() == 0 || arm.App.realh() == 0) return;

		if (arm.App.dragAsset != null) {
			var mouse = iron.system.Input.getMouse();
			var img = UITrait.inst.getImage(arm.App.dragAsset);
			var ratio = 128 / img.width;
			var h = img.height * ratio;
			g.drawScaledImage(img, mouse.x + iron.App.x(), mouse.y + iron.App.y(), 128, h);
		}

		uienabled = !showFiles;
		if (showFiles) renderFiles(g);
	}

	@:access(zui.Zui)
	static function renderFiles(g:kha.graphics2.Graphics) {
		var appw = kha.System.windowWidth();
		var apph = kha.System.windowHeight();
		var left = Std.int(appw / 2 - modalW / 2);
		var right = Std.int(appw / 2 + modalW / 2);
		var top = Std.int(apph / 2 - modalH / 2);
		var bottom = Std.int(apph / 2 + modalH / 2);
		g.color = uimodal.t.SEPARATOR_COL;
		g.fillRect(left, top, modalW, modalH);
		
		g.end();
		uimodal.begin(g);
		var pathHandle = Id.handle();
		if (uimodal.window(whandle, left, top, modalW, modalH - 50, true)) {
			pathHandle.text = uimodal.textInput(pathHandle, "Path");
			if (showFilename) uimodal.textInput(filenameHandle, "File");
			path = zui.Ext.fileBrowser(uimodal, pathHandle, foldersOnly);
		}
		uimodal.end(false);
		g.begin(false);

		if (Format.checkTextureFormat(path) || Format.checkMeshFormat(path) || Format.checkProjectFormat(path)) {
			showFiles = false;
			filesDone(path);
			var sep = kha.System.systemId == "Windows" ? "\\" : "/";
			pathHandle.text = pathHandle.text.substr(0, pathHandle.text.lastIndexOf(sep));
			whandle.redraws = 2;
			UITrait.inst.ddirty = 2;
		}

		g.end();
		uimodal.beginLayout(g, right - Std.int(uimodal.ELEMENT_W()), bottom - Std.int(uimodal.ELEMENT_H() * 1.2), Std.int(uimodal.ELEMENT_W()));
		if (uimodal.button("OK")) {
			showFiles = false;
			filesDone(path);
			UITrait.inst.ddirty = 2;
		}
		uimodal.endLayout(false);

		uimodal.beginLayout(g, right - Std.int(uimodal.ELEMENT_W() * 2), bottom - Std.int(uimodal.ELEMENT_H() * 1.2), Std.int(uimodal.ELEMENT_W()));
		if (uimodal.button("Cancel")) {
			showFiles = false;
			UITrait.inst.ddirty = 2;
		}
		uimodal.endLayout();

		g.begin(false);
	}

	public static function x():Int { return appx; }
	public static function y():Int { return appy; }
	public static function realw():Int { return kha.System.windowWidth(); }
	public static function realh():Int { return kha.System.windowHeight(); }
}
