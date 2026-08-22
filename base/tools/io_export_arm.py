"""Armory Mesh Exporter"""
#
#  Based on Open Game Engine Exchange
#  https://opengex.org/
#  Export plugin for Blender by Eric Lengyel
#  Copyright 2015, Terathon Software LLC
#
#  This software is licensed under the Creative Commons
#  Attribution-ShareAlike 3.0 Unported License:
#  http://creativecommons.org/licenses/by-sa/3.0/deed.en_US

import io
import os
import struct
import time
import bpy
from bpy_extras.io_utils import ExportHelper
from mathutils import Vector
import numpy as np

bl_info = {
    "name": "Armory Mesh Exporter",
    "category": "Import-Export",
    "location": "File -> Export",
    "description": "Armory mesh data",
    "author": "Armory3D.org",
    "version": (2026, 4, 0),
    "blender": (5, 0, 0),
    "doc_url": "",
    "tracker_url": "",
}

NodeTypeMesh = 1
structIdentifier = ["object", "mesh_object"]

class ArmoryExporter(bpy.types.Operator, ExportHelper):
    """Export to Armory format"""

    bl_idname = "export_scene.arm"
    bl_label = "Export Armory"
    filename_ext = ".arm"

    def execute(self, context):
        profile_time = time.time()
        current_frame = context.scene.frame_current
        current_subframe = context.scene.frame_subframe
        self.scene = context.scene
        self.output = {}
        self.bobjectArray = {}
        self.meshArray = {}
        self.depsgraph = context.evaluated_depsgraph_get()
        scene_objects = self.scene.collection.all_objects

        for bobject in scene_objects:
            if not bobject.parent:
                self.process_bobject(bobject)

        self.output["name"] = self.scene.name
        self.output["objects"] = []
        for bo in scene_objects:
            if not bo.parent:
                self.export_object(bo, self.scene)

        self.output["mesh_datas"] = []
        for o in self.meshArray.items():
            self.export_mesh(o)

        self.output["camera_datas"] = None
        self.output["camera_ref"] = None
        self.output["material_datas"] = None
        self.output["shader_datas"] = None
        self.output["world_datas"] = None
        self.output["world_ref"] = None

        self.write_arm(self.filepath, self.output)
        self.scene.frame_set(current_frame, subframe=current_subframe)

        print(f"Scene exported in {str(time.time() - profile_time)}")
        return {"FINISHED"}

    def write_arm(self, filepath, output):
        with open(filepath, "wb") as f:
            f.write(packb(output))

    def write_matrix(self, matrix):
        return [
            matrix[0][0],
            matrix[1][0],
            matrix[2][0],
            matrix[3][0],
            matrix[0][1],
            matrix[1][1],
            matrix[2][1],
            matrix[3][1],
            matrix[0][2],
            matrix[1][2],
            matrix[2][2],
            matrix[3][2],
            matrix[0][3],
            matrix[1][3],
            matrix[2][3],
            matrix[3][3],
        ]

    def process_bobject(self, bobject):
        if bobject.type not in ["MESH"]:
            return

        btype = NodeTypeMesh if bobject.type == "MESH" else 0
        self.bobjectArray[bobject] = {"objectType": btype, "structName": bobject.name}

        for subbobject in bobject.children:
            self.process_bobject(subbobject)

    def export_object(self, bobject, scene, parento=None):
        if bobjectRef := self.bobjectArray.get(bobject):
            o = {}
            o["name"] = bobjectRef["structName"]
            o["type"] = structIdentifier[bobjectRef["objectType"]]
            o["data_ref"] = None
            o["transform"] = self.write_matrix(bobject.matrix_local)
            o["dimensions"] = None
            o["visible"] = True
            o["spawn"] = True
            o["anim"] = None
            o["material_ref"] = None
            o["children"] = None

            if bobjectRef["objectType"] == NodeTypeMesh:
                objref = bobject.data
                if objref not in self.meshArray:
                    self.meshArray[objref] = {
                        "structName": objref.name,
                        "objectTable": [bobject],
                    }
                else:
                    self.meshArray[objref]["objectTable"].append(bobject)
                oid = self.meshArray[objref]["structName"]
                o["data_ref"] = oid
                o["dimensions"] = self.calc_aabb(bobject)

            if parento is None:
                self.output["objects"].append(o)
            else:
                parento["children"].append(o)

            if not hasattr(o, "children") and len(bobject.children) > 0:
                o["children"] = []

        for subbobject in bobject.children:
            self.export_object(subbobject, scene, o)

    def calc_aabb(self, bobject):
        aabb_center = 0.125 * sum((Vector(b) for b in bobject.bound_box), Vector())
        return [
            abs(
                (bobject.bound_box[6][0] - bobject.bound_box[0][0]) / 2
                + abs(aabb_center[0])
            )
            * 2,
            abs(
                (bobject.bound_box[6][1] - bobject.bound_box[0][1]) / 2
                + abs(aabb_center[1])
            )
            * 2,
            abs(
                (bobject.bound_box[6][2] - bobject.bound_box[0][2]) / 2
                + abs(aabb_center[2])
            )
            * 2,
        ]

    def export_mesh_data(self, exportMesh, bobject, o, has_armature=False):
        exportMesh.calc_loop_triangles()

        loops = exportMesh.loops
        num_verts = len(loops)
        num_uv_layers = len(exportMesh.uv_layers)
        num_colors = len(exportMesh.vertex_colors)
        has_tex = num_uv_layers > 0
        has_tex1 = num_uv_layers > 1
        has_col = num_colors > 0
        has_tang = False

        # Scale for packed coords
        aabb = self.calc_aabb(bobject)
        maxdim = max(aabb[0], max(aabb[1], aabb[2]))
        if maxdim > 2:
            o["scale_pos"] = maxdim / 2
        else:
            o["scale_pos"] = 1.0

        pdata = np.empty(num_verts * 4, dtype="<f4")  # p.xyz, n.z
        ndata = np.empty(num_verts * 2, dtype="<f4")  # n.xy
        if has_tex:
            t0map = 0  # Get active uvmap
            t0data = np.empty(num_verts * 2, dtype="<f4")
            uv_layers = exportMesh.uv_layers
            if uv_layers is not None:
                for i in range(0, len(uv_layers)):
                    if uv_layers[i].active_render:
                        t0map = i
                        break
            if has_tex1:
                t1map = 1 if t0map == 0 else 0
                t1data = np.empty(num_verts * 2, dtype="<f4")
            # Scale for packed coords
            maxdim = 1.0
            lay0 = uv_layers[t0map]
            for v in lay0.data:
                if abs(v.uv[0]) > maxdim:
                    maxdim = abs(v.uv[0])
                if abs(v.uv[1]) > maxdim:
                    maxdim = abs(v.uv[1])
            if has_tex1:
                lay1 = uv_layers[t1map]
                for v in lay1.data:
                    if abs(v.uv[0]) > maxdim:
                        maxdim = abs(v.uv[0])
                    if abs(v.uv[1]) > maxdim:
                        maxdim = abs(v.uv[1])
            if maxdim > 1:
                o["scale_tex"] = maxdim
                invscale_tex = (1 / o["scale_tex"]) * 32767
            else:
                o["scale_tex"] = 1.0
                invscale_tex = 1 * 32767
            if has_tang:
                exportMesh.calc_tangents(uvmap=lay0.name)
                tangdata = np.empty(num_verts * 4, dtype="<f4")
        if has_col:
            cdata = np.empty(num_verts * 4, dtype="<f4")

        scale_pos = o["scale_pos"]
        invscale_pos = (1 / scale_pos) * 32767

        verts = exportMesh.vertices
        if has_tex:
            lay0 = exportMesh.uv_layers[t0map]
            if has_tex1:
                lay1 = exportMesh.uv_layers[t1map]
        if has_col:
            vcol0 = exportMesh.vertex_colors[0].data

        for i, loop in enumerate(loops):
            v = verts[loop.vertex_index]
            co = v.co
            normal = loop.normal
            tang = loop.tangent

            i4 = i * 4
            i2 = i * 2
            pdata[i4] = co[0]
            pdata[i4 + 1] = co[1]
            pdata[i4 + 2] = co[2]
            pdata[i4 + 3] = normal[2] * scale_pos  # Cancel scale
            ndata[i2] = normal[0]
            ndata[i2 + 1] = normal[1]
            if has_tex:
                uv = lay0.data[loop.index].uv
                t0data[i2] = uv[0]
                t0data[i2 + 1] = 1.0 - uv[1]  # Reverse Y
                if has_tex1:
                    uv = lay1.data[loop.index].uv
                    t1data[i2] = uv[0]
                    t1data[i2 + 1] = 1.0 - uv[1]
                if has_tang:
                    i4 = i * 4
                    tangdata[i4] = tang[0]
                    tangdata[i4 + 1] = tang[1]
                    tangdata[i4 + 2] = tang[2]
            if has_col:
                col = vcol0[loop.index].color
                i4 = i * 4
                cdata[i4] = col[0]
                cdata[i4 + 1] = col[1]
                cdata[i4 + 2] = col[2]
                cdata[i4 + 3] = col[3]

        # Pack
        pdata *= invscale_pos
        ndata *= 32767
        pdata = np.array(pdata, dtype="<i2")
        ndata = np.array(ndata, dtype="<i2")
        if has_tex:
            t0data *= invscale_tex
            t0data = np.array(t0data, dtype="<i2")
            if has_tex1:
                t1data *= invscale_tex
                t1data = np.array(t1data, dtype="<i2")
        if has_col:
            cdata *= 32767
            cdata = np.array(cdata, dtype="<i2")
        if has_tang:
            tangdata *= 32767
            tangdata = np.array(tangdata, dtype="<i2")

        # Output
        o["vertex_arrays"] = []
        o["vertex_arrays"].append(
            {"attrib": "pos", "data": "short4norm", "values": pdata}
        )
        o["vertex_arrays"].append(
            {"attrib": "nor", "data": "short2norm", "values": ndata}
        )
        if has_tex:
            o["vertex_arrays"].append(
                {"attrib": "tex", "data": "short2norm", "values": t0data}
            )
            if has_tex1:
                o["vertex_arrays"].append(
                    {"attrib": "tex1", "data": "short2norm", "values": t1data}
                )
        if has_col:
            o["vertex_arrays"].append(
                {"attrib": "col", "data": "short4norm", "values": cdata}
            )
        if has_tang:
            o["vertex_arrays"].append(
                {
                    "attrib": "tang",
                    "data": "short4norm",
                    "values": tangdata,
                }
            )

        mats = exportMesh.materials
        poly_map = []
        for i in range(max(len(mats), 1)):
            poly_map.append([])
        for poly in exportMesh.polygons:
            poly_map[poly.material_index].append(poly)

        # map polygon indices to triangle loops
        tri_loops = {}
        for loop in exportMesh.loop_triangles:
            if loop.polygon_index not in tri_loops:
                tri_loops[loop.polygon_index] = []
            tri_loops[loop.polygon_index].append(loop)

        for index, polys in enumerate(poly_map):
            tris = 0
            for poly in polys:
                tris += poly.loop_total - 2
            if tris == 0:  # No face assigned
                continue
            prim = np.empty(tris * 3, dtype="<i4")

            i = 0
            for poly in polys:
                for loop in tri_loops[poly.index]:
                    prim[i] = loops[loop.loops[0]].index
                    prim[i + 1] = loops[loop.loops[1]].index
                    prim[i + 2] = loops[loop.loops[2]].index
                    i += 3

            o["index_array"] = prim

    def export_mesh(self, objectRef):
        # This function exports a single mesh object
        table = objectRef[1]["objectTable"]
        bobject = table[0]
        oid = objectRef[1]["structName"]
        o = {}
        o["name"] = oid

        armature = bobject.find_armature()
        apply_modifiers = not armature

        bobject_eval = (
            bobject.evaluated_get(self.depsgraph) if apply_modifiers else bobject
        )
        exportMesh = bobject_eval.to_mesh()

        self.export_mesh_data(exportMesh, bobject, o, has_armature=armature is not None)

        self.output["mesh_datas"].append(o)
        bobject_eval.to_mesh_clear()

def menu_func(self, context):
    self.layout.operator(ArmoryExporter.bl_idname, text="Armory (.arm)")

def register():
    bpy.utils.register_class(ArmoryExporter)
    bpy.types.TOPBAR_MT_file_export.append(menu_func)

def unregister():
    bpy.types.TOPBAR_MT_file_export.remove(menu_func)
    bpy.utils.unregister_class(ArmoryExporter)

if __name__ == "__main__":
    register()

# Msgpack parser with typed arrays
# Based on u-msgpack-python v2.4.1 - v at sergeev.io
# https://github.com/vsergeev/u-msgpack-python
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

def _pack_integer(obj, fp):
    fp.write(b"\xd2" + struct.pack("<i", obj))

def _pack_nil(fp):
    fp.write(b"\xc0")

def _pack_boolean(obj, fp):
    fp.write(b"\xc3" if obj else b"\xc2")

def _pack_float(obj, fp):
    fp.write(b"\xca" + struct.pack("<f", obj))

def _pack_string(obj, fp):
    obj = obj.encode("utf-8")
    fp.write(b"\xdb" + struct.pack("<I", len(obj)) + obj)

def _pack_binary(obj, fp):
    fp.write(b"\xc6" + struct.pack("<I", len(obj)) + obj)

def _pack_array(obj, fp):
    fp.write(b"\xdd" + struct.pack("<I", len(obj)))

    if len(obj) > 0 and isinstance(obj[0], float):
        fp.write(b"\xca")
        for e in obj:
            fp.write(struct.pack("<f", e))
    elif len(obj) > 0 and isinstance(obj[0], bool):
        for e in obj:
            pack(e, fp)
    elif len(obj) > 0 and isinstance(obj[0], int):
        fp.write(b"\xd2")
        for e in obj:
            fp.write(struct.pack("<i", e))
    # Float32
    elif len(obj) > 0 and isinstance(obj[0], np.float32):
        fp.write(b"\xca")
        fp.write(obj.tobytes())
    # Int32
    elif len(obj) > 0 and isinstance(obj[0], np.int32):
        fp.write(b"\xd2")
        fp.write(obj.tobytes())
    # Int16
    elif len(obj) > 0 and isinstance(obj[0], np.int16):
        fp.write(b"\xd1")
        fp.write(obj.tobytes())
    # Regular
    else:
        for e in obj:
            pack(e, fp)

def _pack_map(obj, fp):
    fp.write(b"\xdf" + struct.pack("<I", len(obj)))
    for k, v in obj.items():
        pack(k, fp)
        pack(v, fp)

def pack(obj, fp):
    if obj is None:
        _pack_nil(fp)
    elif isinstance(obj, bool):
        _pack_boolean(obj, fp)
    elif isinstance(obj, int):
        _pack_integer(obj, fp)
    elif isinstance(obj, float):
        _pack_float(obj, fp)
    elif isinstance(obj, str):
        _pack_string(obj, fp)
    elif isinstance(obj, bytes):
        _pack_binary(obj, fp)
    elif isinstance(obj, (list, np.ndarray, tuple)):
        _pack_array(obj, fp)
    elif isinstance(obj, dict):
        _pack_map(obj, fp)

def packb(obj):
    fp = io.BytesIO()
    pack(obj, fp)
    return fp.getvalue()
