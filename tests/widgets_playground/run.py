#!/usr/bin/env python3
"""Qualify the actual Widgets playground using an installed SDK, without hardware (MIT)."""
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
EXAMPLE = ROOT / 'examples/widgets_playground'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inventory(root, directories):
    return {path.relative_to(root).as_posix(): digest(path)
            for directory in directories for path in sorted(directory.rglob('*'))
            if path.is_file() and '__pycache__' not in path.parts}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--sdk-prefix', type=Path, required=True)
    parser.add_argument('--qt-prefix', type=Path, required=True)
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ninja', default='ninja')
    parser.add_argument('--jobs', default='2')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    output, sdk, qt = args.build_dir.resolve(), args.sdk_prefix.resolve(), args.qt_prefix.resolve()
    if args.sanitize and os.name == 'nt':
        parser.error('--sanitize requires a Linux sanitizer toolchain')
    if not sdk.is_dir() or sdk == ROOT or ROOT in sdk.parents:
        parser.error('--sdk-prefix must be an installed SDK outside the UX source repository')
    for path in (ROOT, sdk):
        if output == path or path in output.parents or output in path.parents:
            parser.error('--build-dir must be a fresh external directory, separate from the SDK prefix')
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        parser.error('--build-dir must be fresh and empty; nothing is deleted')
    output.mkdir(parents=True, exist_ok=True)
    report = dict(passed=False, hardware=False, checks=0, commands=[], sanitize=args.sanitize,
                  started_utc=datetime.now(timezone.utc).isoformat(),
                  source_sha256_before=inventory(ROOT, (HERE, EXAMPLE)),
                  installed_sdk_sha256_before=inventory(sdk, (sdk,)),
                  installed_sdk_prefix=str(sdk), sdk_library_source_build=False)
    environment = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', QT_QPA_PLATFORM='offscreen',
                       QT_FORCE_STDERR_LOGGING='1')
    if args.sanitize:
        environment['ASAN_OPTIONS'] = 'detect_leaks=0:halt_on_error=1'
        environment['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
        report['sanitizers'], report['leak_checking'] = ['address', 'undefined'], False
        report['leak_checking_reason'] = 'Qt offscreen plugin process-global allocation lifetime'

    def check(condition, message):
        report['checks'] += 1
        if not condition:
            raise RuntimeError(message)

    def run(name, command):
        command = list(map(str, command))
        start = time.monotonic()
        result = subprocess.run(command, cwd=output, env=environment, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
        log = output / (name + '.log')
        log.write_text(result.stdout, encoding='utf8')
        report['commands'].append(dict(name=name, command=command, exit=result.returncode,
            seconds=time.monotonic()-start, log=log.name, sha256=digest(log)))
        print(name + ': exit ' + str(result.returncode), flush=True)
        check(result.returncode == 0, name + ' failed\n' + result.stdout[-16000:])
        return result.stdout

    try:
        configs = sorted(sdk.rglob('TelemetryConfig.cmake'))
        metadata = sorted(sdk.rglob('SDKMetadata.json'))
        check(len(configs) == 1 and len(metadata) == 1, 'Require one installed SDK package and metadata')
        identity = json.loads(metadata[0].read_text(encoding='utf8'))
        check(isinstance(identity, dict) and identity.get('sdkVersion') == '0.1.0' and
              identity.get('descriptorWireVersion') == [3, 0] and
              all(type(n) is int for n in identity['descriptorWireVersion']) and
              type(identity.get('encodedWireVersion')) is int and identity['encodedWireVersion'] == 1 and
              isinstance(identity.get('sourceRevision'), str) and
              (not identity['sourceRevision'] or re.fullmatch('[0-9a-fA-F]{40}', identity['sourceRevision'])),
              'Installed SDK metadata is incompatible with this example')
        report['installed_sdk'] = dict(metadata=identity, metadata_sha256=digest(metadata[0]),
                                      metadata_path=metadata[0].relative_to(sdk).as_posix())
        tools = {name: Path(shutil.which(getattr(args, name)) or getattr(args, name)).absolute()
                 for name in ('cxx', 'cmake', 'ninja')}
        for path in tools.values():
            check(path.is_file(), 'Missing tool: ' + str(path))
        environment['PATH'] = os.pathsep.join((str(tools['cxx'].parent), str(qt / 'bin'),
                                               environment.get('PATH', '')))
        if os.name == 'nt':
            fonts = Path(os.environ.get('WINDIR', r'C:\Windows')) / 'Fonts'
            check(fonts.is_dir(), 'Windows offscreen font directory is missing')
            environment['QT_QPA_FONTDIR'] = str(fonts)
        report['tool_sha256'] = {str(path): digest(path) for path in tools.values()}
        report['ux_base_head'] = run('ux-head', ['git', '-C', ROOT, 'rev-parse', 'HEAD']).strip()
        for name, path in tools.items():
            run(name + '-version', [path, '--version'])
        build = output / 'build'
        flags = '-Wall -Wextra -Werror'
        configure = [tools['cmake'], '-S', HERE, '-B', build, '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_CXX_COMPILER=' + str(tools['cxx']),
            '-DCMAKE_MAKE_PROGRAM=' + str(tools['ninja']),
            '-DCMAKE_PREFIX_PATH=' + ';'.join(map(str, (sdk, qt))),
            '-DTelemetry_DIR=' + str(configs[0].parent),
            '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF', '-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF',
            '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON']
        if args.sanitize:
            flags += ' -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
            configure += ['-DCMAKE_CXX_FLAGS_RELEASE=-O1 -DNDEBUG',
                          '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined']
        run('configure', configure + ['-DCMAKE_CXX_FLAGS=' + flags])
        run('build', [tools['cmake'], '--build', build, '-j', args.jobs])
        manifest = build / 'compile_commands.json'
        compiled = json.loads(manifest.read_text(encoding='utf8'))
        facade = [row for row in compiled if Path(row['file']).name == 'DeviceCheck.cpp']
        check(len(facade) == 1, 'Missing generic facade translation unit')
        check(not re.search(r'telemetry-(?:core|reflection|resource_adapter|resource_protocol)(?:[/\\\s]|$)',
                            facade[0]['command']), 'Private native/provider headers leaked to facade consumer')
        check(not re.search(r'Qt(?:Core|Widgets)|Qt6(?:Core|Widgets)', facade[0]['command']),
              'The generic Resource facade consumer acquired a Qt dependency')
        report['compile_commands_sha256'] = digest(manifest)
        report['translation_units'] = len(compiled)
        suffix = '.exe' if os.name == 'nt' else ''
        gui = build / 'playground' / ('telemetry_playground' + suffix)
        console = build / ('widgets_device_facade_check' + suffix)
        run('offscreen-smoke', [gui, '--smoke-test'])
        check('Device resource facade: passed' in run('resource-facade', [console]),
              'Actual facade checks did not publish their completion')
        report['executions'] = dict(offscreen_smoke=True, exact_integer_and_nested_display=True,
            native_and_encoded_mutations=True, resource_facade=True, hardware=False)
        report['binary_sha256'] = {path.relative_to(output).as_posix(): digest(path) for path in (gui, console)}
        report['source_sha256_after'] = inventory(ROOT, (HERE, EXAMPLE))
        report['installed_sdk_sha256_after'] = inventory(sdk, (sdk,))
        check(report['source_sha256_before'] == report['source_sha256_after'], 'UX source inputs changed')
        check(report['installed_sdk_sha256_before'] == report['installed_sdk_sha256_after'],
              'Installed SDK payload changed')
        report['passed'] = True
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        report['error'] = str(error)
        print(report['error'], flush=True)
    report['finished_utc'] = datetime.now(timezone.utc).isoformat()
    (output / 'summary.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf8')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
