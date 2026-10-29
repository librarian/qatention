import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('release', Path(__file__).parents[1] / 'scripts/prepare_release.py')
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)
SHA = 'a' * 40


class ReleaseValidation(unittest.TestCase):
    def test_inputs(self):
        release.validate_inputs('v0.2.0-preview.1', SHA)
        for tag, sha in [('--draft', SHA), ('v1.0.0;echo bad', SHA), ('v1.0.0', 'main')]:
            with self.assertRaises(ValueError):
                release.validate_inputs(tag, sha)

    def test_only_successful_main_push(self):
        good = dict(id=1, head_sha=SHA, head_branch='main', event='push', status='completed', conclusion='success')
        self.assertEqual(release.select_run([good], SHA), 1)
        for changes in [dict(head_sha='b' * 40), dict(event='pull_request'), dict(head_branch='other'), dict(status='in_progress'), dict(conclusion='failure')]:
            with self.assertRaises(ValueError):
                release.select_run([dict(good, **changes)], SHA)
        with self.assertRaises(ValueError):
            release.select_run([good, dict(good, id=2, conclusion='failure')], SHA)

    def test_missing_expired_duplicate_digest(self):
        good = dict(name='app.zip', expired=False, digest='sha256:' + 'a' * 64)
        self.assertEqual(release.select_artifacts([good], ['app.zip']), {'app.zip': 'a' * 64})
        for artifacts in [[], [dict(good, expired=True)], [good, good], [dict(good, digest='')]]:
            with self.assertRaises(ValueError):
                release.select_artifacts(artifacts, ['app.zip'])

    def test_download_integrity_and_unexpected_files(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            digest = hashlib.sha256(b'original').hexdigest()
            (root / 'release-source.json').write_text(json.dumps({'files': {'app.zip': digest}}))
            (root / 'app.zip').write_bytes(b'changed')
            with self.assertRaises(ValueError):
                release.verify(root)
            (root / 'app.zip').write_bytes(b'original')
            (root / 'extra').touch()
            with self.assertRaises(ValueError):
                release.verify(root)
            (root / 'extra').unlink()
            release.verify(root)
            self.assertEqual((root / 'SHA256SUMS').read_text(), f'{digest}  app.zip\n')
