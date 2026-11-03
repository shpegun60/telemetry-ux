#!/usr/bin/env python3
"""Qualify the payload tools using installed C++ and npm SDK packages. MIT.

Authors: Ruslan Kovtun (shpegun60), codexAi. No hardware is accessed.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inventory(root):
    return {path.relative_to(root).as_posix(): digest(path)
            for path in sorted(root.rglob('*'))
            if path.is_file() and '__pycache__' not in path.parts}


def sources():
    return {path.relative_to(ROOT).as_posix(): digest(path)
            for directory in (ROOT / 'examples/structured_client', HERE)
            for path in sorted(directory.rglob('*'))
            if path.is_file() and '__pycache__' not in path.parts}


def tool(value):
    # Driver spelling and a virtual environment's Python path are significant.
    path = Path(shutil.which(str(value)) or value).absolute()
    if not path.is_file():
        raise ValueError('Missing tool: ' + str(path))
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--sdk-prefix', type=Path, required=True)
    parser.add_argument('--sdk-package', type=Path, required=True)
    parser.add_argument('--browser-python', required=True)
    parser.add_argument('--node', required=True)
    parser.add_argument('--cxx', required=True)
    parser.add_argument('--cmake', required=True)
    parser.add_argument('--ninja', required=True)
    parser.add_argument('--npm-js', type=Path)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    output, prefix, archive = (path.resolve() for path in
                               (args.build_dir, args.sdk_prefix, args.sdk_package))
    if not prefix.is_dir() or not archive.is_file():
        parser.error('Provide an installed C++ SDK prefix and an SDK npm tarball')
    if any(output == path or path in output.parents or output in path.parents
           for path in (ROOT, prefix)):
        parser.error('--build-dir must be separate from UX and the installed SDK')
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        parser.error('--build-dir must be fresh and empty; nothing is deleted')
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', PYTHONNOUSERSITE='1',
               npm_config_update_notifier='false')
    env.pop('PYTHONPATH', None)
    env.pop('PYTHONHOME', None)
    if args.sanitize:
        env['ASAN_OPTIONS'] = 'detect_leaks=1:halt_on_error=1'
        env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
    report = dict(passed=False, hardware=False, sanitize=args.sanitize, checks=0,
                  started_utc=datetime.now(timezone.utc).isoformat(), commands=[],
                  source_sha256_before=sources(), installed_sdk_sha256_before=inventory(prefix),
                  sdk_npm_archive_sha256=digest(archive))

    def check(condition, message):
        report['checks'] += 1
        if not condition:
            raise RuntimeError(message)

    def run(name, command, cwd=output):
        command = list(map(str, command))
        started = time.monotonic()
        try:
            result = subprocess.run(command, cwd=cwd, env=env, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=240)
            text, code = result.stdout, result.returncode
        except subprocess.TimeoutExpired as error:
            text = error.stdout or ''
            if isinstance(text, bytes):
                text = text.decode('utf8', errors='replace')
            code = None
        log = output / (name + '.log')
        log.write_text(text, encoding='utf8')
        report['commands'].append(dict(name=name, command=command, returncode=code,
                                       seconds=time.monotonic()-started,
                                       log=log.name, sha256=digest(log)))
        print(name + ': exit ' + str(code), flush=True)
        check(code == 0, name + ' failed\n' + text[-14000:])
        return text

    try:
        tools = {name: tool(getattr(args, name)) for name in
                 ('node', 'browser_python', 'cxx', 'cmake', 'ninja')}
        env['PATH'] = os.pathsep.join((str(tools['node'].parent), str(tools['cxx'].parent),
                                      env.get('PATH', '')))
        report['tools_sha256_before'] = {str(path): digest(path) for path in tools.values()}
        for name, path in tools.items():
            run(name + '-version', [path, '--version'])
        npm = args.npm_js or tools['node'].parent.parent / 'lib/node_modules/npm/bin/npm-cli.js'
        check(npm.is_file(), 'Provide the matching Node npm CLI with --npm-js if needed')
        configs, identities = list(prefix.rglob('TelemetryConfig.cmake')), list(prefix.rglob('SDKMetadata.json'))
        check(len(configs) == 1 and len(identities) == 1, 'Require one installed SDK package and identity')
        report['sdk_metadata'] = json.loads(identities[0].read_text(encoding='utf8'))
        check(report['sdk_metadata'].get('sdkVersion') == '0.1.0', 'SDK version mismatch')
        flags = '-Wall -Wextra -Wpedantic -Werror'
        if args.sanitize:
            flags += (' -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all'
                      ' -fno-omit-frame-pointer')
        alternate = output / 'alternate-sdk'
        shutil.copytree(prefix, alternate)
        original = env.get('Telemetry_ROOT')
        env['Telemetry_ROOT'] = str(alternate)
        build = output / 'native'
        try:
            run('native-configure', [tools['cmake'], '-S', ROOT / 'examples/structured_client',
                '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
                '-DCMAKE_PREFIX_PATH=' + str(prefix), '-DTelemetry_DIR=' + str(configs[0].parent),
                '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF', '-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF',
                '-DCMAKE_CXX_COMPILER=' + str(tools['cxx']),
                '-DCMAKE_MAKE_PROGRAM=' + str(tools['ninja']),
                '-DCMAKE_CXX_FLAGS=' + flags, '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'])
        finally:
            if original is None:
                env.pop('Telemetry_ROOT', None)
            else:
                env['Telemetry_ROOT'] = original
        cache = (build / 'CMakeCache.txt').read_text(encoding='utf8').splitlines()
        selected = [line.partition('=')[2] for line in cache if line.startswith('Telemetry_DIR:')]
        check(len(selected) == 1 and Path(selected[0]).resolve() == configs[0].parent.resolve(),
              'The explicit installed SDK must win over a real competing Telemetry_ROOT')
        report['competing_sdk_root_ignored'] = True
        run('native-build', [tools['cmake'], '--build', build, '--parallel', '2'])
        commands = json.loads((build / 'compile_commands.json').read_text(encoding='utf8'))
        check(bool(commands), 'Missing native compile command proof')
        for entry in commands:
            check(Path(entry['file']).resolve() == (ROOT / 'examples/structured_client/Device.cpp').resolve(),
                  'Native consumer may compile only its own application source')
        device = build / ('telemetry_structured_device.exe' if os.name == 'nt' else 'telemetry_structured_device')
        check(device.is_file(), 'Missing native helper')
        fixtures = output / 'fixtures'
        run('native-export', [device, fixtures])
        report['executable'] = dict(path=str(device), sha256=digest(device), bytes=device.stat().st_size)
        report['native_fixture_sha256'] = inventory(fixtures)

        consumer = output / 'npm-consumer'
        consumer.mkdir()
        (consumer / 'package.json').write_text('{"private":true,"type":"module"}\n', encoding='utf8')
        run('install-npm-sdk', [tools['node'], npm, 'install', '--offline', '--ignore-scripts',
            '--no-audit', '--no-fund', '--cache', output / 'npm-cache', archive], cwd=consumer)
        package = consumer / 'node_modules/telemetry-sdk'
        package_metadata = json.loads((package / 'package.json').read_text(encoding='utf8'))
        check(package_metadata['name'] == 'telemetry-sdk' and package_metadata['version'] == '0.1.0',
              'npm must install the explicit matching SDK archive')
        report['installed_npm_sha256_before'] = inventory(package)
        consumer_script = consumer / 'Consumer.mjs'
        consumer_script.write_text('''import assert from 'node:assert/strict';
import fs from 'node:fs';
import {DescriptorCache, decodeValues} from 'telemetry-sdk/runtime';
const directory = process.argv[2];
const model = new DescriptorCache().load(fs.readFileSync(directory + '/descriptor.bin'));
const values = decodeValues(model, fs.readFileSync(directory + '/values.bin'));
assert.equal(model.fields.length, 1);
assert.equal(model.services.length, 3);
assert.equal(values[0].status, 0);
assert.equal(values[0].value.serial, 18446744073709551615n);
assert.equal(values[0].value.offset, -9223372036854775808n);
assert.ok(Object.is(values[0].value.gain, -0));
assert.deepEqual([...values[0].value.samples], [1, 256, 65535]);
console.log(JSON.stringify({passed:true,checks:7,runtime:import.meta.resolve('telemetry-sdk/runtime')}));
''', encoding='utf8')
        node_report = json.loads(run('installed-runtime-consumer', [tools['node'], consumer_script, fixtures]))
        check(node_report.get('passed') is True and node_report.get('checks') == 7 and
              node_report.get('runtime') == (package / 'web/telemetry.js').as_uri(),
              'Consumer must import the installed npm runtime and inspect exact native values')
        report['node_consumer'] = node_report
        browser = run('browser', [tools['browser_python'], '-B', HERE / 'Browser.py',
            '--device', device, '--build-dir', fixtures, '--sdk-package-root', package])
        matched = re.search(r'Structured browser: (\d+) checks, 0 failures', browser)
        check(matched is not None and int(matched[1]) >= 41, 'Browser did not execute its qualification floor')
        report['browser_checks'] = int(matched[1])
        report['installed_npm_sha256_after'] = inventory(package)
        check(report['installed_npm_sha256_before'] == report['installed_npm_sha256_after'],
              'Installed npm SDK inputs changed')
        report['source_sha256_after'] = sources()
        report['installed_sdk_sha256_after'] = inventory(prefix)
        report['tools_sha256_after'] = {str(path): digest(path) for path in tools.values()}
        for before, after in (('source_sha256_before', 'source_sha256_after'),
                              ('installed_sdk_sha256_before', 'installed_sdk_sha256_after'),
                              ('tools_sha256_before', 'tools_sha256_after')):
            check(report[before] == report[after], before + ' changed during qualification')
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        report['finished_utc'] = datetime.now(timezone.utc).isoformat()
        (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')


if __name__ == '__main__':
    main()
