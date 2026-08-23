"""Readers for the Riot Engine asset formats used by
The Lord of the Rings: The Fellowship of the Ring (Surreal Software, 2002).

Pure Python, no dependencies, usable outside Blender.
"""

from . import srsc, model, texture, anim, klass, level, database, write

SRSC = srsc.SRSC
ModelDatabase = model.ModelDatabase
TextureDatabase = texture.TextureDatabase
TextureResolver = texture.TextureResolver
AnimationDatabase = anim.AnimationDatabase
ClassDatabase = klass.ClassDatabase
DatabaseIndex = database.DatabaseIndex
Level = level.Level
rebuild_archive = write.rebuild_archive
encode_texture = write.encode_texture

__all__ = ['srsc', 'model', 'texture', 'anim', 'klass', 'level', 'database', 'write',
           'SRSC', 'ModelDatabase', 'TextureDatabase', 'TextureResolver',
           'AnimationDatabase', 'ClassDatabase', 'DatabaseIndex', 'Level',
           'rebuild_archive', 'encode_texture']
