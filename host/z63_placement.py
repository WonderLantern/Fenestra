"""Check that resident code is placed where original startup leaves it.

Original C2.01 startup first runs MAIN text in place. It then copies the
content of each text window to DRAM and remaps the whole window to that copy.
Package bytes after a window's copy end are therefore replaced by DRAM the copy
never wrote. An all-zero region in the image is not evidence that it is free.
"""
import hashlib
import struct


def constants(code):
    """Return every value a MOVZ/MOVN/MOVK/MOV-register sequence builds."""
    registers, seen = {}, set()
    for (word,) in struct.iter_unpack('<I', code[:len(code) & ~3]):
        rd, wide = word & 31, word >> 31
        mask = (1 << (64 if wide else 32)) - 1
        kind = word & 0x7f800000
        if kind in (0x52800000, 0x12800000, 0x72800000):
            shift = ((word >> 21) & 3) * 16
            value = ((word >> 5) & 0xffff) << shift
            if kind == 0x52800000:
                registers[rd] = value
            elif kind == 0x12800000:
                registers[rd] = ~value & mask
            else:
                registers[rd] = (registers.get(rd, 0) & ~(0xffff << shift) | value) & mask
        elif word & 0x7fe0ffe0 == 0x2a0003e0:
            source = registers.get((word >> 16) & 31)
            if source is None:
                registers.pop(rd, None)
                continue
            registers[rd] = source & mask
        else:
            continue
        seen.add(registers[rd])
    return seen


def check(profile, original):
    """Raise unless the code region survives original text relocation."""
    base = profile['image_base']
    begin, end = (profile['probe_code_region'][name] for name in ('begin', 'end'))
    relocation = profile.get('text_relocation')
    if not relocation or not relocation.get('windows') or not relocation.get('routines'):
        raise ValueError('target has no traced text relocation; code placement is unproven')
    built = set()
    for routine in relocation['routines']:
        at = routine['address'] - base
        code = original[at:at + routine['bytes']]
        if hashlib.sha256(code).hexdigest() != routine['sha256']:
            raise ValueError('text relocation routine differs')
        built |= constants(code)
    for window in relocation['windows']:
        if not window['begin'] <= window['copy_end'] <= window['remap_end']:
            raise ValueError('invalid text window: ' + window['name'])
        # The profile may not assert bounds the original code does not build.
        if window['begin'] not in built or window['copy_end'] not in built:
            raise ValueError('text window bounds are not in the original routine: ' + window['name'])
        length = window['remap_end'] - window['begin']
        if window['remap_end'] != window['copy_end'] and length not in built:
            raise ValueError('text window remap length is not in the original routine: ' + window['name'])
        if begin < window['remap_end'] and window['begin'] < end:
            raise ValueError('code region lies in relocated text window ' + window['name'] +
                             '; original startup replaces it')
    kept = relocation['in_place_executable']
    if not kept['begin'] <= begin < end <= kept['end']:
        raise ValueError('code region is outside the in-place executable range')
    if not 0 <= begin - base < end - base <= len(original) or any(original[begin - base:end - base]):
        raise ValueError('code region is not empty in the original image')
    guard = profile.get('guard_region')
    if not guard:
        raise ValueError('target has no guard region; boot hooks would be unguarded')
    g0, g1 = guard['begin'], guard['end']
    if g0 & 7 or g1 & 3 or not g0 < g1:
        raise ValueError('invalid guard region')
    # Guards must travel with original code: inside the copied content of a window.
    if not any(w['begin'] <= g0 and g1 <= w['copy_end'] for w in relocation['windows']):
        raise ValueError('guard region is not inside copied original text')
    if any(original[g0 - base:g1 - base]):
        raise ValueError('guard region is not empty in the original image')
    return dict(code_region=[begin, end], guard_region=[g0, g1], outside_relocated_windows=True,
                windows={w['name']: [w['begin'], w['copy_end'], w['remap_end']] for w in relocation['windows']},
                loader_places_padding_verified=bool(relocation.get('loader_places_padding_verified')),
                physical_install_qualified=bool(relocation.get('physical_install_qualified')))


MARKER = b'FXZ63COR'
GUARD_WORDS = 8


def position_independent(word):
    """True unless the instruction reads PC: it may then run at another address."""
    return not ((word & 0x7c000000) == 0x14000000 or (word & 0xff000010) == 0x54000000 or
                (word & 0x7e000000) in (0x34000000, 0x36000000) or
                (word & 0x1f000000) == 0x10000000 or (word & 0x3b000000) == 0x18000000)


def _branch(source, target, link=False):
    delta = target - source
    if delta % 4 or not -(1 << 27) <= delta < (1 << 27):
        raise ValueError('guard branch out of range')
    return (0x94000000 if link else 0x14000000) | ((delta // 4) & 0x3ffffff)


def marker_address(profile):
    return profile['probe_code_region']['end'] - len(MARKER)


def guards(profile, original, hooks):
    """Build one guard per hook: {site: (stub address, core target, is call)} -> bytes.

    Each guard enters the resident core only when the marker stored at the end
    of the code region is present. Otherwise it behaves as the original site:
    a function entry runs its displaced first instruction and continues, and a
    call site reaches its original callee. x16/x17 are the only registers used;
    both are free at a function entry and across a call.
    """
    base = profile['image_base']
    g0, g1 = (profile['guard_region'][name] for name in ('begin', 'end'))
    marker = marker_address(profile)
    if marker & 7:
        raise ValueError('marker is not aligned')
    out = bytearray(MARKER)
    table = {}
    for site in sorted(hooks):
        target, call = hooks[site]
        stub = g0 + len(out)
        word = struct.unpack_from('<I', original, site - base)[0]
        page = (marker >> 12) - (stub >> 12)
        if not -(1 << 20) <= page < (1 << 20):
            raise ValueError('marker out of ADRP range')
        words = [0x90000010 | ((page & 3) << 29) | (((page >> 2) & 0x7ffff) << 5),
                 0xf9400210 | (((marker & 0xfff) // 8) << 10),
                 0x58000011 | ((((g0 - (stub + 8)) // 4) & 0x7ffff) << 5),
                 0xeb11021f,
                 0x54000041,
                 _branch(stub + 20, target)]
        if call:
            if word >> 26 != 0x25:
                raise ValueError('call hook site is not an original BL')
            delta = word & 0x3ffffff
            callee = site + ((delta - (1 << 26) if delta & (1 << 25) else delta) * 4)
            words += [_branch(stub + 24, callee), 0xd4200000]
        else:
            if not position_independent(word):
                raise ValueError('hook site instruction cannot be displaced')
            words += [word, _branch(stub + 28, site + 4)]
        out += struct.pack('<%dI' % GUARD_WORDS, *words)
        table[site] = stub
    if g0 + len(out) > g1:
        raise ValueError('guard region exhausted')
    return bytes(out), table
