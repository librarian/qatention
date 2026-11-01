"""Check that generated overlays track upstream and fail closed on patch drift."""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('setup_vcpkg', ROOT / 'scripts/setup_vcpkg.py')
setup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(setup)


@unittest.skipUnless((ROOT / 'third_party/vcpkg/ports/nss/portfile.cmake').exists(),
                     'Initialize the pinned vcpkg submodule first')
class OverlayTests(unittest.TestCase):
    def test_regenerates_from_upstream_without_modifying_it(self):
        upstream = ROOT / 'third_party/vcpkg'
        original = (upstream / 'ports/nss/portfile.cmake').read_bytes()
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary) / 'overlay'
            setup.generate_overlay(upstream, destination)
            generated = destination / 'nss/portfile.cmake'
            expected = generated.read_bytes()
            self.assertIn(b'include("${CURRENT_PORT_DIR}/mingw.cmake")', expected)
            generated.write_text('unwanted local edit')
            setup.generate_overlay(upstream, destination)
            self.assertEqual(generated.read_bytes(), expected)
        self.assertEqual((upstream / 'ports/nss/portfile.cmake').read_bytes(), original)

    def test_patch_drift_fails_without_replacing_previous_overlay(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            upstream = root / 'upstream'
            shutil.copytree(ROOT / 'third_party/vcpkg/ports/nss', upstream / 'ports/nss')
            (upstream / 'ports/nss/portfile.cmake').write_text('changed upstream recipe\n')
            destination = root / 'overlay'
            destination.mkdir()
            marker = destination / 'previous'
            marker.write_text('retained')
            with self.assertRaises(subprocess.CalledProcessError):
                setup.generate_overlay(upstream, destination)
            self.assertEqual(marker.read_text(), 'retained')


if __name__ == '__main__':
    unittest.main()
