#!/usr/bin/env python3
"""Build a relocatable Inspector from explicit SDK/Qt inputs. No hardware. MIT."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--sdk-root', type=Path, required=True)
    parser.add_argument('--sdk-revision', required=True)
    parser.add_argument('--ux-revision', required=True)
    parser.add_argument('--qt-prefix', type=Path, required=True)
    parser.add_argument('--cobs-root', type=Path)
    parser.add_argument('--no-serial', action='store_true')
    parser.add_argument('--cxx', required=True)
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ninja', default='ninja')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('This producer requires Windows and one matching Qt/MinGW kit')
    output, sdk, qt = (p.resolve() for p in (args.output, args.sdk_root, args.qt_prefix))
    sources = [ROOT, sdk, qt]
    if args.cobs_root:
        sources.append(args.cobs_root.resolve())
    if any(output == p or p in output.parents or output in p.parents for p in sources):
        parser.error('--output must be separate from every source/tool prefix')
    if output.exists():
        parser.error('--output must be new; existing files are never removed')
    if not args.no_serial and not args.cobs_root:
        parser.error('The optional serial SDK producer requires --cobs-root')
    for root, expected in ((sdk, args.sdk_revision), (ROOT, args.ux_revision)):
        if not re.fullmatch('[0-9a-f]{40}', expected) or git(root, 'rev-parse', 'HEAD') != expected:
            parser.error('Source checkout must match the full expected commit: ' + str(root))
        if git(root, 'status', '--porcelain', '--untracked-files=no'):
            parser.error('Tracked source changes must be committed: ' + str(root))
    tools = {name: Path(shutil.which(getattr(args, name)) or getattr(args, name)).absolute()
             for name in ('cxx', 'cmake', 'ninja')}
    tools['deploy'] = qt / 'bin/windeployqt.exe'
    if not all(path.is_file() for path in tools.values()):
        parser.error('Missing compiler, CMake, Ninja or Qt deployment tool')
    output.mkdir(parents=True)
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1')
    env['PATH'] = os.pathsep.join((str(tools['cxx'].parent), str(qt/'bin'), env.get('PATH', '')))
    report = dict(passed=False, hardware=False, sdk_revision=args.sdk_revision,
                  ux_revision=args.ux_revision, serial_compiled=not args.no_serial,
                  started_utc=datetime.now(timezone.utc).isoformat(), commands=[],
                  tools_sha256={name: digest(path) for name, path in tools.items()})

    def run(name, command):
        command = list(map(str, command))
        process = subprocess.run(command, env=env, cwd=output, capture_output=True, timeout=900)
        log = output/(name+'.log')
        log.write_bytes(process.stdout+process.stderr)
        report['commands'].append(dict(name=name, command=command, exit=process.returncode,
                                       log=log.name, sha256=digest(log)))
        print(name, process.returncode, flush=True)
        if process.returncode:
            raise RuntimeError(log.read_text(encoding='utf8', errors='replace')[-8000:])
        return process.stdout.decode('utf8', errors='replace').strip()

    try:
        common = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
                  '-DCMAKE_CXX_COMPILER='+str(tools['cxx']),
                  '-DCMAKE_MAKE_PROGRAM='+str(tools['ninja']), '-DCMAKE_PREFIX_PATH='+str(qt),
                  '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF', '-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF']
        components = 'inspector;workbench;read_many_workbench;encoded_backend;encoded_server;core;tcp_server;qt_transport'
        if not args.no_serial:
            components += ';qt_serial_transport'
        prefix = output/'sdk-prefix'
        sdk_options = ['-DTELEMETRY_COMPONENTS='+components,
                       '-DTELEMETRY_SOURCE_REVISION='+args.sdk_revision,
                       '-DCMAKE_INSTALL_PREFIX='+str(prefix)]
        if args.cobs_root:
            sdk_options.append('-DCOBS_ROOT='+str(args.cobs_root.resolve()))
            report['cobs_revision'] = git(args.cobs_root, 'rev-parse', 'HEAD')
        run('sdk-configure', [tools['cmake'], '-S', sdk, '-B', output/'sdk-build', *common, *sdk_options])
        run('sdk-install', [tools['cmake'], '--build', output/'sdk-build', '--target', 'install', '--parallel', '2'])
        metadata = json.loads((prefix/'share/telemetry/SDKMetadata.json').read_text())
        if metadata['sourceRevision'] != args.sdk_revision or metadata['sdkVersion'] != '0.1.0':
            raise RuntimeError('Installed SDK identity mismatch')
        report['sdk_metadata'] = metadata
        run('app-configure', [tools['cmake'], '-S', ROOT/'apps/inspector', '-B', output/'app-build',
            *common, '-DCMAKE_WIN32_EXECUTABLE=ON',
            '-DTelemetry_DIR='+str(prefix/'lib/cmake/Telemetry'),
            '-DINSPECTOR_ENABLE_SERIAL='+('OFF' if args.no_serial else 'ON')])
        run('app-build', [tools['cmake'], '--build', output/'app-build', '--parallel', '2'])
        package = output/'package'
        package.mkdir()
        executable = package/'telemetry-inspector-qml.exe'
        shutil.copyfile(output/'app-build'/executable.name, executable)
        run('deploy', [tools['deploy'], '--release', '--no-translations', '--compiler-runtime',
            '--qmldir', ROOT/'apps/inspector', '--qmlimport', output/'app-build/qml', executable])
        # Offscreen is used only by the separate clean-runner qualification.
        shutil.copyfile(qt/'plugins/platforms/qoffscreen.dll', package/'platforms/qoffscreen.dll')
        (package/'qt.conf').write_text('[Paths]\nPrefix=.\nPlugins=.\nQmlImports=qml\n')
        notices = package/'licenses'
        notices.mkdir()
        for name, source in (('Telemetry-UX.txt', ROOT/'LICENSE'), ('Telemetry-SDK.txt', sdk/'LICENSE')):
            shutil.copyfile(source, notices/name)
        shutil.copytree(prefix/'share/telemetry', notices/'sdk')
        for name, source in (('MinGW', tools['cxx'].parent.parent/'licenses'),
                             ('Qt', qt/'LICENSES')):
            if source.is_dir():
                shutil.copytree(source, notices/name)
        if args.cobs_root:
            shutil.copyfile(args.cobs_root/'LICENSE', notices/'COBS.txt')
        (package/'README.txt').write_text(
            'Telemetry Inspector / Workbench - Windows x64\n\n'
            'Extract the entire ZIP and run telemetry-inspector-qml.exe.\n'
            'Qt, Python and a compiler are not needed to run it.\n'
            'Keep DLLs, qml, platforms and qt.conf alongside the executable.\n'
            'Demo device works without hardware; writable Demo supports explicit edits, Commands and Services.\n'
            'TCP and optional UART require a compatible device. This package does not flash a board.\n'
            'Mutations require explicit opt-in and are not replayed after an unknown outcome.\n'
            'BUILD-IDENTITY.json and FILES-SHA256.json identify this development build.\n'
            'Qt components are dynamically linked; see https://www.qt.io/licensing/ and shipped notices.\n', encoding='utf8')
        qt_version = run('qt-version', [qt/'bin/qmake.exe', '-query', 'QT_VERSION'])
        identity = dict(sdk_revision=args.sdk_revision, ux_revision=args.ux_revision,
                        qt=qt_version, architecture='x86_64', build='Release, Windows GUI subsystem',
                        serial_compiled=not args.no_serial, hardware_tested=False)
        (package/'BUILD-IDENTITY.json').write_text(json.dumps(identity, indent=2)+'\n')
        files = {p.relative_to(package).as_posix(): digest(p) for p in sorted(package.rglob('*')) if p.is_file()}
        (package/'FILES-SHA256.json').write_text(json.dumps(files, indent=2)+'\n')
        archive = output/'Telemetry-Inspector-Windows-x64.zip'
        with zipfile.ZipFile(archive, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as bundle:
            for p in sorted(package.rglob('*')):
                if p.is_file():
                    bundle.write(p, p.relative_to(package).as_posix())
        with zipfile.ZipFile(archive) as bundle:
            if bundle.testzip() is not None:
                raise RuntimeError('ZIP CRC failure')
        archive_hash = digest(archive)
        (output/(archive.name+'.sha256')).write_text(archive_hash+'  '+archive.name+'\n')
        for root, expected in ((sdk, args.sdk_revision), (ROOT, args.ux_revision)):
            if git(root, 'rev-parse', 'HEAD') != expected or git(root, 'status', '--porcelain', '--untracked-files=no'):
                raise RuntimeError('Source checkout changed during packaging')
        report.update(passed=True, archive_sha256=archive_hash, archive_bytes=archive.stat().st_size,
                      package_files=len(files)+1, identity=identity)
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        report['error'] = str(error)
        print(str(error), flush=True)
    finally:
        report['finished_utc'] = datetime.now(timezone.utc).isoformat()
        (output/'build-summary.json').write_text(json.dumps(report, indent=2)+'\n')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
