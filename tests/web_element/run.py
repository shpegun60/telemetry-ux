#!/usr/bin/env python3
"""Install real npm packages, then qualify element types, SSR and DOM lifetime. MIT."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inventory(root):
    return {path.relative_to(root).as_posix(): digest(path)
            for path in sorted(root.rglob('*')) if path.is_file() and '__pycache__' not in path.parts}


def inputs():
    files = [ROOT / name for name in ('package.json', 'VERSION', 'LICENSE', 'README.md')]
    files += [path for directory in (ROOT / 'web', HERE) for path in directory.rglob('*')]
    return {path.relative_to(ROOT).as_posix(): digest(path)
            for path in sorted(files) if path.is_file() and '__pycache__' not in path.parts}


def tool(value):
    path = Path(shutil.which(str(value)) or value).absolute()
    if not path.is_file():
        raise ValueError('Missing tool: ' + str(path))
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--sdk-package', type=Path, required=True)
    parser.add_argument('--browser-python', required=True)
    parser.add_argument('--node', required=True)
    parser.add_argument('--tsc', type=Path, required=True)
    parser.add_argument('--npm-js', type=Path)
    args = parser.parse_args()
    output, archive = args.build_dir.resolve(), args.sdk_package.resolve()
    if output == ROOT or ROOT in output.parents or output in ROOT.parents:
        parser.error('--build-dir must be separate from the UX checkout')
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        parser.error('--build-dir must be fresh and empty; nothing is deleted')
    if not archive.is_file() or not args.tsc.is_file():
        parser.error('Provide the SDK npm tarball and TypeScript compiler')
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', PYTHONNOUSERSITE='1',
               npm_config_update_notifier='false')
    env.pop('PYTHONPATH', None)
    env.pop('PYTHONHOME', None)
    report = dict(passed=False, hardware=False, commands=[],
                  started_utc=datetime.now(timezone.utc).isoformat(),
                  source_sha256_before=inputs(), sdk_archive_sha256=digest(archive))

    def run(name, command, cwd=output):
        started = time.monotonic()
        command = list(map(str, command))
        result = subprocess.run(command, cwd=cwd, env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=240)
        log = output / (name + '.log')
        log.write_text(result.stdout, encoding='utf8')
        report['commands'].append(dict(name=name, command=command, returncode=result.returncode,
                                       seconds=time.monotonic()-started,
                                       log=log.name, sha256=digest(log)))
        print(name + ': exit ' + str(result.returncode), flush=True)
        if result.returncode:
            raise RuntimeError(name + ' failed\n' + result.stdout[-14000:])
        return result.stdout

    try:
        node, python = tool(args.node), tool(args.browser_python)
        env['PATH'] = str(node.parent) + os.pathsep + env.get('PATH', '')
        npm = args.npm_js or node.parent.parent / 'lib/node_modules/npm/bin/npm-cli.js'
        if not npm.is_file():
            raise ValueError('Provide the matching npm CLI with --npm-js')
        tools = (node, python, args.tsc.absolute(), npm.absolute())
        report['tool_sha256_before'] = {str(path): digest(path) for path in tools}
        run('node-version', [node, '--version'])
        run('python-version', [python, '--version'])
        run('typescript-version', [node, args.tsc, '--version'])
        dist = output / 'dist'
        dist.mkdir()
        packed = json.loads(run('pack-ux', [node, npm, 'pack', '--json', '--ignore-scripts',
            '--pack-destination', dist, '--cache', output / 'npm-cache'], cwd=ROOT))
        if len(packed) != 1 or packed[0]['name'] != 'telemetry-ux':
            raise RuntimeError('Unexpected UX npm package')
        ux_archive = dist / packed[0]['filename']
        report['ux_archive_sha256'] = digest(ux_archive)
        consumer = output / 'consumer'
        consumer.mkdir()
        (consumer / 'package.json').write_text('{"private":true,"type":"module"}\n', encoding='utf8')
        run('install-local-packages', [node, npm, 'install', '--offline', '--ignore-scripts',
            '--no-audit', '--no-fund', '--cache', output / 'npm-cache', archive, ux_archive], cwd=consumer)
        packages = {name: consumer / 'node_modules' / name for name in ('telemetry-sdk', 'telemetry-ux')}
        for name, path in packages.items():
            metadata = json.loads((path / 'package.json').read_text(encoding='utf8'))
            if metadata['name'] != name or metadata['version'] != '0.1.0':
                raise RuntimeError('Wrong installed package: ' + name)
        report['installed_npm_sha256_before'] = {name: inventory(path) for name, path in packages.items()}
        for name in ('Consumer.ts', 'SSR.mjs', 'Element.mjs'):
            shutil.copyfile(HERE / name, consumer / name)
        run('strict-types', [node, args.tsc, '--strict', '--noEmit', '--target', 'ES2022',
            '--module', 'NodeNext', '--moduleResolution', 'NodeNext', consumer / 'Consumer.ts'])
        ssr = json.loads(run('ssr-import', [node, consumer / 'SSR.mjs']))
        if ssr != {'passed': True, 'checks': 16}:
            raise RuntimeError('SSR import controls incomplete')
        report['ssr'] = ssr
        (consumer / 'index.html').write_text('''<!doctype html><html><head>
<meta charset="utf-8"><title>Installed custom element qualification</title>
<link rel="stylesheet" href="/ux/Configurator.css">
<script type="importmap">{"imports":{"telemetry-ux/web":"/ux/index.js",
"telemetry-sdk/client":"/sdk/Client.js","telemetry-sdk/runtime":"/sdk/runtime.js"}}</script>
</head><body></body></html>
''', encoding='utf8')
        run('browser', [python, '-B', HERE / 'Browser.py', '--public-root', consumer,
            '--build-dir', output / 'browser'])
        browser = json.loads((output / 'browser/summary.json').read_text(encoding='utf8'))
        if (browser.get('passed') is not True or
                browser.get('element', {}).get('checks', 0) < 283 or
                browser.get('workspace', {}).get('passed') is not True or
                browser.get('workspace', {}).get('checks', 0) < 13):
            raise RuntimeError('Browser lifecycle controls failed')
        report['browser'] = browser
        report['source_sha256_after'] = inputs()
        report['installed_npm_sha256_after'] = {name: inventory(path) for name, path in packages.items()}
        report['tool_sha256_after'] = {str(path): digest(path) for path in tools}
        for before, after in (('source_sha256_before', 'source_sha256_after'),
                              ('installed_npm_sha256_before', 'installed_npm_sha256_after'),
                              ('tool_sha256_before', 'tool_sha256_after')):
            if report[before] != report[after]:
                raise RuntimeError(before + ' changed during qualification')
        if digest(archive) != report['sdk_archive_sha256']:
            raise RuntimeError('SDK npm archive changed')
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        report['finished_utc'] = datetime.now(timezone.utc).isoformat()
        (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')


if __name__ == '__main__':
    main()
