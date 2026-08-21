"""Fellowship of the Ring (2002) asset importer for Blender.

Imports meshes, textures, skeletons and animations out of the Riot Engine's
SRSC archives (*.mdu / *.tdu / *.adu) shipped with Surreal Software's
"The Lord of the Rings: The Fellowship of the Ring".
"""

bl_info = {
    "name": "LOTR: Fellowship of the Ring (Riot Engine)",
    "author": "Chip",
    "version": (1, 7, 4),
    "blender": (5, 0, 0),
    "location": "File > Import / File > Export > LOTR Fellowship",
    "description": "Import levels, models, textures, skeletons and animations from the "
                   "2002 Fellowship of the Ring PC game (Surreal Riot Engine), and "
                   "write edited meshes and textures back into its archives.",
    "category": "Import-Export",
}

import math
import os
import struct
import time

struct_error = struct.error

import bpy
from bpy.props import (BoolProperty, CollectionProperty, EnumProperty,
                       FloatProperty, IntProperty, StringProperty)
from bpy.types import Operator, Panel, PropertyGroup, UIList
from bpy_extras.io_utils import ImportHelper
from mathutils import Euler, Matrix, Quaternion, Vector

from .fotr import model as fmodel
from .fotr import anim as fanim
from .fotr import texture as ftexture
from .fotr import level as flevel
from .fotr import write as fwrite
from .fotr import mesh as fmesh
from .fotr import strip as fstrip
from .fotr.database import DatabaseIndex
from .fotr.srsc import SRSCError

# 2048 world units make one unit of model space. Dividing through puts terrain,
# object placements and models into a single consistent scale.
WORLD_UNIT = 2048.0

# Placed classes that exist only for the level editor and the runtime: AI
# waypoints, sound emitters, trigger volumes, light sources. They carry marker
# models that would otherwise litter the scene with thousands of tiny shapes.
MARKER_MODELS = frozenset((
    'ai node', 'sound detector', 'eax detector', 'trigger detector',
    'light source', 'playericon', 'portal', 'crate collider',
))

# The level editor textures invisible collision volumes - the walls that stop
# you walking through a hedge, the box around a bed - with a single 32x32
# placeholder. The game never draws them; a model textured entirely with it is
# collision geometry, not scenery.
COLLISION_TEXTURES = frozenset(('boundingwall',))

# The Riot Engine is Y-up, Blender is Z-up.
# The heaviest model in the retail data: the Dark Rider. Used only to warn.
RETAIL_MAX_VERTS = 3684
RETAIL_MAX_POLYS = 6783

# Earlier versions of this add-on deleted record 0x0211 on a whole-mesh export,
# on the theory that it was an optional cache of merged coplanar faces. That was
# wrong, and it is what caused the torn-ribbon rendering: 0x0211 is the triangle
# strip list, the only form the engine actually draws, and a model without one
# falls through to a path that reads past the end of its buffers - which is why
# the corruption changed with screen resolution and why the apparent vertex
# "budget" moved every time it was measured. The exporter now generates the
# record. See fotr/strip.py for the format and how it was established.

GAME_TO_BLENDER = Matrix(((1, 0, 0, 0),
                          (0, 0, -1, 0),
                          (0, 1, 0, 0),
                          (0, 0, 0, 1)))

_model_enum_cache = {'path': None, 'key': None, 'all': [], 'items': []}


# ---------------------------------------------------------------------------
# maths helpers - all in game space, converted once at the end
# ---------------------------------------------------------------------------

def _rest_world(joint):
    rot, pos = joint.rest_matrix()
    m = Matrix(((rot[0][0], rot[0][1], rot[0][2], pos[0]),
                (rot[1][0], rot[1][1], rot[1][2], pos[1]),
                (rot[2][0], rot[2][1], rot[2][2], pos[2]),
                (0.0, 0.0, 0.0, 1.0)))
    return m


def _rest_matrices(skeleton):
    """World and parent-relative rest matrices for every joint, in game space."""
    world = [_rest_world(j) for j in skeleton.joints]
    local = []
    for j, jt in enumerate(skeleton.joints):
        if jt.parent >= 0:
            local.append(world[jt.parent].inverted_safe() @ world[j])
        else:
            local.append(world[j].copy())
    return world, local


def _keyframe_local(key, rest_translation):
    """A keyframe's local transform.

    Most keyframes store rotation only; the bone's offset from its parent then
    comes from the skeleton's bind pose, which is what the mesh is actually bound
    to.  The animation record carries its own per-joint rest offset, but it is
    not reliable - on Gollum 37 of 63 joints disagree with the bind pose by
    roughly twice his own height, which tears the limbs into ribbons.
    """
    q = key.rotation
    m = Quaternion((q[3], q[0], q[1], q[2])).to_matrix().to_4x4()
    t = key.translation if key.translation is not None else rest_translation
    m.translation = Vector(t)
    return m


# ---------------------------------------------------------------------------
# building Blender data
# ---------------------------------------------------------------------------

def _build_image(tex, texture_dir, pack):
    name = '%s.png' % tex.name
    existing = bpy.data.images.get(name)
    if existing is not None and existing.size[0] == tex.width:
        return existing
    if texture_dir:
        path = os.path.join(texture_dir, _safe(tex.name) + '.png')
        try:
            ftexture.write_png(path, tex.width, tex.height, tex.rgba)
            img = bpy.data.images.load(path, check_existing=True)
            img.name = name
            if pack:
                img.pack()
            return img
        except OSError:
            pass
    # fall back to an in-memory image
    img = bpy.data.images.new(name, tex.width, tex.height, alpha=True)
    px = [0.0] * (tex.width * tex.height * 4)
    raw = tex.rgba
    stride = tex.width * 4
    for y in range(tex.height):
        src = (tex.height - 1 - y) * stride
        dst = y * stride
        for i in range(stride):
            px[dst + i] = _SRGB_TO_LINEAR[raw[src + i]] if (i & 3) != 3 else raw[src + i] / 255.0
    img.pixels.foreach_set(px)
    img.update()
    return img


_SRGB_TO_LINEAR = [((c / 255.0) / 12.92 if c / 255.0 <= 0.04045
                    else (((c / 255.0) + 0.055) / 1.055) ** 2.4) for c in range(256)]


def _safe(name):
    return ''.join(c if (c.isalnum() or c in ' ._-()') else '_' for c in name).strip() or 'unnamed'


class MaterialCache(object):
    """One Blender material per game texture, shared across every model imported."""

    def __init__(self, resolver, texture_dir, pack, use_alpha):
        self.resolver = resolver
        self.texture_dir = texture_dir
        self.pack = pack
        self.use_alpha = use_alpha
        self._cache = {}
        # (texture id, database id) pairs a model asked for that could not be
        # resolved, so the importer can say which archive is absent rather than
        # handing back untextured materials in silence.
        self.missing = set()

    def terrain_material(self, ref, index):
        """Terrain cells reference a texture directly rather than through a model."""
        if ref in self._cache:
            return self._cache[ref]
        tex = index.load_texture(*ref) if (self.resolver is not None and ref[0]) else None
        mat = _material_from_texture(tex, 'terrain_%d_%d' % ref,
                                     self.texture_dir, self.pack, self.use_alpha)
        self._cache[ref] = mat
        return mat

    def get(self, mdl, slot):
        key = mdl.textures[slot] if slot < len(mdl.textures) else ('untextured', mdl.id, slot)
        if key not in self._cache:
            self._cache[key] = _build_material(mdl, slot, self.resolver, self.texture_dir,
                                               self.pack, self.use_alpha, self.missing)
        return self._cache[key]


def _build_material(mdl, slot, resolver, texture_dir, pack, use_alpha, missing=None):
    tex_id, db_id = mdl.textures[slot] if slot < len(mdl.textures) else (0, 0)
    tex = resolver.load(tex_id, db_id) if (resolver and tex_id) else None
    if tex is None and tex_id and missing is not None:
        missing.add((tex_id, db_id))
    return _material_from_texture(tex, '%s_%d' % (_safe(mdl.name), slot),
                                  texture_dir, pack, use_alpha)


def _where_textures_live(mdu_path, refs):
    """Name the archives a set of unresolved (texture, database) refs belong to.

    A model can point into a sibling database, so "missing" usually means a
    folder that was not where the manifest said it would be, not a broken file.
    """
    try:
        res = ftexture.TextureResolver(mdu_path)
    except Exception:
        return 'check the .tdu files are where the .db manifests expect them'
    homes, unknown = set(), set()
    for _tid, db_id in refs:
        other = res.database_for(db_id)
        if other is not None:
            homes.add(os.path.basename(other.path))
        else:
            unknown.add(db_id)
    parts = []
    if homes:
        parts.append('they are in %s' % ', '.join(sorted(homes)))
    if unknown:
        parts.append('database%s %s could not be found from this folder'
                     % ('' if len(unknown) == 1 else 's',
                        ', '.join(str(d) for d in sorted(unknown))))
    return '; '.join(parts) or 'the archives holding them are not beside this file'.strip()


def _material_from_texture(tex, fallback_name, texture_dir, pack, use_alpha):
    mat = bpy.data.materials.new(fallback_name if tex is None else _safe(tex.name))
    if mat.node_tree is None:           # 5.0 already has one; use_nodes goes away in 6.0
        mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get('Principled BSDF')
    if bsdf is not None:
        bsdf.inputs['Roughness'].default_value = 0.9
        if 'Specular IOR Level' in bsdf.inputs:
            bsdf.inputs['Specular IOR Level'].default_value = 0.1
    if tex is None or bsdf is None:
        return mat
    img = _build_image(tex, texture_dir, pack)
    node = mat.node_tree.nodes.new('ShaderNodeTexImage')
    node.image = img
    node.interpolation = 'Closest'
    node.location = (-350, 250)
    mat.node_tree.links.new(bsdf.inputs['Base Color'], node.outputs['Color'])
    mode = getattr(tex, 'alpha_mode', 'none')
    if use_alpha and mode != 'none':
        mat.node_tree.links.new(bsdf.inputs['Alpha'], node.outputs['Alpha'])
        # Only true graduated alpha gets blended. Cutouts stay on the clipped
        # path: their alpha is already binary, so clipping is exact, and it
        # avoids the viewport sorting artefacts that blending causes wherever
        # cutout surfaces overlap - hair, beards, leaf cards.
        if mode == 'blend':
            _set_blend(mat)
    return mat


def _set_blend(mat):
    """Alpha blending moved around between Blender versions; try what exists."""
    for attr, value in (('surface_render_method', 'BLENDED'), ('blend_method', 'BLEND')):
        if hasattr(mat, attr):
            try:
                setattr(mat, attr, value)
                return
            except (TypeError, AttributeError):
                continue


# The Riot Engine winds triangles clockwise-front, the opposite of Blender.
# Left as-is, every imported normal points into the model instead of out of it:
# 365 of the 423 closed props in the retail data come out with negative volume,
# and every single terrain triangle faces the ground. Reversing each triangle
# fixes shading, backface culling and anything exported downstream.
def _flip(indices):
    return list(indices)[::-1]


def _usable_faces(mdl, lod):
    """Polygons that survive validation, with their vertex list.

    Exact duplicate faces are dropped here rather than left for Blender to
    remove: Blender cannot hold two faces on the same vertices, and if it
    silently drops one, every UV and material assignment after it shifts by one.
    Around thirty models in the retail data contain duplicates - Sam, Gimli,
    Gollum and Galadriel among them.
    """
    verts, polys = mdl.lod_mesh(lod)
    keep = []
    seen = set()
    for p in polys:
        idx = [i for i in p.indices if i < len(verts)]
        if len(idx) < 3 or len(set(idx)) != len(idx):
            continue
        key = frozenset(idx)
        if key in seen:
            continue
        seen.add(key)
        keep.append((idx, p))
    return verts, keep


def _assign_loops(mesh, sources, uv_data, material_of):
    """Write UVs and material indices onto whatever mesh Blender actually built.

    Faces are matched by their vertex set and UVs are looked up per vertex, so
    the result is correct even if Blender drops a face or rotates a polygon's
    loop order - neither of which it promises not to do.
    """
    for face in mesh.polygons:
        src = sources.get(frozenset(face.vertices))
        if src is None:
            continue
        uv_of, slot = src
        for loop_index in face.loop_indices:
            u, v = uv_of.get(mesh.loops[loop_index].vertex_index, (0.0, 0.0))
            uv_data[loop_index].uv = (u, 1.0 - v)
        face.material_index = slot
        face.use_smooth = True


def _build_mesh(mdl, lod, materials, scale, name=None):
    verts, keep = _usable_faces(mdl, lod)
    if not verts or not keep:
        return None

    mesh = bpy.data.meshes.new(name or _safe(mdl.name))
    mesh.from_pydata([(v[0] * scale, v[1] * scale, v[2] * scale) for v in verts],
                     [], [_flip(idx) for idx, _p in keep])
    mesh.validate(verbose=False)

    remap = {}
    for slot in sorted({p.texture for _idx, p in keep}):
        mesh.materials.append(materials.get(mdl, slot))
        remap[slot] = len(mesh.materials) - 1

    sources = {}
    for idx, p in keep:
        sources[frozenset(idx)] = (dict(zip(idx, p.uvs)), remap.get(p.texture, 0))
    _assign_loops(mesh, sources, mesh.uv_layers.new(name='UVMap').data, remap)

    obj = bpy.data.objects.new(name or _safe(mdl.name), mesh)
    obj.matrix_world = GAME_TO_BLENDER
    _stamp(obj, mdl, lod, scale, remap)
    return obj


def _stamp(obj, mdl, lod, scale, remap):
    """Record where this object came from so it can be exported back.

    Without the material map the exporter would have no way to turn a Blender
    material index back into the texture slot the model's polygons reference:
    the importer only creates slots for the textures a LOD actually uses, so
    the two numberings do not line up.
    """
    obj['fotr_model_id'] = int(mdl.id)
    obj['fotr_model_name'] = mdl.name
    obj['fotr_lod'] = int(lod)
    obj['fotr_scale'] = float(scale)
    slots = [0] * (max(remap.values()) + 1 if remap else 1)
    for game_slot, blender_slot in remap.items():
        slots[blender_slot] = int(game_slot)
    obj['fotr_texture_slots'] = slots


class MeshCombiner(object):
    """Accumulates many models into a single mesh, sharing one material list."""

    def __init__(self, materials, scale, name):
        self.materials = materials
        self.scale = scale
        self.name = name
        self.verts = []
        self.faces = []
        self.sources = {}
        self.material_list = []
        self._material_index = {}
        self.count = 0

    def add(self, mdl, lod):
        verts, keep = _usable_faces(mdl, lod)
        if not verts or not keep:
            return False
        base = len(self.verts)
        s = self.scale
        self.verts.extend((v[0] * s, v[1] * s, v[2] * s) for v in verts)
        for idx, p in keep:
            face = [i + base for i in idx]
            self.faces.append(_flip(face))
            # keyed by vertex, so the reversed winding cannot disturb the UVs
            self.sources[frozenset(face)] = (dict(zip(face, p.uvs)),
                                             self._slot(mdl, p.texture))
        self.count += 1
        return True

    def _slot(self, mdl, texture):
        mat = self.materials.get(mdl, texture)
        if mat.name not in self._material_index:
            self._material_index[mat.name] = len(self.material_list)
            self.material_list.append(mat)
        return self._material_index[mat.name]

    def build(self):
        if not self.faces:
            return None
        mesh = bpy.data.meshes.new(self.name)
        mesh.from_pydata(self.verts, [], self.faces)
        mesh.validate(verbose=False)
        for mat in self.material_list:
            mesh.materials.append(mat)
        _assign_loops(mesh, self.sources,
                      mesh.uv_layers.new(name='UVMap').data, None)
        obj = bpy.data.objects.new(self.name, mesh)
        obj.matrix_world = GAME_TO_BLENDER
        return obj


def _build_armature(mdl, lod, mesh_obj, scale, bone_size, collection=None):
    skel = mdl.skeleton
    if skel is None or not skel.joints:
        return None
    world, local = _rest_matrices(skel)

    arm = bpy.data.armatures.new(_safe(mdl.name) + '_rig')
    arm.display_type = 'OCTAHEDRAL'
    rig = bpy.data.objects.new(_safe(mdl.name) + '_rig', arm)
    (collection or bpy.context.collection).objects.link(rig)
    rig.matrix_world = GAME_TO_BLENDER

    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode='EDIT')

    children = {}
    for j, jt in enumerate(skel.joints):
        children.setdefault(jt.parent, []).append(j)

    edit_names = []
    for j, jt in enumerate(skel.joints):
        eb = arm.edit_bones.new(_bone_name(jt, j, edit_names))
        edit_names.append(eb.name)
        # a zero length bone has no orientation to assign to, so give it one first
        eb.head = (0.0, 0.0, 0.0)
        eb.tail = (0.0, 1.0, 0.0)
        m = world[j].copy()
        m.translation = m.translation * scale
        eb.matrix = m
        kids = children.get(j, [])
        if kids:
            d = (world[kids[0]].translation - world[j].translation).length * scale
            eb.length = max(d, bone_size * 0.25)
        else:
            eb.length = bone_size
    for j, jt in enumerate(skel.joints):
        if jt.parent >= 0:
            arm.edit_bones[edit_names[j]].parent = arm.edit_bones[edit_names[jt.parent]]
    bpy.ops.object.mode_set(mode='OBJECT')

    if mesh_obj is not None:
        vg = {}
        for j, jt in enumerate(skel.joints):
            if lod < len(jt.weights) and jt.weights[lod]:
                vg[j] = mesh_obj.vertex_groups.new(name=edit_names[j])
        nverts = len(mesh_obj.data.vertices)

        # A dozen vertices in the retail data are listed under exactly one joint
        # with a weight of zero - six on the Barrow Wight, six on Asfaloth. Left
        # as written they have no influence at all, so they stay behind while the
        # rest of the limb moves and drag a triangle out behind them. Being named
        # by a single joint and nothing else, the intent is a full bind.
        total = {}
        for j, jt in enumerate(skel.joints):
            if lod < len(jt.weights):
                for vi, w in jt.weights[lod]:
                    total[vi] = total.get(vi, 0.0) + w

        for j, group in vg.items():
            for vi, w in skel.joints[j].weights[lod]:
                if vi < nverts:
                    group.add([vi], w if total.get(vi, 0.0) > 1e-6 else 1.0, 'REPLACE')
        mesh_obj.parent = rig
        mesh_obj.matrix_world = GAME_TO_BLENDER
        mod = mesh_obj.modifiers.new('Armature', 'ARMATURE')
        mod.object = rig
    return rig, edit_names, local


def _bone_name(joint, index, taken):
    name = joint.name.split('|')[-1] or ('joint_%d' % index)
    base = name
    n = 1
    while name in taken:
        n += 1
        name = '%s.%03d' % (base, n)
    return name


def _build_action(rig, bone_names, rest_local, animation, fps, scale):
    action = bpy.data.actions.new(_safe(animation.name))
    action.use_fake_user = True
    if rig.animation_data is None:
        rig.animation_data_create()
    rig.animation_data.action = action

    for pb in rig.pose.bones:
        pb.rotation_mode = 'QUATERNION'

    last = 1
    for track in animation.tracks:
        j = track.joint
        if j >= len(bone_names) or not track.keyframes:
            continue
        pb = rig.pose.bones.get(bone_names[j])
        if pb is None:
            continue
        inv_rest = rest_local[j].inverted_safe()
        bind_offset = rest_local[j].translation
        for key in track.keyframes:
            local = _keyframe_local(key, bind_offset)
            basis = inv_rest @ local
            loc, rot, _scl = basis.decompose()
            pb.location = loc * scale
            pb.rotation_quaternion = rot
            frame = 1 + key.time * fps
            last = max(last, frame)
            pb.keyframe_insert('location', frame=frame, group=pb.name)
            pb.keyframe_insert('rotation_quaternion', frame=frame, group=pb.name)

    for fc in _action_fcurves(action):
        for kp in fc.keyframe_points:
            kp.interpolation = 'LINEAR'
    _set_action_range(action, 1, last)
    return action, last


def _action_fcurves(action):
    """Every F-Curve in an action, on any Blender version.

    Blender 4.4 introduced slotted actions and 5.0 removed `Action.fcurves`
    outright; curves now live under layers -> strips -> channelbags.
    """
    legacy = getattr(action, 'fcurves', None)
    if legacy is not None:
        return list(legacy)
    curves = []
    for layer in getattr(action, 'layers', ()):
        for strip in getattr(layer, 'strips', ()):
            for bag in getattr(strip, 'channelbags', ()):
                curves.extend(bag.fcurves)
    return curves


def _set_action_range(action, start, end):
    try:
        action.use_frame_range = True
        action.frame_start = start
        action.frame_end = max(end, start + 1)
    except AttributeError:
        pass


# ---------------------------------------------------------------------------
# levels
# ---------------------------------------------------------------------------

def _terrain_object(level, layers, materials, index, scale, name):
    """Build one mesh from a set of terrain layers.

    Each layer is an independent grid of cells anchored at (origin_x, origin_z);
    a cell carries one texture and its own UVs, and is split along whichever
    diagonal the face flags name. Cells with no texture are holes and are skipped.
    """
    verts, faces, uvs, slots = [], [], [], []
    material_index = {}
    unit = WORLD_UNIT

    for layer in layers:
        if not layer.vertices or not layer.faces:
            continue
        stride = layer.row_stride
        base = len(verts)
        ox, oz = layer.origin_x, layer.origin_z
        wh = layer.world_height
        for row in range(layer.height + 1):
            for col in range(stride):
                v = layer.vertices[row * stride + col]
                verts.append(((ox + col) * scale,
                              (wh + v.height) / unit * scale,
                              (oz + row) * scale))
        corner = {}
        for row in range(layer.height):
            for col in range(layer.width):
                face = layer.faces[row * layer.width + col]
                if face.is_hole:
                    continue
                corner['a'] = base + row * stride + col
                corner['b'] = corner['a'] + 1
                corner['c'] = base + (row + 1) * stride + col
                corner['d'] = corner['c'] + 1
                slot = _terrain_slot(face.texture, materials, index, material_index)
                for tri in face.triangles():
                    if len({corner[c] for c in tri}) < 3:
                        continue
                    faces.append(_flip([corner[c] for c in tri]))
                    uvs.extend(face.uv[c] for c in reversed(tri))
                    slots.append(slot)

    if not faces:
        return None
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(verts, [], faces)
    mesh.validate(verbose=False)
    ordered = [m for m, _i in sorted(material_index.values(), key=lambda p: p[1])]
    for mat in ordered:
        mesh.materials.append(mat)
    sources = {}
    at = 0
    for face, slot in zip(faces, slots):
        sources[frozenset(face)] = (dict(zip(face, uvs[at:at + len(face)])), slot)
        at += len(face)
    _assign_loops(mesh, sources, mesh.uv_layers.new(name='UVMap').data, None)
    obj = bpy.data.objects.new(name, mesh)
    obj.matrix_world = GAME_TO_BLENDER
    return obj


def _terrain_slot(ref, materials, index, material_index):
    if ref in material_index:
        return material_index[ref][1]
    mat = materials.terrain_material(ref, index)
    material_index[ref] = (mat, len(material_index))
    return material_index[ref][1]


def _is_collision_only(mdl, index):
    """True when every texture on the model is the collision placeholder."""
    if not mdl.textures:
        return False
    for tex_id, db_id in mdl.textures:
        tex = index.load_texture(tex_id, db_id)
        if tex is None or tex.name.lower() not in COLLISION_TEXTURES:
            return False
    return True


# The engine builds an object's orientation as a quaternion from the three
# stored angles in XYZ order, and subtracts 90 degrees from the yaw. Without
# that offset every door, window and building facade sits a quarter turn out of
# true - most props are trees and rocks, so it only shows on things with a front.
OBJECT_YAW_OFFSET = math.radians(-90.0)
OBJECT_EULER_ORDER = 'XYZ'


def _object_transform(obj, scale):
    """Placement transform in game space: translate, rotate, scale."""
    x, y, z = obj.position
    m = Matrix.Translation(Vector((x / WORLD_UNIT * scale,
                                   y / WORLD_UNIT * scale,
                                   z / WORLD_UNIT * scale)))
    rx, ry, rz = (math.radians(v) for v in obj.rotation)
    m = m @ Euler((rx, ry + OBJECT_YAW_OFFSET, rz),
                  OBJECT_EULER_ORDER).to_matrix().to_4x4()
    sx, sy, sz = obj.scale
    if (sx, sy, sz) != (1.0, 1.0, 1.0):
        m = m @ Matrix.Diagonal(Vector((sx, sy, sz, 1.0)))
    return m


# ---------------------------------------------------------------------------
# import operator
# ---------------------------------------------------------------------------

def _model_items(self, context):
    """Populate the model dropdown from the selected *.mdu, narrowed by the filter.

    Blender calls this constantly while the file browser is open, so the parsed
    listing is cached and only the cheap filtering step repeats.
    """
    path = getattr(self, 'filepath', '')
    if not path or not os.path.isfile(path):
        return [('NONE', '(select a .mdu file)', '')]

    if _model_enum_cache['path'] != path:
        try:
            db = fmodel.ModelDatabase(path)
            listing = [('%d' % mid, name, 'model id %d' % mid) for mid, name in db.listing()]
        except (SRSCError, OSError, ValueError) as exc:
            listing = [('NONE', '(%s)' % exc, '')]
        _model_enum_cache['path'] = path
        _model_enum_cache['all'] = listing or [('NONE', '(no models)', '')]
        _model_enum_cache['key'] = None

    needle = (getattr(self, 'name_filter', '') or '').strip().lower()
    if _model_enum_cache['key'] != needle:
        everything = _model_enum_cache['all']
        if needle:
            hits = [i for i in everything if needle in i[1].lower()]
        else:
            hits = everything
        _model_enum_cache['key'] = needle
        _model_enum_cache['items'] = hits or [('NONE', '(no name contains "%s")' % needle, '')]
    return _model_enum_cache['items']


class IMPORT_SCENE_OT_fotr(Operator, ImportHelper):
    """Import a model from The Lord of the Rings: The Fellowship of the Ring (2002)"""
    bl_idname = "import_scene.fotr_mdu"
    bl_label = "Import Fellowship Model"
    bl_options = {'REGISTER', 'UNDO'}

    filename_ext = ".mdu"
    filter_glob: StringProperty(default="*.mdu", options={'HIDDEN'})

    # SKIP_SAVE matters: Blender otherwise remembers the chosen model between
    # invocations, and an id from the last archive will not exist in the next
    # one - which makes the operator fail before execute() ever runs.
    name_filter: StringProperty(name="Find", default="",
                                description="Narrows the model list below. Type part of a "
                                            "name, like 'legolas' or 'orc'",
                                options={'SKIP_SAVE'})
    model: EnumProperty(name="Model", description="The model to import",
                        items=_model_items, options={'SKIP_SAVE'})
    import_all: BoolProperty(name="Import Every Match", default=False,
                             description="Import the whole filtered list at once instead "
                                         "of just the model selected above")

    combine: BoolProperty(name="Combine Into One Object", default=False,
                          description="Merge every imported model into a single mesh "
                                      "instead of one object each. Rigged characters are "
                                      "always kept separate so their skeletons survive")
    use_collection: BoolProperty(name="Put In A Collection", default=True,
                                 description="Group the import under a new collection "
                                             "named after the archive")

    lod: IntProperty(name="LOD", default=0, min=0, max=4,
                     description="0 is the highest detail mesh")
    scale: FloatProperty(name="Scale", default=1.0, min=0.0001, soft_max=100.0)

    import_textures: BoolProperty(name="Textures", default=True)
    pack_textures: BoolProperty(name="Pack Into .blend", default=True)
    texture_dir: StringProperty(name="Texture Folder", subtype='DIR_PATH', default="",
                                description="Where extracted PNGs are written. "
                                            "Leave empty to use a temporary folder")
    use_alpha: BoolProperty(name="Use Alpha", default=True,
                            description="Cut out foliage, hair and anything else whose "
                                        "texture is transparent. Without it leaf cards "
                                        "render as opaque slabs")

    import_armature: BoolProperty(name="Armature + Weights", default=True)
    bone_size: FloatProperty(name="Leaf Bone Length", default=0.05, min=0.001, soft_max=1.0)

    import_animations: BoolProperty(name="Animations", default=True)
    anim_filter: StringProperty(name="Animation Filter", default="",
                                description="Substring match on animation names")
    max_animations: IntProperty(name="Max Animations", default=8, min=0, max=200,
                                description="0 imports every animation the model references")
    fps: IntProperty(name="Frame Rate", default=30, min=1, max=240)

    def draw(self, context):
        layout = self.layout
        layout.use_property_split = True
        layout.use_property_decorate = False

        box = layout.box()
        box.label(text="Model", icon='MESH_DATA')
        box.prop(self, 'name_filter', icon='VIEWZOOM')
        row = box.row()
        row.enabled = not self.import_all
        row.prop(self, 'model')
        box.prop(self, 'import_all')
        row = box.row()
        row.enabled = self.import_all
        row.prop(self, 'combine')
        box.prop(self, 'use_collection')
        if self.import_all:
            n = len([i for i in _model_enum_cache.get('items', []) if i[0] != 'NONE'])
            box.label(text="%d model%s will be imported" % (n, '' if n == 1 else 's'),
                      icon='INFO')
        box.prop(self, 'lod')
        box.prop(self, 'scale')

        box = layout.box()
        box.label(text="Textures", icon='TEXTURE')
        box.prop(self, 'import_textures')
        sub = box.column()
        sub.enabled = self.import_textures
        sub.prop(self, 'pack_textures')
        sub.prop(self, 'texture_dir')
        sub.prop(self, 'use_alpha')

        box = layout.box()
        box.label(text="Rig", icon='ARMATURE_DATA')
        box.prop(self, 'import_armature')
        sub = box.column()
        sub.enabled = self.import_armature
        sub.prop(self, 'bone_size')
        sub.prop(self, 'import_animations')
        col = sub.column()
        col.enabled = self.import_animations
        col.prop(self, 'anim_filter')
        col.prop(self, 'max_animations')
        col.prop(self, 'fps')

    def execute(self, context):
        t0 = time.time()
        try:
            db = fmodel.ModelDatabase(self.filepath)
        except (SRSCError, OSError, ValueError) as exc:
            self.report({'ERROR'}, str(exc))
            return {'CANCELLED'}

        targets = []
        if self.import_all:
            targets = db.find(self.name_filter) if self.name_filter else list(db.ids)
        else:
            chosen = None
            try:
                chosen = self.model
            except TypeError:               # stale value from another archive
                chosen = None
            if chosen and chosen != 'NONE' and chosen.isdigit() and int(chosen) in db.ids:
                targets = [int(chosen)]
            elif self.name_filter:          # fall back to the search box
                matches = db.find(self.name_filter)
                targets = matches[:1]
        if not targets:
            self.report({'ERROR'}, "No model selected - pick one from the Model list "
                                   "(%d in this archive)" % len(db.ids))
            return {'CANCELLED'}

        resolver = None
        missing_archive = None
        if self.import_textures:
            try:
                resolver = ftexture.TextureResolver(self.filepath)
            except Exception:
                resolver = None
            # Textures are found by looking for the .tdu beside the .mdu and then
            # following the folder's .db manifests. Move or rename that file and
            # the lookup simply comes back empty - materials with no image and no
            # complaint, which reads as a broken importer rather than a misplaced
            # file. Say it instead.
            sibling = os.path.splitext(self.filepath)[0] + '.tdu'
            if not os.path.isfile(sibling):
                missing_archive = os.path.basename(sibling)

        target = context.collection
        if self.use_collection:
            archive = os.path.splitext(os.path.basename(self.filepath))[0]
            target = bpy.data.collections.new(_safe(archive))
            context.scene.collection.children.link(target)

        tex_dir = bpy.path.abspath(self.texture_dir) if self.texture_dir else bpy.app.tempdir
        if tex_dir and not os.path.isdir(tex_dir):
            try:
                os.makedirs(tex_dir)
            except OSError:
                tex_dir = bpy.app.tempdir

        anim_db = None
        if self.import_animations and self.import_armature:
            apath = fanim.find_database(self.filepath)
            if apath:
                try:
                    anim_db = fanim.AnimationDatabase(apath)
                except Exception:
                    anim_db = None

        materials = MaterialCache(resolver, tex_dir, self.pack_textures, self.use_alpha)
        combiner = None
        if self.combine and self.import_all:
            archive = os.path.splitext(os.path.basename(self.filepath))[0]
            combiner = MeshCombiner(materials, self.scale, _safe(archive))

        imported = skipped = actions = 0
        last_frame = 1
        for mid in targets:
            mdl = db.load(mid)
            if not mdl.has_mesh:
                skipped += 1                    # collision hulls carry no drawable geometry
                continue

            # a rigged character cannot be merged away without losing its skeleton
            rigged = bool(self.import_armature and mdl.skeleton and mdl.skeleton.joints)
            if combiner is not None and not rigged:
                if combiner.add(mdl, self.lod):
                    imported += 1
                else:
                    skipped += 1
                continue

            obj = _build_mesh(mdl, self.lod, materials, self.scale)
            if obj is None:
                skipped += 1
                continue
            obj['fotr_archive'] = self.filepath
            target.objects.link(obj)
            imported += 1

            if self.import_armature and mdl.skeleton:
                built = _build_armature(mdl, self.lod, obj, self.scale, self.bone_size, target)
                if built and anim_db is not None:
                    rig, names, rest_local = built
                    n = 0
                    for aid, _dbid in mdl.animation_refs:
                        if self.max_animations and n >= self.max_animations:
                            break
                        if aid not in anim_db.infos:
                            continue
                        if self.anim_filter and self.anim_filter.lower() not in \
                                anim_db.name_of(aid).lower():
                            continue
                        animation = anim_db.load(aid)
                        if animation is None or not animation.tracks:
                            continue
                        _act, end = _build_action(rig, names, rest_local, animation,
                                                  self.fps, self.scale)
                        last_frame = max(last_frame, end)
                        actions += 1
                        n += 1

        combined = 0
        if combiner is not None:
            merged = combiner.build()
            if merged is not None:
                target.objects.link(merged)
                combined = combiner.count

        if actions:
            context.scene.render.fps = self.fps
            context.scene.frame_start = 1
            context.scene.frame_end = int(last_frame) or 1

        msg = "Imported %d model%s" % (imported, '' if imported == 1 else 's')
        if combined:
            msg += " (%d merged into one mesh)" % combined
        if actions:
            msg += ", %d animation%s" % (actions, '' if actions == 1 else 's')
        if skipped:
            msg += " (%d had no mesh)" % skipped
        untextured = sorted(materials.missing)
        msg += " in %.1fs" % (time.time() - t0)
        if missing_archive:
            self.report({'WARNING'}, msg + " - no %s beside this .mdu, so nothing is "
                                           "textured. Put the .tdu back next to the .mdu."
                        % missing_archive)
            return {'FINISHED'}
        if self.import_textures and untextured:
            self.report({'WARNING'}, msg + " - %d texture%s could not be found; %s"
                        % (len(untextured), '' if len(untextured) == 1 else 's',
                           _where_textures_live(self.filepath, untextured)))
            return {'FINISHED'}
        self.report({'INFO'}, msg)
        return {'FINISHED'} if imported else {'CANCELLED'}


class IMPORT_SCENE_OT_fotr_textures(Operator, ImportHelper):
    """Extract every texture from a Fellowship *.tdu archive to PNG"""
    bl_idname = "import_scene.fotr_tdu"
    bl_label = "Extract Fellowship Textures"
    bl_options = {'REGISTER'}

    filename_ext = ".tdu"
    filter_glob: StringProperty(default="*.tdu", options={'HIDDEN'})
    output_dir: StringProperty(name="Output Folder", subtype='DIR_PATH', default="")
    load_into_blend: BoolProperty(name="Load Into Blend", default=False)

    def execute(self, context):
        try:
            db = ftexture.TextureDatabase(self.filepath)
        except (SRSCError, OSError, ValueError) as exc:
            self.report({'ERROR'}, str(exc))
            return {'CANCELLED'}
        out = bpy.path.abspath(self.output_dir) if self.output_dir else bpy.app.tempdir
        if not os.path.isdir(out):
            try:
                os.makedirs(out)
            except OSError:
                self.report({'ERROR'}, "Cannot write to %s" % out)
                return {'CANCELLED'}
        n = 0
        used = set()
        for tid in db.ids:
            tex = db.load(tid)
            if tex is None:
                continue
            stem = _safe(tex.name)
            if stem.lower() in used:                    # names repeat inside an archive
                stem = '%s_%d' % (stem, tid)
            used.add(stem.lower())
            path = os.path.join(out, '%s.png' % stem)
            ftexture.write_png(path, tex.width, tex.height, tex.rgba)
            if self.load_into_blend:
                bpy.data.images.load(path, check_existing=True)
            n += 1
        self.report({'INFO'}, "Wrote %d textures to %s" % (n, out))
        return {'FINISHED'}


class IMPORT_SCENE_OT_fotr_level(Operator, ImportHelper):
    """Import a whole level: terrain and every object placed on it"""
    bl_idname = "import_scene.fotr_lvl"
    bl_label = "Import Fellowship Level"
    bl_options = {'REGISTER', 'UNDO'}

    filename_ext = ".lvl"
    filter_glob: StringProperty(default="*.lvl", options={'HIDDEN'})

    import_terrain: BoolProperty(name="Terrain", default=True,
                                 description="Build the heightmap layers as meshes")
    import_objects: BoolProperty(name="Objects", default=True,
                                 description="Place every prop, building and tree "
                                             "the level puts on the map")
    skip_markers: BoolProperty(name="Skip Markers", default=True,
                               description="Leave out AI waypoints, trigger volumes, "
                                           "sound emitters and light markers")
    skip_invisible: BoolProperty(name="Skip Hidden Objects", default=True,
                                 description="Leave out objects the level flags as "
                                             "not visible in game")
    skip_collision: BoolProperty(name="Skip Collision Volumes", default=True,
                                 description="Leave out the invisible walls the engine "
                                             "uses for collision, which the game never draws")
    link_duplicates: BoolProperty(name="Share Mesh Data", default=True,
                                  description="Repeated props reuse one mesh, so a "
                                              "thousand trees cost one tree's memory")

    scale: FloatProperty(name="Scale", default=1.0, min=0.0001, soft_max=100.0)
    lod: IntProperty(name="LOD", default=0, min=0, max=4)
    import_textures: BoolProperty(name="Textures", default=True)
    pack_textures: BoolProperty(name="Pack Into .blend", default=True)
    texture_dir: StringProperty(name="Texture Folder", subtype='DIR_PATH', default="")
    use_alpha: BoolProperty(name="Use Alpha", default=True,
                            description="Cut out foliage and other textures that carry "
                                        "an alpha channel. Levels are full of leaf cards, "
                                        "so this is on by default")

    def draw(self, context):
        layout = self.layout
        layout.use_property_split = True
        layout.use_property_decorate = False
        box = layout.box()
        box.label(text="Contents", icon='WORLD')
        box.prop(self, 'import_terrain')
        box.prop(self, 'import_objects')
        sub = box.column()
        sub.enabled = self.import_objects
        sub.prop(self, 'skip_markers')
        sub.prop(self, 'skip_invisible')
        sub.prop(self, 'skip_collision')
        sub.prop(self, 'link_duplicates')
        sub.prop(self, 'lod')
        box.prop(self, 'scale')
        box = layout.box()
        box.label(text="Textures", icon='TEXTURE')
        box.prop(self, 'import_textures')
        sub = box.column()
        sub.enabled = self.import_textures
        sub.prop(self, 'pack_textures')
        sub.prop(self, 'texture_dir')
        sub.prop(self, 'use_alpha')

    def execute(self, context):
        t0 = time.time()
        try:
            level = flevel.Level(self.filepath,
                                 with_terrain=self.import_terrain,
                                 with_objects=self.import_objects)
        except (SRSCError, OSError, ValueError, struct_error) as exc:
            self.report({'ERROR'}, 'Could not read level: %s' % exc)
            return {'CANCELLED'}

        index = DatabaseIndex(self.filepath, extra_roots=level.dependencies)
        tex_dir = bpy.path.abspath(self.texture_dir) if self.texture_dir else bpy.app.tempdir
        if tex_dir and not os.path.isdir(tex_dir):
            try:
                os.makedirs(tex_dir)
            except OSError:
                tex_dir = bpy.app.tempdir
        materials = MaterialCache(index if self.import_textures else None,
                                  tex_dir, self.pack_textures, self.use_alpha)

        root = bpy.data.collections.new(_safe(level.name or 'Level'))
        context.scene.collection.children.link(root)

        terrain_count = 0
        if self.import_terrain and level.layers:
            groups = {}
            for layer in level.layers:
                groups.setdefault(layer.type_name, []).append(layer)
            sub = bpy.data.collections.new('%s Terrain' % _safe(level.name))
            root.children.link(sub)
            for kind, layers in sorted(groups.items()):
                obj = _terrain_object(level, layers, materials, index, self.scale,
                                      '%s_%s' % (_safe(level.name), kind))
                if obj is not None:
                    sub.objects.link(obj)
                    terrain_count += 1

        placed = skipped = missing = 0
        if self.import_objects and level.objects:
            sub = bpy.data.collections.new('%s Objects' % _safe(level.name))
            root.children.link(sub)
            mesh_cache = {}
            for lo in level.objects:
                if self.skip_invisible and not lo.visible:
                    skipped += 1
                    continue
                cls = index.load_class(lo.class_id, lo.class_db)
                if cls is None or not cls.model:
                    skipped += 1
                    continue
                if self.skip_markers and cls.name.lower() in MARKER_MODELS:
                    skipped += 1
                    continue
                key = cls.model
                if key not in mesh_cache:
                    mdl = index.load_model(*key)
                    if mdl is not None and self.skip_collision and \
                            _is_collision_only(mdl, index):
                        mesh_cache[key] = 'collision'
                    else:
                        mesh_cache[key] = self._prototype(mdl, materials)
                if mesh_cache[key] == 'collision':
                    skipped += 1
                    continue
                proto = mesh_cache[key]
                if proto is None:
                    missing += 1
                    continue
                name, mesh = proto
                obj = bpy.data.objects.new(name, mesh if self.link_duplicates
                                           else mesh.copy())
                obj.matrix_world = GAME_TO_BLENDER @ _object_transform(lo, self.scale)
                obj['fotr_object_id'] = lo.id
                obj['fotr_class'] = cls.name
                sub.objects.link(obj)
                placed += 1

        msg = 'Imported %s: %d terrain mesh%s, %d objects' % (
            level.name or os.path.basename(self.filepath), terrain_count,
            '' if terrain_count == 1 else 'es', placed)
        if missing:
            msg += ', %d models unresolved' % missing
        if skipped:
            msg += ', %d markers/hidden skipped' % skipped
        msg += ' in %.1fs' % (time.time() - t0)
        self.report({'INFO'}, msg)
        return {'FINISHED'}

    def _prototype(self, mdl, materials):
        """One mesh per unique model; every placement of it is a linked copy."""
        if mdl is None or not mdl.has_mesh:
            return None
        if self.skip_markers and mdl.name.lower() in MARKER_MODELS:
            return None
        obj = _build_mesh(mdl, self.lod, materials, self.scale)
        if obj is None:
            return None
        mesh = obj.data
        bpy.data.objects.remove(obj, do_unlink=True)
        return (_safe(mdl.name), mesh)


# ---------------------------------------------------------------------------
# export
# ---------------------------------------------------------------------------

def _image_rgba(img):
    """An image's pixels as top-down RGBA8, the way the game stores them."""
    w, h = img.size
    if not w or not h:
        return None, 0, 0
    buf = [0.0] * (w * h * 4)
    img.pixels.foreach_get(buf)
    out = bytearray(w * h * 4)
    for y in range(h):                       # Blender's origin is bottom-left
        src = (h - 1 - y) * w * 4
        dst = y * w * 4
        for i in range(w * 4):
            v = buf[src + i]
            out[dst + i] = 0 if v <= 0.0 else (255 if v >= 1.0 else int(v * 255.0 + 0.5))
    return bytes(out), w, h


def _resample(rgba, w, h, tw, th):
    """Nearest-neighbour resize of top-down RGBA8.

    Good enough because the destination is always small - 256x256 is the
    largest texture the game uses - and because the 8-bit palette quantisation
    that follows costs far more quality than the filter choice does.
    """
    if (w, h) == (tw, th):
        return rgba
    out = bytearray(tw * th * 4)
    for y in range(th):
        sy = min(h - 1, (y * h) // th)
        row = sy * w * 4
        base = y * tw * 4
        for x in range(tw):
            sx = min(w - 1, (x * w) // tw)
            out[base + x * 4:base + x * 4 + 4] = rgba[row + sx * 4:row + sx * 4 + 4]
    return bytes(out)


_texture_enum_cache = {'path': None, 'all': None, 'key': None, 'items': None}


def _texture_items(self, context):
    """Every texture in the chosen .tdu, narrowed by the Find box.

    An archive holds up to 172 textures and the names are not memorable, so
    scrolling for one is the worst part of this dialog. Same treatment as the
    model list: parse once, filter cheaply.
    """
    path = getattr(self, 'filepath', '')
    if not path or not os.path.isfile(path):
        return [('NONE', '(select a .tdu file)', '')]
    if _texture_enum_cache['path'] != path:
        try:
            db = ftexture.TextureDatabase(path)
            items = []
            for tid in db.ids:
                tex = db.load(tid)
                if tex is None:
                    continue
                items.append(('%d' % tid, '%s  (%dx%d)' % (tex.name, tex.width, tex.height),
                              'texture id %d' % tid))
        except (SRSCError, OSError, ValueError) as exc:
            items = [('NONE', '(%s)' % exc, '')]
        _texture_enum_cache['path'] = path
        _texture_enum_cache['all'] = items or [('NONE', '(no textures)', '')]
        _texture_enum_cache['key'] = None

    needle = (getattr(self, 'tex_filter', '') or '').strip().lower()
    if _texture_enum_cache['key'] != needle:
        everything = _texture_enum_cache['all']
        hits = [i for i in everything if needle in i[1].lower()] if needle else everything
        _texture_enum_cache['key'] = needle
        _texture_enum_cache['items'] = hits or [('NONE', '(no name contains "%s")' % needle, '')]
    return _texture_enum_cache['items']


_tdu_model_cache = {'path': None, 'all': None, 'key': None, 'items': None}


def _sibling_mdu(tdu_path):
    mdu = os.path.splitext(tdu_path or '')[0] + '.mdu'
    return mdu if os.path.isfile(mdu) else None


def _tdu_model_items(self, context):
    """Models in the .mdu beside the chosen .tdu, narrowed by the Find box."""
    mdu = _sibling_mdu(getattr(self, 'filepath', ''))
    if mdu is None:
        return [('NONE', '(no .mdu beside this .tdu)', '')]
    if _tdu_model_cache['path'] != mdu:
        try:
            db = fmodel.ModelDatabase(mdu)
            listing = [('%d' % mid, name, 'model id %d' % mid) for mid, name in db.listing()]
        except (SRSCError, OSError, ValueError) as exc:
            listing = [('NONE', '(%s)' % exc, '')]
        _tdu_model_cache['path'] = mdu
        _tdu_model_cache['all'] = listing or [('NONE', '(no models)', '')]
        _tdu_model_cache['key'] = None

    needle = (getattr(self, 'model_filter', '') or '').strip().lower()
    if _tdu_model_cache['key'] != needle:
        everything = _tdu_model_cache['all']
        hits = [i for i in everything if needle in i[1].lower()] if needle else everything
        _tdu_model_cache['key'] = needle
        _tdu_model_cache['items'] = hits or [('NONE', '(no name contains "%s")' % needle, '')]
    return _tdu_model_cache['items']


def _model_texture_slots(tdu_path, model_id):
    """Every texture slot of a model, and whether it can be written here.

    Returns [(slot, texture_id, name, w, h, home)] where `home` is None when the
    texture lives in this archive and the other archive's filename when it does
    not.  A model can reference a texture in a sibling database - the scarecrow's
    wooden pole comes from World Common - and those cannot be written into this
    file.  They are listed anyway, because a slot silently missing from the list
    is indistinguishable from a slot that did not take.

    The database id is part of the match, not just the texture id.  The two
    numbering spaces overlap, so checking the id alone can point a write at an
    unrelated local texture that happens to share a number.
    """
    mdu = _sibling_mdu(tdu_path)
    if mdu is None or not model_id or model_id == 'NONE':
        return []
    try:
        m = fmodel.ModelDatabase(mdu).load(int(model_id))
        here = ftexture.TextureDatabase(tdu_path)
        res = ftexture.TextureResolver(mdu)
    except (SRSCError, OSError, ValueError, KeyError):
        return []
    mine = res.self_db_id
    out = []
    for slot, (tid, db_id) in enumerate(m.textures):
        tex = res.load(tid, db_id)
        name = tex.name if tex is not None else 'texture %d' % tid
        w, h = (tex.width, tex.height) if tex is not None else (0, 0)
        if db_id == mine and tid in here.ids:
            out.append((slot, tid, name, w, h, None))
        else:
            other = res.database_for(db_id)
            home = os.path.basename(other.path) if other is not None else 'database %d' % db_id
            out.append((slot, tid, name, w, h, home))
    return out


def _model_slot_items(self, context):
    slots = _model_texture_slots(getattr(self, 'filepath', ''), getattr(self, 'model', 'NONE'))
    if not slots:
        return [('NONE', '(choose a model first)', '')]
    here = [s for s in slots if s[5] is None]
    items = []
    if here:
        items.append(('ALL', 'Every slot in this archive (%d of %d)' % (len(here), len(slots)),
                      'Put the same image on all of them, which is what you want when '
                      'your mesh has one material'))
    for slot, tid, name, w, h, home in slots:
        if home is None:
            items.append(('%d' % tid, 'slot %d - %s  (%dx%d)' % (slot, name, w, h),
                          'texture id %d' % tid))
        else:
            items.append(('ELSEWHERE_%d' % slot,
                          'slot %d - %s  (lives in %s)' % (slot, name, home),
                          'This texture is in another archive. Open that .tdu and '
                          'write it there.'))
    return items or [('NONE', '(this model has no textures)', '')]


def _image_items(self, context):
    items = [(img.name, img.name, '%dx%d' % (img.size[0], img.size[1]))
             for img in bpy.data.images if img.size[0] and img.size[1]]
    return items or [('NONE', '(no images in this .blend)', '')]


def _material_images(mat):
    """Every image a material's node tree actually samples."""
    out = []
    if not getattr(mat, 'use_nodes', False) or mat.node_tree is None:
        return out
    for node in mat.node_tree.nodes:
        img = getattr(node, 'image', None)
        if img is not None and img not in out:
            out.append(img)
    return out


def _texture_users(mdu_path, ref):
    """Which models in the .mdu beside a .tdu reference this texture."""
    try:
        db = fmodel.ModelDatabase(mdu_path)
    except (SRSCError, OSError, ValueError):
        return []
    out = []
    for mid in db.ids:
        try:
            m = db.load(mid)
        except Exception:
            continue
        if ref in m.textures:
            out.append(m.name)
    return out


def _load_image(path):
    img = bpy.data.images.load(path, check_existing=False)
    try:
        img.colorspace_settings.name = 'Non-Color'
    except Exception:
        pass
    return img


class EXPORT_SCENE_OT_fotr_tdu(Operator, ImportHelper):
    """Write edited textures back into a Fellowship texture archive"""
    bl_idname = "export_scene.fotr_tdu"
    bl_label = "Write Into .tdu"
    bl_options = {'REGISTER'}

    filename_ext = ".tdu"
    filter_glob: StringProperty(default="*.tdu", options={'HIDDEN'})

    mode: EnumProperty(
        name="Mode",
        items=[('MATCH', "Match By Name", "Replace every texture whose name matches an "
                                          "image, which is what re-importing a folder "
                                          "you extracted and edited does"),
               ('ONE', "Replace One", "Put one image into one texture, whatever either "
                                      "of them is called"),
               ('MODEL', "Re-skin A Model", "Pick a model and work with just the "
                                            "textures it actually uses, rather than "
                                            "hunting through the whole archive")],
        default='MATCH',
        options={'SKIP_SAVE'})
    tex_filter: StringProperty(name="Find", default="", options={'SKIP_SAVE'},
                               description="Show only textures whose name contains this")
    target: EnumProperty(name="Texture", items=_texture_items,
                         description="The texture in the archive to overwrite",
                         options={'SKIP_SAVE'})
    model_filter: StringProperty(name="Find", default="", options={'SKIP_SAVE'},
                                 description="Show only models whose name contains this")
    model: EnumProperty(name="Model", items=_tdu_model_items,
                        description="Which model's textures to work with",
                        options={'SKIP_SAVE'})
    slot: EnumProperty(name="Slot", items=_model_slot_items,
                       description="Which of that model's textures to overwrite",
                       options={'SKIP_SAVE'})
    image: EnumProperty(name="Image", items=_image_items,
                        description="The image to put there",
                        options={'SKIP_SAVE'})
    fit: BoolProperty(name="Fit To Original Size", default=True,
                      description="Resample the image to the size the texture already "
                                  "is. Leaves every record the same length, so the write "
                                  "goes in place and the rest of the archive is untouched "
                                  "- and 256x256 is the largest the game itself uses")

    source: EnumProperty(
        name="Take Images From",
        items=[('BLEND', "This .blend", "Images in this file, including anything you "
                                        "painted or edited in Blender"),
               ('FOLDER', "Folder", "Image files on disk, named after the texture")],
        default='BLEND')
    source_dir: StringProperty(name="Folder", subtype='DIR_PATH', default="")
    limit: EnumProperty(
        name="Write",
        items=[('ALL', "Everything That Matches", "Every texture whose name matches an "
                                                  "image. Careful: an image left over "
                                                  "from an earlier import will overwrite "
                                                  "whatever is in the archive now"),
               ('SELECTED', "Only What The Selection Uses", "Only textures used by the "
                                                            "materials on the selected "
                                                            "objects"),
               ('FILTER', "Only Names Containing", "Only textures whose name contains "
                                                   "the text below")],
        default='ALL')
    name_filter: StringProperty(name="Containing", default="",
                                description="Part of a texture name, e.g. pumpkin")
    make_backup: BoolProperty(name="Keep a .bak", default=True,
                              description="Copy the archive to <name>.tdu.bak before "
                                          "the first write, so the original survives")
    verify: BoolProperty(name="Verify After Writing", default=True,
                         description="Read the archive back and compare every replaced "
                                     "texture against the image it came from")
    dry_run: BoolProperty(name="Dry Run", default=False,
                          description="Report what would be replaced without writing")

    def draw(self, context):
        layout = self.layout
        layout.use_property_split = True
        layout.use_property_decorate = False
        layout.prop(self, 'mode')
        if self.mode == 'ONE':
            layout.prop(self, 'tex_filter', icon='VIEWZOOM')
            layout.prop(self, 'target')
            layout.prop(self, 'image')
            layout.prop(self, 'fit')
        elif self.mode == 'MODEL':
            layout.prop(self, 'model_filter', icon='VIEWZOOM')
            layout.prop(self, 'model')
            layout.prop(self, 'slot')
            layout.prop(self, 'image')
            layout.prop(self, 'fit')
        else:
            layout.prop(self, 'source')
            col = layout.column()
            col.enabled = self.source == 'FOLDER'
            col.prop(self, 'source_dir')
            layout.prop(self, 'limit')
            row = layout.row()
            row.enabled = self.limit == 'FILTER'
            row.prop(self, 'name_filter')
        layout.prop(self, 'make_backup')
        layout.prop(self, 'verify')
        layout.prop(self, 'dry_run')

    def execute(self, context):
        t0 = time.time()
        try:
            db = ftexture.TextureDatabase(self.filepath)
        except (SRSCError, OSError, ValueError) as exc:
            self.report({'ERROR'}, str(exc))
            return {'CANCELLED'}

        index_of = {id(r): i for i, r in enumerate(db.srsc.records)}
        by_name = {}
        for tid in db.ids:
            by_name.setdefault(_safe(db.name_of(tid)).lower(), tid)

        sources, temporary = self._gather(by_name, db)
        if not sources:
            if self.mode == 'ONE':
                self.report({'ERROR'}, "Pick a texture to overwrite and an image to put "
                                       "there. The image has to be loaded in this .blend.")
            elif self.mode == 'MODEL':
                away = [(n, home) for _s, _t, n, _w, _h, home
                        in _model_texture_slots(self.filepath, self.model)
                        if home is not None]
                if away and self.image != 'NONE':
                    self.report({'ERROR'}, "Nothing to write here: %s"
                                % '; '.join('%s is in %s' % (n, h) for n, h in away[:4])
                                + ". Open that archive and write it there.")
                else:
                    self.report({'ERROR'}, "Pick a model, a slot and an image.")
            else:
                names = sorted(by_name)[:6]
                self.report({'ERROR'}, "Nothing matched. In Match By Name mode an image "
                                       "must be named after the texture it replaces, e.g. "
                                       "%s - or switch to Replace One." % ', '.join(names))
            return {'CANCELLED'}

        replacements, written, skipped, resized, unchanged = {}, [], [], 0, 0
        for tid, (rgba, w, h, label) in sources.items():
            rec = db.pixels[tid]
            body = db.srsc.body(rec)

            # Leave textures that were not actually edited alone. The usual
            # workflow dumps a whole archive to PNG and changes one file; without
            # this every record would be re-encoded and the archive would differ
            # almost everywhere, for no gain.
            current = db.load(tid)
            if current is not None and current.width == w and current.height == h \
                    and current.rgba == rgba:
                unchanged += 1
                continue

            try:
                new = fwrite.encode_texture(body, rgba, w, h)
            except fwrite.WriteError as exc:
                skipped.append('%s (%s)' % (label, exc))
                continue
            if len(new) != len(body):
                resized += 1
            replacements[index_of[id(rec)]] = new
            written.append((tid, label, rgba, w, h))

        for img in temporary:
            bpy.data.images.remove(img)

        if not replacements:
            self.report({'INFO'}, "Nothing to do: all %d matching image%s already "
                                  "match the archive" % (unchanged,
                                                         '' if unchanged == 1 else 's'))
            return {'FINISHED'}

        if self.dry_run:
            names = [db.name_of(tid) for tid, _l, _r, _w, _h in written]
            self.report({'INFO'}, "Dry run: %d texture%s would be replaced (%s), %d "
                                  "unchanged%s"
                        % (len(written), '' if len(written) == 1 else 's',
                           ', '.join(names[:8]) + (', ...' if len(names) > 8 else ''),
                           unchanged,
                           ', %d change size' % resized if resized else ''))
            return {'FINISHED'}

        if self.make_backup:
            backup = self.filepath + '.bak'
            if not os.path.exists(backup):
                try:
                    with open(self.filepath, 'rb') as src, open(backup, 'wb') as dst:
                        dst.write(src.read())
                except OSError as exc:
                    self.report({'ERROR'}, "Could not write a backup: %s" % exc)
                    return {'CANCELLED'}

        try:
            # An edit at the original resolution keeps every record the same
            # size, so it can go in place and leave the rest of the file
            # bit-identical. Only a resize needs the archive rebuilt.
            in_place = fwrite.patch_records(self.filepath, replacements)
            if not in_place:
                fwrite.rebuild_archive(self.filepath, replacements)
        except (OSError, ValueError) as exc:
            self.report({'ERROR'}, "Write failed: %s" % exc)
            return {'CANCELLED'}

        # Name them. A write that silently reverts an earlier edit is the easiest
        # mistake to make here - an image left over from a previous import still
        # matches by name - and seeing the list is what catches it.
        names = [db.name_of(tid) for tid, _l, _r, _w, _h in written]
        listed = ': ' + ', '.join(names[:8]) + (', ...' if len(names) > 8 else '')
        if self.mode == 'MODEL' and self.slot == 'ALL':
            away = [(n, home) for _s, _t, n, _w, _h, home
                    in _model_texture_slots(self.filepath, self.model) if home is not None]
            if away:
                listed += ' (%s not here - %s)' % (
                    ', '.join(n for n, _h in away),
                    ', '.join(sorted({h for _n, h in away})))
        message = "Wrote %d texture%s %s%s%s" % (
            len(written), '' if len(written) == 1 else 's',
            'in place' if in_place else '(archive rebuilt, %d resized)' % resized,
            listed, ', %d unchanged' % unchanged if unchanged else '')

        # A texture record is shared by whichever models reference it, and there
        # is nothing in the .tdu that says so. Repainting one to re-skin a prop
        # silently re-skins its neighbours too, which is worth hearing about at
        # the moment it happens rather than in game.
        mdu = os.path.splitext(self.filepath)[0] + '.mdu'
        if len(written) <= 4 and os.path.isfile(mdu):
            try:
                db_id = ftexture.TextureResolver(mdu).self_db_id
            except Exception:
                db_id = None
            shared = []
            if db_id is not None:
                for tid, label, _rgba, _w, _h in written:
                    users = _texture_users(mdu, (tid, db_id))
                    if len(users) > 1:
                        shared.append('%s is also on %s'
                                      % (db.name_of(tid), ', '.join(users[1:5])))
            if shared:
                message += '; note: ' + '; '.join(shared)

        if self.verify:
            worst, worst_name = 0, ''
            check = ftexture.TextureDatabase(self.filepath)
            for tid, label, rgba, w, h in written:
                got = check.load(tid)
                if got is None or got.width != w or got.height != h:
                    worst, worst_name = 255, label
                    break
                err = max((abs(a - b) for a, b in zip(rgba, got.rgba)), default=0)
                if err > worst:
                    worst, worst_name = err, label
            message += '; verified, worst channel error %d/255%s' % (
                worst, ' on %s' % worst_name if worst else '')
            if worst > 24:
                self.report({'WARNING'}, message + ' - check that texture')
                return {'FINISHED'}

        if skipped:
            message += '; skipped %d (%s)' % (len(skipped), skipped[0])
        message += ' in %.1fs' % (time.time() - t0)
        self.report({'INFO'}, message)
        return {'FINISHED'}

    def _gather(self, by_name, db=None):
        """Choose what goes where. Returns {texture_id: (rgba, w, h, label)}."""
        found, temporary = {}, []
        if self.mode in ('ONE', 'MODEL'):
            if self.image == 'NONE':
                return {}, []
            img = bpy.data.images.get(self.image)
            if img is None or not img.size[0]:
                return {}, []
            rgba, w, h = _image_rgba(img)
            if not rgba:
                return {}, []

            if self.mode == 'ONE':
                if self.target == 'NONE':
                    return {}, []
                targets = [int(self.target)]
            else:
                slots = _model_texture_slots(self.filepath, self.model)
                if not slots:
                    return {}, []
                if self.slot == 'ALL':
                    targets = [tid for _s, tid, _n, _w, _h, home in slots if home is None]
                elif self.slot.startswith('ELSEWHERE_') or self.slot == 'NONE':
                    return {}, []
                else:
                    targets = [int(self.slot)]

            # Each destination gets the image resampled to its own size, so one
            # picture can cover slots of different resolutions in a single write.
            for tid in targets:
                one, ow, oh, label = rgba, w, h, img.name
                if self.fit and db is not None:
                    current = db.load(tid)
                    if current is not None and (current.width, current.height) != (w, h):
                        one = _resample(rgba, w, h, current.width, current.height)
                        ow, oh = current.width, current.height
                        label = '%s scaled %dx%d to %dx%d' % (img.name, w, h, ow, oh)
                found[tid] = (one, ow, oh, label)
            return found, temporary

        # Narrowing happens on the texture side rather than the image side, so it
        # behaves the same whether the images came from this .blend or a folder.
        allowed = None
        if self.limit == 'SELECTED':
            allowed = set()
            for obj in (bpy.context.selected_objects or
                        ([bpy.context.active_object] if bpy.context.active_object else [])):
                for slot in getattr(obj, 'material_slots', ()):
                    mat = slot.material
                    if mat is None:
                        continue
                    for img in _material_images(mat):
                        stem = os.path.splitext(img.name)[0]
                        tid = by_name.get(_safe(stem).lower())
                        if tid is not None:
                            allowed.add(tid)
        elif self.limit == 'FILTER':
            needle = (self.name_filter or '').strip().lower()
            allowed = {tid for name, tid in by_name.items() if needle and needle in name}
        if allowed is not None:
            by_name = {n: t for n, t in by_name.items() if t in allowed}

        if self.source == 'FOLDER':
            folder = bpy.path.abspath(self.source_dir) if self.source_dir else \
                os.path.dirname(self.filepath)
            try:
                entries = sorted(os.listdir(folder))
            except OSError:
                return {}, []
            for name in entries:
                stem, ext = os.path.splitext(name)
                if ext.lower() not in ('.png', '.tga', '.bmp', '.jpg', '.jpeg', '.tif', '.tiff'):
                    continue
                tid = by_name.get(_safe(stem).lower())
                if tid is None or tid in found:
                    continue
                try:
                    img = _load_image(os.path.join(folder, name))
                except RuntimeError:
                    continue
                temporary.append(img)
                rgba, w, h = _image_rgba(img)
                if rgba:
                    found[tid] = (rgba, w, h, name)
        else:
            for img in bpy.data.images:
                stem = os.path.splitext(img.name)[0]
                tid = by_name.get(_safe(stem).lower())
                # Deliberately not img.has_data: a packed image reports False
                # until something touches it, so testing it here silently skips
                # every texture that came in with an import. Reading the size is
                # what loads the buffer, and _image_rgba already refuses an
                # image that has no pixels.
                if tid is None or tid in found or not img.size[0]:
                    continue
                rgba, w, h = _image_rgba(img)
                if rgba:
                    found[tid] = (rgba, w, h, img.name)
        return found, temporary


_slots_cache = {'key': None, 'value': None}


def _model_slots_of_mdu(mdu_path, model_id):
    """[(slot, (tid, db_id), name, home)] for a model, `home` naming the archive
    a texture lives in when it is not the one beside this .mdu.

    Cached, because this is called from an enum callback and from draw(), which
    Blender runs on every redraw of the file browser. Opening the archive and
    walking the .db manifests each time makes the dialog crawl on a big level.
    """
    if not mdu_path or not os.path.isfile(mdu_path) or not model_id or model_id == 'NONE':
        return []
    key = (mdu_path, str(model_id))
    if _slots_cache['key'] == key:
        return _slots_cache['value']
    _slots_cache['key'] = key
    _slots_cache['value'] = []
    try:
        m = fmodel.ModelDatabase(mdu_path).load(int(model_id))
        res = ftexture.TextureResolver(mdu_path)
    except (SRSCError, OSError, ValueError, KeyError):
        return []
    out = []
    for slot, ref in enumerate(m.textures):
        tex = res.load(*ref)
        name = tex.name if tex is not None else 'texture %d' % ref[0]
        if ref[1] == res.self_db_id:
            home = None
        else:
            other = res.database_for(ref[1])
            home = os.path.basename(other.path) if other is not None else 'database %d' % ref[1]
        out.append((slot, tuple(ref), name, home))
    _slots_cache['value'] = out
    return out


_export_tex_cache = {'path': None, 'all': None}


def _export_texture_items(self, context):
    """What a material can be pointed at: this model's own slots first, then
    every texture in the .tdu beside the archive, narrowed by Find.

    The model's own slots come first because they are the answer most of the
    time - you are re-skinning something, and you want to know what it used to
    use and which of those you can repaint.
    """
    path = getattr(self, 'filepath', '')
    items = []
    for slot, ref, name, home in _model_slots_of_mdu(path, getattr(self, 'model', 'NONE')):
        items.append(('%d:%d' % ref,
                      'slot %d - %s%s' % (slot, name, '  (in %s)' % home if home else ''),
                      'what this model used for slot %d' % slot))
    tdu = os.path.splitext(path or '')[0] + '.tdu'
    if os.path.isfile(tdu):
        if _export_tex_cache['path'] != tdu:
            try:
                db = ftexture.TextureDatabase(tdu)
                res = ftexture.TextureResolver(path)
                mine = res.self_db_id if res.self_db_id is not None else 0
                every = []
                for tid in db.ids:
                    tex = db.load(tid)
                    if tex is not None:
                        every.append(('%d:%d' % (tid, mine),
                                      '%s  (%dx%d)' % (tex.name, tex.width, tex.height),
                                      'texture id %d' % tid))
            except (SRSCError, OSError, ValueError):
                every = []
            _export_tex_cache['path'] = tdu
            _export_tex_cache['all'] = every
        needle = (getattr(self, 'slot_filter', '') or '').strip().lower()
        seen = {i[0] for i in items}
        for entry in _export_tex_cache['all']:
            if entry[0] in seen:
                continue
            if not needle or needle in entry[1].lower():
                items.append(entry)
    return items or [('NONE', '(choose a model first)', '')]


def _export_model_items(self, context):
    """The import list with an explicit "nothing chosen" entry in front.

    An enum with no default lands on its first item, and for an export that
    silently means overwriting whichever model happens to sort first. Making
    the empty choice the default turns a destructive accident into a prompt.
    """
    items = list(_model_items(self, context))
    if items and items[0][0] == 'NONE':
        return items
    return [('NONE', '(choose a model to overwrite)', '')] + items


class EXPORT_SCENE_OT_fotr_mdu(Operator, ImportHelper):
    """Write the selected mesh back into a Fellowship model archive"""
    bl_idname = "export_scene.fotr_mdu"
    bl_label = "Write Mesh Into .mdu"
    bl_options = {'REGISTER'}

    filename_ext = ".mdu"
    filter_glob: StringProperty(default="*.mdu", options={'HIDDEN'})

    # Filled in from the object when it carries import provenance, and left for
    # the user otherwise - a mesh imported before this add-on started stamping
    # objects, or one appended from another file, has nothing to go on.
    name_filter: StringProperty(name="Find", default="",
                                description="Narrows the model list below",
                                options={'SKIP_SAVE'})
    model: EnumProperty(name="Overwrite Model", description="Which model in the archive "
                                                            "this mesh replaces",
                        items=_export_model_items, options={'SKIP_SAVE'})
    lod: IntProperty(name="LOD", default=0, min=0, max=4,
                     description="Which detail level this mesh is. Only used when the "
                                 "object does not remember where it came from")
    scale: FloatProperty(name="Scale", default=1.0, min=0.0001, soft_max=100.0,
                         description="The import scale this mesh was built at")

    mode: EnumProperty(
        name="Write",
        items=[('AUTO', "Automatic", "Rewrite only what changed: vertices alone if the "
                                     "topology still matches, otherwise the whole mesh"),
               ('VERTS', "Moved Vertices Only", "Only positions and normals. The face "
                                                "list, UVs and rig are left alone, so "
                                                "this is safe on rigged characters"),
               ('FULL', "Whole Mesh", "Vertices, faces, UVs and bounds. Not available "
                                      "for rigged or multi-LOD models")],
        default='AUTO')
    textures: EnumProperty(
        name="Texture Slots",
        items=[('KEEP', "Leave As They Are", "Keep the model's texture list. Your "
                                             "material slots map onto it in order, so "
                                             "a one-material mesh lands on slot 0 "
                                             "whatever slot 0 happens to be"),
               ('ONE', "Point Everything At One", "Give the model a single texture "
                                                  "slot, chosen below. Right when your "
                                                  "mesh has one material"),
               ('PER_MATERIAL', "One Per Material", "Give the model one slot per "
                                                    "material, matching each material's "
                                                    "image to a texture by name and "
                                                    "keeping the model's old slot where "
                                                    "there is no match")],
        default='KEEP')
    slot_filter: StringProperty(name="Find", default="", options={'SKIP_SAVE'},
                                description="Narrows the texture list below")
    slot_texture: EnumProperty(name="Texture", items=_export_texture_items,
                               description="Which texture every face should use",
                               options={'SKIP_SAVE'})
    make_backup: BoolProperty(name="Keep a .bak", default=True,
                              description="Copy the archive to <name>.mdu.bak before the "
                                          "first write, so the original survives")
    verify: BoolProperty(name="Verify After Writing", default=True,
                         description="Read the archive back and compare the geometry "
                                     "against what was exported")
    dry_run: BoolProperty(name="Dry Run", default=False,
                          description="Report what would be written without writing")

    @classmethod
    def poll(cls, context):
        return _export_mesh(context) is not None

    def invoke(self, context, event):
        obj = _export_mesh(context)
        remembered = obj.get('fotr_archive') if obj else None
        if remembered and os.path.exists(remembered):
            self.filepath = remembered
            self.lod = int(obj.get('fotr_lod', 0))
            self.scale = float(obj.get('fotr_scale', 1.0)) or 1.0
            self.name_filter = obj.get('fotr_model_name', '') or ''
        return ImportHelper.invoke(self, context, event)

    def draw(self, context):
        layout = self.layout
        layout.use_property_split = True
        layout.use_property_decorate = False
        obj = _export_mesh(context)
        if obj is None:
            layout.label(text="Select a mesh first", icon='ERROR')
            return

        known = 'fotr_model_id' in obj
        box = layout.box()
        box.label(text=obj.name, icon='MESH_DATA')
        if known:
            box.label(text="Imported as \"%s\"" % obj.get('fotr_model_name', '?'),
                      icon='CHECKMARK')
        else:
            box.label(text="No import history - pick the model to overwrite",
                      icon='INFO')
        box.prop(self, 'name_filter', icon='VIEWZOOM')
        box.prop(self, 'model')
        sub = box.column()
        sub.enabled = not known
        sub.prop(self, 'lod')
        sub.prop(self, 'scale')

        layout.prop(self, 'mode')

        if self.mode != 'VERTS':
            slots = _model_slots_of_mdu(self.filepath, self.model)
            if slots:
                info = layout.box()
                info.label(text="This model's texture slots", icon='TEXTURE')
                for slot, _ref, name, home in slots:
                    info.label(text="slot %d - %s%s"
                                    % (slot, name,
                                       '   (in %s)' % home if home else ''),
                               icon='DOT' if home is None else 'ERROR')
                mats = len(obj.data.materials) or 1
                if mats == 1 and len(slots) > 1 and self.textures == 'KEEP':
                    info.label(text="One material - every face lands on slot 0",
                               icon='INFO')
            layout.prop(self, 'textures')
            if self.textures == 'ONE':
                layout.prop(self, 'slot_filter', icon='VIEWZOOM')
                layout.prop(self, 'slot_texture')

        layout.prop(self, 'make_backup')
        layout.prop(self, 'verify')
        layout.prop(self, 'dry_run')

    def execute(self, context):
        t0 = time.time()
        obj = _export_mesh(context)
        if obj is None:
            self.report({'ERROR'}, "Select the mesh you want to write out")
            return {'CANCELLED'}

        path = self.filepath or obj.get('fotr_archive')
        if not path or not os.path.exists(path):
            self.report({'ERROR'}, "Pick the .mdu to write into")
            return {'CANCELLED'}

        try:
            db = fmodel.ModelDatabase(path)
        except (SRSCError, OSError, ValueError) as exc:
            self.report({'ERROR'}, "Could not open %s: %s" % (os.path.basename(path), exc))
            return {'CANCELLED'}

        mid, lod, scale = self._target(obj, db, path)
        if mid is None:
            self.report({'ERROR'}, "Choose which of the %d models in %s this mesh "
                                   "replaces" % (len(db.ids), os.path.basename(path)))
            return {'CANCELLED'}
        try:
            mdl = db.load(mid)
        except (SRSCError, ValueError, KeyError, struct_error) as exc:
            self.report({'ERROR'}, "Could not read model %d: %s" % (mid, exc))
            return {'CANCELLED'}

        index_of = {}
        for i, rec in enumerate(db.srsc.records):
            index_of[(rec.type, rec.id)] = i
        src_verts, src_polys = mdl.lod_mesh(lod)
        rigged = mdl.skeleton is not None and bool(mdl.skeleton.joints)
        multi_lod = len(mdl.lods) > 1

        positions, normals, faces = self._read_mesh(obj, scale)
        if not positions or not faces:
            self.report({'ERROR'}, "The mesh has no geometry to write")
            return {'CANCELLED'}

        mode = self.mode
        if mode == 'AUTO':
            # Faces only decide it when the face list can actually be written.
            # About thirty models ship with duplicate faces that Blender cannot
            # hold, so they come in one face short and would otherwise send
            # every rigged character down the whole-mesh path just to be refused.
            if len(positions) != len(src_verts):
                mode = 'FULL'
            elif len(faces) != len(src_polys) and not (rigged or multi_lod):
                mode = 'FULL'
            else:
                mode = 'VERTS'
        if mode == 'VERTS' and len(positions) != len(src_verts):
            self.report({'ERROR'}, "Vertex-only export needs the vertex count to match: "
                                   "the mesh has %d, the model has %d. Add or remove "
                                   "vertices and you need the whole-mesh mode."
                        % (len(positions), len(src_verts)))
            return {'CANCELLED'}
        if mode == 'FULL' and (rigged or multi_lod):
            self.report({'ERROR'}, "%s is %s, so its face list cannot be replaced - the "
                                   "vertex weights and LOD ranges are indexed against it. "
                                   "Move vertices instead, or export it as a new model."
                        % (mdl.name, 'rigged' if rigged else 'multi-LOD'))
            return {'CANCELLED'}

        replacements, drop, add, notes, warnings = {}, [], [], [], []

        vert_key = (0x0203, mdl.id)
        if vert_key not in index_of:
            self.report({'ERROR'}, "The model has no vertex record to replace")
            return {'CANCELLED'}

        if mode == 'VERTS':
            # Positions sit in the whole-model array, not the LOD's slice, so a
            # LOD 1 edit has to be written back at its own offset with the other
            # levels left exactly as they were.
            base = mdl.lods[lod].first_vertex if mdl.lods else 0
            all_pos = list(mdl.vertices)
            body = db.srsc.body(db.srsc.records[index_of[vert_key]])
            words = struct.unpack_from('<%dI' % len(all_pos), body,
                                       2 + len(all_pos) * 12)
            all_nrm = [fmesh.decode_normal(w) for w in words]
            for i, p in enumerate(positions):
                all_pos[base + i] = p
                if all_nrm[base + i] is not None:
                    all_nrm[base + i] = normals[i]
            replacements[index_of[vert_key]] = fmesh.encode_vertices(all_pos, all_nrm)
            moved = sum(1 for i, p in enumerate(positions)
                        if max(abs(p[k] - src_verts[i][k]) for k in range(3)) > 1e-6)
            notes.append("%d of %d vertices moved" % (moved, len(positions)))
            if len(faces) != len(src_polys):
                notes.append("faces left alone (%d here, %d in the model)"
                             % (len(faces), len(src_polys)))
        else:
            replacements[index_of[vert_key]] = fmesh.encode_vertices(positions, normals)

            poly_key = (0x0204, mdl.id)
            if poly_key not in index_of:
                self.report({'ERROR'}, "The model has no polygon record to replace")
                return {'CANCELLED'}
            # An object the importer stamped knows which game texture each of its
            # material slots came from; one that came from anywhere else can only
            # assume they line up.
            slots = list(obj.get('fotr_texture_slots', [])) or \
                list(range(max(1, len(obj.data.materials))))

            # Optionally rewrite the model's texture list. Without this a
            # one-material mesh always lands on slot 0, and if slot 0 happens to
            # point into another archive - the scarecrow's slot 0 is a World
            # Common wood texture - nothing you paint into the .tdu beside this
            # .mdu can ever appear on it.
            head_key = (0x0200, mdl.id)
            new_refs = None
            if self.textures != 'KEEP' and head_key in index_of:
                head = db.srsc.body(db.srsc.records[index_of[head_key]])
                old_refs = list(mdl.textures)
                if self.textures == 'ONE':
                    if self.slot_texture == 'NONE':
                        self.report({'ERROR'}, "Pick the texture every face should use")
                        return {'CANCELLED'}
                    tid, db_id = (int(x) for x in self.slot_texture.split(':'))
                    new_refs = [(tid, db_id)]
                    slots = [0] * max(1, len(obj.data.materials))
                else:
                    by_name = {}
                    tdu = os.path.splitext(path)[0] + '.tdu'
                    mine = 0
                    if os.path.isfile(tdu):
                        try:
                            tdb = ftexture.TextureDatabase(tdu)
                            res = ftexture.TextureResolver(path)
                            mine = res.self_db_id or 0
                            for t in tdb.ids:
                                by_name.setdefault(_safe(tdb.name_of(t)).lower(), t)
                        except (SRSCError, OSError, ValueError):
                            pass
                    new_refs, slots, guessed = [], [], []
                    for i, mat in enumerate(obj.data.materials or [None]):
                        ref = None
                        for img in (_material_images(mat) if mat else ()):
                            hit = by_name.get(_safe(os.path.splitext(img.name)[0]).lower())
                            if hit is not None:
                                ref = (hit, mine)
                                break
                        if ref is None and mat is not None:
                            hit = by_name.get(_safe(mat.name).lower())
                            if hit is not None:
                                ref = (hit, mine)
                        if ref is None:
                            ref = old_refs[i] if i < len(old_refs) else \
                                (old_refs[0] if old_refs else (0, mine))
                            guessed.append(mat.name if mat else 'material %d' % i)
                        new_refs.append(ref)
                        slots.append(i)
                    if guessed:
                        warnings.append("kept the old texture for %s - no texture in the "
                                        ".tdu is named after it" % ', '.join(guessed[:3]))
                if new_refs != old_refs:
                    replacements[index_of[head_key]] = fmesh.set_texture_list(head, new_refs)
                    notes.append("texture list %d -> %d entries"
                                 % (len(old_refs), len(new_refs)))
                # Point at something in another archive and the same trap is set
                # again, so say so rather than let it be discovered in game.
                try:
                    res = ftexture.TextureResolver(path)
                    away = [r for r in new_refs if r[1] != res.self_db_id]
                except Exception:
                    away = []
                if away:
                    warnings.append("slot texture %s lives in another database - "
                                    "repaint it there, not in the .tdu beside this file"
                                    % ', '.join('%d:%d' % r for r in away[:3]))

            flag_of = {}
            for p in src_polys:
                flag_of[frozenset(p.indices)] = p.flags
            default_flags = _modal([p.flags for p in src_polys]) if src_polys else 0
            polygons = []
            for idx, uvs, material_index in faces:
                texture = slots[material_index] if material_index < len(slots) else 0
                polygons.append((flag_of.get(frozenset(idx), default_flags),
                                 texture, idx, uvs))
            # Group the faces by texture: 842 of the 843 LODs in the retail data
            # have exactly one run per texture, and a mesh that switches back to
            # a texture it already used renders with faces missing. Blender has
            # no reason to order faces that way, so it is done here. The sort is
            # stable, so within a texture the original order survives.
            before = _texture_runs(polygons)
            polygons.sort(key=lambda p: p[1])
            after = _texture_runs(polygons)
            if after < before:
                notes.append("regrouped %d texture runs into %d" % (before, after))
            # A face carries a slot number, and the engine looks that up in the
            # model's texture list. More materials than the list has entries and
            # it reads past the end of that array - so this is refused rather
            # than written, because the symptom in game is a crash or garbage
            # rather than anything that points back here.
            declared = len(new_refs) if new_refs is not None else len(mdl.textures)
            over = max((p[1] for p in polygons), default=-1)
            if declared and over >= declared:
                self.report({'ERROR'},
                            "This mesh uses %d texture slots but %s declares only %d. "
                            "Set Texture Slots to \"One Per Material\" to give it one "
                            "entry per material, or to \"Point Everything At One\" if "
                            "they should all share a texture."
                            % (over + 1, mdl.name, declared))
                return {'CANCELLED'}
            if declared > fmesh.RETAIL_MAX_TEXTURES:
                warnings.append("%d textures on one model; the most the game itself "
                                "ships is %d, so this is past anything proven to work"
                                % (declared, fmesh.RETAIL_MAX_TEXTURES))
            replacements[index_of[poly_key]] = fmesh.encode_polygons(polygons,
                                                                     uncached=0)

            bounds_key = (0x0207, mdl.id)
            if bounds_key in index_of:
                body = db.srsc.body(db.srsc.records[index_of[bounds_key]])
                replacements[index_of[bounds_key]] = fmesh.encode_bounds(body, positions)

            # 0x0211 is the triangle strip list - the form the engine draws -
            # and it indexes vertices directly, so a topology edit invalidates
            # it and it has to be rebuilt, not deleted. Every model the game
            # ships has one.
            try:
                entry = fstrip.build(
                    [tuple(p[2]) for p in polygons],
                    lambda t, c: (polygons[t][3][c]
                                  if c < len(polygons[t][3]) else (0.0, 0.0)),
                    lambda t: polygons[t][1],
                    len(positions))
            except fstrip.StripError as exc:
                self.report({'ERROR'}, "Could not build the strip record: %s" % exc)
                return {'CANCELLED'}
            blob = fstrip.encode([entry])
            strip_key = (0x0211, mdl.id)
            if strip_key in index_of:
                replacements[index_of[strip_key]] = blob
            else:
                rec = db.srsc.records[index_of[poly_key]]
                add.append((0x0211, mdl.id, rec.group, blob))
                notes.append("added a strip record (the model had none)")
            notes.append("%d strip groups over %d vertex spans"
                         % (len(entry.groups), len(entry.ranges)))
            notes.append("%d vertices, %d triangles (was %d and %d)"
                         % (len(positions), len(faces), len(src_verts), len(src_polys)))
            # The heaviest thing the game ships is the Dark Rider at 3684
            # vertices and 6783 triangles. Nothing proves that is a hard
            # ceiling, but a model an order of magnitude past it is worth
            # knowing about before it goes in the game rather than after.
            if len(positions) > RETAIL_MAX_VERTS or len(faces) > RETAIL_MAX_POLYS:
                warnings.append("heavier than anything the game ships (its "
                                "biggest model is %d vertices, %d triangles)"
                                % (RETAIL_MAX_VERTS, RETAIL_MAX_POLYS))
            span = _span(positions)
            was = _span(src_verts)
            if was > 1e-6 and (span > was * 3.0 or span < was / 3.0):
                warnings.append("%.1fx the size of the model it replaces - scale "
                                "the object in Blender if that is not deliberate"
                                % (span / was))

        if self.dry_run:
            message = ("Dry run on %s: %s; %s"
                       % (mdl.name, 'vertices only' if mode == 'VERTS' else 'whole mesh',
                          '; '.join(notes)))
            self.report({'WARNING'} if warnings else {'INFO'},
                        message + ('; ' + '; '.join(warnings) if warnings else ''))
            return {'FINISHED'}

        if self.make_backup:
            backup = path + '.bak'
            if not os.path.exists(backup):
                try:
                    with open(path, 'rb') as src, open(backup, 'wb') as dst:
                        dst.write(src.read())
                except OSError as exc:
                    self.report({'ERROR'}, "Could not write a backup: %s" % exc)
                    return {'CANCELLED'}

        try:
            in_place = False
            if not drop and not add:
                in_place = fwrite.patch_records(path, replacements)
            if not in_place:
                fwrite.rebuild_archive(path, replacements, drop=drop, add=add)
        except (OSError, ValueError, struct_error) as exc:
            self.report({'ERROR'}, "Write failed: %s" % exc)
            return {'CANCELLED'}

        message = "Wrote %s into %s (%s); %s" % (
            mdl.name, os.path.basename(path),
            'in place' if in_place else 'archive rebuilt', '; '.join(notes))

        if self.verify:
            try:
                back = fmodel.ModelDatabase(path).load(int(mid))
            except Exception as exc:
                self.report({'WARNING'}, message + '; could not verify: %s' % exc)
                return {'FINISHED'}
            got, _ = back.lod_mesh(lod)
            worst = 0.0
            if len(got) != len(positions):
                self.report({'WARNING'}, message + '; verify found %d vertices, expected %d'
                            % (len(got), len(positions)))
                return {'FINISHED'}
            for a, b in zip(got, positions):
                worst = max(worst, max(abs(a[k] - b[k]) for k in range(3)))
            message += '; verified, worst vertex error %.2g' % worst

        message += ' in %.1fs' % (time.time() - t0)
        if warnings:
            self.report({'WARNING'}, message + '; ' + '; '.join(warnings))
        else:
            self.report({'INFO'}, message)
        return {'FINISHED'}

    def _target(self, obj, db, path):
        """Which model, LOD and scale this mesh is being written as.

        The object's own memory of the import wins, but only for the archive it
        was imported from: pointed at a different file it means nothing, and the
        model picked in the browser takes over.
        """
        same_archive = os.path.normcase(os.path.abspath(path)) == \
            os.path.normcase(os.path.abspath(obj.get('fotr_archive', '') or ' '))
        if same_archive and 'fotr_model_id' in obj:
            mid = int(obj['fotr_model_id'])
            if mid in db.ids:
                return (mid, int(obj.get('fotr_lod', 0)),
                        float(obj.get('fotr_scale', 1.0)) or 1.0)

        chosen = None
        try:
            chosen = self.model
        except TypeError:                   # stale value from another archive
            chosen = None
        if chosen and chosen != 'NONE' and chosen.isdigit() and int(chosen) in db.ids:
            return int(chosen), self.lod, self.scale or 1.0
        matches = db.find(self.name_filter) if self.name_filter else []
        if len(matches) != 1:
            # "Sam" also matches "sam_collider"; an exact name breaks the tie.
            needle = (self.name_filter or '').strip().lower()
            matches = [m for m in matches if db.name_of(m).lower() == needle]
        if len(matches) == 1:
            return matches[0], self.lod, self.scale or 1.0
        return None, self.lod, self.scale or 1.0

    @staticmethod
    def _read_mesh(obj, scale):
        """Pull the mesh back into game space: positions, normals and faces.

        The object's own transform is folded in, so a model that was moved or
        rotated in Blender exports where it was put. An untouched object has
        exactly the import matrix, and the two cancel to nothing.
        """
        mesh = obj.data
        to_game = GAME_TO_BLENDER.inverted() @ obj.matrix_world
        rot = to_game.to_3x3()
        inv_scale = 1.0 / scale

        positions = []
        for v in mesh.vertices:
            p = to_game @ v.co
            positions.append((p.x * inv_scale, p.y * inv_scale, p.z * inv_scale))

        normals = []
        for v in mesh.vertex_normals:
            n = rot @ Vector(v.vector)
            normals.append((n.x, n.y, n.z))

        uv_layer = mesh.uv_layers.active
        uvs = uv_layer.data if uv_layer else None

        # Triangulate. Every one of the 517135 polygons the game ships is a
        # triangle - there is not a single quad in the retail data - and a quad
        # written into a model record comes back as long twisted shards, so
        # anything modelled elsewhere has to be cut down before it goes in.
        # Blender's loop triangles carry their own loop indices, so the UVs and
        # material of each fragment follow the face it came from.
        try:
            mesh.calc_loop_triangles()
        except (AttributeError, RuntimeError):
            pass
        faces = []
        for tri in mesh.loop_triangles:
            # back to the engine's clockwise-front winding
            loops = list(tri.loops)[::-1]
            idx = [mesh.loops[l].vertex_index for l in loops]
            if uvs is None:
                pair = [(0.0, 0.0)] * 3
            else:
                pair = [(uvs[l].uv[0], 1.0 - uvs[l].uv[1]) for l in loops]
            faces.append((idx, pair, tri.material_index))
        return positions, normals, faces


def _span(points):
    """Longest bounding-box edge, for comparing a mesh against what it replaces."""
    if not points:
        return 0.0
    return max(max(p[k] for p in points) - min(p[k] for p in points) for k in range(3))


def _export_mesh(context):
    """The mesh this export is about: the active object, or the one selected.

    Selecting a character usually means clicking its armature, and clicking a
    row in the outliner does not always make that object active, so falling back
    to the selection is what makes the menu item available when the user thinks
    it should be.
    """
    obj = context.active_object
    if obj is not None and obj.type == 'MESH':
        return obj
    candidates = [o for o in getattr(context, 'selected_objects', ()) if o.type == 'MESH']
    if len(candidates) == 1:
        return candidates[0]
    if obj is not None and obj.type == 'ARMATURE':
        children = [c for c in obj.children if c.type == 'MESH']
        if len(children) == 1:
            return children[0]
    return candidates[0] if candidates else None


def _texture_runs(polygons):
    runs = 0
    last = None
    for poly in polygons:
        if poly[1] != last:
            runs += 1
            last = poly[1]
    return runs


def _modal(values):
    counts = {}
    for v in values:
        counts[v] = counts.get(v, 0) + 1
    return max(counts, key=counts.get) if counts else 0


# ---------------------------------------------------------------------------
# registration
# ---------------------------------------------------------------------------

def _menu(self, context):
    self.layout.operator(IMPORT_SCENE_OT_fotr.bl_idname,
                         text="LOTR Fellowship Model (.mdu)")
    self.layout.operator(IMPORT_SCENE_OT_fotr_level.bl_idname,
                         text="LOTR Fellowship Level (.lvl)")
    self.layout.operator(IMPORT_SCENE_OT_fotr_textures.bl_idname,
                         text="LOTR Fellowship Textures (.tdu)")


def _export_menu(self, context):
    self.layout.operator(EXPORT_SCENE_OT_fotr_mdu.bl_idname,
                         text="LOTR Fellowship Model (.mdu)")
    self.layout.operator(EXPORT_SCENE_OT_fotr_tdu.bl_idname,
                         text="LOTR Fellowship Textures (.tdu)")


classes = (IMPORT_SCENE_OT_fotr, IMPORT_SCENE_OT_fotr_level,
           IMPORT_SCENE_OT_fotr_textures, EXPORT_SCENE_OT_fotr_tdu,
           EXPORT_SCENE_OT_fotr_mdu)


def register():
    for c in classes:
        bpy.utils.register_class(c)
    bpy.types.TOPBAR_MT_file_import.append(_menu)
    bpy.types.TOPBAR_MT_file_export.append(_export_menu)


def unregister():
    bpy.types.TOPBAR_MT_file_export.remove(_export_menu)
    bpy.types.TOPBAR_MT_file_import.remove(_menu)
    for c in reversed(classes):
        bpy.utils.unregister_class(c)


if __name__ == "__main__":
    register()
