#!/usr/bin/env python3
"""Install current UX, relocate it and check semantic hints in a native consumer. MIT."""
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
FIXTURE = ROOT / 'tests/dynamic_qml_configurator/Fixture.hpp'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inventory(directory):
    return {path.relative_to(directory).as_posix(): digest(path)
            for path in sorted(directory.rglob('*'))
            if path.is_file() and '__pycache__' not in path.parts}


def sources():
    paths = [ROOT / name for name in ('CMakeLists.txt', 'VERSION', 'LICENSE')]
    paths.append(FIXTURE)
    for directory in (ROOT / 'lib/qml', HERE):
        paths += [path for path in directory.rglob('*')
                  if path.is_file() and '__pycache__' not in path.parts]
    return {path.relative_to(ROOT).as_posix(): digest(path) for path in sorted(set(paths))}


def overlaps(left, right):
    return left == right or left in right.parents or right in left.parents


def executable(value):
    # Keep a selected clang++ symlink's driver spelling.
    path = Path(shutil.which(value) or value).absolute()
    if not path.is_file():
        raise ValueError('Required executable is unavailable: ' + str(path))
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True, type=Path)
    parser.add_argument('--sdk-prefix', required=True, type=Path)
    parser.add_argument('--qt-prefix', required=True, type=Path)
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ninja', default='ninja')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    output, sdk, qt = (path.resolve() for path in
                       (args.build_dir, args.sdk_prefix, args.qt_prefix))
    if any(overlaps(output, path) for path in (ROOT, sdk, qt)):
        parser.error('--build-dir must be outside sources and installed SDK/Qt prefixes')
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        parser.error('--build-dir must be new or empty; earlier output is never removed')
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    sdk_configs = list(sdk.rglob('TelemetryConfig.cmake'))
    if len(sdk_configs) != 1:
        parser.error('--sdk-prefix must contain exactly one installed Telemetry package')
    try:
        tools = {name: executable(getattr(args, name)) for name in ('cxx', 'cmake', 'ninja')}
    except ValueError as error:
        parser.error(str(error))
    for path in (HERE / 'Qt.cpp', HERE / 'CMakeLists.txt', HERE / 'cases.json', FIXTURE):
        if not path.is_file():
            parser.error('Native fixture input is missing: ' + str(path))
    output.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', QT_QPA_PLATFORM='offscreen',
                       QT_QUICK_BACKEND='software', QT_QUICK_CONTROLS_STYLE='Basic',
                       QT_FORCE_STDERR_LOGGING='1')
    for name in ('QML_IMPORT_PATH', 'QML2_IMPORT_PATH', 'LSAN_OPTIONS', 'QT_LOGGING_RULES',
                 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH'):
        environment.pop(name, None)
    environment['PATH'] = os.pathsep.join((str(tools['cxx'].parent), str(qt / 'bin'),
                                           environment.get('PATH', '')))
    if os.name == 'nt':
        environment['QT_QPA_FONTDIR'] = str(Path(os.environ.get('WINDIR', r'C:\Windows')) / 'Fonts')
    else:
        runtime = output / 'runtime'
        runtime.mkdir(mode=0o700)
        environment['XDG_RUNTIME_DIR'] = str(runtime)
    report = dict(passed=False, hardware=False, checks=0, warnings=None, commands=[],
                  sanitize=args.sanitize, started_utc=datetime.now(timezone.utc).isoformat(),
                  source_sha256_before=sources(), installed_sdk_sha256_before=inventory(sdk),
                  tools={name: dict(path=str(path), sha256=digest(path))
                         for name, path in tools.items()})
    report_path = output / 'summary.json'
    report_path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')

    def run(name, command, expected=0, timeout=600):
        started = time.monotonic()
        command = list(map(str, command))
        try:
            result = subprocess.run(command, cwd=output, env=environment, encoding='utf8',
                                    errors='replace', stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, timeout=timeout)
            text, code = result.stdout, result.returncode
        except subprocess.TimeoutExpired as error:
            text = error.stdout or ''
            if isinstance(text, bytes):
                text = text.decode('utf8', errors='replace')
            text += '\nRunner timeout: ' + str(error)
            code = None
        log = output / (name + '.log')
        log.write_text(text, encoding='utf8')
        report['commands'].append(dict(name=name, command=command, exit=code,
            expected_exit=expected, seconds=time.monotonic() - started,
            log=log.name, sha256=digest(log)))
        print(name + ': exit ' + str(code), flush=True)
        if code != expected:
            raise RuntimeError(name + ' failed; inspect ' + str(log) + '\n' + text[-14000:])
        return text

    try:
        flags = '-Wall -Wextra -Werror'
        if args.sanitize:
            flags += ' -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
            suppression = output / 'qt-software-textures.lsan'
            suppression.write_text('leak:QSGRenderContext::textureForFactory\n', encoding='ascii')
            environment.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1',
                LSAN_OPTIONS='exitcode=86:print_suppressions=1:suppressions=' + str(suppression))
        common = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
                  '-DCMAKE_CXX_COMPILER=' + str(tools['cxx']),
                  '-DCMAKE_MAKE_PROGRAM=' + str(tools['ninja']),
                  '-DCMAKE_CXX_FLAGS=' + flags,
                  '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF',
                  '-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF',
                  '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON']
        if args.sanitize:
            common.append('-DCMAKE_CXX_FLAGS_RELEASE=-O1 -g -DNDEBUG')
        producer = output / 'producer'
        for relative in report['source_sha256_before']:
            if relative.startswith('tests/'):
                continue
            target = producer / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / relative, target)
        stage, prefix = output / 'stage', output / 'relocated-prefix'
        producer_build = output / 'producer-build'
        run('configure-ux', [tools['cmake'], '-S', producer, '-B', producer_build, *common,
            '-DTELEMETRY_UX_BUILD_INSPECTOR=OFF', '-DTELEMETRY_UX_BUILD_CONFIGURATOR=ON',
            '-DTELEMETRY_UX_BUILD_DYNAMIC_CONFIGURATOR=ON', '-DTELEMETRY_UX_BUILD_QT_CONNECTION=ON',
            '-DCMAKE_PREFIX_PATH=' + ';'.join(map(str, (sdk, qt))),
            '-DTelemetry_DIR=' + str(sdk_configs[0].parent),
            '-DCMAKE_INSTALL_PREFIX=' + str(stage)])
        run('install-ux', [tools['cmake'], '--build', producer_build,
                           '--target', 'install', '--parallel', str(args.jobs)])
        for path in (stage, prefix, producer, producer_build):
            if not path.resolve().is_relative_to(output):
                raise RuntimeError('Fresh producer or prefix escaped its output directory')
        stage.rename(prefix)
        producer.rename(output / 'producer.hidden')
        producer_build.rename(output / 'producer-build.hidden')
        report['installed_ux_sha256_before'] = inventory(prefix)
        configs = list(prefix.rglob('TelemetryUXConfig.cmake'))
        if len(configs) != 1:
            raise RuntimeError('Expected one relocated UX CMake package')
        for path in prefix.rglob('*.cmake'):
            text = path.read_text(encoding='utf8')
            if any(str(source).replace('\\', '/') in text for source in (producer, producer_build)):
                raise RuntimeError('Installed export retains producer path: ' + str(path))
        if args.sanitize:
            nm = shutil.which('nm', path=environment['PATH'])
            if not nm:
                raise RuntimeError('--sanitize needs nm for installed archive checks')
            archives = []
            for name in ('libtelemetry_core.a', 'libtelemetry_encoded_server.a',
                         'libtelemetry_qt_connection.a'):
                found = list(sdk.rglob(name))
                if len(found) != 1:
                    raise RuntimeError('Expected one instrumented SDK archive: ' + name)
                archives.extend(found)
            for name in ('libtelemetry_ux_configurator.a', 'libtelemetry_ux_static_configurator.a',
                         'libtelemetry_ux_dynamic_configurator.a', 'libtelemetry_ux_qt_connection.a'):
                found = list(prefix.rglob(name))
                if len(found) != 1:
                    raise RuntimeError('Expected one instrumented UX archive: ' + name)
                archives.extend(found)
            report['archive_instrumentation'] = []
            for index, archive in enumerate(archives):
                symbols = run('archive-symbols-' + str(index), [nm, '-a', archive])
                item = dict(path=str(archive), sha256=digest(archive),
                            asan='__asan_' in symbols, ubsan='__ubsan_' in symbols)
                report['archive_instrumentation'].append(item)
                if not item['asan'] or not item['ubsan']:
                    raise RuntimeError('Installed archive lacks ASan/UBSan: ' + str(archive))
        consumer = output / 'consumer'
        consumer.mkdir()
        for name in ('Qt.cpp', 'CMakeLists.txt', 'cases.json'):
            shutil.copyfile(HERE / name, consumer / name)
        shutil.copyfile(FIXTURE, consumer / 'Fixture.hpp')
        report['consumer_inputs_sha256'] = inventory(consumer)
        build = output / 'consumer-build'
        run('configure-consumer', [tools['cmake'], '-S', consumer, '-B', build, *common,
            '-DCMAKE_PREFIX_PATH=' + ';'.join(map(str, (sdk, prefix, qt))),
            '-DTelemetry_DIR=' + str(sdk_configs[0].parent),
            '-DTelemetryUX_DIR=' + str(configs[0].parent)])
        run('build-consumer', [tools['cmake'], '--build', build, '--parallel', str(args.jobs)])
        commands = json.loads((build / 'compile_commands.json').read_text(encoding='utf8'))
        for entry in commands:
            command = entry.get('command', ' '.join(entry.get('arguments', []))).replace('\\', '/')
            if str(ROOT).replace('\\', '/') in command:
                raise RuntimeError('Downstream consumer refers to UX checkout: ' + command)
        binary = build / ('telemetry_presentation_check.exe' if os.name == 'nt'
                          else 'telemetry_presentation_check')
        text = run('runtime', [binary, consumer / 'cases.json'])
        count = re.search(r'Presentation metadata checks=(\d+) warnings=(\d+) cases=(\d+)', text)
        cases = json.loads((consumer / 'cases.json').read_text(encoding='utf8'))['cases']
        if not count or int(count[1]) < len(cases) or int(count[2]) or int(count[3]) != len(cases):
            raise RuntimeError('Native metadata acceptance missed shared cases or emitted warnings')
        report.update(checks=int(count[1]), warnings=int(count[2]), cases=int(count[3]),
                      executable_sha256=digest(binary))
        if args.sanitize:
            negative = run('application-leak-negative-control',
                           [binary, consumer / 'cases.json', '--application-leak-control'],
                           expected=86)
            if '123 byte(s)' not in negative or 'injectedApplicationLeak' not in negative:
                raise RuntimeError('Narrow Qt exception did not reject the application leak')
            report['application_leak_rejected'] = True
        report['installed_ux_sha256_after'] = inventory(prefix)
        if report['installed_ux_sha256_before'] != report['installed_ux_sha256_after']:
            raise RuntimeError('Installed UX changed during consumer qualification')
        if report['consumer_inputs_sha256'] != inventory(consumer):
            raise RuntimeError('Copied downstream fixture changed during qualification')
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        report['source_sha256_after'] = sources()
        report['installed_sdk_sha256_after'] = inventory(sdk)
        if (report['source_sha256_before'] != report['source_sha256_after'] or
                report['installed_sdk_sha256_before'] != report['installed_sdk_sha256_after']):
            report.update(passed=False, error='Source or installed SDK changed during qualification')
        report['finished_utc'] = datetime.now(timezone.utc).isoformat()
        report_path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
