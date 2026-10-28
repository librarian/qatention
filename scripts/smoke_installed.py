"""Check an installed app without runtime help from the build-time Qt SDK."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    prefix = Path(sys.argv[1]).resolve()
    executable = prefix / "bin" / ("qatention.exe" if os.name == "nt" else "qatention")
    environment = os.environ.copy()
    qt_root = environment.get("QT_ROOT_DIR")
    if not qt_root:
        raise RuntimeError("QT_ROOT_DIR is required to isolate the installed-package test")
    qt_root = os.path.normcase(os.path.realpath(qt_root))

    def outside_sdk(entry):
        try:
            resolved = os.path.normcase(os.path.realpath(entry))
            return os.path.commonpath([qt_root, resolved]) != qt_root
        except ValueError:  # Different Windows drives cannot share a directory.
            return True

    for key in ("PATH", "LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH"):
        if key in environment:
            environment[key] = os.pathsep.join(
                entry for entry in environment[key].split(os.pathsep)
                if entry and outside_sdk(entry)
            )
    for key in ("QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QML_IMPORT_PATH",
                "QML2_IMPORT_PATH", "QTDIR", "QT_ROOT_DIR", "Qt6_DIR"):
        environment.pop(key, None)
    environment["QT_QPA_PLATFORM"] = "offscreen"
    environment["QT_QUICK_BACKEND"] = "software"
    # Also exclude build/source-directory lookups.
    with tempfile.TemporaryDirectory(prefix="qatention-smoke-") as workdir:
        subprocess.run([str(executable), "--smoke-test"], env=environment,
                       cwd=workdir, check=True, timeout=30)


if __name__ == "__main__":
    main()
