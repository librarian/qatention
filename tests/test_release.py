import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import os
import subprocess
import sys
from unittest.mock import patch
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


sys.path.insert(0, str(Path(__file__).parents[1] / 'scripts'))
import release_version


class ReleasePreparation(unittest.TestCase):
    def test_correlated_builds(self):
        good = dict(id=3, event='workflow_dispatch', head_branch='main',
                    display_title=release_version.marker(SHA, '123-1'),
                    status='completed', conclusion='success')
        self.assertEqual(release_version.matching_run([good], SHA, '123-1'), 3)
        for changes in [dict(event='push'), dict(head_branch='other'),
                        dict(display_title='unrelated'), dict(status='queued')]:
            self.assertIsNone(release_version.matching_run([dict(good, **changes)], SHA, '123-1'))
        for runs in [[good, good], [dict(good, conclusion='failure')],
                     [dict(good, conclusion='cancelled')]]:
            with self.assertRaises(ValueError):
                release_version.matching_run(runs, SHA, '123-1')
        with self.assertRaises(ValueError):
            release_version.marker(SHA, 'bad;input')

    def test_dispatch_and_wait(self):
        with tempfile.TemporaryDirectory() as temp, patch.dict(os.environ, GITHUB_REPOSITORY='owner/repo'):
            old = os.getcwd()
            os.chdir(temp)
            try:
                good = dict(id=3, event='workflow_dispatch', head_branch='main',
                            display_title=release_version.marker(SHA, '123-1'),
                            status='completed', conclusion='success')
                with patch.object(release_version.subprocess, 'run') as dispatch, patch.object(
                        release_version, 'api', return_value={'workflow_runs': [good]}):
                    release_version.build(SHA, '123-1')
                self.assertEqual(dispatch.call_count, 4)
                for call in dispatch.call_args_list:
                    self.assertIn('release_sha=' + SHA, call.args[0])
                manifest = json.loads(Path('release-runs.json').read_text())
                self.assertEqual(manifest['commit'], SHA)
                self.assertEqual(set(manifest['runs']), set(release.WORKFLOWS))
                with patch.object(release_version.subprocess, 'run'), patch.object(
                        release_version, 'api', return_value={'workflow_runs': []}):
                    with self.assertRaises(TimeoutError):
                        release_version.build(SHA, '123-1', timeout=0)
            finally:
                os.chdir(old)

    def test_promotion_rejects_version_or_source_mismatch(self):
        with tempfile.TemporaryDirectory() as temp, patch.dict(
                os.environ, GITHUB_REPOSITORY='owner/repo'), patch.object(
                release.subprocess, 'check_output', return_value='0.4.0\n'):
            with self.assertRaises(ValueError):
                release.prepare('v0.5.0', SHA, Path(temp))
            with self.assertRaises(ValueError):
                release.prepare('v0.4.0', SHA, Path(temp), {'commit': 'b' * 40})
            sources = {'commit': SHA, 'request': '123-1',
                       'runs': {key: 3 for key in release.WORKFLOWS}}
            with patch.object(release, 'api', return_value={'path': 'wrong.yml'}):
                with self.assertRaises(ValueError):
                    release.prepare('v0.4.0', SHA, Path(temp), sources)

    def test_bump_local_git_and_reject_existing_tag(self):
        # Exercise actual commit/push logic using only a temporary local remote.
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            def run(*args):
                return subprocess.check_output(['git', *args], cwd=root, stderr=subprocess.DEVNULL, text=True).strip()
            run('init', '--bare', '--initial-branch=main', 'remote')
            run('clone', str(root / 'remote'), 'work')
            old = os.getcwd()
            os.chdir(root / 'work')
            try:
                Path('VERSION').write_text('0.3.0\n')
                release_version.git('add', 'VERSION')
                release_version.git('-c', 'user.name=Test', '-c', 'user.email=test@example.invalid',
                                    'commit', '-m', 'initial')
                release_version.git('push', 'origin', 'main')
                with patch.dict(os.environ, GITHUB_REF='refs/heads/main', GITHUB_OUTPUT=str(root / 'output')):
                    sha = release_version.bump('v0.4.0-rc.1')
                    self.assertEqual(Path('VERSION').read_text(), '0.4.0-rc.1\n')
                    self.assertEqual(release_version.git('rev-parse', 'origin/main'), sha)
                    self.assertEqual(release_version.bump('v0.4.0-rc.1'), sha)
                    release_version.git('tag', 'v0.4.0-rc.1')
                    with self.assertRaises(ValueError):
                        release_version.bump('v0.4.0-rc.1')
                    with self.assertRaises(ValueError):
                        release_version.check_source('b' * 40, '123-1')
                    release_version.check_source(sha, '123-1')
            finally:
                os.chdir(old)
