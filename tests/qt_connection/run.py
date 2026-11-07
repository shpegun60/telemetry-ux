#!/usr/bin/env python3
"""Installed owned Qt Session, two QML panels, real TCP and lifecycle. MIT."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inventory(root):
    return {p.relative_to(root).as_posix(): digest(p) for p in sorted(root.rglob('*'))
            if p.is_file() and '__pycache__' not in p.parts}


def sources():
    paths = [ROOT / 'CMakeLists.txt', ROOT / 'lib/qml/TelemetryUXConfig.cmake.in',
             ROOT / 'apps/inspector/DemoPeer.cpp', ROOT / 'apps/inspector/DemoPeer.hpp',
             ROOT / 'apps/inspector/TcpDemoServer.cpp', ROOT / 'apps/inspector/TcpDemoServer.hpp']
    for directory in (HERE, ROOT / 'lib/qml/Telemetry/UX', ROOT / 'examples/qt_connection'):
        paths += [p for p in directory.rglob('*') if p.is_file() and '__pycache__' not in p.parts]
    return {p.relative_to(ROOT).as_posix(): digest(p) for p in sorted(set(paths))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True, type=Path)
    parser.add_argument('--sdk-prefix', required=True, type=Path)
    parser.add_argument('--ux-prefix', required=True, type=Path)
    parser.add_argument('--qt-prefix', required=True, type=Path)
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ninja', default='ninja')
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    output, sdk, ux, qt = (p.resolve() for p in (args.build_dir, args.sdk_prefix, args.ux_prefix, args.qt_prefix))
    if any(output == base or base in output.parents or output in base.parents for base in (ROOT, sdk, ux)):
        parser.error('--build-dir must be outside sources and installed prefixes')
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        previous = output / 'summary.json'
        if previous.is_file():
            report = json.loads(previous.read_text(encoding='utf8'))
            report.update(passed=False, error='Refused nonempty output reuse; previous qualification invalidated')
            previous.write_text(json.dumps(report, indent=2)+'\n', encoding='utf8')
        parser.error('--build-dir must be new or empty; nothing is deleted')
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    sdk_configs, ux_configs = list(sdk.rglob('TelemetryConfig.cmake')), list(ux.rglob('TelemetryUXConfig.cmake'))
    if len(sdk_configs) != 1 or len(ux_configs) != 1:
        parser.error('Each prefix must contain exactly one installed CMake package')
    if not (ux / 'include/Telemetry/UX/QtConnectionBinding.hpp').is_file():
        parser.error('--ux-prefix has no installed qt_connection component')
    tools = {key: Path(shutil.which(getattr(args, key)) or getattr(args, key)).absolute()
             for key in ('cmake', 'ninja', 'cxx')}
    for key, path in tools.items():
        if not path.is_file(): parser.error(key + ' executable is unavailable: ' + str(path))
    output.mkdir(parents=True)
    environment = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', QT_QPA_PLATFORM='offscreen',
                       QT_QUICK_BACKEND='software', QT_QUICK_CONTROLS_STYLE='Basic')
    for key in ('QML_IMPORT_PATH', 'QML2_IMPORT_PATH', 'LSAN_OPTIONS'):
        environment.pop(key, None)
    environment['PATH'] = os.pathsep.join((str(tools['cxx'].parent), str(qt / 'bin'), environment.get('PATH', '')))
    if os.name == 'nt':
        environment['QT_QPA_FONTDIR'] = str(Path(os.environ.get('WINDIR', r'C:\Windows')) / 'Fonts')
    report = dict(passed=False, hardware=False, started_utc=datetime.now(timezone.utc).isoformat(), commands=[],
                  sanitize=args.sanitize, source_sha256_before=sources(),
                  installed_sdk_sha256_before=inventory(sdk), installed_ux_sha256_before=inventory(ux),
                  tools={key: dict(path=str(path), sha256=digest(path)) for key, path in tools.items()})
    report_path = output / 'summary.json'
    report_path.write_text(json.dumps(report, indent=2)+'\n', encoding='utf8')

    def run(label, command, expected=0, timeout=600):
        start = time.monotonic()
        try:
            result = subprocess.run(list(map(str, command)), cwd=output, env=environment, text=True,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
            log_text, code = result.stdout, result.returncode
        except subprocess.TimeoutExpired as error:
            value = error.stdout or ''
            log_text = value.decode('utf8', errors='replace') if isinstance(value, bytes) else value
            log_text += '\nRunner timeout: ' + str(error)
            code = None
        log = output / (label + '.log'); log.write_text(log_text, encoding='utf8')
        report['commands'].append(dict(name=label, command=list(map(str, command)), exit=code,
            expected_exit=expected, seconds=time.monotonic()-start, log=log.name, sha256=digest(log)))
        print(label + ': ' + str(code), flush=True)
        if code != expected:
            raise RuntimeError(label + ' failed; inspect ' + str(log) + '\n' + log_text[-14000:])
        return log_text

    try:
        flags = '-Wall -Wextra -Werror'
        if args.sanitize:
            flags += ' -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
            # The same narrowly named upstream software-texture exception used by
            # the existing installed static-provider gate; no application frames.
            suppression = output / 'qt-software-textures.lsan'
            suppression.write_text('leak:QSGRenderContext::textureForFactory\n', encoding='ascii')
            environment.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1',
                LSAN_OPTIONS='exitcode=86:print_suppressions=1:suppressions='+str(suppression))
            nm = shutil.which('nm', path=environment['PATH'])
            if not nm: raise RuntimeError('--sanitize needs nm for installed archive instrumentation')
            archives = list(sdk.rglob('libtelemetry_core.a')) + list(sdk.rglob('libtelemetry_encoded_server.a'))
            archives += list(ux.rglob('libtelemetry_ux_dynamic_configurator.a'))
            archives += list(ux.rglob('libtelemetry_ux_qt_connection.a'))
            archives += list(sdk.rglob('libtelemetry_qt_connection.a'))
            archives += list(sdk.rglob('libtelemetry_qt_tcp_transport.a'))
            if len(archives) != 6:
                raise RuntimeError('Expected six instrumented core, connection and UX archives')
            report['archive_instrumentation'] = []
            for i, archive in enumerate(archives):
                symbols = run('archive-symbols-'+str(i), [nm, '-a', archive])
                record = dict(path=str(archive), sha256=digest(archive), asan='__asan_' in symbols, ubsan='__ubsan_' in symbols)
                report['archive_instrumentation'].append(record)
                if not record['asan'] or not record['ubsan']:
                    raise RuntimeError('Installed archive lacks ASan/UBSan: ' + str(archive))
        build = output / 'build'
        configure = [tools['cmake'], '-S', HERE, '-B', build, '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_CXX_COMPILER='+str(tools['cxx']),
            '-DCMAKE_MAKE_PROGRAM='+str(tools['ninja']), '-DCMAKE_CXX_FLAGS='+flags,
            '-DCMAKE_PREFIX_PATH='+';'.join(map(str,(sdk,ux,qt))),
            '-DTelemetry_DIR='+str(sdk_configs[0].parent), '-DTelemetryUX_DIR='+str(ux_configs[0].parent),
            '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF', '-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF',
            '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON']
        if args.sanitize: configure.append('-DCMAKE_CXX_FLAGS_RELEASE=-O1 -g -DNDEBUG')
        run('configure', configure)
        run('build', [tools['cmake'], '--build', build, '--parallel', str(args.jobs)])
        commands = json.loads((build / 'compile_commands.json').read_text(encoding='utf8'))
        for entry in commands:
            command = entry.get('command', ' '.join(entry.get('arguments', []))).replace('\\','/')
            if '/apps/inspector/InspectorController' in command or '/apps/inspector/TcpTransport' in command:
                raise RuntimeError('Downstream test must use installed session/carrier, not Inspector lifecycle sources')
        executable = build / ('telemetry_qt_connection_check.exe' if os.name == 'nt' else 'telemetry_qt_connection_check')
        log = run('runtime', [executable])
        count = re.search(r'Qt owned connection checks=(\d+) warnings=(\d+)', log)
        if not count or int(count[1]) < 137 or int(count[2]) != 0:
            raise RuntimeError('Native/QML acceptance did not reach its warning-free assertion floor')
        report.update(checks=int(count[1]), warnings=int(count[2]), executable_sha256=digest(executable))
        if args.sanitize:
            negative = run('application-leak-negative-control', [executable, '--application-leak-control'], expected=86)
            if '123 byte(s)' not in negative or 'injectedApplicationLeak' not in negative:
                raise RuntimeError('Narrow Qt exception did not independently reject the application leak')
            report['application_leak_rejected'] = True
        example_build = output / 'example-build'
        example_configure = list(configure)
        example_configure[example_configure.index('-S') + 1] = ROOT / 'examples/qt_connection'
        example_configure[example_configure.index('-B') + 1] = example_build
        run('example-configure', example_configure)
        run('example-build', [tools['cmake'], '--build', example_build, '--parallel', str(args.jobs)])
        example_commands = json.loads((example_build / 'compile_commands.json').read_text(encoding='utf8'))
        for entry in example_commands:
            command = entry.get('command', ' '.join(entry.get('arguments', []))).replace('\\','/')
            if '/apps/inspector/InspectorController' in command or '/apps/inspector/TcpTransport' in command:
                raise RuntimeError('Runnable example must not compile or include Inspector application files')
        example_executable = example_build / ('telemetry-qt-connection.exe' if os.name == 'nt' else 'telemetry-qt-connection')
        report['example'] = dict(panels=2, executable_sha256=digest(example_executable),
                                 runtime_checked_by_same_qml_in_tcp_fixture=True)
        report['source_sha256_after'] = sources()
        report['installed_sdk_sha256_after'] = inventory(sdk)
        report['installed_ux_sha256_after'] = inventory(ux)
        if any(report[name+'_before'] != report[name+'_after'] for name in
               ('source_sha256','installed_sdk_sha256','installed_ux_sha256')):
            raise RuntimeError('Source or installed input drifted during qualification')
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        report['finished_utc'] = datetime.now(timezone.utc).isoformat()
        report_path.write_text(json.dumps(report, indent=2)+'\n', encoding='utf8')


if __name__ == '__main__':
    main()
