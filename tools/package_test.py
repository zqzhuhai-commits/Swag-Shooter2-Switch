#!/usr/bin/env python3
"""Package the compiled probe only; never include proprietary game libraries."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('nro', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    data = args.nro.read_bytes()
    if data[16:20] != b'NRO0':
        raise SystemExit('Missing NRO0 header')
    image_size = struct.unpack_from('<I', data, 24)[0]
    if not 128 <= image_size <= len(data) or data[image_size:image_size + 4] != b'ASET':
        raise SystemExit('Invalid NRO image or missing asset header')
    for offset in (32, 40, 48):
        start, size = struct.unpack_from('<II', data, offset)
        if start % 4096 or start + size > image_size:
            raise SystemExit('Invalid NRO segment')
    report = {'artifact': args.nro.name, 'bytes': len(data),
              'sha256': hashlib.sha256(data).hexdigest(),
              'nro_structure': 'PASS', 'switch_device_execution': 'NOT_RUN',
              'unity_started': False, 'playable': False}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        archive.write(args.nro, 'switch/swag2-test/swag2-armv7-test.nro')
        archive.write(root / 'docs/test-0.0.1.md', 'README-TEST.md')
        for name in ('prepare_test_data.ps1', 'prepare_test_data.py'):
            archive.write(root / 'tools' / name, 'tools/' + name)
        for path in (root / 'third_party_licenses').iterdir():
            archive.write(path, 'licenses/' + path.name)
        archive.writestr('audit/nro-verification.json', json.dumps(report, indent=2) + '\n')
    print(json.dumps({'package': args.output.name,
                     'package_sha256': hashlib.sha256(args.output.read_bytes()).hexdigest(),
                     'nro': report}, indent=2))


if __name__ == '__main__':
    main()
