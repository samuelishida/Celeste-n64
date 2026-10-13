"""Blender 4.0 background worker for static player bind-pose baking."""

import argparse
import json
import math
import sys
from pathlib import Path

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from player_color_bake import sample_nearest_rgba


# The source Hair texture stores grayscale shading values. Tint those values
# with Madeline's red-orange hair color before baking them into COLOR_0.
_HAIR_TINT = (1.0, 0.22, 0.04, 1.0)


def _finite_vector(values):
    return all(math.isfinite(float(value)) for value in values)


def _apply_rest_pose():
    for obj in bpy.context.scene.objects:
        if obj.type == "ARMATURE":
            obj.data.pose_position = "REST"
            obj.hide_viewport = False
            obj.hide_render = False
    bpy.context.view_layer.update()


def _apply_mesh_modifiers():
    meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    for obj in meshes:
        bpy.context.view_layer.objects.active = obj
        obj.select_set(True)
        for modifier in list(obj.modifiers):
            if modifier.type == "ARMATURE":
                bpy.ops.object.modifier_apply(modifier=modifier.name)
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
        for polygon in obj.data.polygons:
            polygon.use_smooth = False
        obj.select_set(False)
    return meshes


def _remove_rig_and_animations():
    for obj in list(bpy.context.scene.objects):
        if obj.type == "ARMATURE":
            bpy.data.objects.remove(obj, do_unlink=True)
    for action in list(bpy.data.actions):
        bpy.data.actions.remove(action)


def _base_color_image(material):
    if not material.use_nodes or material.node_tree is None:
        return None
    principled = next((node for node in material.node_tree.nodes if node.type == "BSDF_PRINCIPLED"), None)
    if principled is not None:
        base_color = principled.inputs.get("Base Color")
        if base_color is not None:
            for link in base_color.links:
                if link.from_node.type == "TEX_IMAGE" and link.from_node.image is not None:
                    return link.from_node.image
    return next((node.image for node in material.node_tree.nodes
                 if node.type == "TEX_IMAGE" and node.image is not None), None)


def _base_color_factor(material):
    if material.use_nodes and material.node_tree is not None:
        principled = next((node for node in material.node_tree.nodes if node.type == "BSDF_PRINCIPLED"), None)
        if principled is not None:
            socket = principled.inputs.get("Base Color")
            if socket is not None:
                return tuple(float(value) for value in socket.default_value)
    return tuple(float(value) for value in material.diffuse_color)


def _bake_vertex_colors(meshes):
    color_samples = 0
    fallback_material = bpy.data.materials.get("Player")
    if fallback_material is None:
        raise ValueError("source Player material is missing")
    for obj in meshes:
        uv_layer = obj.data.uv_layers.active
        if uv_layer is None:
            raise ValueError(f"{obj.name}: missing UV map required for source color bake")
        loop_uvs = [(float(item.uv.x), float(item.uv.y)) for item in uv_layer.data]
        color_attribute = obj.data.color_attributes.get("Color")
        if color_attribute is not None:
            obj.data.color_attributes.remove(color_attribute)
        color_attribute = obj.data.color_attributes.new(name="Color", type="BYTE_COLOR", domain="CORNER")
        obj.data.color_attributes.active_color = color_attribute
        if not obj.material_slots:
            obj.data.materials.append(fallback_material)
            for polygon in obj.data.polygons:
                polygon.material_index = 0
        material_cache = {}
        for polygon in obj.data.polygons:
            material = obj.material_slots[polygon.material_index].material if polygon.material_index < len(obj.material_slots) else None
            if material is None:
                raise ValueError(f"{obj.name}: face {polygon.index} has no material slot "
                                 f"(slot_index={polygon.material_index}, slots={len(obj.material_slots)})")
            if material.name not in material_cache:
                image = _base_color_image(material)
                factor = _base_color_factor(material)
                if material.name == "Hair":
                    factor = tuple(factor[index] * _HAIR_TINT[index] for index in range(4))
                material_cache[material.name] = (image, factor)
            image, factor = material_cache[material.name]
            if len(polygon.loop_indices) < 3:
                raise ValueError(f"{obj.name}/{material.name}: degenerate face {polygon.index}")
            uv_sum = [0.0, 0.0]
            for loop_index in polygon.loop_indices:
                uv = loop_uvs[loop_index]
                uv_sum[0] += uv[0]
                uv_sum[1] += uv[1]
            inv_count = 1.0 / len(polygon.loop_indices)
            uv_center = (uv_sum[0] * inv_count, uv_sum[1] * inv_count)
            if not _finite_vector(uv_center):
                raise ValueError(f"{obj.name}/{material.name}: non-finite face UV "
                                 f"at face {polygon.index}: {uv_center!r}")
            color = factor if image is None else sample_nearest_rgba(
                image.pixels, int(image.size[0]), int(image.size[1]), uv_center, factor
            )
            for loop_index in polygon.loop_indices:
                color_attribute.data[loop_index].color = color
            color_samples += 1
        for layer in list(obj.data.uv_layers):
            obj.data.uv_layers.remove(layer)
    if color_samples <= 0:
        raise ValueError("no face colors were baked")
    return color_samples


def _set_fast64_properties(materials):
    for material in materials:
        material.use_nodes = True
        material["f3d_mat"] = {
            "combiner": "RDPQ_COMBINER_SHADE",
            "draw_flags": ["SHADED", "DEPTH"],
            "blend": "OPAQUE",
        }
        material["f3d_combiner"] = "RDPQ_COMBINER_SHADE"
        material["f3d_draw_flags"] = ["SHADED", "DEPTH"]
        material["f3d_blend"] = "OPAQUE"
        if material.node_tree is not None:
            for node in list(material.node_tree.nodes):
                if node.type == "TEX_IMAGE":
                    material.node_tree.nodes.remove(node)
            for node in material.node_tree.nodes:
                if node.type == "BSDF_PRINCIPLED":
                    node.inputs["Base Color"].default_value = (1.0, 1.0, 1.0, 1.0)
        material.diffuse_color = (1.0, 1.0, 1.0, 1.0)


def _report(meshes, materials, output, recipe, color_samples):
    vertices = 0
    triangles = 0
    bounds_min = [float("inf")] * 3
    bounds_max = [float("-inf")] * 3
    for obj in meshes:
        vertices += len(obj.data.vertices)
        triangles += sum(max(0, len(poly.vertices) - 2) for poly in obj.data.polygons)
        for vertex in obj.data.vertices:
            position = obj.matrix_world @ vertex.co
            values = [float(position.x), float(position.y), float(position.z)]
            if not _finite_vector(values):
                raise ValueError("non-finite baked vertex")
            for axis in range(3):
                bounds_min[axis] = min(bounds_min[axis], values[axis])
                bounds_max[axis] = max(bounds_max[axis], values[axis])
    if vertices == 0 or triangles == 0 or any(not math.isfinite(v) for v in bounds_min + bounds_max):
        raise ValueError("empty or non-finite baked geometry")
    return {
        "asset_id": recipe["asset_id"],
        "source": recipe.get("source_id", Path(recipe["source"]).name),
        "prepared": output.name,
        "preparation": "blender-rest-bind-pose-static-bake",
        "mesh_count": len(meshes),
        "node_count": len(meshes),
        "material_names": [material.name for material in materials],
        "image_dependencies": [],
        "skin_present": False,
        "animation_count": 0,
        "vertex_count": vertices,
        "triangle_count": triangles,
        "prepared_glb_bytes": output.stat().st_size,
        "bounds": {"min": bounds_min, "max": bounds_max},
        "pivot": recipe.get("pivot", "origin"),
        "base_scale": float(recipe.get("base_scale", 5.0)),
        "axis_transform": recipe.get("axis_transform", "game-y-up-xz-travel"),
        "flat_shading": True,
        "color_source": f"{Path(recipe['source']).name}#image[{int(recipe.get('texture_image_index', 0))}]",
        "color_source_sha256": recipe["color_source_sha256"],
        "vertex_color_attribute": "COLOR_0",
        "color_samples": color_samples,
        "alpha_policy": "opaque",
    }


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--report", required=True)
    parser.add_argument("--recipe", required=True)
    args = parser.parse_args(argv)
    source = Path(args.source).resolve()
    output = Path(args.output).resolve()
    report_path = Path(args.report).resolve()
    recipe = json.loads(Path(args.recipe).read_text(encoding="utf-8"))
    recipe["source"] = str(source)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(source))
    _apply_rest_pose()
    meshes = _apply_mesh_modifiers()
    color_samples = _bake_vertex_colors(meshes)
    materials = []
    for obj in meshes:
        for material_slot in obj.material_slots:
            if material_slot.material is not None and material_slot.material not in materials:
                materials.append(material_slot.material)
    _set_fast64_properties(materials)
    _remove_rig_and_animations()
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(
        filepath=str(output),
        export_format="GLB",
        export_copyright="",
        export_texcoords=True,
        export_normals=True,
        export_tangents=False,
        export_materials="EXPORT",
        export_image_format="AUTO",
        export_animations=False,
        export_skins=False,
        use_selection=False,
    )
    report = _report(meshes, materials, output, recipe, color_samples)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:])
