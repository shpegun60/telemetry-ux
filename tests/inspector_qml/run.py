#!/usr/bin/env python3
"""Execute real Qt Inspector models, controller and offscreen QML (MIT).

Requires Qt >= 6.4. All builds/logs/receipts remain in a fresh external directory.
Source and selected tool/runtime hashes identify inputs, not a hermetic SDK.
"""
import argparse
import ctypes
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def screenshot_record(path):
    png = path.read_bytes()
    if (png[:8] != b'\x89PNG\r\n\x1a\n' or len(png) < 24 or png[12:16] != b'IHDR'):
        raise RuntimeError('Screenshot is not a PNG image: ' + str(path))
    width, height = struct.unpack('>II', png[16:24])
    if width < 1 or height < 1:
        raise RuntimeError('Screenshot is empty: ' + str(path))
    return dict(path=path.name, sha256=digest(path), width=width, height=height, bytes=len(png))


def executable(name):
    # Preserve clang++ invocation spelling instead of switching its driver mode.
    path = Path(shutil.which(name) or name).absolute()
    if not path.is_file():
        raise ValueError('Required executable not found: ' + str(path))
    return path


def python_image():
    if os.name == 'nt':
        buffer = ctypes.create_unicode_buffer(32768)
        size = ctypes.windll.kernel32.GetModuleFileNameW(None, buffer, len(buffer))
        if not size or size >= len(buffer):
            raise OSError('Cannot identify Python executable image')
        return Path(buffer.value)
    return Path(sys.executable).resolve()


def sources():
    paths = []
    for directory in (HERE, ROOT / 'apps/inspector', ROOT / 'lib/qml'):
        paths += [path for path in directory.rglob('*') if path.is_file() and
                  path.suffix in ('.py', '.cpp', '.hpp', '.h', '.qml', '.txt', '.cmake', '.pri') and
                  '__pycache__' not in path.parts]
    return {path.relative_to(ROOT).as_posix(): digest(path) for path in sorted(set(paths))}


def sdk_inputs(prefix):
    # Fingerprint the actual installed SDK, never an inferred source checkout.
    return {path.relative_to(prefix).as_posix(): digest(path)
            for path in sorted(prefix.rglob('*')) if path.is_file()}


def sdk_identity(prefix):
    configs = sorted(prefix.rglob('TelemetryConfig.cmake'))
    metadata_files = sorted(prefix.rglob('SDKMetadata.json'))
    if len(configs) != 1 or len(metadata_files) != 1:
        raise RuntimeError('--sdk-prefix must contain one installed Telemetry package and SDKMetadata.json')
    metadata = metadata_files[0]
    value = json.loads(metadata.read_text(encoding='utf-8'))
    if (not isinstance(value, dict) or value.get('sdkVersion') != '0.1.0' or
            value.get('descriptorWireVersion') != [3, 0] or
            any(type(n) is not int for n in value['descriptorWireVersion']) or
            type(value.get('encodedWireVersion')) is not int or value['encodedWireVersion'] != 1 or
            not isinstance(value.get('sourceRevision'), str) or
            (value['sourceRevision'] and not re.fullmatch('[0-9a-fA-F]{40}', value['sourceRevision']))):
        raise RuntimeError('Installed SDK metadata is incompatible with this Inspector')
    return configs[0].parent, dict(prefix=str(prefix),
                                  metadata_path=metadata.relative_to(prefix).as_posix(),
                                  metadata_sha256=digest(metadata), metadata=value,
                                  sha256_before=sdk_inputs(prefix))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True, type=Path)
    parser.add_argument('--qt-prefix', required=True, type=Path)
    parser.add_argument('--sdk-prefix', required=True, type=Path,
                        help='Installed SDK prefix, independent of its source checkout')
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ninja', default='ninja')
    parser.add_argument('--jobs', default='2')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    if args.sanitize and os.name == 'nt':
        parser.error('--sanitize requires a Linux sanitizer toolchain')
    sdk = args.sdk_prefix.resolve()
    if not sdk.is_dir() or sdk == ROOT or ROOT in sdk.parents:
        parser.error('--sdk-prefix must be an installed SDK directory outside the UX repository')
    output = args.build_dir.resolve()
    if output == sdk or sdk in output.parents or output in sdk.parents:
        parser.error('--build-dir and --sdk-prefix must be separate directory trees')
    if output == ROOT or ROOT in output.parents or output in ROOT.parents:
        parser.error('--build-dir must be a dedicated directory outside the repository')
    if output.exists() and any(output.iterdir()):
        parser.error('--build-dir must be fresh and empty')
    output.mkdir(parents=True, exist_ok=True)
    report = dict(passed=False, hardware=False, sanitize=args.sanitize,
                  started_utc=datetime.now(timezone.utc).isoformat(),
                  commands=[], executions={}, source_sha256_before=sources())
    environment = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', QT_QPA_PLATFORM='offscreen',
                       QT_QUICK_BACKEND='software', QSG_RHI_BACKEND='software',
                       QT_FORCE_STDERR_LOGGING='1')
    if args.sanitize:
        # Qt's offscreen plugin retains process-global allocations. This profile
        # checks ASan/UBSan errors and does not claim LeakSanitizer coverage.
        environment['ASAN_OPTIONS'] = 'detect_leaks=0:halt_on_error=1'
        environment['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
        report['sanitizers'] = ['address', 'undefined']
        report['leak_checking'] = False
        report['leak_checking_reason'] = 'Qt offscreen plugin process-global allocation lifetime'

    def run(name, command):
        started = time.monotonic()
        command = list(map(str, command))
        try:
            result = subprocess.run(command, cwd=ROOT, env=environment, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
            text, code = result.stdout, result.returncode
        except subprocess.TimeoutExpired as error:
            text = error.stdout or ''
            if isinstance(text, bytes):
                text = text.decode(errors='replace')
            code = None
        log = output / (name + '.log')
        log.write_text(text, encoding='utf-8')
        report['commands'].append(dict(name=name, command=command, exit=code,
                                       seconds=time.monotonic() - started, log=log.name,
                                       sha256=digest(log)))
        print(name + ': exit ' + str(code), flush=True)
        if code != 0:
            raise RuntimeError(name + ' failed\n' + text[-16000:])
        return text

    try:
        sdk_config, report['sdk'] = sdk_identity(sdk)
        if os.name == 'nt':
            fonts = Path(os.environ.get('WINDIR', r'C:\Windows')) / 'Fonts'
            if not fonts.is_dir():
                raise RuntimeError('Windows offscreen font directory is unavailable: ' + str(fonts))
            environment['QT_QPA_FONTDIR'] = str(fonts)
            report['font_directory'] = str(fonts)
        for name in ('Models.hpp', 'Models.cpp', 'InspectorController.hpp', 'InspectorController.cpp',
                     'DemoPeer.hpp', 'DemoPeer.cpp', 'Main.qml', 'CMakeLists.txt', 'main.cpp'):
            if not (ROOT / 'apps/inspector' / name).is_file():
                raise RuntimeError('Required runnable example input is missing: ' + name)
        tools = {name: executable(getattr(args, name)) for name in ('cxx', 'cmake', 'ninja')}
        qt = args.qt_prefix.resolve()
        environment['PATH'] = os.pathsep.join((str(tools['cxx'].parent), str(qt / 'bin'),
                                               environment.get('PATH', '')))
        report['tool_sha256'] = {str(path): digest(path) for path in (*tools.values(), python_image())}
        for name, path in tools.items():
            run(name + '-version', [path, '--version'])
        run('python-version', [sys.executable, '--version'])
        report['source_head'] = run('source-head', ['git', 'rev-parse', 'HEAD']).strip()
        build = output / 'build'
        configure = [tools['cmake'], '-S', HERE, '-B', build, '-G', 'Ninja',
                     '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_CXX_COMPILER=' + str(tools['cxx']),
                     '-DCMAKE_MAKE_PROGRAM=' + str(tools['ninja']),
                     '-DCMAKE_PREFIX_PATH=' + ';'.join((str(sdk), str(qt))),
                     '-DTelemetry_DIR=' + str(sdk_config),
                     '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF',
                     '-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF',
                     '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON']
        flags = '-Wall -Wextra -Werror'
        if args.sanitize:
            flags += ' -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
            configure += ['-DCMAKE_CXX_FLAGS_RELEASE=-O1 -DNDEBUG',
                          '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined']
        run('configure', configure + ['-DCMAKE_CXX_FLAGS=' + flags])
        run('build', [tools['cmake'], '--build', build, '-j', args.jobs])
        report['compile_commands_sha256'] = digest(build / 'compile_commands.json')
        suffix = '.exe' if os.name == 'nt' else ''
        check = build / ('telemetry_inspector_qml_check' + suffix)
        runtime = run('runtime', [check])
        counts = re.search(r'Inspector Qt checks=(\d+) reads=(\d+) Qt=(\S+)', runtime)
        if not counts or int(counts[1]) < 763 or int(counts[2]) < 23:
            raise RuntimeError('Runtime did not execute the maintained model/controller checks')
        disconnected = re.findall(r'Inspector Apply disconnect phase=(queued|offered|prepared|executed|write_confirmed) '
                                  r'finished=(\d+) writes=(\d+)', runtime)
        if disconnected != [('queued', '1', '0'), ('offered', '1', '0'), ('prepared', '1', '0'),
                            ('executed', '1', '1'), ('write_confirmed', '1', '1')]:
            raise RuntimeError('Explicit Disconnect did not cover all five Apply phases exactly once')
        report['executions']['apply_disconnect_phases'] = [phase[0] for phase in disconnected]
        if 'Inspector write history: success=1 unknown=1 reset=1 refused=1 callbacks=3' not in runtime:
            raise RuntimeError('Write history lost its session identity, outcome or exact-once completion')
        report['executions']['write_history_sessions'] = True
        report['executions']['checks'], report['executions']['reads'] = map(int, counts.groups()[:2])
        report['qt_version'] = counts[3]
        physical_input = build / ('telemetry_inspector_qml_input_check' + suffix)
        typed = run('qml-physical-input', [physical_input])
        typed_counts = re.search(r'Inspector QML physical input checks=(\d+) writes=2 noEnter=2 '
                                 r'gain=4\.25 counter=18446744073709551614 Qt=(\S+)', typed)
        if not typed_counts or int(typed_counts[1]) < 58 or typed_counts[2] != counts[3]:
            raise RuntimeError('Actual keyboard/mouse input did not complete both no-Enter Apply workflows')
        report['executions']['physical_input_checks'] = int(typed_counts[1])
        report['executions']['keyboard_mouse_no_enter_applies'] = 2
        command_check = build / ('telemetry_inspector_qml_command_check' + suffix)
        command_screenshot = output / 'command-window.png'
        command_output = run('qml-command', [command_check, '--screenshot', command_screenshot])
        command_counts = re.search(r'Inspector Command checks=(\d+) executed=1 accepted=1 '
                                   r'unknown=1 noEnter=1 Qt=(\S+)', command_output)
        if not command_counts or int(command_counts[1]) < 112 or command_counts[2] != counts[3]:
            raise RuntimeError('Command controls did not preserve execution, acceptance and uncertainty')
        report['executions']['command_checks'] = int(command_counts[1])
        report['executions']['command_keyboard_mouse_no_enter'] = 1
        report['command_screenshot'] = screenshot_record(command_screenshot)
        service_check = build / ('telemetry_inspector_qml_service_check' + suffix)
        service_screenshot = output / 'service-window.png'
        service_output = run('qml-service', [service_check, '--screenshot', service_screenshot])
        service_counts = re.search(r'Inspector Service checks=(\d+) completed=1 rejected=1 '
                                   r'unknown=1 noEnter=1 Qt=(\S+)', service_output)
        if not service_counts or int(service_counts[1]) < 126 or service_counts[2] != counts[3]:
            raise RuntimeError('Service controls did not preserve request, response and uncertainty')
        report['executions']['service_checks'] = int(service_counts[1])
        report['executions']['service_keyboard_mouse_no_enter'] = 1
        report['service_screenshot'] = screenshot_record(service_screenshot)
        watch_check = build / ('telemetry_inspector_qml_watchlist_check' + suffix)
        watch_screenshot = output / 'watchlist-window.png'
        watched = run('qml-watchlist', [watch_check, '--screenshot', watch_screenshot])
        watch_counts = re.search(r'Inspector Watchlist checks=(\d+) limit=32 exact=1 '
                                 r'skippedTicks=1 fallback=1 qml=1 Qt=(\S+)', watched)
        if not watch_counts or int(watch_counts[1]) < 100 or watch_counts[2] != counts[3]:
            raise RuntimeError('Watchlist did not prove bounded polling, exact values and explicit fallback')
        report['executions']['watchlist_checks'] = int(watch_counts[1])
        report['watchlist_screenshot'] = screenshot_record(watch_screenshot)
        watch_failures = build / ('telemetry_inspector_qml_watchlist_failures_check' + suffix)
        failed_paths = run('qml-watchlist-failures', [watch_failures])
        failed_counts = re.search(r'Inspector Watchlist Failures checks=(\d+) paths=5 qml=1 '
                                  r'drain=1 acceptedMutations=9 refusedMutations=3 '
                                  r'priorQueued=3 priorHanded=3 failures=0 Qt=(\S+)', failed_paths)
        if not failed_counts or int(failed_counts[1]) < 800 or failed_counts[2] != counts[3]:
            raise RuntimeError('Watchlist did not retire live QML values on all foreground failure paths')
        report['executions']['watchlist_failure_checks'] = int(failed_counts[1])
        stale_display = re.findall(r'Watchlist stale endpoint phase=(queued-watch|handed-watch) '
                                   r'rows=(\d+) pending=1 requestDelta=(\d+)', failed_paths)
        if ([(phase, delta) for phase, _, delta in stale_display] !=
                [('queued-watch', '0'), ('handed-watch', '1')] or
                any(int(rows) < 1 for _, rows, _ in stale_display)):
            raise RuntimeError('Live endpoint delegates did not reread stale display roles before both drains')
        report['executions']['watchlist_stale_display_phases'] = [phase for phase, _, _ in stale_display]
        watch_scheduling = build / ('telemetry_inspector_qml_watchlist_scheduling_check' + suffix)
        scheduled = run('qml-watchlist-scheduling', [watch_scheduling])
        schedule_counts = re.search(r'Inspector Watchlist scheduling checks=(\d+) failures=0 '
                                    r'cycleWindow=1 continuation=1 stop=1 qmlGap=1 Qt=(\S+)', scheduled)
        if not schedule_counts or int(schedule_counts[1]) < 272 or schedule_counts[2] != counts[3]:
            raise RuntimeError('Watchlist logical-cycle guards or QML controls did not execute')
        report['executions']['watchlist_scheduling_checks'] = int(schedule_counts[1])
        embedding = build / ('telemetry_configurator_embedding_check' + suffix)
        embedding_screenshot = output / 'embedding-window.png'
        embedded = run('configurator-embedding', [embedding, '--screenshot', embedding_screenshot])
        embedding_counts = re.search(r'Configurator embedding checks=(\d+) provider=explicit '
                                     r'context=0 writes=1 normalized=10 unload=1 Qt=(\S+)', embedded)
        if not embedding_counts or int(embedding_counts[1]) < 55 or embedding_counts[2] != counts[3]:
            raise RuntimeError('Alternate host did not exercise the explicit provider and required dependency')
        report['executions']['embedding_checks'] = int(embedding_counts[1])
        report['embedding_screenshot'] = screenshot_record(embedding_screenshot)
        missing = run('configurator-required-provider', [embedding, '--missing-provider'])
        missing_counts = re.search(r'Configurator missing provider checks=(\d+) rejected=1 required=1', missing)
        if not missing_counts or int(missing_counts[1]) != 3:
            raise RuntimeError('Missing provider did not produce the specific required-property refusal')
        report['executions']['required_provider_negative_checks'] = int(missing_counts[1])
        app = build / 'example' / ('telemetry-inspector-qml' + suffix)
        screenshot = output / 'window.png'
        smoke = run('qml-smoke', [app, '--smoke', '--screenshot', screenshot])
        views = re.search(r'Inspector QML smoke: published=1 roots=1 endpointRows=(\d+) valueRows=(\d+)', smoke)
        if not views or int(views[1]) < 1 or int(views[2]) < 4:
            raise RuntimeError('Offscreen QML did not confirm actual endpoint and value delegates')
        report['executions']['offscreen_smoke'] = 1
        report['executions']['visible_endpoint_rows'], report['executions']['visible_value_rows'] = map(int, views.groups())
        report['screenshot'] = screenshot_record(screenshot)
        edit_screenshot = output / 'edit-window.png'
        edited = run('qml-edit-smoke', [app, '--edit-smoke', '--screenshot', edit_screenshot])
        if 'Inspector QML edit smoke: writes=1 refreshed=1 gain=10 controls=Edit,TextField,Apply' not in edited:
            raise RuntimeError('QML controls did not complete the actual Write and normalized readback')
        report['executions']['offscreen_edit_smoke'] = 1
        report['edit_screenshot'] = screenshot_record(edit_screenshot)
        report['binary_sha256'] = {str(path.relative_to(output)): digest(path)
                                  for path in (check, physical_input, command_check, service_check,
                                               watch_check, watch_failures, watch_scheduling, embedding, app)}
        report['qt_runtime_sha256'] = {str(path.relative_to(qt)): digest(path)
                                       for path in (qt / 'bin').glob('Qt6*.dll') if path.is_file()}
        report['source_sha256_after'] = sources()
        if report['source_sha256_before'] != report['source_sha256_after']:
            raise RuntimeError('Source inputs changed during qualification')
        report['sdk']['sha256_after'] = sdk_inputs(sdk)
        if report['sdk']['sha256_before'] != report['sdk']['sha256_after']:
            raise RuntimeError('Installed SDK inputs changed during qualification')
        report['passed'] = True
    except (ValueError, OSError, RuntimeError, subprocess.SubprocessError) as error:
        report['error'] = str(error)
        print(report['error'], flush=True)
    report['finished_utc'] = datetime.now(timezone.utc).isoformat()
    (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
