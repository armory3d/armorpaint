package arm;

import kha.System;
import kha.Window;
import kha.Image;
import zui.Zui;
import zui.Id;
import zui.Nodes;
import iron.data.SceneFormat;
import iron.data.MeshData;
import iron.data.Data;
import iron.object.MeshObject;
import iron.Scene;
import arm.util.RenderUtil;
import arm.util.ViewportUtil;
import arm.sys.File;
import arm.sys.Path;
import arm.ui.UISidebar;
import arm.ui.UIFiles;
import arm.ui.UIBox;
import arm.ui.UINodes;
import arm.ui.UIHeader;
import arm.data.LayerSlot;
import arm.data.BrushSlot;
import arm.data.FontSlot;
import arm.data.MaterialSlot;
import arm.node.MakeMaterial;
import arm.io.ImportAsset;
import arm.io.ImportArm;
import arm.io.ImportBlend;
import arm.io.ImportMesh;
import arm.io.ImportTexture;
import arm.io.ExportArm;
import arm.node.NodesBrush;
import arm.ProjectFormat;
import arm.Enums;

class Project {

	public static var raw: TProjectFormat = {};
	public static var filepath = "";
	public static var assets: Array<TAsset> = [];
	public static var assetNames: Array<String> = [];
	public static var assetId = 0;
	public static var meshAssets: Array<String> = [];
	public static var materials: Array<MaterialSlot> = null;
	public static var materialGroups: Array<TNodeGroup> = [];
	public static var brushes: Array<BrushSlot> = null;
	public static var layers: Array<LayerSlot> = null;
	public static var fonts: Array<FontSlot> = null;
	public static var paintObjects: Array<MeshObject> = null;
	public static var atlasObjects: Array<Int> = null;
	public static var atlasNames: Array<String> = null;
	public static var assetMap = new Map<Int, Dynamic>(); // kha.Image | kha.Font
	static var meshList: Array<String> = null;

	public static function projectOpen() {
		UIFiles.show("arm", false, function(path: String) {
			if (!path.endsWith(".arm")) {
				Console.error(Strings.error0());
				return;
			}

			var current = @:privateAccess kha.graphics2.Graphics.current;
			if (current != null) current.end();

			ImportArm.runProject(path);

			if (current != null) current.begin(false);
		});
	}

	public static function projectOpenRecentBox() {
		UIBox.showCustom(function(ui: Zui) {
			if (ui.tab(Id.handle(), tr("Recent Projects"))) {
				for (path in Config.raw.recent_projects) {
					var file = path;
					#if krom_windows
					file = path.replace("/", "\\");
					#else
					file = path.replace("\\", "/");
					#end
					file = file.substr(file.lastIndexOf(Path.sep) + 1);
					if (ui.button(file, Left)) {
						var current = @:privateAccess kha.graphics2.Graphics.current;
						if (current != null) current.end();

						ImportArm.runProject(path);

						if (current != null) current.begin(false);
						UIBox.show = false;
					}
					if (ui.isHovered) ui.tooltip(path);
				}
				if (ui.button("Clear", Left)) {
					Config.raw.recent_projects = [];
					Config.save();
				}
			}
		}, 400, 320);
	}

	public static function projectSave(saveAndQuit = false) {
		if (filepath == "") {
			projectSaveAs();
			return;
		}
		var filename = Project.filepath.substring(Project.filepath.lastIndexOf(Path.sep) + 1, Project.filepath.length - 4);
		Window.get(0).title = filename + " - ArmorPaint";

		function _init() {
			ExportArm.runProject();
			if (saveAndQuit) System.stop();
		}
		iron.App.notifyOnInit(_init);
	}

	public static function projectSaveAs() {
		UIFiles.show("arm", true, function(path: String) {
			var f = UIFiles.filename;
			if (f == "") f = tr("untitled");
			filepath = path + Path.sep + f;
			if (!filepath.endsWith(".arm")) filepath += ".arm";
			projectSave();
		});
	}

	public static function projectNewBox() {
		UIBox.showCustom(function(ui: Zui) {
			if (ui.tab(Id.handle(), tr("New Project"))) {
				if (meshList == null) {
					meshList = File.readDirectory(Path.data() + Path.sep + "meshes");
					for (i in 0...meshList.length) meshList[i] = meshList[i].substr(0, meshList[i].length - 4); // Trim .arm
					meshList.unshift("plane");
					meshList.unshift("sphere");
					meshList.unshift("rounded_cube");

				}

				ui.row([0.5, 0.5]);
				Context.projectType = ui.combo(Id.handle({position: Context.projectType}), meshList, tr("Template"), true);
				Context.projectAspectRatio = ui.combo(Id.handle({position: Context.projectAspectRatio}), ["1:1", "2:1", "1:2"], tr("Aspect Ratio"), true);

				@:privateAccess ui.endElement();
				ui.row([0.5, 0.5]);
				if (ui.button(tr("Cancel"))) {
					UIBox.show = false;
				}
				if (ui.button(tr("OK")) || ui.isReturnDown) {
					Project.projectNew();
					ViewportUtil.scaleToBounds();
					UIBox.show = false;
					App.redrawUI();
				}
			}
		});
	}

	public static function projectNew(resetLayers = true) {
		Window.get(0).title = "ArmorPaint";
		filepath = "";
		if (Context.mergedObject != null) {
			Context.mergedObject.remove();
			Data.deleteMesh(Context.mergedObject.data.handle);
			Context.mergedObject = null;
		}

		ViewportUtil.resetViewport();
		Context.layerPreviewDirty = true;
		Context.layerFilter = 0;
		Project.meshAssets = [];

		Context.paintObject = Context.mainObject();

		Context.selectPaintObject(Context.mainObject());
		for (i in 1...paintObjects.length) {
			var p = paintObjects[i];
			if (p == Context.paintObject) continue;
			Data.deleteMesh(p.data.handle);
			p.remove();
		}
		var meshes = Scene.active.meshes;
		var len = meshes.length;
		for (i in 0...len) {
			var m = meshes[len - i - 1];
			if (Context.projectObjects.indexOf(m) == -1 &&
				m.name != ".ParticleEmitter" &&
				m.name != ".Particle") {
				Data.deleteMesh(m.data.handle);
				m.remove();
			}
		}
		var handle = Context.paintObject.data.handle;
		if (handle != "SceneSphere" && handle != "ScenePlane") {
			Data.deleteMesh(handle);
		}

		if (Context.projectType != ModelRoundedCube) {
			var raw: TMeshData = null;
			if (Context.projectType == ModelSphere || Context.projectType == ModelTessellatedPlane) {
				var mesh: Dynamic = Context.projectType == ModelSphere ?
					new arm.geom.Sphere(1, 512, 256) :
					new arm.geom.Plane(1, 1, 512, 512);
				raw = {
					name: "Tessellated",
					vertex_arrays: [
						{ values: mesh.posa, attrib: "pos", data: "short4norm" },
						{ values: mesh.nora, attrib: "nor", data: "short2norm" },
						{ values: mesh.texa, attrib: "tex", data: "short2norm" }
					],
					index_arrays: [
						{ values: mesh.inda, material: 0 }
					],
					scale_pos: mesh.scalePos,
					scale_tex: mesh.scaleTex
				};
			}
			else {
				Data.getBlob("meshes/" + meshList[Context.projectType] + ".arm", function(b: kha.Blob) {
					raw = iron.system.ArmPack.decode(b.toBytes()).mesh_datas[0];
				});
			}

			var md = new MeshData(raw, function(md: MeshData) {});
			Data.cachedMeshes.set("SceneTessellated", md);

			if (Context.projectType == ModelTessellatedPlane) {
				ViewportUtil.setView(0, 0, 0.75, 0, 0, 0); // Top
			}
		}

		var n = Context.projectType == ModelRoundedCube ? ".Cube" : "Tessellated";
		Data.getMesh("Scene", n, function(md: MeshData) {

			var current = @:privateAccess kha.graphics2.Graphics.current;
			if (current != null) current.end();

			Context.pickerMaskHandle.position = MaskNone;
			Context.paintObject.setData(md);
			Context.paintObject.transform.scale.set(1, 1, 1);
			Context.paintObject.transform.buildMatrix();
			Context.paintObject.name = n;
			paintObjects = [Context.paintObject];
			while (materials.length > 0) materials.pop().unload();
			Data.getMaterial("Scene", "Material", function(m: iron.data.MaterialData) {
				materials.push(new MaterialSlot(m));
			});
			Context.material = materials[0];
			arm.ui.UINodes.inst.hwnd.redraws = 2;
			arm.ui.UINodes.inst.groupStack = [];
			materialGroups = [];
			brushes = [new BrushSlot()];
			Context.brush = brushes[0];
			var fontNames = App.font.getFontNames();
			fonts = [new FontSlot(fontNames.length > 0 ? fontNames[0] : "default.ttf", App.font)];
			Context.font = fonts[0];
			Project.setDefaultSwatches();
			Context.swatch = Project.raw.swatches[0];

			History.reset();

			MakeMaterial.parsePaintMaterial();
			RenderUtil.makeMaterialPreview();
			for (a in assets) Data.deleteImage(a.file);
			assets = [];
			assetNames = [];
			assetMap = [];
			assetId = 0;
			Project.raw.packed_assets = [];
			Context.ddirty = 4;
			UISidebar.inst.hwnd0.redraws = 2;
			UISidebar.inst.hwnd1.redraws = 2;
			UISidebar.inst.hwnd2.redraws = 2;

			if (resetLayers) {
				var aspectRatioChanged = layers[0].texpaint.width != Config.getTextureResX() || layers[0].texpaint.height != Config.getTextureResY();
				while (layers.length > 0) layers.pop().unload();
				var layer = new LayerSlot();
				layers.push(layer);
				Context.setLayer(layer);
				if (aspectRatioChanged) {
					iron.App.notifyOnInit(Layers.resizeLayers);
				}
				iron.App.notifyOnInit(Layers.initLayers);
			}

			if (current != null) current.begin(false);

			Context.savedEnvmap = null;
			Context.envmapLoaded = false;
			Scene.active.world.envmap = Context.emptyEnvmap;
			Scene.active.world.raw.envmap = "World_radiance.k";
			Context.showEnvmapHandle.selected = Context.showEnvmap = false;
			Scene.active.world.probe.radiance = Context.defaultRadiance;
			Scene.active.world.probe.radianceMipmaps = Context.defaultRadianceMipmaps;
			Scene.active.world.probe.irradiance = Context.defaultIrradiance;
			Scene.active.world.probe.raw.strength = 4.0;
			Context.initTool();
		});
	}

	public static function importMaterial() {
		UIFiles.show("arm,blend", false, function(path: String) {
			path.endsWith(".blend") ?
				ImportBlend.runMaterial(path) :
				ImportArm.runMaterial(path);
		});
	}

	public static function importBrush() {
		UIFiles.show("arm," + Path.textureFormats.join(","), false, function(path: String) {
			// Create brush from texture
			if (Path.isTexture(path)) {
				// Import texture
				ImportAsset.run(path);
				var assetIndex = 0;
				for (i in 0...Project.assets.length) {
					if (Project.assets[i].file == path) {
						assetIndex = i;
						break;
					}
				}

				// Create a new brush
				Context.brush = new BrushSlot();
				Project.brushes.push(Context.brush);

				// Create and link image node
				var n = NodesBrush.createNode("TEX_IMAGE");
				n.x = 83;
				n.y = 340;
				n.buttons[0].default_value = assetIndex;
				var links = Context.brush.canvas.links;
				links.push({
					id: Context.brush.nodes.getLinkId(links),
					from_id: n.id,
					from_socket: 0,
					to_id: 0,
					to_socket: 4
				});

				// Parse brush
				MakeMaterial.parseBrush();
				UINodes.inst.hwnd.redraws = 2;
				function _init() {
					RenderUtil.makeBrushPreview();
				}
				iron.App.notifyOnInit(_init);
			}
			// Import from project file
			else {
				ImportArm.runBrush(path);
			}
		});
	}

	public static function importMesh(replaceExisting = true) {
		UIFiles.show(Path.meshFormats.join(","), false, function(path: String) {
			importMeshBox(path, replaceExisting);
		});
	}

	public static function importMeshBox(path: String, replaceExisting = true, clearLayers = true) {

		#if krom_ios
		// Import immediately while access to resource is unlocked
		Data.getBlob(path, function(b: kha.Blob) {});
		#end

		UIBox.showCustom(function(ui: Zui) {
			if (ui.tab(Id.handle(), tr("Import Mesh"))) {

				if (path.toLowerCase().endsWith(".obj")) {
					Context.splitBy = ui.combo(Id.handle(), [
						tr("Object"),
						tr("Group"),
						tr("Material"),
						tr("UDIM Tile"),
					], tr("Split By"), true);
					if (ui.isHovered) ui.tooltip(tr("Split .obj mesh into objects"));
				}

				if (path.toLowerCase().endsWith(".fbx")) {
					Context.parseTransform = ui.check(Id.handle({selected: Context.parseTransform}), tr("Parse Transforms"));
					if (ui.isHovered) ui.tooltip(tr("Load per-object transforms from .fbx"));
				}

				if (path.toLowerCase().endsWith(".fbx") || path.toLowerCase().endsWith(".blend")) {
					Context.parseVCols = ui.check(Id.handle({selected: Context.parseVCols}), tr("Parse Vertex Colors"));
					if (ui.isHovered) ui.tooltip(tr("Import vertex color data"));
				}

				ui.row([0.45, 0.45, 0.1]);
				if (ui.button(tr("Cancel"))) {
					UIBox.show = false;
				}
				if (ui.button(tr("Import")) || ui.isReturnDown) {
					UIBox.show = false;
					App.redrawUI();
					ImportMesh.run(path, clearLayers, replaceExisting);
				}
				if (ui.button(tr("?"))) {
					File.openInStdApp("https://github.com/armory3d/armorpaint_docs/blob/master/faq.md");
				}
			}
		});
		UIBox.clickToHide = false; // Prevent closing when going back to window from file browser
	}

	public static function reimportMesh() {
		if (Project.meshAssets != null && Project.meshAssets.length > 0 && File.exists(Project.meshAssets[0])) {
			importMeshBox(Project.meshAssets[0], true, false);
		}
		else importAsset();
	}

	public static function importAsset(filters: String = null, hdrAsEnvmap = true) {
		if (filters == null) filters = Path.textureFormats.join(",") + "," + Path.meshFormats.join(",");
		UIFiles.show(filters, false, function(path: String) {
			ImportAsset.run(path, -1.0, -1.0, true, hdrAsEnvmap);
		});
	}

	public static function importSwatches() {
		UIFiles.show("arm", false, function(path: String) {
			ImportArm.runSwatches(path);
		});
	}

	public static function reimportTextures() {
		for (asset in Project.assets) {
			reimportTexture(asset);
		}
	}

	public static function reimportTexture(asset: TAsset) {
		function load(path: String) {
			asset.file = path;
			var i = Project.assets.indexOf(asset);
			Data.deleteImage(asset.file);
			Project.assetMap.remove(asset.id);
			Project.assets.splice(i, 1);
			Project.assetNames.splice(i, 1);
			ImportTexture.run(asset.file);
			Project.assets.insert(i, Project.assets.pop());
			Project.assetNames.insert(i, Project.assetNames.pop());
			function _next() {
				arm.node.MakeMaterial.parsePaintMaterial();
				arm.util.RenderUtil.makeMaterialPreview();
				UISidebar.inst.hwnd1.redraws = 2;
			}
			App.notifyOnNextFrame(_next);
		}
		if (!File.exists(asset.file)) {
			var filters = Path.textureFormats.join(",");
			UIFiles.show(filters, false, function(path: String) {
				load(path);
			});
		}
		else load(asset.file);
	}

	public static function getImage(asset: TAsset): Image {
		return asset != null ? Project.assetMap.get(asset.id) : null;
	}

	public static function getUsedAtlases(): Array<String> {
		if (Project.atlasObjects == null) return null;
		var used: Array<Int> = [];
		for (i in Project.atlasObjects) if (used.indexOf(i) == -1) used.push(i);
		if (used.length > 1) {
			var res: Array<String> = [];
			for (i in used) res.push(Project.atlasNames[i]);
			return res;
		}
		else return null;
	}

	public static function isAtlasObject(p: MeshObject): Bool {
		if (Context.layerFilter <= Project.paintObjects.length) return false;
		var atlasName = getUsedAtlases()[Context.layerFilter - Project.paintObjects.length - 1];
		var atlasI = Project.atlasNames.indexOf(atlasName);
		return atlasI == Project.atlasObjects[Project.paintObjects.indexOf(p)];
	}

	public static function getAtlasObjects(objectMask: Int): Array<MeshObject> {
		var atlasName = Project.getUsedAtlases()[objectMask - Project.paintObjects.length - 1];
		var atlasI = Project.atlasNames.indexOf(atlasName);
		var visibles: Array<MeshObject> = [];
		for (i in 0...Project.paintObjects.length) if (Project.atlasObjects[i] == atlasI) visibles.push(Project.paintObjects[i]);
		return visibles;
	}

	public static function packedAssetExists(packed_assets: Array<TPackedAsset>, name: String): Bool {
		for (pa in packed_assets) if (pa.name == name) return true;
		return false;
	}

	public static function exportSwatches() {
		UIFiles.show("arm", true, function(path: String) {
			var f = UIFiles.filename;
			if (f == "") f = tr("untitled");
			ExportArm.runSwatches(path + Path.sep + f);
		});
	}

	public static function makeSwatch(base = 0xffffffff): TSwatchColor {
		return { base: base, opacity: 1.0, occlusion: 1.0, roughness: 0.0, metallic: 0.0, normal: 0xff8080ff, emission: 0.0, height: 0.0, subsurface: 0.0 };
	}

	public static function setDefaultSwatches() {
		// 32-Color Palette by Andrew Kensler
		// http://eastfarthing.com/blog/2016-05-06-palette/
		Project.raw.swatches = [];
		var colors = [0xffffffff, 0xff000000, 0xffd6a090, 0xffa12c32, 0xfffa2f7a, 0xfffb9fda, 0xffe61cf7, 0xff992f7c, 0xff47011f, 0xff051155, 0xff4f02ec, 0xff2d69cb, 0xff00a6ee, 0xff6febff, 0xff08a29a, 0xff2a666a, 0xff063619, 0xff4a4957, 0xff8e7ba4, 0xffb7c0ff, 0xffacbe9c, 0xff827c70, 0xff5a3b1c, 0xffae6507, 0xfff7aa30, 0xfff4ea5c, 0xff9b9500, 0xff566204, 0xff11963b, 0xff51e113, 0xff08fdcc];
		for (c in colors) Project.raw.swatches.push(Project.makeSwatch(c));
	}
}

typedef TNodeGroup = {
	public var nodes: Nodes;
	public var canvas: TNodeCanvas;
}
