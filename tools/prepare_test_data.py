#!/usr/bin/env python3
"""Extract only the ARMv7 library needed by the execution probe, locally."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('apk', type=Path)
    parser.add_argument('output', type=Path, help='Destination switch/swag2-test directory')
    args = parser.parse_args()
    with zipfile.ZipFile(args.apk) as archive:
        data = archive.read('lib/armeabi-v7a/libil2cpp.so')
    if len(data) < 52 or data[:6] != b'\x7fELF\x01\x01' or struct.unpack_from('<H', data, 18)[0] != 40:
        raise SystemExit('Expected a little-endian ARM ELF32 library')
    args.output.mkdir(parents=True, exist_ok=True)
    target = args.output / 'libil2cpp.so'
    target.write_bytes(data)
    manifest = {'file': target.name, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
                'usage': 'Pure-export execution probe only; Unity is not initialized.'}
    (args.output / 'library-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
