"""Read-only C1.10 filesystem calling-contract evidence; no camera commands."""
import hashlib
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
BASE=0x40160000
PIN='7d81293b5a793f034f15c31554e0ca7b552847da8d15055eec33541c3a509957'
RANGES={
    'mutex_serialized_open_path_flags_attributes':(0x402e883c,0x60),
    'mutex_serialized_close':(0x402e889c,0x48),
    'mutex_serialized_read_fd_buffer_count_out':(0x402e8a0c,0xc4),
    'mutex_serialized_write_fd_buffer_count_out_callback':(0x402e8ad0,0xf8),
    'native_create_truncate_wrapper_uses_0x301':(0x402e8ca0,0x54),
    'open_flags_to_write_create_and_truncate':(0x4090c4f8,0xbc),
    'existing_exclusive_create_returns_0x80000002':(0x40904f9c,0x64),
    'close_updates_directory_and_flushes':(0x4090dc80,0x108),
    'drive_and_backslash_ASCII_path_parser':(0x409136fc,0x58),
    'drive_mount_state_gate':(0x40913620,0x60),
    'ordinary_register_and_mount':(0x40912c70,0xbc),
    'three_device_callbacks_registered':(0x40911ce4,0x74),
    'geometry_and_sector_read_callbacks':(0x40914034,0x58),
    'geometry_callback_and_exfat_type07':(0x40914174,0x58),
    'type07_reads_exfat_boot_region':(0x40912264,0x54),
    'ordinary_drive_unregistration':(0x40913428,0xd4),
    'mutex_serialized_directory_open_path_out_handle':(0x402e8fb8,0x50),
    'mutex_serialized_directory_read_handle_record':(0x402e9068,0x50),
    'mutex_serialized_directory_close_handle':(0x402e90b8,0x48),
    'directory_record_utf16_attributes_size_and_end':(0x40902a5c,0x384),
}
def inspect(data=None):
    if data is None:raise ValueError('explicit original application bytes required')
    from platform_profiles import PROFILES
    digest=hashlib.sha256(data).hexdigest()
    if digest not in (PIN,PROFILES['zr-c110']['sha256']):raise ValueError('C1.10 ROM fingerprint differs')
    return {'target':'Nikon ZR C1.10','rom_sha256':digest,'public_Nikon_SDK':False,
        'scope':'independently inferred normal native file APIs; physical camera unqualified',
        'create_flags':'0x101 = write and exclusive create; no replace/append/truncate',
        'path':'A:\\FENESTRA\\fenestra.txt','functions':{name:{'address':hex(a),'bytes':n,
            'hex':data[a-BASE:a-BASE+n].hex(),
            'sha256':hashlib.sha256(data[a-BASE:a-BASE+n]).hexdigest()}
            for name,(a,n) in RANGES.items()}}
