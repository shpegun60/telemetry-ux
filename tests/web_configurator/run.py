#!/usr/bin/env python3
"""Qualify installed browser UX against an installed SDK and native Model (MIT)."""
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


def sources():
    result = {name: digest(ROOT / name) for name in ('package.json', 'VERSION', 'LICENSE', 'README.md')}
    for directory in (ROOT / 'web', ROOT / 'examples/web_demo', HERE):
        result.update({path.relative_to(ROOT).as_posix(): digest(path)
            for path in sorted(directory.rglob('*')) if path.is_file() and '__pycache__' not in path.parts})
    return result


def tool(value):
    # Retain clang++ driver spelling when the selected image is a symlink.
    path = Path(shutil.which(str(value)) or value).absolute()
    if not path.is_file():
        raise ValueError('Missing tool: ' + str(path))
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--sdk-prefix', type=Path, required=True)
    parser.add_argument('--sdk-package', type=Path, required=True)
    parser.add_argument('--codegen-python', required=True)
    parser.add_argument('--browser-python', required=True)
    parser.add_argument('--node', required=True)
    parser.add_argument('--tsc', type=Path, required=True)
    parser.add_argument('--npm-js', type=Path)
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ninja', default='ninja')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    output, sdk, sdk_package = (path.resolve() for path in
        (args.build_dir, args.sdk_prefix, args.sdk_package))
    if not sdk.is_dir() or not sdk_package.is_file():
        parser.error('Provide the installed C++ SDK prefix and an SDK npm package tarball')
    if any(output == path or path in output.parents or output in path.parents for path in (ROOT, sdk)):
        parser.error('--build-dir must be separate from the UX repository and installed SDK')
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        parser.error('--build-dir must be fresh and empty; nothing is deleted')
    output.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', PYTHONNOUSERSITE='1',
                       PIP_DISABLE_PIP_VERSION_CHECK='1', npm_config_update_notifier='false')
    environment.pop('PYTHONPATH', None)
    if args.sanitize:
        environment['ASAN_OPTIONS'] = 'detect_leaks=1:halt_on_error=1'
        environment['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
    report = dict(passed=False, hardware=False, checks=0, commands=[],
        started_utc=datetime.now(timezone.utc).isoformat(), sanitize=args.sanitize,
        source_sha256_before=sources(), installed_sdk_sha256_before=inventory(sdk),
        sdk_npm_package_sha256=digest(sdk_package))

    def check(condition, message):
        report['checks'] += 1
        if not condition:
            raise RuntimeError(message)

    def run(name, command, cwd=output):
        command = list(map(str, command))
        start = time.monotonic()
        result = subprocess.run(command, cwd=cwd, env=environment, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=900)
        log = output / (name + '.log')
        log.write_text(result.stdout, encoding='utf8')
        report['commands'].append(dict(name=name, command=command, exit=result.returncode,
            seconds=time.monotonic()-start, log=log.name, sha256=digest(log)))
        print(name + ': exit ' + str(result.returncode), flush=True)
        check(result.returncode == 0, name + ' failed\n' + result.stdout[-14000:])
        return result.stdout

    try:
        tools = {name: tool(getattr(args, name)) for name in
                 ('node', 'codegen_python', 'browser_python', 'cxx', 'cmake', 'ninja')}
        report['tools'] = {name: dict(path=str(path), sha256=digest(path)) for name, path in tools.items()}
        environment['PATH'] = os.pathsep.join((str(tools['node'].parent), str(tools['cxx'].parent),
                                             environment.get('PATH', '')))
        npm = args.npm_js or tools['node'].parent.parent / 'lib/node_modules/npm/bin/npm-cli.js'
        check(npm.is_file() and args.tsc.is_file(), 'Missing npm CLI or TypeScript compiler')
        metadata = list(sdk.rglob('SDKMetadata.json'))
        configs = list(sdk.rglob('TelemetryConfig.cmake'))
        check(len(metadata) == 1 and len(configs) == 1, 'Require one installed SDK identity and package')
        report['installed_sdk'] = json.loads(metadata[0].read_text(encoding='utf8'))
        check(report['installed_sdk'].get('sdkVersion') == '0.1.0', 'SDK version mismatch')
        config = [tools['cmake'], '-S', ROOT / 'examples/web_demo/native', '-B', output / 'native',
            '-G', 'Ninja', '-DCMAKE_PREFIX_PATH=' + str(sdk),
            '-DTelemetry_DIR=' + str(configs[0].parent),
            '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF', '-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF',
            '-DCMAKE_CXX_COMPILER=' + str(tools['cxx']), '-DCMAKE_MAKE_PROGRAM=' + str(tools['ninja']),
            '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON']
        if args.sanitize:
            config += ['-DCMAKE_CXX_FLAGS_RELEASE=-O1 -g -DNDEBUG -fsanitize=address,undefined '
                       '-fno-sanitize-recover=all -fno-omit-frame-pointer']
        # A real competing prefix makes this a regression control rather than
        # assuming that CMAKE_PREFIX_PATH outranks a caller's Telemetry_ROOT.
        alternate = output / 'alternate-sdk'
        shutil.copytree(sdk, alternate)
        original_root = environment.get('Telemetry_ROOT')
        environment['Telemetry_ROOT'] = str(alternate)
        try:
            run('native-configure', config)
        finally:
            if original_root is None:
                environment.pop('Telemetry_ROOT', None)
            else:
                environment['Telemetry_ROOT'] = original_root
        cache = (output / 'native/CMakeCache.txt').read_text(encoding='utf8')
        selected = [line.partition('=')[2] for line in cache.splitlines()
                    if line.startswith('Telemetry_DIR:')]
        check(len(selected) == 1 and Path(selected[0]).resolve() == configs[0].parent.resolve(),
              'CMake must use --sdk-prefix despite a real competing Telemetry_ROOT')
        report['cmake_package_selection'] = dict(selected=selected[0], competing_root=str(alternate),
                                                registries=False, verified=True)
        run('native-build', [tools['cmake'], '--build', output / 'native', '--parallel', '2'])
        device = output / 'native' / ('telemetry_web_demo_peer.exe' if os.name == 'nt' else 'telemetry_web_demo_peer')
        check(device.is_file(), 'Native peer executable was not built')
        run('native-descriptor', [device, '--descriptor', output / 'descriptor.bin'])
        codegen_script = '''import hashlib,json
from importlib.metadata import distribution
import tools.telemetry_codegen as c
result=dict(version=c.__version__,path=c.__file__,distributions={})
for name in ('telemetry-codegen-core','telemetry-ux-codegen'):
    dist=distribution(name)
    files={str(p):hashlib.sha256(dist.locate_file(p).read_bytes()).hexdigest()
        for p in dist.files if dist.locate_file(p).is_file() and str(p).endswith(('.py','.js','.ts','.json','.in','.txt','METADATA','entry_points.txt'))}
    result['distributions'][name]=dict(version=dist.version,files_sha256=files)
print(json.dumps(result))
'''
        run('installed-codegen-identity', [tools['codegen_python'], '-c', codegen_script])
        codegen_identity = json.loads((output / 'installed-codegen-identity.log').read_text())
        check(codegen_identity['version'] == '0.1.0' and
              'site-packages' in Path(codegen_identity['path']).parts,
              'Generation must use the installed matching SDK core distribution')
        check(all(row['version'] == '0.1.0' for row in codegen_identity['distributions'].values()),
              'Both installed generator distributions must have the matching development version')
        report['codegen'] = codegen_identity
        public = output / 'public'
        public.mkdir()
        (public / 'package.json').write_text('{"private":true,"type":"module"}\n', encoding='utf8')
        run('generate-static-sdk', [tools['codegen_python'], '-m', 'tools.telemetry_codegen',
            'generate', output / 'descriptor.bin', '--targets', 'js', '--structural-metadata',
            '--ux-starter', 'web',
            '--namespace', 'web_demo_api', '--output', public / 'generated'])
        dist = output / 'dist'
        dist.mkdir()
        packed = json.loads(run('pack-ux', [tools['node'], npm, 'pack', '--json',
            '--ignore-scripts', '--pack-destination', dist, '--cache', output / 'npm-cache'], cwd=ROOT))
        check(len(packed) == 1 and packed[0]['name'] == 'telemetry-ux', 'Unexpected UX package')
        ux_package = dist / packed[0]['filename']
        report['ux_npm_package_sha256'] = digest(ux_package)
        run('install-local-packages', [tools['node'], npm, 'install', '--offline', '--ignore-scripts',
            '--no-audit', '--no-fund', '--package-lock=false', '--cache', output / 'npm-cache',
            '--prefix', public, sdk_package, ux_package])
        report['installed_npm_sha256_before'] = {
            name: inventory(public / 'node_modules' / name) for name in ('telemetry-sdk', 'telemetry-ux')}
        for name in ('app.js', 'index.html'):
            shutil.copyfile(ROOT / 'examples/web_demo' / name, public / name)
        shutil.copyfile(HERE / 'Consumer.ts', public / 'Consumer.ts')
        shutil.copyfile(HERE / 'Provider.mjs', public / 'Provider.mjs')
        shutil.copyfile(HERE / 'Renderer.js', public / 'Renderer.js')
        run('strict-installed-types', [tools['node'], args.tsc, '--strict', '--noEmit',
            '--target', 'ES2022', '--module', 'NodeNext', '--moduleResolution', 'NodeNext',
            public / 'Consumer.ts'])
        for name in ('Provider.js', 'Configurator.js', 'index.js'):
            run('syntax-' + name.replace('.js', ''), [tools['node'], '--check',
                public / 'node_modules/telemetry-ux/web' / name])
        provider_output = run('installed-providers-real-native', [tools['node'], public / 'Provider.mjs',
            '--device', device, '--descriptor', output / 'descriptor.bin',
            '--generated', public / 'generated/js'])
        provider = json.loads(provider_output.strip().splitlines()[-1])
        check(provider.get('passed') and provider.get('checks', 0) >= 222,
              'Installed providers must pass the maintained native and failure controls')
        report['providers'] = provider
        run('browser-real-native-model', [tools['browser_python'], '-B', HERE / 'Browser.py',
            '--device', device, '--public-root', public, '--data-dir', output / 'browser'])
        browser = json.loads((output / 'browser/summary.json').read_text(encoding='utf8'))
        check(browser.get('passed') and browser.get('static') and browser.get('dynamic'),
              'Both installed-provider browser journeys must pass')
        report['browser'] = browser
        report['device'] = dict(path=str(device), sha256=digest(device))
        report['source_sha256_after'] = sources()
        report['installed_sdk_sha256_after'] = inventory(sdk)
        report['installed_npm_sha256_after'] = {
            name: inventory(public / 'node_modules' / name) for name in ('telemetry-sdk', 'telemetry-ux')}
        check(report['source_sha256_before'] == report['source_sha256_after'], 'UX input drift')
        check(report['installed_sdk_sha256_before'] == report['installed_sdk_sha256_after'],
              'Installed SDK input drift')
        check(digest(sdk_package) == report['sdk_npm_package_sha256'], 'SDK npm package changed')
        check(report['installed_npm_sha256_before'] == report['installed_npm_sha256_after'],
              'Installed npm package input drift')
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        report['finished_utc'] = datetime.now(timezone.utc).isoformat()
        (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')


if __name__ == '__main__':
    main()
