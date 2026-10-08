"""Bundle a built executable, its data files and notices into a release archive.

usage: package.py path/to/nesticle[.exe] TARGET [OUTPUT_DIR]
Windows builds become TARGET.zip; other builds become .tar.gz, keeping the
executable bit.
"""
from pathlib import Path
import sys
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    exe = Path(sys.argv[1]).resolve()
    name = f'nesticle-sdl-{sys.argv[2]}'
    output = Path(sys.argv[3]).resolve() if len(sys.argv) > 3 else ROOT / 'dist'
    files = [(exe, exe.name)]
    files += [(exe.parent / f, f) for f in ('gui.vol', 'NESticle.pal', 'SDL-LICENSE.txt')]
    files += [(ROOT / f, f) for f in ('README.md', 'LICENSE', 'PROVENANCE.md')]
    missing = [str(path) for path, _ in files if not path.is_file()]
    if missing:
        sys.exit('missing: ' + ', '.join(missing))
    output.mkdir(parents=True, exist_ok=True)
    if exe.suffix.lower() == '.exe':
        archive = output / f'{name}.zip'
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
            for path, member in files:
                z.write(path, f'{name}/{member}')
    else:
        archive = output / f'{name}.tar.gz'

        def normalize(info):
            # Same modes and owner whatever filesystem or user built it.
            info.uid = info.gid = 0
            info.uname = info.gname = ''
            info.mode = 0o755 if info.name == f'{name}/{exe.name}' else 0o644
            return info

        with tarfile.open(archive, 'w:gz') as t:
            for path, member in files:
                t.add(path, f'{name}/{member}', filter=normalize)
    print(archive)


if __name__ == '__main__':
    main()
