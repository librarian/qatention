#!/usr/bin/env python3
"""Validate a release's source runs and downloaded artifacts; never publish."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

WORKFLOWS = {
    'build.yml': ('desktop', ['qatention-linux-x64.zip', 'qatention-windows-x64.zip']),
    'macos-cross.yml': ('macos', ['qatention-macos-arm64.zip', 'qatention-macos-x86_64.zip']),
    'android.yml': ('android', ['qatention-android-arm64-preview.apk']),
    'quality.yml': ('quality', []),
}


def validate_inputs(tag, sha):
    if not re.fullmatch(r'v\d+\.\d+\.\d+(?:-[A-Za-z0-9][A-Za-z0-9.-]*)?', tag):
        raise ValueError('Tag must be vX.Y.Z or vX.Y.Z-prerelease')
    if not re.fullmatch(r'[0-9a-f]{40}', sha):
        raise ValueError('Provide the full 40-character commit SHA')


def select_run(runs, sha):
    matches = [r for r in runs if r['head_sha'] == sha
               and r['head_branch'] == 'main' and r['event'] == 'push']
    if not matches:
        raise ValueError('No main push build for this commit')
    latest = max(matches, key=lambda r: r['id'])
    if latest['status'] != 'completed' or latest['conclusion'] != 'success':
        raise ValueError('Latest source run must have completed successfully')
    return latest['id']


def select_artifacts(artifacts, names):
    result = {}
    for name in names:
        matches = [a for a in artifacts if a['name'] == name and not a['expired']]
        if len(matches) != 1:
            raise ValueError(f'Missing, expired or duplicate artifact: {name}')
        digest = matches[0].get('digest', '')
        if not re.fullmatch(r'sha256:[0-9a-f]{64}', digest):
            raise ValueError(f'Missing SHA256 digest: {name}')
        result[name] = digest.removeprefix('sha256:')
    return result


def api(endpoint):
    return json.loads(subprocess.check_output(['gh', 'api', endpoint]))


def prepare(tag, sha, directory):
    validate_inputs(tag, sha)
    repo = os.environ['GITHUB_REPOSITORY']
    files, run_ids = {}, {}
    for workflow, (key, names) in WORKFLOWS.items():
        runs = api(f'repos/{repo}/actions/workflows/{workflow}/runs'
                   f'?head_sha={sha}&branch=main&event=push&per_page=100')['workflow_runs']
        run_id = select_run(runs, sha)
        run_ids[key] = run_id
        if names:
            artifacts = api(f'repos/{repo}/actions/runs/{run_id}/artifacts?per_page=100')['artifacts']
            files.update(select_artifacts(artifacts, names))
    directory.mkdir(parents=True, exist_ok=True)
    (directory / 'release-source.json').write_text(json.dumps(
        {'tag': tag, 'commit': sha, 'runs': run_ids, 'files': files}, indent=2) + '\n')
    with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
        for key, run_id in run_ids.items():
            output.write(f'{key}={run_id}\n')


def verify(directory):
    manifest = json.loads((directory / 'release-source.json').read_text())
    expected = set(manifest['files']) | {'release-source.json'}
    if {p.name for p in directory.iterdir()} != expected:
        raise ValueError('Unexpected or missing release files')
    lines = []
    for name, digest in sorted(manifest['files'].items()):
        actual = hashlib.sha256((directory / name).read_bytes()).hexdigest()
        if actual != digest:
            raise ValueError(f'Artifact digest mismatch: {name}')
        lines.append(f'{digest}  {name}\n')
    (directory / 'SHA256SUMS').write_text(''.join(lines))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['prepare', 'verify'])
    parser.add_argument('--directory', type=Path, default=Path('release-assets'))
    args = parser.parse_args()
    if args.mode == 'prepare':
        prepare(os.environ['RELEASE_TAG'], os.environ['RELEASE_SHA'], args.directory)
    else:
        verify(args.directory)
