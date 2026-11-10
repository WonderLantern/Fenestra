"""Validate original Zf C3.01 menu inputs without installing a menu adapter."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct

from zf_c301 import BASE, EG_SIZE, EG_SHA256, EG_PAYLOAD_SHA256

PAGE1_OFFSET = 0x48684b8
PAGE_STOP = 0x6c6
PAGE_ROOT = 0xa03401b88
TEXTS = 2898
LANGUAGES = 29
OFFSETS = 0x431a2a48
POOLS = 0x437c3e40
ATTRIBUTES = 0x437d8778
CLASSES = 0x4314d00d
TEXT_GUARD = 0xa0282267c
NAVIGATION_SHA256 = '04c5bd50d7789332a4be6428a8c526d47da996a9897f48835725d3f72f54745d'
MENU_HOOKS = {
    'page': dict(address=0x403e42b4, bytes=52,
                 sha256='68be22c5652b421b46d72b94b19f6cb704e594746d54c5db0f14278fb0ea82ae'),
    'text': dict(address=0x40338ee8, bytes=108,
                 sha256='50ff2fd31090e88500febc986926c8551299ee2eb235f62506caf32ec4fcbc08'),
    'navigate': dict(address=0x403b8024, bytes=340,
                     sha256='6619380fc60ddc80304e988574a6df7284b91ddaaa100d3b8f7646b584250e14'),
}
ENTRY_CONTRACTS = {
    'page': {
        'arguments': 'w0=group, w1=page_id',
        'behavior': 'group>6 returns 0; otherwise table[group]+96*(w1&0xffff)',
        'table': '0xa03401b88', 'row_bytes': 96, 'complete_function': True,
    },
    'text': {
        'arguments': 'w0=language, w1=text_id, x2=output_pointer, x3=output_bytes',
        'behavior': 'guarded adjacent UTF-16 offset lookup through language pool and attribute tables',
        'offsets_table': '0x431a2a48', 'pool_table': '0x437c3e40',
        'attributes_table': '0x437d8778', 'text_capacity': 2898,
        'complete_function': True,
    },
    'navigate': {
        'arguments': 'w0=page',
        'behavior': 'Zf-native 340 B navigation candidate; several callees differ from Z9',
        'complete_function': False,
    },
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def capacity(original_rows, extra_rows, allocated):
    """Compute required row slots; no allocation or runtime capacity is implied."""
    if any(type(n) is not int or n < 0 for n in (original_rows, extra_rows)):
        raise ValueError('row counts must be nonnegative integers')
    if allocated is not None and (type(allocated) is not int or allocated < 0):
        raise ValueError('allocated rows must be a nonnegative integer')
    needed = original_rows + extra_rows
    return dict(original_rows=original_rows, additional_rows=extra_rows, required_rows=needed,
                supplied_capacity=allocated, sufficient=None if allocated is None else allocated >= needed)


def namespace(image, first, stop):
    require(0 < stop <= 65536 and first >= 0 and first + 96*(stop-1) <= len(image),
            'page namespace outside application')
    counts, gaps = Counter(), []
    for page_id in range(1, stop):
        identity = struct.unpack_from('<I', image, first + 96*(page_id-1))[0]
        kind = identity & 0xffff
        counts[kind] += 1
        if identity == 0:
            gaps.append(page_id)
        else:
            require(identity >> 16 == page_id and kind in (2, 3, 5), 'page namespace identity differs')
    return counts, gaps


def _read(image, address, size, source, ram, length):
    if size < 0:
        raise ValueError('negative range')
    if ram <= address and address + size <= ram + length:
        offset = source + address - ram
    elif BASE <= address and address + size <= BASE + len(image):
        offset = address - BASE
    else:
        raise ValueError('address outside application or initialized RAM mapping')
    require(offset >= 0 and offset + size <= len(image), 'source range outside application')
    return memoryview(image)[offset:offset + size]


def _text_tables(read):
    offsets = struct.unpack('<29Q', read(OFFSETS, LANGUAGES*8))
    pools = struct.unpack('<29Q', read(POOLS, LANGUAGES*8))
    attributes = struct.unpack('<29Q', read(ATTRIBUTES, LANGUAGES*8))
    require(all(offsets[i+1] - offsets[i] == (TEXTS+1)*4 for i in range(LANGUAGES-1)),
            'text offset-table stride differs')
    require(all(attributes[i+1] - attributes[i] == TEXTS for i in range(LANGUAGES-1)),
            'text attribute-table stride differs')
    read(CLASSES, TEXTS)
    ends = []
    for language in range(LANGUAGES):
        values = struct.unpack('<2899I', read(offsets[language], (TEXTS+1)*4))
        require(values[0] == 0 and all(a <= b and not (a & 1) for a,b in zip(values, values[1:]))
                and not (values[-1] & 1), 'text offsets are not ordered UTF-16 byte ranges')
        read(pools[language], values[-1])
        read(attributes[language], TEXTS)
        if language + 1 < LANGUAGES:
            require(pools[language+1] - pools[language] == values[-1], 'text pool extent differs')
        ends.append(values[-1])
    return offsets, pools, attributes, ends


def _english(read, offsets, pools, text_id):
    require(0 <= text_id < TEXTS, 'text ID outside known original tables')
    begin, end = struct.unpack('<II', read(offsets[1] + 4*text_id, 8))
    require(begin <= end and not ((begin | end) & 1), 'invalid English text range')
    return bytes(read(pools[1] + begin, end-begin)).decode('utf-16le')


def _page(image, read, page_id):
    require(1 <= page_id < PAGE_STOP, 'page ID outside checked namespace')
    record = memoryview(image)[PAGE1_OFFSET + 96*(page_id-1):PAGE1_OFFSET + 96*page_id]
    require(len(record) == 96 and struct.unpack_from('<I', record)[0] == (page_id << 16) | 2,
            'page identity differs')
    descriptor = struct.unpack_from('<Q', record, 80)[0]
    raw = read(descriptor, 64)
    flags, reserved, header, rows, events, count = struct.unpack_from('<IIQQQH', raw)
    title = struct.unpack('<I', read(header + 68, 4))[0]
    return dict(id=page_id, descriptor=descriptor, flags=flags, header=header, rows=rows,
                events=events, count=count, title=title,
                record_sha256=hashlib.sha256(record).hexdigest(),
                descriptor_sha256=hashlib.sha256(raw).hexdigest())


def _row(read, page, index):
    require(0 <= index < page['count'], 'row outside descriptor')
    raw = read(page['rows'] + 44*index, 44)
    words = struct.unpack('<11I', raw)
    events = struct.unpack('<Q', read(page['events'] + 8*index, 8))[0]
    return dict(index=index, text_id=words[0], row_id=words[6], event_table=events,
                sha256=hashlib.sha256(raw).hexdigest())


def _menu_hooks(image):
    payload = image[:-2] if len(image) == EG_SIZE else image
    rows = {}
    for name, expected in MENU_HOOKS.items():
        at = expected['address'] - BASE
        require(0 <= at and at + expected['bytes'] <= len(payload),
                f'{name} hook window outside Zf application')
        raw = payload[at:at + expected['bytes']]
        digest = hashlib.sha256(raw).hexdigest()
        require(digest == expected['sha256'], f'{name} hook window differs')
        rows[name] = dict(address=hex(expected['address']), bytes=expected['bytes'],
                           sha256=digest, complete_function_verified=False,
                           runtime_hook_verified=False)
    return rows


def inspect(image, *, extra_rows=2, allocated_rows=None):
    """Check pinned source data; results do not establish execution or ownership."""
    digest = hashlib.sha256(image).hexdigest()
    if len(image) == EG_SIZE and digest == EG_SHA256:
        form = 'original module including CRC'
    elif len(image) == EG_SIZE-2 and digest == EG_PAYLOAD_SHA256:
        form = 'original application without CRC'
    else:
        raise ValueError('expected the pinned Zf C3.01 application')
    length, ram, source = (struct.unpack_from('<Q', image, i)[0] for i in (0x80d0, 0x80d8, 0x8148))
    require((length, ram, source) == (0x3c1000, 0xa02808000, 0x4777000),
            'original initialized RAM mapping differs')
    read = lambda addr, n: _read(image, addr, n, source, ram, length)
    counts, gaps = namespace(image, PAGE1_OFFSET, PAGE_STOP)
    require(counts == {2:1433, 0:3, 3:128, 5:169} and gaps == [0x59a,0x61b,0x61c],
            'original page namespace counts differ')
    page0 = ram + PAGE1_OFFSET - 96 - source
    require(page0 == 0xa028f9458, 'page initializer mapping differs')
    offsets, pools, attributes, ends = _text_tables(read)
    initial_guard = struct.unpack('<I', read(TEXT_GUARD, 4))[0]
    require(initial_guard == 0xffff, 'text guard initializer differs')

    setup = _page(image, read, 672)
    require((setup['descriptor'],setup['header'],setup['rows'],setup['events'],setup['count'],setup['title']) ==
            (0xa028cd3b0,0x43b38c80,0x43b38d90,0x43b395a8,47,617), 'setup descriptor differs')
    require(_english(read,offsets,pools,setup['title']) == 'SETUP MENU', 'setup title differs')
    first, version = _row(read,setup,0), _row(read,setup,44)
    require((first['text_id'],version['text_id']) == (618,866), 'setup row identities differ')
    require(_english(read,offsets,pools,first['text_id']) == 'Format memory card' and
            _english(read,offsets,pools,version['text_id']) == 'Firmware version', 'setup row labels differ')
    old = _page(image,read,638)
    require(_english(read,offsets,pools,old['title']) == 'Picture review', 'old Z9 page-ID control differs')

    iptc = _page(image,read,1292)
    row = _row(read,iptc,0)
    require(_english(read,offsets,pools,iptc['title']) == 'IPTC' and
            _english(read,offsets,pools,row['text_id']) == 'Edit/save' and
            row['event_table'] == 0x439db818, 'event template owner differs')
    raw_events = read(row['event_table'],15*32)
    event_ids = [struct.unpack_from('<Q',raw_events,32*i)[0] & 0xffff for i in range(15)]
    require(hashlib.sha256(raw_events).hexdigest() == NAVIGATION_SHA256 and event_ids[-1] == 0x3dc and
            all(event_ids.count(event) == 1 for event in (0x2ae,0x2af,0x2b4,0x2c8,0x2d7)),
            'original navigation template identity or required events differ')
    firmware_events = read(version['event_table'],54*32)
    require(all(struct.unpack_from('<Q',firmware_events,32*i)[0] & 0xffff != 0x3dc for i in range(53)) and
            struct.unpack_from('<Q',firmware_events,53*32)[0] & 0xffff == 0x3dc,
            'firmware-version event table boundary differs')
    return dict(status='original-input-validation-only', application_form=form, application_sha256=digest,
        menu_hooks=_menu_hooks(image), entry_contracts=ENTRY_CONTRACTS,
        page_namespace=dict(first_module_offset=PAGE1_OFFSET, initialized_page0=hex(page0),
                            root_global_candidate=hex(PAGE_ROOT), root_global_binding_checked=False,
                            stop_exclusive=PAGE_STOP,
                            counts=dict(counts), zero_ids=gaps, dynamic_registrations_checked=False),
        setup=setup, setup_first_row=first, setup_firmware_version_row=version,
        setup_capacity=capacity(setup['count'],extra_rows,allocated_rows),
        inherited_z9_setup_page_638='Picture review',
        text_resources=dict(languages=LANGUAGES, original_text_capacity=TEXTS,
            adjacent_offset_table_strides_verified=LANGUAGES-1,
            adjacent_attribute_table_strides_verified=LANGUAGES-1,
            language_pool_byte_counts=ends, guard_initializer=initial_guard, runtime_guard=None,
            offsets_table=hex(OFFSETS), pool_table=hex(POOLS), attributes_table=hex(ATTRIBUTES),
            classes_table=hex(CLASSES), final_table_external_boundary_verified=False),
        navigation_template=dict(owner_page=1292, address=hex(row['event_table']), records=15,
            terminal=0x3dc, back=0x2ae, select=[0x2af,0x2b4,0x2c8,0x2d7], stay=0x6c6,
            sha256=hashlib.sha256(raw_events).hexdigest(), runtime_safety_verified=False),
        setup_firmware_version_event_records=54, runtime_menu_adapter_available=False,
        physical_camera_tested=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--application', type=Path, required=True)
    parser.add_argument('--extra-rows', type=int, default=2)
    parser.add_argument('--allocated-rows', type=int)
    args = parser.parse_args()
    if args.application.stat().st_size not in (EG_SIZE, EG_SIZE-2):
        parser.error('expected the pinned Zf C3.01 application')
    print(json.dumps(inspect(args.application.read_bytes(),extra_rows=args.extra_rows,
                             allocated_rows=args.allocated_rows),indent=2))


if __name__ == '__main__':
    main()
