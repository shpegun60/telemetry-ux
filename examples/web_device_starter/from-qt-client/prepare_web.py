#!/usr/bin/env python3
"""Prepare this application's browser UI from local SDK/UX npm packages. MIT.

All generated files live in a fresh external directory; no native device process
is built here. The existing UX exporter owns dependency closure and asset hashes.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import tarfile

APP = Path(__file__).resolve().parents[1]
WORKSPACE = APP.parent
ASSETS = ('index.html', 'DeviceApp.js', 'favicon.svg')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--sdk-source', type=Path, default=WORKSPACE / 'telemetry-sdk')
    parser.add_argument('--ux-source', type=Path, default=WORKSPACE / 'telemetry-ux')
    parser.add_argument('--node', default='node')
    parser.add_argument('--npm-cli', type=Path, help='Path to npm/bin/npm-cli.js')
    args = parser.parse_args()
    output = args.output.resolve()
    sdk, ux = args.sdk_source.resolve(), args.ux_source.resolve()
    if output.exists() or any(output == root or root in output.parents
                              for root in (APP, sdk, ux)):
        parser.error('--output must be a fresh directory outside application and library sources')
    node = shutil.which(args.node)
    if not node:
        parser.error('Node.js is required to prepare assets; pass --node')
    npm_cli = args.npm_cli
    if npm_cli is None:
        npm = shutil.which('npm')
        if npm:
            candidates = (Path(npm).resolve(), Path(npm).parent / 'node_modules/npm/bin/npm-cli.js')
            npm_cli = next((path for path in candidates if path.name == 'npm-cli.js' and path.is_file()), None)
    if npm_cli is None or not npm_cli.is_file():
        parser.error('Cannot locate npm-cli.js; pass --npm-cli explicitly')
    exporter = ux / 'tools/export_web_assets.py'
    if not exporter.is_file() or any(not (root / 'package.json').is_file() for root in (sdk, ux)):
        parser.error('Matching SDK/UX source packages and the UX asset exporter are required')
    inputs = {str(path): sha(path) for path in [Path(__file__), exporter, *[APP / 'web' / name for name in ASSETS]]}
    output.mkdir(parents=True)
    packages = output / 'packages'
    packages.mkdir()
    environment = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', npm_config_update_notifier='false')
    report = dict(passed=False, hardware=False, packages={}, source_sha256=inputs)

    def run(name, command, cwd=None):
        result = subprocess.run(list(map(str, command)), cwd=cwd, env=environment,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True, encoding='utf8', errors='replace', timeout=120)
        (output / (name + '.stdout.log')).write_text(result.stdout, encoding='utf8')
        (output / (name + '.stderr.log')).write_text(result.stderr, encoding='utf8')
        if result.returncode:
            raise RuntimeError(f'{name} failed ({result.returncode}); see {output}')
        return result.stdout

    try:
        command = [node, npm_cli]
        for name, source in (('sdk', sdk), ('ux', ux)):
            # npm's explicit package files lists keep developer notes out of the
            # package. Validate member names before installing any tarball.
            packed = json.loads(run('pack-' + name, command + [
                'pack', '--ignore-scripts', '--json', '--pack-destination', packages], source))
            if len(packed) != 1:
                raise RuntimeError('Expected exactly one package')
            package = packages / packed[0]['filename']
            if package.parent != packages or not package.is_file():
                raise RuntimeError('Unexpected package filename')
            with tarfile.open(package, 'r:gz') as archive:
                for member in archive.getmembers():
                    path = PurePosixPath(member.name)
                    lower = tuple(part.casefold() for part in path.parts)
                    if (path.is_absolute() or '..' in path.parts or not path.parts or
                            path.parts[0] != 'package' or not (member.isfile() or member.isdir()) or
                            lower[-1] == 'claude.md' or lower[-2:] == ('doc', '123.txt')):
                        raise RuntimeError('Unsafe/unwanted package member: ' + member.name)
            report['packages'][name] = dict(path=str(package), sha256=sha(package))
        public = output / 'public'
        public.mkdir()
        (public / 'package.json').write_text('{"private":true,"type":"module"}\n', encoding='utf8')
        run('install', command + ['install', '--offline', '--ignore-scripts', '--no-audit', '--no-fund',
            '--package-lock=false', '--prefix', public, '--cache', output / 'npm-cache',
            *[entry['path'] for entry in report['packages'].values()]])
        for name in ASSETS:
            shutil.copy2(APP / 'web' / name, public / name)
        run('export', [sys.executable, '-B', exporter, '--public-root', public,
            '--output', output / 'www', *[arg for name in ASSETS for arg in ('--asset', name)]])
        if any(sha(Path(path)) != value for path, value in inputs.items()):
            raise RuntimeError('An application/exporter source changed during preparation')
        report['manifest_sha256'] = sha(output / 'www/assets.json')
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        (output / 'prepared.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(json.dumps(dict(passed=True, www=str(output / 'www'), manifest_sha256=report['manifest_sha256'])))


if __name__ == '__main__':
    main()
