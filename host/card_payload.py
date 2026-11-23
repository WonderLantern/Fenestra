"""Package a flat, locally supplied AArch64 boot image for the memory card."""
import struct

from cipher_patch import atomic_new, read_bounded

MAGIC = b'FNB1'
HEADER = struct.Struct('<4sIII')
ADDRESS = 0x401a0000
CAPACITY = 0x5d000


def pack(image, entry_offset):
    if not 4 <= len(image) <= CAPACITY:
        raise ValueError('image exceeds the payload arena or has no entry instruction')
    if not 0 <= entry_offset <= len(image)-4 or entry_offset % 4:
        raise ValueError('invalid AArch64 entry offset')
    return HEADER.pack(MAGIC, HEADER.size, len(image), entry_offset)+image


def write(image_path, entry_offset, destination):
    data = pack(read_bounded(image_path, CAPACITY), entry_offset)
    atomic_new(destination, data)
    return len(data)
