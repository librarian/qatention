"""Deploy a MinGW cross-build with Qt's Windows deploy tool under Wine, then test it."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
for name in ('qt', 'openssl', 'build', 'dest'):
    parser.add_argument('--' + name, required=True, type=Path)
parser.add_argument('--wine', default='wine')
args = parser.parse_args()
qt, openssl, build, dest = (getattr(args, name).resolve() for name in ('qt', 'openssl', 'build', 'dest'))
bin_dir = dest / 'bin'

def windows(path):
    return 'Z:' + str(Path(path).resolve()).replace('/', '\\')

environment = os.environ.copy()
for key in ('QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QML_IMPORT_PATH',
            'QML2_IMPORT_PATH', 'QTDIR', 'QT_ROOT_DIR', 'Qt6_DIR', 'WINEPATH'):
    environment.pop(key, None)
environment.update(WINEDEBUG='-all', QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software')
subprocess.run([args.wine, str(qt / 'bin/windeployqt.exe'), '--release', '--no-translations',
                '--no-compiler-runtime',
                windows(bin_dir / 'qatention.exe')], env=environment, check=True, timeout=120)
for name in ('libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll'):
    library = Path(subprocess.check_output(['x86_64-w64-mingw32-g++-posix', '-print-file-name=' + name], text=True).strip())
    if not library.is_file():
        raise RuntimeError('Missing MinGW runtime: ' + name)
    shutil.copy2(library, bin_dir / name)
for name in ('libcrypto-3-x64.dll', 'libssl-3-x64.dll'):
    shutil.copy2(openssl / 'bin' / name, bin_dir / name)
(bin_dir / 'tls').mkdir(exist_ok=True)
shutil.copy2(qt / 'plugins/tls/qopensslbackend.dll', bin_dir / 'tls')
licenses = dest / 'licenses'
licenses.mkdir(exist_ok=True)
shutil.copy2(openssl / 'licenses/OpenSSL.txt', licenses)
(bin_dir / 'qt.conf').write_text('[Paths]\nPrefix=.\nPlugins=.\nQmlImports=qml\n')
# Test from an unrelated working directory, with no SDK runtime search paths.
with tempfile.TemporaryDirectory(prefix='qatention-wine-') as work:
    test_runtime = bin_dir / 'Qt6Test.dll'
    shutil.copy2(qt / 'bin/Qt6Test.dll', test_runtime)
    try:
        for name in ('core_tests', 'ui_tests', 'certificatechain_tests'):
            tests = bin_dir / (name + '.exe')
            shutil.copy2(build / tests.name, tests)
            try:
                report = Path(work) / (name + '.txt')
                try:
                    result = subprocess.run([args.wine, str(tests), '-o', windows(report) + ',txt'],
                                            cwd=work, env=environment, timeout=90)
                finally:
                    if report.exists():
                        print(report.read_text(), flush=True)
                result.check_returncode()
            finally:
                tests.unlink(missing_ok=True)
    finally:
        test_runtime.unlink(missing_ok=True)
    subprocess.run([args.wine, str(bin_dir / 'qatention.exe'), '--smoke-test'],
                   cwd=work, env=environment, check=True, timeout=45)

subprocess.run([args.wine, str(bin_dir / 'qatention-capture.exe'), '--check-native-decoder'], env=environment, check=True, timeout=30)
