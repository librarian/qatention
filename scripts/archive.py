"""Create a single ZIP of the installed files, preserving Unix executable modes."""
from pathlib import Path
import shutil
import sys

source = Path(sys.argv[1]).resolve()
output = Path(sys.argv[2]).resolve()
if output.suffix != '.zip':
    raise ValueError('Output must end in .zip')
shutil.make_archive(str(output.with_suffix('')), 'zip', source)
