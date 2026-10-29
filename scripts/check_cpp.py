#!/usr/bin/env python3
"""Format/check first-party C++ or run tidy against a built compilation database."""
import argparse
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fix', action='store_true')
    parser.add_argument('--build', type=Path, help='Run clang-tidy using this build directory')
    args = parser.parse_args()
    files = subprocess.check_output(
        ['git', 'ls-files', '-z', 'src', 'tests'], cwd=ROOT
    ).decode().split('\0')
    files = [f for f in files if Path(f).suffix in ('.cpp', '.h')]
    if args.build:
        build = args.build.resolve()
        entries = json.loads((build / 'compile_commands.json').read_text())
        compiled = {str((Path(e['directory']) / e['file']).resolve()) for e in entries}
        sources = [f for f in files if str(ROOT / f) in compiled]
        if not sources:
            raise SystemExit("No first-party sources in compilation database")
        failed = False
        for file in sources:
            print(f"Analyzing {file}", flush=True)
            result = subprocess.run(['clang-tidy', '-p', str(build), file], cwd=ROOT)
            failed |= result.returncode != 0
        if failed:
            raise SystemExit(1)
    else:
        mode = ['-i'] if args.fix else ['--dry-run', '--Werror']
        subprocess.run(['clang-format', *mode, *files], cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
