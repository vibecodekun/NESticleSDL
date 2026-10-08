"""Inventory user-supplied NESticle archives without executing their binaries."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import struct
import zipfile


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def executable_format(data):
    if data[:2] != b'MZ':
        return None
    if len(data) >= 64:
        offset = struct.unpack_from('<I', data, 60)[0]
        signature = data[offset:offset + 4]
        if signature == b'PE\0\0':
            return 'PE'
        if signature[:2] in (b'LE', b'LX', b'NE'):
            return signature[:2].decode('ascii')
    return 'MZ'


def inventory(source, output):
    output.mkdir(parents=True, exist_ok=True)
    expected = {}
    sums = source / 'md5sums.txt'
    if sums.exists():
        for line in sums.read_text().splitlines():
            match = re.fullmatch(r'([0-9a-f]{32})\s+(.+)', line)
            if match:
                expected[match[2]] = match[1]
    releases = []
    snapshots = []
    for path in sorted(source.glob('*.zip')):
        data = path.read_bytes()
        digest = hashlib.md5(data).hexdigest()
        record = {'archive': path.name, 'md5': digest, 'sha256': sha256(data),
                  'md5_matches': digest == expected[path.name] if path.name in expected else None,
                  'members': []}
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            for member in archive.infolist():
                if member.is_dir():
                    continue
                content = archive.read(member)
                kind = executable_format(content)
                item = {'name': member.filename, 'size': len(content),
                        'sha256': sha256(content), 'format': kind}
                record['members'].append(item)
                # Flatten selected names; never extract archive-controlled paths.
                if kind == 'PE' or member.filename.upper() == 'README.TXT':
                    target = output / 'releases' / path.stem / Path(member.filename).name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(content)
                    item['local_path'] = target.relative_to(output).as_posix()
                if path.name == 'kronksrc.zip' and member.filename.upper().endswith('.ZIP'):
                    with zipfile.ZipFile(io.BytesIO(content)) as snapshot:
                        entries = []
                        for entry in snapshot.infolist():
                            if entry.is_dir():
                                continue
                            value = snapshot.read(entry)
                            entries.append({'name': entry.filename, 'size': len(value),
                                            'sha256': sha256(value)})
                            if Path(entry.filename).name.upper() in ('MMC.CPP', 'MMC.H', 'NESSOUND.CPP', 'NESSOUND.H'):
                                target = output / 'snapshots' / Path(member.filename).stem / Path(entry.filename).name
                                target.parent.mkdir(parents=True, exist_ok=True)
                                target.write_bytes(value)
                        snapshots.append({'archive': member.filename, 'members': entries})
        releases.append(record)
    # Paths stay relative so the manifest does not record the local machine.
    manifest = {'source': source.resolve().name, 'archives': releases, 'snapshots': snapshots}
    (output / 'inventory.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'{len(releases)} archives inventoried; {len(snapshots)} nested source snapshots')
    print(f'MD5 matches: {sum(r["md5_matches"] is True for r in releases)}/{len(releases)}')
    for record in releases:
        if record['md5_matches'] is False:
            print(f'WARNING: MD5 mismatch: {record["archive"]}')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1] / 'recovery')
    args = parser.parse_args()
    inventory(args.source, args.output)
