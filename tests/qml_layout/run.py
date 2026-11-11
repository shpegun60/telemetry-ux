#!/usr/bin/env python3
"""Real Qt window layout at desktop and minimum sizes, without network/device I/O. MIT."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inventory(root):
    return {p.relative_to(root).as_posix(): digest(p) for p in sorted(root.rglob('*'))
            if p.is_file() and '__pycache__' not in p.parts}


def sources():
    paths = [ROOT / 'VERSION', ROOT / 'tests/dynamic_qml_configurator/Fixture.hpp', ROOT / 'lib/qml/CMakeLists.txt',
             ROOT / 'lib/qml/TelemetryUXConfig.cmake.in']
    for directory in (HERE, ROOT / 'lib/qml/Telemetry/UX'):
        paths += [p for p in directory.rglob('*') if p.is_file() and '__pycache__' not in p.parts]
    return {p.relative_to(ROOT).as_posix(): digest(p) for p in sorted(set(paths))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--sdk-prefix', type=Path, required=True)
    parser.add_argument('--qt-prefix', type=Path, required=True)
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ninja', default='ninja')
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    output, sdk, qt = (p.resolve() for p in (args.build_dir, args.sdk_prefix, args.qt_prefix))
    if any(output == p or output in p.parents or p in output.parents for p in (ROOT, sdk, qt)):
        parser.error('output must be outside sources and installed dependencies')
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        parser.error('output must be new or empty; earlier evidence is never replaced')
    configs = list(sdk.rglob('TelemetryConfig.cmake'))
    if len(configs) != 1 or args.jobs < 1:
        parser.error('one installed SDK package and a positive job count are required')
    tools = {name: Path(shutil.which(getattr(args, name)) or getattr(args, name)).absolute()
             for name in ('cxx', 'cmake', 'ninja')}
    if not all(p.is_file() for p in tools.values()):
        parser.error('a required build executable is unavailable')
    output.mkdir(parents=True)
    environment = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', QT_QPA_PLATFORM='offscreen',
                       QT_QUICK_BACKEND='software', QT_QUICK_CONTROLS_STYLE='Basic')
    for name in ('QML_IMPORT_PATH', 'QML2_IMPORT_PATH', 'LSAN_OPTIONS'):
        environment.pop(name, None)
    environment['PATH'] = os.pathsep.join((str(tools['cxx'].parent), str(qt / 'bin'), environment.get('PATH', '')))
    if os.name == 'nt':
        environment['QT_QPA_FONTDIR'] = str(Path(os.environ.get('WINDIR', r'C:\Windows')) / 'Fonts')
    report = dict(passed=False, hardware=False, network=False, started_utc=datetime.now(timezone.utc).isoformat(),
                  commands=[], source_sha256_before=sources(), sdk_sha256_before=inventory(sdk),
                  tools={name: dict(path=str(path), sha256=digest(path)) for name, path in tools.items()})

    def run(name, command):
        command = list(map(str, command))
        result = subprocess.run(command, cwd=output, env=environment, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, encoding='utf8', errors='replace', timeout=600)
        log = output / (name + '.log')
        log.write_text(result.stdout, encoding='utf8')
        report['commands'].append(dict(name=name, argv=command, exit=result.returncode, log=log.name, sha256=digest(log)))
        print(name + ': ' + str(result.returncode), flush=True)
        if result.returncode:
            raise RuntimeError(name + ' failed: ' + result.stdout[-6000:])
        return result.stdout

    try:
        run('configure', [tools['cmake'], '-S', HERE, '-B', output / 'build', '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_CXX_COMPILER='+str(tools['cxx']),
            '-DCMAKE_MAKE_PROGRAM='+str(tools['ninja']), '-DCMAKE_PREFIX_PATH='+str(qt),
            '-DTelemetry_DIR='+str(configs[0].parent), '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF',
            '-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF', '-DCMAKE_CXX_FLAGS=-Wall -Wextra -Werror'])
        run('build', [tools['cmake'], '--build', output / 'build', '--parallel', args.jobs])
        binary = output / 'build' / ('telemetry_qml_layout_check.exe' if os.name == 'nt' else 'telemetry_qml_layout_check')
        report['executable'] = dict(sha256=digest(binary), bytes=binary.stat().st_size)
        text = run('runtime', [binary, output])
        match = re.search(r'QML layout checks=(\d+) warnings=0', text)
        if not match or int(match[1]) < 65:
            raise RuntimeError('missing successful geometry/warning contract')
        report['checks'] = int(match[1])
        search_binary = output / 'build' / ('telemetry_qml_search_providers.exe' if os.name == 'nt' else 'telemetry_qml_search_providers')
        search_text = run('search-providers', [search_binary])
        search_match = re.search(r'QML search native-provider checks=(\d+) warnings=0', search_text)
        if not search_match or int(search_match[1]) < 29:
            raise RuntimeError('missing native-provider search contract')
        report['search_providers'] = dict(checks=int(search_match[1]), executable_sha256=digest(search_binary))
        bulk_binary = output / 'build' / ('telemetry_qml_bulk_selection.exe' if os.name == 'nt' else 'telemetry_qml_bulk_selection')
        bulk_text = run('bulk-selection', [bulk_binary])
        bulk_match = re.search(r'QML bulk-selection checks=(\d+) warnings=0', bulk_text)
        if not bulk_match or int(bulk_match[1]) < 25:
            raise RuntimeError('missing bulk-selection UI contract')
        report['bulk_selection'] = dict(checks=int(bulk_match[1]), executable_sha256=digest(bulk_binary))
        report['screenshots'] = {p.name: digest(p) for p in sorted(output.glob('*.png'))}
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
    finally:
        report['source_sha256_after'] = sources()
        report['sdk_sha256_after'] = inventory(sdk)
        if report['source_sha256_before'] != report['source_sha256_after'] or report['sdk_sha256_before'] != report['sdk_sha256_after']:
            report.update(passed=False, error='source or installed SDK changed during qualification')
        (output / 'summary.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf8')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
