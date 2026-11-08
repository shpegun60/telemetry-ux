#!/usr/bin/env python3
"""Qualify generated applications outside their producer, including refusal controls. MIT."""
import argparse
from datetime import datetime, timezone
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile


ROOT = Path(__file__).absolute().parents[2]
TEMPLATES = ('examples/dynamic_qml/CMakeLists.txt', 'examples/dynamic_qml/Device.hpp',
             'examples/dynamic_qml/main.cpp', 'examples/dynamic_qml/Main.qml',
             'examples/web_device_starter/prepare.py', 'examples/web_device_starter/serve.py',
             'examples/web_device_starter/DeviceApp.js', 'examples/web_device_starter/index.html',
             'examples/web_device_starter/favicon.svg', 'tools/export_web_assets.py',
             'examples/web_device_starter/native/CMakeLists.txt',
             'examples/web_device_starter/native/Device.hpp',
             'examples/web_device_starter/native/Peer.cpp',
             'examples/web_demo/serve.py', 'examples/web_demo/serve_ws.py',
             'examples/qt_connection/CMakeLists.txt', 'examples/qt_connection/main.cpp',
             'examples/qt_connection/Main.qml')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def hashes(root):
    return {path.relative_to(root).as_posix(): digest(path)
            for path in sorted(root.rglob('*')) if path.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--sdk-prefix', type=Path)
    parser.add_argument('--ux-prefix', type=Path)
    parser.add_argument('--qt-prefix', type=Path)
    parser.add_argument('--sdk-package', type=Path)
    parser.add_argument('--ux-package', type=Path)
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ninja', default='ninja')
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--sanitize', action='store_true', help='Match ASan/UBSan-instrumented installed native packages')
    parser.add_argument('--qt-connection', action='store_true',
                        help='Also build the generated owned-connection target against installed qt_connection packages')
    parser.add_argument('--node', default='node')
    parser.add_argument('--npm-cli', type=Path)
    parser.add_argument('--browser-python', type=Path,
                        help='Python with Playwright/Chromium and websockets for the maintained real browser journey')
    args = parser.parse_args()
    output = args.output.absolute()
    if output.exists() or output == ROOT or ROOT in output.parents:
        parser.error('--output must be a fresh directory outside this checkout')
    qt = (args.sdk_prefix, args.ux_prefix, args.qt_prefix)
    if (args.ux_prefix or args.qt_prefix) and not all(qt):
        parser.error('SDK, UX and Qt prefixes are required together for installed QML qualification')
    if args.qt_connection and not all(qt):
        parser.error('--qt-connection requires SDK, UX and Qt prefixes')
    if (args.sdk_package is None) != (args.ux_package is None):
        parser.error('Both npm packages are required together')
    if args.sdk_package and (not args.sdk_prefix or not args.browser_python):
        parser.error('Web qualification requires --sdk-prefix and --browser-python')
    output.mkdir(parents=True)
    report = {'passed': False, 'hardware': False, 'checks': 0, 'commands': [],
              'qt_connection_requested': args.qt_connection,
              'started_utc': datetime.now(timezone.utc).isoformat()}
    before = {relative: digest(ROOT / relative) for relative in (*TEMPLATES, 'tools/create_project.py')}
    report['source_sha256_before'] = before
    environment = dict(os.environ, PYTHONDONTWRITEBYTECODE='1')
    if args.cxx != 'g++':
        environment['PATH'] = str(Path(args.cxx).absolute().parent) + os.pathsep + environment.get('PATH', '')

    def check(condition, message):
        report['checks'] += 1
        if not condition:
            raise AssertionError(message)

    def run(label, command, *, expected=0, cwd=output, env=environment):
        result = subprocess.run(list(map(str, command)), cwd=cwd, env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=900)
        log = output / (label + '.log')
        log.write_text(result.stdout, encoding='utf8')
        report['commands'].append({'name': label, 'command': list(map(str, command)),
                                  'exit': result.returncode, 'log': log.name, 'sha256': digest(log)})
        check((result.returncode == 0) if expected == 0 else (result.returncode != 0),
              label + ': unexpected exit; inspect ' + str(log))
        return result.stdout

    def creator(label, destination, *options, refused=False, target='dynamic-qml', name='MyDevice'):
        return run(label, [sys.executable, '-B', producer / 'tools/create_project.py', '--target', target,
                          '--name', name, '--output', destination, *options], expected=1 if refused else 0)

    try:
        # The producer fixture contains precisely the public template allowlist.
        # It can be hidden without renaming any actual checkout source.
        producer = output / 'producer'
        for relative in (*TEMPLATES, 'tools/create_project.py'):
            destination = producer / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / relative, destination)
        first, second, web = output / 'qml-a', output / 'qml-b', output / 'web-app'
        creator('create-qml-a', first)
        creator('create-qml-b', second)
        creator('create-web', web, target='web-device')
        check((web / 'tools/export_web_assets.py').is_file(), 'Created Web project owns its deploy exporter')
        check((web / 'examples/web_device_starter/favicon.svg').is_file(), 'Created Web project owns its local favicon')
        connection, connection_again = output / 'connection-a', output / 'connection-b'
        creator('create-connection-a', connection, target='qt-connection')
        creator('create-connection-b', connection_again, target='qt-connection')
        check(hashes(first) == hashes(second), 'Same templates/options produce deterministic project bytes')
        check(hashes(connection) == hashes(connection_again), 'Owned-connection project creation is deterministic')
        check(set(hashes(connection)) == {'CMakeLists.txt', 'main.cpp', 'Main.qml',
                                        'project-options.json', 'README.md', 'project-manifest.json'},
              'Owned-connection project contains only the three public source templates and creator metadata')
        for project in (first, web, connection):
            manifest = json.loads((project / 'project-manifest.json').read_text(encoding='utf8'))
            for relative, expected in manifest['files'].items():
                check(digest(project / relative) == expected, 'Published file hash: ' + relative)
            check(not any(str(producer).encode() in path.read_bytes() for path in project.rglob('*') if path.is_file()),
                  'Generated project contains no absolute producer paths')
            check(not any(part in ('__pycache__', 'node_modules', 'build') for path in project.rglob('*') for part in path.parts),
                  'Only application sources were copied')

        empty = output / 'existing-empty'
        empty.mkdir()
        creator('refuse-empty-directory', empty, refused=True)
        check(not list(empty.iterdir()), 'Existing empty directory remains untouched')
        sentinel = output / 'existing'
        sentinel.mkdir()
        (sentinel / 'keep.txt').write_text('keep this application\n', encoding='utf8')
        original = hashes(sentinel)
        creator('refuse-existing-project', sentinel, refused=True)
        check(hashes(sentinel) == original, 'Existing application remains byte-identical')
        creator('refuse-output-file', sentinel / 'keep.txt', refused=True)
        creator('refuse-name-syntax', output / 'bad-name', name='bad);message(FATAL_ERROR boom)', refused=True)
        check(not (output / 'bad-name').exists(), 'Invalid name fails before destination creation')
        creator('refuse-reserved-name', output / 'reserved', name='CON', refused=True)
        check(not (output / 'reserved').exists(), 'Reserved name fails before destination creation')
        creator('refuse-traversal', str(output / 'unused') + '/../traversal', refused=True)
        check(not (output / 'unused').exists() and not (output / 'traversal').exists(), 'Traversal creates no parents')
        creator('refuse-producer-output', producer / 'generated', refused=True)
        check(not (producer / 'generated').exists(), 'Producer sources remain outside generated output')
        upper = output / 'UpperCase'
        upper.mkdir()
        creator('refuse-case-alias', output / 'uppercase' / 'child', refused=True)
        check(not (upper / 'child').exists(), 'Case alias creates no project')
        creator('refuse-partial-options', output / 'partial', '--sdk-prefix', upper, refused=True)
        check(not (output / 'partial').exists(), 'Partial options fail before destination creation')
        creator('refuse-wrong-target-option', output / 'wrong-option', '--sdk-package', upper, refused=True)
        check(not (output / 'wrong-option').exists(), 'Inapplicable option fails before destination creation')
        creator('refuse-connection-partial-options', output / 'connection-partial',
                '--sdk-prefix', upper, refused=True, target='qt-connection')
        check(not (output / 'connection-partial').exists(), 'Connection partial prefixes create no destination')
        creator('refuse-connection-package-option', output / 'connection-package',
                '--sdk-package', upper, refused=True, target='qt-connection')
        check(not (output / 'connection-package').exists(), 'Connection target refuses npm-only inputs')
        original_connection = hashes(connection)
        creator('refuse-existing-connection', connection, refused=True, target='qt-connection')
        check(hashes(connection) == original_connection, 'Existing connection application remains byte-identical')
        creator('refuse-connection-name', output / 'connection-bad-name', name='bad);message(FATAL_ERROR boom)',
                refused=True, target='qt-connection')
        check(not (output / 'connection-bad-name').exists(), 'Connection invalid name fails before creation')

        target, link = output / 'link-target', output / 'linked-parent'
        target.mkdir()
        if os.name == 'nt':
            # A directory junction exercises real Windows reparse containment.
            run('create-junction', ['cmd.exe', '/d', '/c', 'mklink', '/J', link, target])
        else:
            link.symlink_to(target, target_is_directory=True)
        creator('refuse-linked-parent', link / 'child', refused=True)
        check(not (target / 'child').exists(), 'Linked parent does not redirect destination writes')
        answer = creator('refuse-linked-input', output / 'linked-input', '--sdk-prefix', link, refused=True)
        check('Symlink or reparse path' in answer and not (output / 'linked-input').exists(),
              'Linked installed input is refused before destination creation')
        report['path_control'] = 'Windows junction' if os.name == 'nt' else 'Linux symlink'

        fake_sdk = output / 'fake-sdk'
        config = fake_sdk / 'lib/cmake/Telemetry/TelemetryConfig.cmake'
        config.parent.mkdir(parents=True)
        config.write_text('# Identity preflight fixture only\n', encoding='utf8')
        bad_package = output / 'wrong.tgz'
        with tarfile.open(bad_package, 'w:gz') as archive:
            contents = b'{"name":"another-package","version":"0.1.0"}'
            entry = tarfile.TarInfo('package/package.json')
            entry.size = len(contents)
            archive.addfile(entry, io.BytesIO(contents))
        creator('refuse-package-identity', output / 'wrong-package', '--sdk-prefix', fake_sdk,
                '--sdk-package', bad_package, '--ux-package', bad_package, refused=True, target='web-device')
        check(not (output / 'wrong-package').exists(), 'Wrong package is refused before destination creation')
        for label, metadata in (('list', []), ('null', None)):
            invalid_package = output / ('metadata-' + label + '.tgz')
            with tarfile.open(invalid_package, 'w:gz') as archive:
                contents = json.dumps(metadata).encode('utf8')
                entry = tarfile.TarInfo('package/package.json')
                entry.size = len(contents)
                archive.addfile(entry, io.BytesIO(contents))
            destination = output / ('metadata-' + label)
            answer = creator('refuse-metadata-' + label, destination, '--sdk-prefix', fake_sdk,
                '--sdk-package', invalid_package, '--ux-package', invalid_package,
                refused=True, target='web-device')
            check('Package metadata must be a JSON object' in answer and 'Traceback' not in answer,
                  'Invalid package metadata gets an actionable diagnostic')
            check(not destination.exists(), 'Invalid package metadata creates no output')

        # Generate configured projects before hiding their only producer templates.
        configured_qml = output / 'qml-installed'
        if all(qt):
            creator('create-configured-qml', configured_qml, '--sdk-prefix', args.sdk_prefix,
                    '--ux-prefix', args.ux_prefix, '--qt-prefix', args.qt_prefix)
        configured_connection = output / 'connection-installed'
        if args.qt_connection:
            creator('create-configured-connection', configured_connection, '--sdk-prefix', args.sdk_prefix,
                    '--ux-prefix', args.ux_prefix, '--qt-prefix', args.qt_prefix, target='qt-connection')
        configured_web = output / 'web-installed'
        if args.sdk_package:
            creator('create-configured-web', configured_web, '--sdk-prefix', args.sdk_prefix,
                    '--sdk-package', args.sdk_package, '--ux-package', args.ux_package, target='web-device')
            extra = configured_web / 'examples/web_device_starter/presentation.json'
            extra.write_text('{"label":"My device","unit":"V","exact":"18446744073709551615"}\n', encoding='utf8')
            for label, asset_options in (
                    ('traversal', ['--web-asset', '../escape.json']),
                    ('absolute', ['--web-asset', '/outside.json']),
                    ('remote', ['--web-asset', 'https://example.invalid/extra.json']),
                    ('missing', ['--web-asset', 'missing-extra.json']),
                    ('managed', ['--web-asset', 'index.html']),
                    ('duplicate', ['--web-asset', 'presentation.json', '--web-asset', 'presentation.json']),
                    ('case-alias', ['--web-asset', 'presentation.json', '--web-asset', 'Presentation.json'])):
                destination = output / ('refused-extra-' + label)
                answer = run('web-refuse-extra-' + label, [sys.executable, '-B', configured_web / 'prepare.py',
                    '--output', destination, '--export-web', 'cpp', *asset_options], expected=1)
                check(not destination.exists() and 'Traceback' not in answer,
                      'Invalid extra ' + label + ' asset is refused before preparation output or native build')
            destination = output / 'refused-extra-without-export'
            answer = run('web-refuse-extra-without-export', [sys.executable, '-B', configured_web / 'prepare.py',
                '--output', destination, '--web-asset', 'presentation.json'], expected=1)
            check(not destination.exists() and 'Traceback' not in answer,
                  'Extra assets require explicit deploy export before preparation begins')
            # Windows cannot contain both aliases physically. Only the first
            # source is present: reject the complete requested namespace before
            # attempting to read the conflicting second source.
            extra_root = configured_web / 'examples/web_device_starter'
            (extra_root / 'parent-first').write_bytes(b'first asset\n')
            child = extra_root / 'parent-last' / 'child.json'
            child.parent.mkdir()
            child.write_bytes(b'{"label":"child"}\n')
            for label, names in (
                    ('parent-before-child', ('parent-first', 'PARENT-FIRST/child.json')),
                    ('child-before-parent', ('parent-last/child.json', 'PARENT-LAST'))):
                destination = output / ('refused-extra-' + label)
                options = [argument for name in names for argument in ('--web-asset', name)]
                answer = run('web-refuse-extra-' + label, [sys.executable, '-B', configured_web / 'prepare.py',
                    '--output', destination, '--export-web', 'cpp', *options], expected=1)
                check(not destination.exists() and 'Traceback' not in answer and
                      'file/directory asset collision' in answer.lower(),
                      'Case-folded extra asset file/directory conflict fails preflight in either order')
        hidden = producer / 'hidden-templates'
        (producer / 'examples').rename(hidden)
        check(not (producer / 'examples').exists(), 'All producer templates hidden before consumer builds')

        if all(qt):
            flags = '-Wall -Wextra -Werror'
            if args.sanitize:
                flags += ' -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
            run('qml-configure', [args.cmake, '--preset', 'local', '-DCMAKE_CXX_COMPILER=' + str(args.cxx),
                '-DCMAKE_MAKE_PROGRAM=' + str(args.ninja), '-DCMAKE_CXX_FLAGS=' + flags], cwd=configured_qml)
            run('qml-build', [args.cmake, '--build', '--preset', 'local'], cwd=configured_qml)
            executable = configured_qml / 'build' / ('telemetry_dynamic_example.exe' if os.name == 'nt' else 'telemetry_dynamic_example')
            qml_env = dict(environment, QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software')
            qml_env['PATH'] = str(args.qt_prefix / 'bin') + os.pathsep + qml_env.get('PATH', '')
            if os.name == 'nt':
                qml_env['QT_QPA_FONTDIR'] = str(Path(os.environ.get('SystemRoot', 'C:/Windows')) / 'Fonts')
            if args.sanitize:
                # The copied example uses Qt's offscreen platform/global caches;
                # lifetime/leak controls of the provider live in its maintained suite.
                qml_env['ASAN_OPTIONS'] = 'detect_leaks=0:halt_on_error=1'
                qml_env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
            answer = run('qml-runtime', [executable, '--smoke'], cwd=configured_qml, env=qml_env)
            check('panels=2 reads=2 valid=1 warnings=0' in answer, 'Generated actual QML panels share the admitted owner')
            report['qml'] = {'passed': True, 'sanitize': args.sanitize,
                             'leak_checking': False if args.sanitize else None,
                             'executable_sha256': digest(executable)}

        if args.qt_connection:
            check(not (producer / 'examples').exists(), 'Connection consumer builds with its producer templates hidden')
            run('connection-configure', [args.cmake, '--preset', 'local',
                '-DCMAKE_CXX_COMPILER=' + str(args.cxx), '-DCMAKE_MAKE_PROGRAM=' + str(args.ninja),
                '-DCMAKE_CXX_FLAGS=' + flags, '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'], cwd=configured_connection)
            run('connection-build', [args.cmake, '--build', '--preset', 'local'], cwd=configured_connection)
            executable = configured_connection / 'build' / ('telemetry-qt-connection.exe' if os.name == 'nt'
                                                            else 'telemetry-qt-connection')
            check(executable.is_file(), 'Generated owned-connection consumer linked an actual executable')
            commands = json.loads((configured_connection / 'build/compile_commands.json').read_text(encoding='utf8'))
            for entry in commands:
                paths = (str(entry.get('file', '')) + ' ' +
                         entry.get('command', ' '.join(entry.get('arguments', [])))).replace('\\', '/')
                check(str(ROOT).replace('\\', '/') not in paths and
                      str(ROOT.parent / 'telemetry-sdk').replace('\\', '/') not in paths and
                      str(producer).replace('\\', '/') not in paths,
                      'Generated connection consumer compiles without producer or SDK/UX source paths')
            report['qt_connection'] = {'passed': True, 'built': True, 'runtime': False,
                                       'producer_templates_hidden': True, 'sanitize': args.sanitize,
                                       'executable_sha256': digest(executable)}

        if args.sdk_package:
            options = [args.browser_python, '-B', configured_web / 'prepare.py',
                       '--cmake', args.cmake, '--ninja', args.ninja, '--cxx', args.cxx, '--node', args.node,
                       '--export-web', 'cpp', '--web-asset', 'presentation.json']
            if args.npm_cli:
                options += ['--npm-cli', args.npm_cli]
            web_env = dict(environment)
            if args.sanitize:
                options.append('--sanitize')
                web_env['ASAN_OPTIONS'] = 'detect_leaks=1:halt_on_error=1'
                web_env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
            run('web-prepare', options, cwd=configured_web, env=web_env)
            prepared = configured_web.with_name(configured_web.name + '-prepared')
            prepared_report = json.loads((prepared / 'prepared.json').read_text(encoding='utf8'))
            check(prepared_report['passed'], 'Generated wrapper prepares the complete native/browser application')
            check(not prepared_report['static_generated'], 'Created Dynamic project needs no generator')
            check(prepared_report.get('web_export', {}).get('mode') == 'cpp',
                  'Created project exports actual Flash-ready browser assets without its producer')
            run('web-cpp-assets', [sys.executable, '-B', ROOT / 'tests/web_assets/run.py',
                '--export-root', prepared / 'www', '--build-dir', output / 'cpp-assets', '--cxx', args.cxx], env=web_env)
            # Run the maintained browser journey against generated paths. Its
            # imports/server/fixtures now refer only to this generated project.
            invocation = '''import importlib.util,sys
from pathlib import Path
spec=importlib.util.spec_from_file_location('generated_starter_checks',sys.argv[1])
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
module.ROOT=Path(sys.argv[2]);module.STARTER=module.ROOT/'examples/web_device_starter'
sys.argv=[sys.argv[1],'--prepared',sys.argv[3],'--output',sys.argv[4],'--exported','--extra-json','presentation.json']
module.main()
'''
            run('web-browser', [args.browser_python, '-B', '-c', invocation,
                ROOT / 'tests/web_device_starter/run.py', configured_web, prepared, output / 'browser'], env=web_env)
            browser = json.loads((output / 'browser' / 'summary.json').read_text(encoding='utf8'))
            check(browser['passed'] and not browser['errors'] and not browser['warnings'], 'Generated app passed real HTTP/WebSocket browser journey')
            report['web'] = {'passed': True, 'checks': browser['checks'], 'modes': list(browser['modes']),
                             'sanitize': args.sanitize, 'leak_checking': args.sanitize}

        report['source_sha256_after'] = {relative: digest(ROOT / relative) for relative in before}
        check(report['source_sha256_after'] == before, 'Creator and maintained templates remain unchanged')
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        report['finished_utc'] = datetime.now(timezone.utc).isoformat()
        (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print('Project starter checks=' + str(report['checks']) + ' hardware=0')


if __name__ == '__main__':
    main()
