"""LVL binary format (V2) serialization and deserialization.

V2 format layout (big-endian):
+0x00  magic            char[4]   = "LVL2"
+0x04  version          uint32    = 2
+0x08  collider_count   uint32
+0x0C  face_count       uint32
+0x10  vertex_count     uint32
+0x14  entity_count     uint32
+0x18  string_count     uint32
+0x1C  skybox_str_id    uint16
+0x1E  music_str_id     uint16
+0x20  ambience_str_id  uint16
+0x22  snow_amount_q8   uint16    (Q8 fixed; 0x0080 = 0.5)
+0x24  snow_dir_x_q8    int16
+0x26  snow_dir_y_q8    int16
+0x28  snow_dir_z_q8    int16
+0x2A  reserved         uint16    = 0
+0x2C  off_strings      uint32    (byte offset to string table)
+0x30  off_colliders    uint32
+0x34  off_faces        uint32
+0x38  off_vertices     uint32
+0x3C  off_entities     uint32
+0x40  off_props_blob   uint32

Face.flags (uint16):
  bit 0 = solid
  bit 1 = visual_only
  bits 2-15 = reserved
"""

import struct
import io
from typing import List, Tuple

class Collider:
    def __init__(self, type_=0, solid=1, has_plane_origin=0,
                 bounds_min=(0, 0, 0), bounds_max=(0, 0, 0),
                 normal=(0, 0, 1), plane_origin=(0, 0, 0),
                 face_id=-1, owner_id=-1):
        self.type = type_
        self.solid = solid
        self.has_plane_origin = has_plane_origin
        self.bounds_min = bounds_min
        self.bounds_max = bounds_max
        self.normal = normal
        self.plane_origin = plane_origin
        self.face_id = face_id
        self.owner_id = owner_id

class Face:
    def __init__(self, vertex_start=0, vertex_count=0, material_id=0, normal=(0, 0, 1), flags=0):
        self.vertex_start = vertex_start
        self.vertex_count = vertex_count
        self.material_id = material_id
        self.normal = normal
        self.flags = flags

class Vertex:
    def __init__(self, pos=(0, 0, 0), uv=(0, 0)):
        self.pos = pos
        self.uv = uv

class Entity:
    def __init__(self, classname_id=0, position=(0, 0, 0), props_offset=0, props_len=0):
        self.classname_id = classname_id
        self.position = position
        self.props_offset = props_offset
        self.props_len = props_len

class LvlFile:
    def __init__(self):
        self.strings = []
        self.colliders = []
        self.faces = []
        self.vertices = []
        self.entities = []
        self.props_blob = b""
        self.skybox_str_id = 0
        self.music_str_id = 0
        self.ambience_str_id = 0
        self.snow_amount_q8 = 0
        self.snow_dir = (0, 0, 0)

    def write(self, filename):
        """Serialize to binary LVL file."""
        with open(filename, "wb") as f:
            self._write_binary(f)

    def _write_binary(self, f):
        """Write binary LVL data."""
        # Build string table
        str_table_buf = io.BytesIO()
        for s in self.strings:
            s_bytes = s.encode("utf-8")
            str_table_buf.write(struct.pack(">B", len(s_bytes)))
            str_table_buf.write(s_bytes)
        str_table_data = str_table_buf.getvalue()
        
        # Calculate offsets (header is 0x44 = 68 bytes)
        header_size = 0x44
        off_strings = header_size
        off_colliders = off_strings + len(str_table_data)
        off_faces = off_colliders + len(self.colliders) * 56
        off_vertices = off_faces + len(self.faces) * 24
        off_entities = off_vertices + len(self.vertices) * 20
        off_props_blob = off_entities + len(self.entities) * 24
        
        # Write header
        f.write(b"LVL2")
        f.write(struct.pack(">I", 2))  # version
        f.write(struct.pack(">I", len(self.colliders)))
        f.write(struct.pack(">I", len(self.faces)))
        f.write(struct.pack(">I", len(self.vertices)))
        f.write(struct.pack(">I", len(self.entities)))
        f.write(struct.pack(">I", len(self.strings)))
        f.write(struct.pack(">HHH", self.skybox_str_id, self.music_str_id, self.ambience_str_id))
        f.write(struct.pack(">H", self.snow_amount_q8))
        f.write(struct.pack(">hhh", int(self.snow_dir[0]), int(self.snow_dir[1]), int(self.snow_dir[2])))
        f.write(struct.pack(">H", 0))  # reserved
        f.write(struct.pack(">I", off_strings))
        f.write(struct.pack(">I", off_colliders))
        f.write(struct.pack(">I", off_faces))
        f.write(struct.pack(">I", off_vertices))
        f.write(struct.pack(">I", off_entities))
        f.write(struct.pack(">I", off_props_blob))
        
        # Write string table
        f.write(str_table_data)
        
        # Write colliders (28 bytes each)
        for c in self.colliders:
            f.write(struct.pack(">BBB B", c.type, c.solid, c.has_plane_origin, 0))
            f.write(struct.pack(">fff", *c.bounds_min))
            f.write(struct.pack(">fff", *c.bounds_max))
            f.write(struct.pack(">fff", *c.normal))
            f.write(struct.pack(">fff", *c.plane_origin))
            f.write(struct.pack(">hh", c.face_id, c.owner_id))
        
        # Write faces (24 bytes each)
        for face in self.faces:
            f.write(struct.pack(">IIHH", face.vertex_start, face.vertex_count,
                               face.material_id, face.flags))
            f.write(struct.pack(">fff", *face.normal))
        
        # Write vertices (20 bytes each)
        for v in self.vertices:
            f.write(struct.pack(">fff", *v.pos))
            f.write(struct.pack(">ff", *v.uv))
        
        # Write entities (20 bytes each)
        for e in self.entities:
            f.write(struct.pack(">HH", e.classname_id, 0))
            f.write(struct.pack(">fff", *e.position))
            f.write(struct.pack(">II", e.props_offset, e.props_len))
        
        # Write props blob
        f.write(self.props_blob)

    @staticmethod
    def read(filename):
        """Deserialize from binary LVL file."""
        with open(filename, "rb") as f:
            return LvlFile._read_binary(f)

    @staticmethod
    def _read_binary(f):
        """Read binary LVL data."""
        lvl = LvlFile()
        
        # Read header
        magic = f.read(4)
        if magic != b"LVL2":
            raise ValueError(f"Invalid magic: {magic}")

        version = struct.unpack(">I", f.read(4))[0]
        if version != 2:
            raise ValueError(f"Unsupported version: {version}")
        
        collider_count = struct.unpack(">I", f.read(4))[0]
        face_count = struct.unpack(">I", f.read(4))[0]
        vertex_count = struct.unpack(">I", f.read(4))[0]
        entity_count = struct.unpack(">I", f.read(4))[0]
        string_count = struct.unpack(">I", f.read(4))[0]
        
        skybox_str_id, music_str_id, ambience_str_id = struct.unpack(">HHH", f.read(6))
        snow_amount_q8 = struct.unpack(">H", f.read(2))[0]
        snow_dir_x, snow_dir_y, snow_dir_z = struct.unpack(">hhh", f.read(6))
        f.read(2)  # reserved
        
        off_strings, off_colliders, off_faces, off_vertices, off_entities, off_props_blob = \
            struct.unpack(">IIIIII", f.read(24))
        
        lvl.skybox_str_id = skybox_str_id
        lvl.music_str_id = music_str_id
        lvl.ambience_str_id = ambience_str_id
        lvl.snow_amount_q8 = snow_amount_q8
        lvl.snow_dir = (snow_dir_x, snow_dir_y, snow_dir_z)
        
        # Read strings
        f.seek(off_strings)
        for _ in range(string_count):
            str_len = struct.unpack(">B", f.read(1))[0]
            str_data = f.read(str_len).decode("utf-8")
            lvl.strings.append(str_data)
        
        # Read colliders
        f.seek(off_colliders)
        for _ in range(collider_count):
            ctype, solid, has_origin, _ = struct.unpack(">BBB B", f.read(4))
            bounds_min = struct.unpack(">fff", f.read(12))
            bounds_max = struct.unpack(">fff", f.read(12))
            normal = struct.unpack(">fff", f.read(12))
            plane_origin = struct.unpack(">fff", f.read(12))
            face_id, owner_id = struct.unpack(">hh", f.read(4))
            c = Collider(ctype, solid, has_origin, bounds_min, bounds_max,
                        normal, plane_origin, face_id, owner_id)
            lvl.colliders.append(c)
        
        # Read faces (24 bytes each)
        f.seek(off_faces)
        for _ in range(face_count):
            vs, vc, material_id, flags = struct.unpack(">IIHH", f.read(12))
            normal = struct.unpack(">fff", f.read(12))
            face = Face(vs, vc, material_id, normal, flags)
            lvl.faces.append(face)
        
        # Read vertices (20 bytes each)
        f.seek(off_vertices)
        for _ in range(vertex_count):
            pos = struct.unpack(">fff", f.read(12))
            uv = struct.unpack(">ff", f.read(8))
            v = Vertex(pos, uv)
            lvl.vertices.append(v)
        
        # Read entities (20 bytes each)
        f.seek(off_entities)
        for _ in range(entity_count):
            classname_id, _ = struct.unpack(">HH", f.read(4))
            position = struct.unpack(">fff", f.read(12))
            props_offset, props_len = struct.unpack(">II", f.read(8))
            e = Entity(classname_id, position, props_offset, props_len)
            lvl.entities.append(e)
        
        # Read props blob
        f.seek(off_props_blob)
        lvl.props_blob = f.read()
        
        return lvl
