"""Create a plain ZIP preserving executable modes and macOS framework symlinks."""
import os
from pathlib import Path
import stat
import sys
import zipfile

source = Path(sys.argv[1]).resolve()
output = Path(sys.argv[2]).resolve()
if output.suffix != '.zip':
    raise ValueError('Output must end in .zip')
with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(source.rglob('*')):
        name = path.relative_to(source).as_posix()
        if path.is_symlink():
            entry = zipfile.ZipInfo(name)
            entry.create_system = 3
            entry.external_attr = (stat.S_IFLNK | 0o777) << 16
            archive.writestr(entry, os.fsencode(os.readlink(path)))
        else:
            archive.write(path, name)
