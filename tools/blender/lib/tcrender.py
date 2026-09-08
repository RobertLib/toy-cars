"""Scene previews through EEVEE - to see what the models really look like."""
import bpy
import math
import os
from mathutils import Vector, Euler


def setup_studio(bg="#dfe9f5", strength=1.0):
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_EEVEE'
    try:
        sc.eevee.use_raytracing = True
        sc.eevee.taa_render_samples = 48
        sc.eevee.use_shadows = True
    except Exception:
        pass
    sc.render.film_transparent = False
    sc.view_settings.view_transform = 'Standard'
    sc.view_settings.look = 'None'

    world = bpy.data.worlds.new("W")
    sc.world = world
    world.use_nodes = True
    bg_node = world.node_tree.nodes.get("Background")
    from .tcmesh import hexcol
    bg_node.inputs[0].default_value = hexcol(bg)
    bg_node.inputs[1].default_value = strength

    key = bpy.data.lights.new("Key", 'SUN')
    key.energy = 3.0
    key.angle = math.radians(8)
    ko = bpy.data.objects.new("Key", key)
    ko.rotation_euler = Euler((math.radians(52), 0, math.radians(38)))
    sc.collection.objects.link(ko)

    fill = bpy.data.lights.new("Fill", 'AREA')
    fill.energy = 300
    fill.size = 8
    fo = bpy.data.objects.new("Fill", fill)
    fo.location = (-5, 6, 4)
    fo.rotation_euler = Euler((math.radians(60), 0, math.radians(210)))
    sc.collection.objects.link(fo)
    return sc


def add_ground(size=200, color='#e7edf3'):
    from . import tcmesh as T
    g = T.box("PreviewGround", (size, size, 0.2), (0, 0, -0.1),
              T.material("PreviewGround", color, roughness=0.9))
    return g


def render(filepath, cam_loc, look_at=(0, 0, 0), res=(900, 640), lens=60,
           ortho=None):
    sc = bpy.context.scene
    cam_data = bpy.data.cameras.new("Cam")
    if ortho:
        cam_data.type = 'ORTHO'
        cam_data.ortho_scale = ortho
    else:
        cam_data.lens = lens
    cam = bpy.data.objects.new("Cam", cam_data)
    sc.collection.objects.link(cam)
    cam.location = Vector(cam_loc)
    d = Vector(look_at) - Vector(cam_loc)
    if d.length < 1e-6:
        d = Vector((0, 0, -1))
    cam.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
    sc.camera = cam
    sc.render.resolution_x, sc.render.resolution_y = res
    sc.render.resolution_percentage = 100
    sc.render.image_settings.file_format = 'PNG'
    # RGB, not Blender's RGBA default. `film_transparent` is off, so the
    # alpha channel carries nothing - but App Store Connect rejects an app
    # icon that *has* one at all, transparent or not, and this is the
    # function that renders the icon.
    sc.render.image_settings.color_mode = 'RGB'
    sc.render.filepath = filepath
    os.makedirs(os.path.dirname(filepath), exist_ok=True)
    bpy.ops.render.render(write_still=True)
    sc.collection.objects.unlink(cam)
    return filepath
