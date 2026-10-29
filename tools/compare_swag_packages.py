#!/usr/bin/env python3
"""Compare local SWAG source packages without copying proprietary payloads.

Supports the two verified Unity metadata v24 layouts (2018.3 and 2019.4).
This is a static feasibility check, not a runtime compatibility test.
"""
import argparse
import json
import re
import struct
import zipfile


CLASSES = ('GameManager', 'PlayerManager', 'PlayerCarInteractionHandler')


def inspect(path):
    with zipfile.ZipFile(path) as archive:
        def unique(suffix):
            matches = [p for p in archive.namelist() if p.endswith(suffix)]
            if len(matches) != 1:
                raise ValueError(f'{suffix}: expected one entry, found {len(matches)}')
            return archive.read(matches[0])

        header = unique('/globalgamemanagers')
        match = re.search(rb'20\d\d\.\d+\.\d+[fp]\d+', header[:128])
        if not match:
            raise ValueError('Missing Unity asset version')
        version = match[0].decode()
        if version.startswith('2019.4.'):
            stride, field_position, count_position = 92, 36, 68
        elif version.startswith('2018.3.'):
            stride, field_position, count_position = 100, 44, 76
        else:
            raise ValueError(f'Unverified metadata layout: {version}')
        data = unique('/global-metadata.dat')
        if struct.unpack_from('<II', data) != (0xFAB11BAF, 24):
            raise ValueError('Expected IL2CPP metadata version 24')

        def table(position):
            offset, size = struct.unpack_from('<II', data, position)
            if offset + size > len(data):
                raise ValueError('Metadata table out of bounds')
            return offset, size

        offset, size = table(24)
        strings = data[offset:offset + size]

        def string(index):
            if not 0 <= index < len(strings):
                raise ValueError('String index out of bounds')
            end = strings.find(b'\0', index)
            if end < 0:
                raise ValueError('Unterminated metadata string')
            return strings[index:end].decode('utf-8', 'replace')

        types_offset, types_size = table(160)
        fields_offset, fields_size = table(96)
        if types_size % stride or fields_size % 12:
            raise ValueError('Unexpected metadata record size')
        classes = {}
        for position in range(types_offset, types_offset + types_size, stride):
            name, namespace = struct.unpack_from('<II', data, position)
            name, namespace = string(name), string(namespace)
            if namespace != 'brijesh.CountryWar2' or name not in CLASSES:
                continue
            start = struct.unpack_from('<i', data, position + field_position)[0]
            methods, _, count = struct.unpack_from('<HHH', data, position + count_position)
            if start < 0 or (start + count) * 12 > fields_size:
                raise ValueError('Class field range out of bounds')
            names = [string(struct.unpack_from('<I', data,
                     fields_offset + (start + i) * 12)[0]) for i in range(count)]
            classes[name] = {'method_count': methods, 'fields': names}
        libraries = []
        for name in archive.namelist():
            if not name.endswith(('/libunity.so', '/libil2cpp.so', '/libmain.so')):
                continue
            with archive.open(name) as stream:
                elf = stream.read(64)
            if elf[:4] != b'\x7fELF' or elf[5] != 1:
                raise ValueError(f'Expected little-endian ELF: {name}')
            machine = struct.unpack_from('<H', elf, 18)[0]
            libraries.append({'path': name, 'bits': {1: 32, 2: 64}[elf[4]],
                              'machine': machine,
                              'architecture': {40: 'ARMv7', 183: 'AArch64', 3: 'x86'}.get(machine, 'other'),
                              'accepted_by_current_loader': elf[4] == 2 and machine == 183})
        return {'unity_version': version, 'metadata_version': 24,
                'type_record_bytes': stride, 'type_count': types_size // stride,
                'libraries': libraries, 'classes': classes}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('original')
    parser.add_argument('candidate')
    args = parser.parse_args()
    original, candidate = inspect(args.original), inspect(args.candidate)
    comparisons = {}
    for name in CLASSES:
        old = set(original['classes'].get(name, {}).get('fields', []))
        new = set(candidate['classes'].get(name, {}).get('fields', []))
        comparisons[name] = {'shared_fields': len(old & new),
                             'removed_fields': sorted(old - new),
                             'added_fields': sorted(new - old)}
    print(json.dumps({'validation': 'static_only_no_guest_execution',
          'original': original, 'candidate': candidate, 'field_comparison': comparisons,
          'limitations': ['Shared names do not prove matching field types or behavior.',
                         'No ARMv7 execution backend exists in the current loader.',
                         'Original ARM64 IL2CPP code cannot be paired with candidate metadata without verification.',
                         'Existing GC offsets are specific to the original binary.']},
          ensure_ascii=True, indent=2))


if __name__ == '__main__':
    main()
