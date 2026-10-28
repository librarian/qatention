"""Package and statically verify a macOS cross-build without running target code."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--qt', required=True, type=Path)
parser.add_argument('--dest', required=True, type=Path)
parser.add_argument('--tools', required=True, type=Path)
parser.add_argument('--arch', choices=('arm64', 'x86_64'), required=True)
args = parser.parse_args()
qt, dest, tools = args.qt.resolve(), args.dest.resolve(), args.tools.resolve()
bundle = dest / 'qatention.app'
contents = bundle / 'Contents'
frameworks = contents / 'Frameworks'
plugins = contents / 'PlugIns'
resources = contents / 'Resources'
frameworks.mkdir(exist_ok=True)
resources.mkdir(exist_ok=True)
cli = dest / 'bin/qatention-capture'
shutil.move(cli, contents / 'MacOS/qatention-capture')
otool = tools / (args.arch + '-apple-darwin23.5-otool')
lipo = tools / (args.arch + '-apple-darwin23.5-lipo')
signer = tools / 'rcodesign'
queue = list((contents / 'MacOS').iterdir())

for category in ('platforms', 'styles', 'imageformats', 'iconengines', 'tls', 'networkinformation'):
    origin = qt / 'plugins' / category
    if not origin.exists():
        continue
    for path in origin.glob('*.dylib'):
        if category == 'platforms' and path.name not in ('libqcocoa.dylib', 'libqoffscreen.dylib'):
            continue
        if category == 'tls' and 'openssl' in path.name:
            continue
        target = plugins / category / path.name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
        queue.append(target)

def dependencies(path):
    output = subprocess.check_output([str(otool), '-L', str(path)], text=True)
    return set(re.findall(r'^\s+(\S+)\s+\(compatibility version', output, re.M))

copied = set()
checked = set()
while queue:
    binary = queue.pop()
    if binary in checked:
        continue
    checked.add(binary)
    # Frameworks/plugins from Qt may be universal; keep only the requested slice.
    architectures = subprocess.check_output([str(lipo), '-archs', str(binary)], text=True).split()
    if args.arch not in architectures:
        raise RuntimeError(f'{binary}: missing {args.arch}: {architectures}')
    if len(architectures) > 1:
        thin = binary.with_name(binary.name + '.thin')
        subprocess.run([str(lipo), str(binary), '-thin', args.arch, '-output', str(thin)], check=True)
        thin.chmod(binary.stat().st_mode)
        thin.replace(binary)
    for dependency in dependencies(binary):
        if dependency.startswith(('/System/Library/', '/usr/lib/')):
            continue
        if dependency.startswith('@rpath/') and '.framework/' in dependency:
            name = dependency.split('/')[1]
            if name not in copied:
                copied.add(name)
                source = qt / 'lib' / name
                if not source.is_dir():
                    raise RuntimeError(f'Unresolved dependency: {dependency}')
                target = frameworks / name
                shutil.copytree(source, target, symlinks=True,
                                ignore=shutil.ignore_patterns('Headers', '*.prl', '*.dSYM', '_CodeSignature'))
                queue.append(target / 'Versions/A' / name.removesuffix('.framework'))
        else:
            raise RuntimeError(f'{binary}: unexpected external dependency {dependency}')

# Qt resolves a bundle's Prefix relative to Contents, not Contents/MacOS.
(resources / 'qt.conf').write_text('[Paths]\nPrefix=.\nPlugins=PlugIns\n')
# Fallback for the CLI if CoreFoundation does not recognize it as the main bundle.
(contents / 'MacOS/qt.conf').write_text('[Paths]\nPrefix=..\nPlugins=PlugIns\n')
if not (plugins / 'platforms/libqcocoa.dylib').is_file():
    raise RuntimeError('Cocoa platform plugin missing')
if not (plugins / 'tls/libqsecuretransportbackend.dylib').is_file():
    raise RuntimeError('macOS native TLS backend missing')

# Use an ad-hoc signature: no Developer ID credentials or notarization implied.
subprocess.run([str(signer), 'sign', str(bundle)], check=True)
for binary in sorted(checked):
    subprocess.run([str(signer), 'verify', str(binary)], check=True)
for link in bundle.rglob('*'):
    if link.is_symlink() and not link.exists():
        raise RuntimeError(f'Broken bundle link: {link}')
print(f'Validated {len(checked)} Mach-O files for {args.arch}; no runtime test on Linux.')
(dest / 'MACOS-TESTING.txt').write_text(
    'Requires macOS 12 or newer.\n'
    'Extract the ZIP and open qatention.app. This is an ad-hoc-signed test build,\n'
    'not Developer-ID signed or notarized. If macOS blocks it, use System Settings >\n'
    'Privacy & Security > Open Anyway after attempting to open it.\n'
    'CLI: qatention.app/Contents/MacOS/qatention-capture --check-native-decoder\n'
    'Please check login, video, input, reconnect, ISO mount/unmount and debug logging.\n')
