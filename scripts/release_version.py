#!/usr/bin/env python3
"""Prepare a version commit and dispatch its builds, without publishing."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import time

from prepare_release import WORKFLOWS, api, validate_inputs


def git(*args):
    return subprocess.check_output(['git', *args], text=True).strip()


def marker(sha, request):
    validate_inputs('v0.0.0', sha)
    if not re.fullmatch(r'[0-9]+-[0-9]+', request):
        raise ValueError('Invalid release correlation ID')
    return f'Release {sha} ({request})'


def check_source(sha, request):
    marker(sha, request)
    if os.environ.get('GITHUB_REF') != 'refs/heads/main':
        raise ValueError('Release builds must be dispatched on main')
    if git('rev-parse', 'HEAD') != sha:
        raise ValueError('Checkout does not match requested commit')
    subprocess.run(['git', 'merge-base', '--is-ancestor', sha, 'origin/main'], check=True)


def bump(tag):
    validate_inputs(tag, '0' * 40)
    if os.environ.get('GITHUB_REF') != 'refs/heads/main':
        raise ValueError('Release preparation must run on main')
    if git('status', '--porcelain'):
        raise ValueError('Release checkout must be clean')
    git('fetch', 'origin', 'main', '--tags')
    git('checkout', '--detach', 'origin/main')
    existing = subprocess.run(['git', 'show-ref', '--verify', '--quiet', f'refs/tags/{tag}'])
    if existing.returncode == 0:
        raise ValueError('Tag already exists; existing releases are never overwritten')
    if existing.returncode != 1:
        raise ValueError('Could not check existing tags')
    # One source for all build metadata. Re-running a failed preparation can
    # reuse the version commit, but cannot replace an existing tag.
    version = tag.removeprefix('v') + '\n'
    if Path('VERSION').read_text() != version:
        Path('VERSION').write_text(version)
        git('add', 'VERSION')
        git('-c', 'user.name=github-actions[bot]',
            '-c', 'user.email=41898282+github-actions[bot]@users.noreply.github.com',
            'commit', '-m', f'Prepare release {tag}')
        # A concurrent main push rejects this ordinary fast-forward push.
        git('push', 'origin', 'HEAD:refs/heads/main')
    sha = git('rev-parse', 'HEAD')
    with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
        output.write(f'sha={sha}\n')
    return sha


def matching_run(runs, sha, request):
    title = marker(sha, request)
    matches = [run for run in runs if run.get('event') == 'workflow_dispatch'
               and run.get('head_branch') == 'main'
               and run.get('display_title') == title]
    if len(matches) > 1:
        raise ValueError('Duplicate release builds for the same request')
    if not matches:
        return None
    run = matches[0]
    if run['status'] != 'completed':
        return None
    if run['conclusion'] != 'success':
        raise ValueError(f"Release build failed: {run.get('html_url', run['id'])}")
    return run['id']


def build(sha, request, timeout=4500):
    marker(sha, request)
    repo = os.environ['GITHUB_REPOSITORY']
    for workflow in WORKFLOWS:
        subprocess.run(['gh', 'workflow', 'run', workflow, '--repo', repo,
                        '--ref', 'main', '-f', f'release_sha={sha}',
                        '-f', f'release_id={request}'], check=True)
    pending, completed = set(WORKFLOWS), {}
    deadline = time.monotonic() + timeout
    while pending and time.monotonic() < deadline:
        for workflow in sorted(pending):
            runs = api(f'repos/{repo}/actions/workflows/{workflow}/runs'
                       '?branch=main&event=workflow_dispatch&per_page=100')['workflow_runs']
            run_id = matching_run(runs, sha, request)
            if run_id is not None:
                completed[workflow] = run_id
                pending.remove(workflow)
                print(f'{workflow} passed: {run_id}', flush=True)
        if pending:
            print('Waiting for: ' + ', '.join(sorted(pending)), flush=True)
            time.sleep(20)
    if pending:
        raise TimeoutError('Release build wait timed out')
    Path('release-runs.json').write_text(json.dumps(
        {'commit': sha, 'request': request, 'runs': completed}, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['bump', 'build', 'check-source'])
    args = parser.parse_args()
    if args.mode == 'bump':
        bump(os.environ['RELEASE_TAG'])
    elif args.mode == 'build':
        build(os.environ['RELEASE_SHA'], os.environ['RELEASE_ID'])
    else:
        check_source(os.environ['RELEASE_SHA'], os.environ['RELEASE_ID'])
