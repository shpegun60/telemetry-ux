#!/usr/bin/env python3
"""Prepare a real own-Model browser application using installed SDK/UX packages. MIT."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--sdk-prefix', required=True, type=Path)
    parser.add_argument('--sdk-package', required=True, type=Path)
    parser.add_argument('--ux-package', required=True, type=Path)
    parser.add_argument('--node', default='node')
    parser.add_argument('--npm-cli', type=Path, help='npm/bin/npm-cli.js; otherwise npm is found on PATH')
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ninja', default='ninja')
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--codegen-python', help='Optional Python with matching installed telemetry-codegen-core for Static mode')
    parser.add_argument('--sanitize', action='store_true', help='Enable supported native Clang/GCC ASan/UBSan checks')
    args = parser.parse_args()
    output = args.output.resolve()
    prefix = args.sdk_prefix.resolve()
    if ROOT == output or ROOT in output.parents or output in ROOT.parents or prefix == output or prefix in output.parents or output in prefix.parents:
        parser.error('--output must be outside the checkout and installed prefix')
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        parser.error('--output must be fresh and empty; no existing files are deleted')
    for name in ('sdk_package', 'ux_package'):
        if not getattr(args, name).resolve().is_file():
            parser.error('--' + name.replace('_', '-') + ' must be an existing matching package tarball')
    configs = list(prefix.rglob('TelemetryConfig.cmake'))
    if len(configs) != 1:
        parser.error('--sdk-prefix must contain exactly one installed TelemetryConfig.cmake')

    def executable(value):
        # Preserve a venv interpreter's lexical path: resolving its Python symlink
        # to /usr/bin/python would silently discard the installed code generator.
        result = Path(value).absolute() if Path(value).is_file() else Path(shutil.which(value) or '')
        if not result.is_file():
            parser.error('Tool not found: ' + value + '; add it to PATH or pass an explicit tool option')
        return str(result.absolute())

    tools = {key: executable(getattr(args, key)) for key in ('node', 'cmake', 'ninja', 'cxx')}
    npm = [tools['node'], str(args.npm_cli.resolve())] if args.npm_cli else [executable('npm')]
    if args.npm_cli and not args.npm_cli.is_file():
        parser.error('--npm-cli must name npm-cli.js')
    output.mkdir(parents=True, exist_ok=True)
    source_paths = [p for p in HERE.rglob('*') if p.is_file() and '__pycache__' not in p.parts]
    source_paths += [ROOT / 'examples/web_demo/serve.py', ROOT / 'examples/web_demo/serve_ws.py']
    source_hashes = lambda: {p.relative_to(ROOT).as_posix(): digest(p) for p in sorted(source_paths)}
    report = dict(passed=False, hardware=False, started_utc=datetime.now(timezone.utc).isoformat(), commands=[],
                  source_sha256=source_hashes(),
                  packages={name:dict(path=str(path.resolve()), sha256=digest(path.resolve()))
                            for name, path in [('sdk', args.sdk_package), ('ux', args.ux_package)]})
    report_path = output / 'prepared.json'
    report_path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    environment = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', npm_config_update_notifier='false')
    environment['PATH'] = str(Path(tools['cxx']).parent) + os.pathsep + environment.get('PATH', '')

    def run(name, command):
        result = subprocess.run(list(map(str, command)), cwd=output, env=environment,
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=900)
        log = output / (name + '.log')
        log.write_text(result.stdout, encoding='utf8')
        report['commands'].append(dict(name=name, command=list(map(str, command)), exit=result.returncode, sha256=digest(log)))
        if result.returncode:
            raise RuntimeError(name + ' failed; inspect ' + str(log))
        return result.stdout

    try:
        configure = [tools['cmake'], '-S', HERE / 'native', '-B', output / 'native', '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_PREFIX_PATH=' + str(prefix),
            '-DTelemetry_DIR=' + str(configs[0].parent), '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF',
            '-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF', '-DCMAKE_CXX_COMPILER=' + tools['cxx'],
            '-DCMAKE_MAKE_PROGRAM=' + tools['ninja']]
        if args.sanitize:
            configure.append('-DCMAKE_CXX_FLAGS_RELEASE=-O1 -g -DNDEBUG -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer')
        run('configure', configure)
        run('build', [tools['cmake'], '--build', output / 'native', '--parallel', '2'])
        device = output / 'native' / ('telemetry_device_starter.exe' if os.name == 'nt' else 'telemetry_device_starter')
        run('descriptor', [device, '--descriptor', output / 'descriptor.bin'])
        public = output / 'public'
        public.mkdir()
        (public / 'package.json').write_text('{"private":true,"type":"module"}\n', encoding='utf8')
        run('install', npm + ['install', '--offline', '--ignore-scripts', '--no-audit', '--no-fund', '--package-lock=false',
            '--prefix', public, '--cache', output / 'npm-cache', args.sdk_package.resolve(), args.ux_package.resolve()])
        for name in ('index.html', 'DeviceApp.js'):
            shutil.copy2(HERE / name, public / name)
        (public / 'starter-options.js').write_text(
            'export const staticAvailable = ' + ('true' if args.codegen_python else 'false') + ';\n', encoding='utf8')
        if args.codegen_python:
            run('generate', [executable(args.codegen_python), '-m', 'tools.telemetry_codegen', 'generate',
                output / 'descriptor.bin', '--targets', 'js', '--structural-metadata', '--wire-editing',
                '--namespace', 'my_device', '--output', public / 'generated'])
        if source_hashes() != report['source_sha256']:
            raise RuntimeError('Starter inputs changed during preparation; use a fresh output directory')
        for package in report['packages'].values():
            if digest(Path(package['path'])) != package['sha256']:
                raise RuntimeError('Package input changed during preparation')
        report.update(passed=True, static_generated=bool(args.codegen_python), device_sha256=digest(device),
                      descriptor_sha256=digest(output / 'descriptor.bin'))
        print('Prepared ' + str(output))
        print('Start: python ' + str(HERE / 'serve.py') + ' --prepared ' + str(output))
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        report['finished_utc'] = datetime.now(timezone.utc).isoformat()
        report_path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')


if __name__ == '__main__':
    main()
