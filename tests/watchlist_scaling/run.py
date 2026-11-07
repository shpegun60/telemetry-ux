#!/usr/bin/env python3
"""Installed 65-Field host Watchlist, bounded native chunks and actual browser controls. MIT."""
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
DEFAULT_DESCRIPTOR_SHA256 = '9f503f564f119eb5416142f0e558ba789d5086b7149cfcc2320cfacc137fdc94'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inventory(root):
    return {p.relative_to(root).as_posix(): digest(p) for p in sorted(root.rglob('*'))
            if p.is_file() and '__pycache__' not in p.parts}


def sources():
    files = [ROOT / 'package.json']
    for directory in (ROOT / 'web', ROOT / 'examples/web_demo', HERE):
        files += [p for p in directory.rglob('*') if p.is_file() and '__pycache__' not in p.parts]
    return {p.relative_to(ROOT).as_posix(): digest(p) for p in sorted(files)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True, type=Path)
    parser.add_argument('--sdk-prefix', required=True, type=Path)
    parser.add_argument('--sdk-root', type=Path, default=ROOT.parent / 'telemetry-sdk')
    parser.add_argument('--sdk-package', type=Path)
    parser.add_argument('--codegen-python', required=True)
    parser.add_argument('--browser-python', required=True)
    parser.add_argument('--node', required=True)
    parser.add_argument('--npm-js', required=True, type=Path)
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ninja', default='ninja')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    output, sdk, sdk_root = args.build_dir.resolve(), args.sdk_prefix.resolve(), args.sdk_root.resolve()
    for source in (ROOT, sdk, sdk_root):
        if output == source or source in output.parents or output in source.parents:
            parser.error('Build directory must be external to sources and installed SDK')
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        parser.error('Build directory must be fresh and empty; no previous data is removed')
    output.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', PYTHONNOUSERSITE='1', npm_config_update_notifier='false')
    environment.pop('PYTHONPATH', None)
    if args.sanitize:
        environment['ASAN_OPTIONS'] = 'detect_leaks=1:halt_on_error=1'
        environment['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
    report = dict(passed=False, hardware=False, checks=0, commands=[], sanitize=args.sanitize,
                  started_utc=datetime.now(timezone.utc).isoformat(), source_sha256_before=sources())

    def check(value, message):
        report['checks'] += 1
        if not value:
            raise RuntimeError(message)

    def run(name, command, cwd=output):
        started = time.monotonic()
        result = subprocess.run(list(map(str, command)), cwd=cwd, env=environment,
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=900)
        log = output / (name + '.log')
        log.write_text(result.stdout, encoding='utf8')
        report['commands'].append(dict(name=name, command=list(map(str, command)), exit=result.returncode,
                                       seconds=time.monotonic() - started, log=log.name, sha256=digest(log)))
        print(name + ': exit ' + str(result.returncode), flush=True)
        check(result.returncode == 0, name + ' failed\n' + result.stdout[-14000:])
        return result.stdout

    try:
        tools = {}
        for name in ('node', 'codegen_python', 'browser_python', 'cxx', 'cmake', 'ninja'):
            # Preserve lexical venv executable paths; resolving their symlinks
            # on Linux would select a different Python installation context.
            path = Path(shutil.which(str(getattr(args, name))) or getattr(args, name)).absolute()
            check(path.is_file(), 'Missing tool ' + str(path)); tools[name] = path
        check(args.npm_js.is_file(), 'Missing npm CLI')
        environment['PATH'] = os.pathsep.join((str(tools['node'].parent), str(tools['cxx'].parent), environment.get('PATH', '')))
        report['tools'] = {key: dict(path=str(value), sha256=digest(value)) for key, value in tools.items()}
        configs = list(sdk.rglob('TelemetryConfig.cmake'))
        check(len(configs) == 1, 'Require one installed SDK package')
        report['installed_cpp_sha256_before'] = inventory(sdk)

        def native_profile(name, scaling):
            target = output / name
            command = [tools['cmake'], '-S', ROOT / 'examples/web_demo/native', '-B', target, '-G', 'Ninja',
                       '-DCMAKE_PREFIX_PATH=' + str(sdk), '-DTelemetry_DIR=' + str(configs[0].parent),
                       '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF', '-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF',
                       '-DCMAKE_CXX_COMPILER=' + str(tools['cxx']), '-DCMAKE_MAKE_PROGRAM=' + str(tools['ninja']),
                       '-DCMAKE_BUILD_TYPE=Release', '-DTELEMETRY_UX_WATCHLIST_SCALING=' + ('ON' if scaling else 'OFF')]
            if args.sanitize:
                command += ['-DCMAKE_CXX_FLAGS_RELEASE=-O1 -g -DNDEBUG -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer']
            run(name + '-configure', command)
            run(name + '-build', [tools['cmake'], '--build', target, '--parallel', '2'])
            peer = target / ('telemetry_web_demo_peer.exe' if os.name == 'nt' else 'telemetry_web_demo_peer')
            check(peer.is_file(), 'Missing actual native peer')
            run(name + '-descriptor', [peer, '--descriptor', target / 'descriptor.bin'])
            return peer, target / 'descriptor.bin'

        _, default_descriptor = native_profile('native-default', False)
        check(digest(default_descriptor) == DEFAULT_DESCRIPTOR_SHA256, 'Default demo Descriptor changed under an opt-in test profile')
        report['default_descriptor_sha256'] = digest(default_descriptor)
        device, descriptor = native_profile('native-scaling', True)
        report['scaling_descriptor_sha256'] = digest(descriptor)
        check(digest(descriptor) != DEFAULT_DESCRIPTOR_SHA256, 'Scaling test did not enable its separate real Fields')
        public = output / 'public'; public.mkdir()
        (public / 'package.json').write_text('{"private":true,"type":"module"}\n', encoding='utf8')
        run('installed-codegen-identity', [tools['codegen_python'], '-c',
            'import json,tools.telemetry_codegen as c; print(json.dumps(dict(version=c.__version__,path=c.__file__)))'])
        identity = json.loads((output / 'installed-codegen-identity.log').read_text(encoding='utf8'))
        check(identity['version'] == '0.1.0' and 'site-packages' in Path(identity['path']).parts, 'Require installed matching codegen')
        run('generate', [tools['codegen_python'], '-m', 'tools.telemetry_codegen', 'generate', descriptor,
            '--targets', 'js', '--structural-metadata', '--wire-editing', '--namespace', 'scaling_api', '--output', public / 'generated'])
        shutil.copy2(descriptor, public / 'descriptor.bin')
        dist = output / 'dist'; dist.mkdir()

        def pack(name, source):
            packed = json.loads(run('pack-' + name, [tools['node'], args.npm_js, 'pack', '--json', '--ignore-scripts',
                '--pack-destination', dist, '--cache', output / 'npm-cache'], cwd=source))
            check(len(packed) == 1 and packed[0]['name'] == 'telemetry-' + name, 'Unexpected packed package')
            return dist / packed[0]['filename']

        sdk_package = args.sdk_package.resolve() if args.sdk_package else pack('sdk', sdk_root)
        ux_package = pack('ux', ROOT)
        report['packages'] = {name: dict(path=str(path), sha256=digest(path)) for name, path in [('sdk', sdk_package), ('ux', ux_package)]}
        run('install', [tools['node'], args.npm_js, 'install', '--offline', '--ignore-scripts', '--no-audit', '--no-fund',
            '--package-lock=false', '--cache', output / 'npm-cache', '--prefix', public, sdk_package, ux_package])
        report['installed_npm_sha256_before'] = {name: inventory(public / 'node_modules' / name) for name in ('telemetry-sdk', 'telemetry-ux')}
        for name in ('Scaling.mjs', 'App.js', 'index.html'):
            shutil.copy2(HERE / name, public / name)
        provider = json.loads(run('installed-watchlist-scaling', [tools['node'], public / 'Scaling.mjs', device]).strip().splitlines()[-1])
        check(provider.get('passed') and provider.get('checks', 0) >= 3686 and provider.get('scenarios', 0) >= 30,
              'Both real Session modes and deterministic scaling floor')
        report['providers'] = provider
        run('chromium-scaling-controls', [tools['browser_python'], '-B', HERE / 'Browser.py', '--device', device,
            '--public-root', public, '--data-dir', output / 'browser'])
        browser = json.loads((output / 'browser/summary.json').read_text(encoding='utf8'))
        check(browser.get('passed') and set(browser.get('modes', {})) == {'static', 'dynamic'} and browser.get('checks', 0) >= 40,
              'Both actual browser modes and polling controls')
        report['browser'] = browser
        report['source_sha256_after'] = sources()
        report['installed_cpp_sha256_after'] = inventory(sdk)
        report['installed_npm_sha256_after'] = {name: inventory(public / 'node_modules' / name) for name in ('telemetry-sdk', 'telemetry-ux')}
        check(report['source_sha256_before'] == report['source_sha256_after'], 'Source input drift')
        check(report['installed_cpp_sha256_before'] == report['installed_cpp_sha256_after'], 'Installed SDK input drift')
        check(report['installed_npm_sha256_before'] == report['installed_npm_sha256_after'], 'Installed npm input drift')
        for name, path in [('sdk', sdk_package), ('ux', ux_package)]:
            check(digest(path) == report['packages'][name]['sha256'], 'Packed ' + name + ' input drift')
        report['device'] = dict(path=str(device), sha256=digest(device))
        report['checks'] += provider['checks'] + browser['checks']
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        report['finished_utc'] = datetime.now(timezone.utc).isoformat()
        (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')


if __name__ == '__main__':
    main()
