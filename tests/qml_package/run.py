#!/usr/bin/env python3
"""Install, relocate and embed the pure Qt Configurator, without Telemetry SDK (MIT)."""
import argparse
import ctypes
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


def inputs():
    files = [ROOT / name for name in ('CMakeLists.txt', 'VERSION', 'LICENSE')]
    for directory in (ROOT / 'lib/qml', HERE):
        files += [path for path in directory.rglob('*') if path.is_file() and '__pycache__' not in path.parts]
    return {path.relative_to(ROOT).as_posix(): digest(path) for path in sorted(set(files))}


def inventory(directory):
    return {path.relative_to(directory).as_posix(): digest(path)
            for path in sorted(directory.rglob('*')) if path.is_file()}


def executable(value):
    # Preserve clang++ driver spelling when an explicitly selected tool is a symlink.
    path = Path(shutil.which(value) or value).absolute()
    if not path.is_file():
        raise ValueError('Required executable not found: ' + str(path))
    return path


def python_image():
    if os.name == 'nt':
        buffer = ctypes.create_unicode_buffer(32768)
        size = ctypes.windll.kernel32.GetModuleFileNameW(None, buffer, len(buffer))
        if not size or size >= len(buffer):
            raise OSError('Cannot identify the Python executable image')
        return Path(buffer.value)
    return Path(sys.executable).resolve()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True, type=Path)
    parser.add_argument('--qt-prefix', required=True, type=Path)
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ninja', default='ninja')
    parser.add_argument('--jobs', default='2')
    args = parser.parse_args()
    output, qt = args.build_dir.resolve(), args.qt_prefix.resolve()
    if any(output == path or path in output.parents or output in path.parents for path in (ROOT, qt)):
        parser.error('--build-dir must be outside the UX repository and Qt installation')
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        parser.error('--build-dir must be fresh and empty; no earlier output is removed')
    output.mkdir(parents=True, exist_ok=True)
    report = dict(passed=False, hardware=False, sdk=False, checks=0, commands=[],
                  started_utc=datetime.now(timezone.utc).isoformat(), source_sha256_before=inputs())
    environment = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', QT_QPA_PLATFORM='offscreen',
                       QT_QUICK_BACKEND='software', QSG_RHI_BACKEND='software',
                       QT_FORCE_STDERR_LOGGING='1')

    def check(condition, message):
        report['checks'] += 1
        if not condition:
            raise RuntimeError(message)

    def run(name, command, expected_success=True):
        command = list(map(str, command))
        started = time.monotonic()
        try:
            result = subprocess.run(command, cwd=output, env=environment,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    encoding='utf-8', errors='replace', timeout=600)
            text, code = result.stdout, result.returncode
        except subprocess.TimeoutExpired as error:
            text = error.stdout or ''
            if isinstance(text, bytes):
                text = text.decode(errors='replace')
            code = None
        log = output / (name + '.log')
        log.write_text(text, encoding='utf-8')
        report['commands'].append(dict(name=name, command=command, exit=code,
            expected_success=expected_success, seconds=time.monotonic() - started,
            log=log.name, sha256=digest(log)))
        print(name + ': exit ' + str(code), flush=True)
        check(code is not None and ((code == 0) == expected_success), name + ' failed\n' + text[-16000:])
        return text

    def inside_output(path):
        check(output in path.resolve().parents, 'A moved probe directory escaped its fresh output root')

    try:
        tools = {name: executable(getattr(args, name)) for name in ('cxx', 'cmake', 'ninja')}
        environment['PATH'] = os.pathsep.join((str(tools['cxx'].parent), str(qt / 'bin'),
                                               environment.get('PATH', '')))
        if os.name == 'nt':
            fonts = Path(os.environ.get('WINDIR', r'C:\Windows')) / 'Fonts'
            check(fonts.is_dir(), 'Windows offscreen font directory is unavailable')
            environment['QT_QPA_FONTDIR'] = str(fonts)
        report['tool_sha256'] = {str(path): digest(path) for path in (*tools.values(), python_image())}
        for name, path in tools.items():
            run(name + '-version', [path, '--version'])
        report['ux_base_head'] = run('ux-head', ['git', '-C', ROOT, 'rev-parse', 'HEAD']).strip()
        producer = output / 'producer'
        for relative in report['source_sha256_before']:
            if relative.startswith('tests/'):
                continue
            target = producer / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / relative, target)
        stage, prefix, build = output / 'stage', output / 'relocated', output / 'producer-build'
        common = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
                  '-DCMAKE_CXX_COMPILER=' + str(tools['cxx']),
                  '-DCMAKE_MAKE_PROGRAM=' + str(tools['ninja']),
                  '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF', '-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF']
        run('configure-module', [tools['cmake'], '-S', producer, '-B', build, *common,
            '-DTELEMETRY_UX_BUILD_INSPECTOR=OFF', '-DTELEMETRY_UX_BUILD_CONFIGURATOR=ON',
            '-DCMAKE_DISABLE_FIND_PACKAGE_Telemetry=TRUE', '-DCMAKE_PREFIX_PATH=' + str(qt),
            '-DCMAKE_INSTALL_PREFIX=' + str(stage)])
        run('install-module', [tools['cmake'], '--build', build, '--target', 'install', '-j', args.jobs])
        inside_output(stage)
        inside_output(prefix)
        inside_output(producer)
        inside_output(build)
        stage.rename(prefix)
        producer.rename(output / 'producer.hidden')
        build.rename(output / 'producer-build.hidden')
        report['installed_sha256_before'] = inventory(prefix)
        configs = sorted(prefix.rglob('TelemetryUXConfig.cmake'))
        exports = sorted(prefix.rglob('TelemetryUXTargets.cmake'))
        qmldirs = sorted(prefix.rglob('qmldir'))
        check(len(configs) == len(exports) == len(qmldirs) == 1, 'Require one installed UX package and QML module')
        exported = exports[0].read_text(encoding='utf-8')
        check('add_library(TelemetryUX::configurator STATIC IMPORTED)' in exported, 'Public static view target is missing')
        check('QmlPrivate' not in exported and 'qmlcache' not in exported,
              'Installed view acquired a private QML-cache ABI dependency')
        check(bool(re.search(r'add_library\(TelemetryUX::[^\s]+ OBJECT IMPORTED\)', exported)),
              'Qt resource objects are missing from the installed export')
        for path in prefix.rglob('*.cmake'):
            value = path.read_text(encoding='utf-8')
            check(str(producer).replace('\\', '/') not in value and str(build).replace('\\', '/') not in value,
                  'Installed package retains its producer source/build directory')
        module = qmldirs[0].read_text(encoding='utf-8')
        check('module Telemetry.UX' in module and 'Configurator 1.0 Configurator.qml' in module,
              'Installed URI or public component name/version changed')
        check(not re.search(r'^\s*(?:optional\s+)?plugin\s', module, re.MULTILINE),
              'Pure view unexpectedly requires a runtime plugin')
        check('prefer :/qt-project.org/imports/Telemetry/UX/' in module, 'Qt 6.4 resource import prefix changed')
        check(any(path.endswith('share/telemetry-ux/LICENSE') for path in report['installed_sha256_before']),
              'Installed view license is missing')
        report['module'] = dict(uri='Telemetry.UX', version='1.0', package_version='0.1.0',
            target='TelemetryUX::configurator', resource_prefix='/qt-project.org/imports',
            plugin=False, cachegen=False, sdk_dependency=False, export_sha256=digest(exports[0]),
            qmldir_sha256=digest(qmldirs[0]), config_sha256=digest(configs[0]))
        consumer = output / 'consumer'
        for path in HERE.rglob('*'):
            if path.is_file() and '__pycache__' not in path.parts:
                target = consumer / path.relative_to(HERE)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, target)
        view_args = ['-DCMAKE_PREFIX_PATH=' + ';'.join((str(prefix), str(qt))),
                     '-DTelemetryUX_DIR=' + str(configs[0].parent)]
        consumer_build = output / 'consumer-build'
        run('configure-consumer', [tools['cmake'], '-S', consumer, '-B', consumer_build, *common, *view_args,
            '-DCMAKE_DISABLE_FIND_PACKAGE_Telemetry=TRUE', '-DCMAKE_CXX_FLAGS=-Wall -Wextra -Werror',
            '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'])
        run('build-consumer', [tools['cmake'], '--build', consumer_build, '-j', args.jobs])
        suffix = '.exe' if os.name == 'nt' else ''
        binary = consumer_build / ('telemetry_ux_package_check' + suffix)
        runtime = run('owned-provider', [binary, consumer / 'Main.qml'])
        match = re.search(r'Configurator package checks=(\d+) provider=owned sdk=0 scanner=0 plugin=0 warnings=0', runtime)
        check(match is not None and int(match[1]) == 6, 'Actual owned-provider UI checks did not complete warning-free')
        report['runtime_checks'] = int(match[1])
        report['binary_sha256'] = digest(binary)
        report['compile_commands_sha256'] = digest(consumer_build / 'compile_commands.json')
        controls = []
        for case, success in [('optional', True), ('optional-unknown', True), ('required', False),
                              ('unknown', False), ('default', False)]:
            run('config-' + case, [tools['cmake'], '-S', consumer / 'controls', '-B', output / ('control-' + case),
                *common, '-DTelemetryUX_DIR=' + str(configs[0].parent),
                '-DCMAKE_DISABLE_FIND_PACKAGE_Qt6=TRUE', '-DCASE=' + case], success)
            controls.append(dict(case=case, expected_success=success))
        report['config_controls'] = controls
        run('no-components-built', [tools['cmake'], '-S', output / 'producer.hidden', '-B', output / 'no-components',
            *common, '-DTELEMETRY_UX_BUILD_INSPECTOR=OFF', '-DTELEMETRY_UX_BUILD_CONFIGURATOR=OFF',
            '-DCMAKE_DISABLE_FIND_PACKAGE_Qt6=TRUE', '-DCMAKE_DISABLE_FIND_PACKAGE_Telemetry=TRUE'])
        report['source_sha256_after'] = inputs()
        report['installed_sha256_after'] = inventory(prefix)
        check(report['source_sha256_before'] == report['source_sha256_after'], 'UX source inputs changed during qualification')
        check(report['installed_sha256_before'] == report['installed_sha256_after'], 'Installed prefix changed during qualification')
        report['passed'] = True
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        report['error'] = str(error)
        print(report['error'], flush=True)
    report['finished_utc'] = datetime.now(timezone.utc).isoformat()
    (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
