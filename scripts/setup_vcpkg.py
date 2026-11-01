"""Bootstrap pinned vcpkg and generate our NSS overlay from its upstream recipe."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

REPOSITORY = Path(__file__).resolve().parent.parent
SUBMODULE = 'third_party/vcpkg'


def git_output(*arguments, cwd=REPOSITORY):
    return subprocess.check_output(['git', *arguments], cwd=cwd, text=True).strip()


def generate_overlay(upstream, destination):
    """Regenerate from upstream every time; never accumulate local recipe edits."""
    patches = REPOSITORY / 'cmake/patches/nss'
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='qatention-overlay-') as temporary:
        generated = Path(temporary) / 'nss'
        shutil.copytree(upstream / 'ports/nss', generated)
        recipe = generated / 'portfile.cmake'
        recipe.write_text(recipe.read_text())  # Normalize upstream CRLF for git apply.
        subprocess.run(['git', 'apply', '--check', str(patches / 'recipe.patch')],
                       cwd=generated, check=True, capture_output=True, text=True)
        subprocess.run(['git', 'apply', str(patches / 'recipe.patch')],
                       cwd=generated, check=True, capture_output=True, text=True)
        for name in ('mingw-source.patch', 'mingw.cmake', 'macos-generator.patch'):
            shutil.copy2(patches / name, generated / name)
        # Publish only a completely generated overlay. Patch failures stop setup.
        if destination.exists():
            shutil.rmtree(destination)
        shutil.copytree(generated, destination / 'nss')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=REPOSITORY / SUBMODULE,
                        help='Use an existing checkout of the same pinned revision')
    parser.add_argument('--prepare-only', action='store_true',
                        help='Validate the pin and generate the overlay without bootstrapping')
    args = parser.parse_args()
    revision = json.loads((REPOSITORY / 'vcpkg.json').read_text())['builtin-baseline']
    recorded = git_output('ls-files', '--stage', SUBMODULE).split()
    if len(recorded) < 2 or recorded[0] != '160000' or recorded[1] != revision:
        raise SystemExit('vcpkg submodule commit and builtin-baseline must match')
    upstream = args.root.resolve()
    if upstream == REPOSITORY / SUBMODULE and not (upstream / '.git').exists():
        subprocess.run(['git', 'submodule', 'update', '--init', '--', SUBMODULE],
                       cwd=REPOSITORY, check=True)
    if git_output('status', '--porcelain', cwd=upstream):
        raise SystemExit('Refusing to use a modified vcpkg checkout')
    if git_output('rev-parse', 'HEAD', cwd=upstream) != revision:
        raise SystemExit('vcpkg checkout does not match the pinned submodule revision')
    try:
        generate_overlay(upstream, REPOSITORY / '.vcpkg-overlays')
    except subprocess.CalledProcessError as error:
        raise SystemExit(f'NSS overlay patch failed:\n{error.stderr}') from error
    if not args.prepare_only:
        bootstrap = [str(upstream / 'bootstrap-vcpkg.bat')] if os.name == 'nt' else ['bash', 'bootstrap-vcpkg.sh']
        subprocess.run([*bootstrap, '-disableMetrics'], cwd=upstream, check=True)


if __name__ == '__main__':
    main()
