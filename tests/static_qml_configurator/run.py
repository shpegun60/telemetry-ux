"""Installed generated SDK -> static provider -> shared QML view, offline. MIT."""
from __future__ import annotations
import argparse
import datetime as dt
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
sys.path.insert(0, str(ROOT / 'tests/codegen_qml'))
from support import (check_cache, check_compile_sources, codegen_identity, digest,
                     installed_sdk, sdk_inputs, snapshot)

GUI_CHECK_FLOOR = 494
CONTRACT_CHECK_FLOOR = 297
QT_TEXTURE_RULE = 'leak:QSGRenderContext::textureForFactory\n'
QT_TEXTURE_SOURCES = (
    'https://raw.githubusercontent.com/qt/qtdeclarative/v6.4.2/src/quick/scenegraph/qsgcontext.cpp',
    'https://raw.githubusercontent.com/qt/qtdeclarative/v6.4.2/src/quick/scenegraph/adaptations/software/qsgsoftwarecontext.cpp',
    'https://raw.githubusercontent.com/qt/qtdeclarative/v6.8.0/src/quick/scenegraph/adaptations/software/qsgsoftwarecontext.cpp',
)


def candidate_inputs():
    """Actual candidate sources; preserved unrelated Widgets work is not input."""
    paths = [ROOT / name for name in ('CMakeLists.txt', 'README.md', 'MIGRATION.md',
                                     'LICENSE', 'VERSION', 'MANIFEST.in', 'setup.py', 'pyproject.toml')]
    for directory in ('lib/qml', 'python', 'tests/codegen_qml',
                      'tests/static_qml_configurator', 'examples/codegen_qml'):
        paths.extend((ROOT / directory).rglob('*'))
    return snapshot(paths, ROOT)


def installed_ux(prefix):
    configs = list(prefix.rglob('TelemetryUXConfig.cmake'))
    versions = list(prefix.rglob('share/telemetry-ux/VERSION'))
    if len(configs) != 1 or len(versions) != 1 or versions[0].read_text().strip() != '0.1.0':
        raise ValueError('--ux-prefix needs an installed Telemetry UX 0.1.0 package')
    for name in ('StaticConfiguratorBinding.hpp', 'StaticConfiguratorProvider.hpp'):
        if not (prefix / 'include/Telemetry/UX' / name).is_file():
            raise ValueError('Installed static Configurator header missing: ' + name)
    return configs[0].parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--sdk-prefix', type=Path, required=True)
    parser.add_argument('--ux-prefix', type=Path, required=True)
    parser.add_argument('--codegen-python', type=Path, required=True)
    parser.add_argument('--qt-prefix', type=Path, required=True)
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ninja', default='ninja')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    output, prefix, ux_prefix, qt = (path.resolve() for path in
                                    (args.build_dir, args.sdk_prefix, args.ux_prefix, args.qt_prefix))
    if any(output == base or base in output.parents for base in (ROOT, prefix, ux_prefix)):
        parser.error('--build-dir must be outside UX and both installed prefixes')
    if output.exists() and any(output.iterdir()):
        parser.error('--build-dir must be new or empty; existing evidence is never overwritten')
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    config, metadata = installed_sdk(prefix)
    ux_config = installed_ux(ux_prefix)
    output.mkdir(parents=True, exist_ok=True)
    tools = {name: Path(shutil.which(getattr(args, name)) or getattr(args, name)).absolute()
             for name in ('cxx', 'cmake', 'ninja')}
    python = args.codegen_python.expanduser().absolute()
    # Do not resolve executable symlinks: the venv and C++ driver are semantic.
    runner_python = Path(sys.executable).absolute()
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', PYTHONNOUSERSITE='1',
               QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software')
    env.pop('PYTHONPATH', None)
    env.pop('PYTHONHOME', None)
    env.pop('LSAN_OPTIONS', None)
    env['PATH'] = os.pathsep.join((str(tools['cxx'].parent), str(qt / 'bin'), env.get('PATH', '')))
    if os.name == 'nt':
        fonts = Path(os.environ.get('WINDIR', r'C:\Windows')) / 'Fonts'
        if not fonts.is_dir():
            parser.error('Windows offscreen font directory is unavailable: ' + str(fonts))
        # Native Windows controls require a real HWND, absent on offscreen.
        env.update(QT_QUICK_CONTROLS_STYLE='Basic', QT_QPA_FONTDIR=str(fonts))
    if args.sanitize:
        env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                   UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1',
                   LSAN_OPTIONS='exitcode=86:print_suppressions=1')
        nm = shutil.which('nm', path=env['PATH'])
        if not nm:
            parser.error('--sanitize requires nm to verify the installed archive instrumentation')
        tools['nm'] = Path(nm).absolute()
    observed_tools = (*tools.values(), python, runner_python)
    report = dict(passed=False, status='running', hardware=False,
                  started_utc=dt.datetime.now(dt.timezone.utc).isoformat(),
                  sdk_prefix=str(prefix), sdk_metadata=metadata, ux_prefix=str(ux_prefix),
                  qt_prefix=str(qt), sanitize=args.sanitize, commands=[],
                  environment={key: env.get(key) for key in
                               ('QT_QPA_PLATFORM', 'QT_QUICK_BACKEND', 'QT_QUICK_CONTROLS_STYLE',
                                'QT_QPA_FONTDIR', 'ASAN_OPTIONS', 'UBSAN_OPTIONS',
                                'LSAN_OPTIONS')},
                  source_sha256=candidate_inputs(), installed_sdk_sha256=sdk_inputs(prefix),
                  installed_ux_sha256=sdk_inputs(ux_prefix))

    def run(command, label, selected_env=None, timeout=600, expected_returncode=0):
        command = list(map(str, command))
        actual_env = env if selected_env is None else selected_env
        start = time.monotonic()
        failure = None
        try:
            result = subprocess.run(command, cwd=output, env=actual_env, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
            text, returncode = result.stdout, result.returncode
        except subprocess.TimeoutExpired as error:
            value = error.stdout or ''
            text = value.decode('utf-8', errors='replace') if isinstance(value, bytes) else value
            text += '\nRunner timeout: ' + str(error) + '\n'
            returncode, failure = None, error
        log = output / (label + '.log')
        log.write_text(text, encoding='utf-8')
        report['commands'].append(dict(name=label, command=command, returncode=returncode,
                                       expected_returncode=expected_returncode,
                                       seconds=time.monotonic()-start, log=log.name,
                                       log_sha256=digest(log), timeout_seconds=timeout,
                                       environment={key: actual_env.get(key) for key in
                                                    ('ASAN_OPTIONS', 'UBSAN_OPTIONS', 'LSAN_OPTIONS',
                                                     'QT_QPA_PLATFORM', 'QT_QUICK_BACKEND')}))
        print(label, 'exit', returncode, flush=True)
        if failure is not None:
            raise RuntimeError(label + ' timed out') from failure
        expected = ((expected_returncode,) if isinstance(expected_returncode, int)
                    else expected_returncode)
        if returncode not in expected:
            raise RuntimeError(label + ' failed\n' + text[-16000:])
        return text

    def gui_counts(text):
        counts = re.search(r'Static QML provider checks=(\d+) warnings=(\d+)', text)
        if not counts or int(counts[1]) < GUI_CHECK_FLOOR or int(counts[2]) != 0:
            raise RuntimeError('Actual native/provider/QML checks did not reach their warning-free floor')
        return dict(checks=int(counts[1]), warnings=int(counts[2]))

    flags = '-Wall -Wextra -Werror'
    if args.sanitize:
        flags += ' -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
    common = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
              '-DCMAKE_CXX_COMPILER='+str(tools['cxx']), '-DCMAKE_MAKE_PROGRAM='+str(tools['ninja']),
              '-DCMAKE_CXX_FLAGS='+flags, '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
              '-DCMAKE_PREFIX_PATH='+';'.join(map(str, (prefix, ux_prefix, qt))),
              '-DTelemetry_DIR='+str(config), '-DTelemetryUX_DIR='+str(ux_config),
              '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF', '-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF']
    if args.sanitize:
        common.append('-DCMAKE_CXX_FLAGS_RELEASE=-O1 -g -DNDEBUG')
    suffix = '.exe' if os.name == 'nt' else ''
    child_output, build = output / 'generated-seam', output / 'build'
    try:
        report['codegen_before'] = codegen_identity(python, env, output)
        report['tools_sha256'] = {str(path): digest(path) for path in observed_tools}
        for name, path in (*tools.items(), ('codegen-python', python), ('runner-python', runner_python)):
            run([path, '--version'], name+'-version')
        if args.sanitize:
            archives = [('sdk', path) for path in sorted(prefix.rglob('*.a'))]
            archives += [('ux', path) for path in sorted(ux_prefix.rglob('*static_configurator*.a'))]
            if not any(path.name == 'libtelemetry_core.a' for _, path in archives) or not any(
                    path.name == 'libtelemetry_encoded_server.a' for _, path in archives) or not any(
                    owner == 'ux' for owner, _ in archives):
                raise RuntimeError('The instrumented profile requires actual SDK and static-provider archives')
            report['archive_instrumentation'] = []
            for ordinal, (owner, path) in enumerate(archives):
                symbols = run([tools['nm'], '-a', path], 'archive-symbols-'+str(ordinal))
                record = dict(owner=owner, path=str(path), sha256=digest(path),
                              asan='__asan_' in symbols, ubsan='__ubsan_' in symbols)
                report['archive_instrumentation'].append(record)
                if not record['asan'] or not record['ubsan']:
                    raise RuntimeError('Installed archive is not ASan/UBSan instrumented: ' + str(path))
        nested_args = ['--sanitize'] if args.sanitize else []
        run([runner_python, '-B', ROOT/'tests/codegen_qml/configurator/run.py',
             '--build-dir', child_output, '--sdk-prefix', prefix, '--ux-prefix', ux_prefix,
             '--codegen-python', python, '--qt-prefix', qt, '--cxx', tools['cxx'],
             '--cmake', tools['cmake'], '--ninja', tools['ninja'], '--jobs', args.jobs, *nested_args],
            'generated-seam', timeout=1800)
        nested_summary = child_output / 'summary.json'
        child = json.loads(nested_summary.read_text(encoding='utf-8'))
        if (not child.get('passed') or child.get('generated_checks', 0) < 340 or
                child.get('naming_endpoints') != 36 or child['codegen_before'] != report['codegen_before']):
            raise RuntimeError('Nested generated seam did not pass against the same installed codegen')
        report['generated_seam'] = dict(path=str(nested_summary), sha256=digest(nested_summary),
                                        checks=child['generated_checks'], naming_endpoints=36,
                                        unit_tests=3, sealed_default_file_hashes=14)
        report['nested_logs_sha256'] = {str(Path('generated-seam') / command['log']):
                                       digest(child_output / command['log']) for command in child['commands']}
        report['native_fixture_sha256'] = snapshot((child_output/'native').rglob('*'), child_output/'native')
        generated = child_output / 'sdk'
        report['generated_sha256'] = snapshot(generated.rglob('*'), generated)
        alternate_sdk, alternate_ux = output / 'alternate-sdk', output / 'alternate-ux'
        shutil.copytree(prefix, alternate_sdk)
        shutil.copytree(ux_prefix, alternate_ux)
        run([tools['cmake'], '-S', HERE, '-B', build, *common,
             '-DGENERATED_SDK='+str(generated)], 'configure-competing-roots',
            dict(env, Telemetry_ROOT=str(alternate_sdk), TelemetryUX_ROOT=str(alternate_ux)))
        check_cache(build, config)
        selected = [line.split('=', 1)[1] for line in (build/'CMakeCache.txt').read_text().splitlines()
                    if line.startswith('TelemetryUX_DIR:')]
        if len(selected) != 1 or Path(selected[0]).resolve() != ux_config:
            raise RuntimeError('CMake did not select the explicit installed UX prefix')
        report['competing_roots_ignored'] = True
        run([tools['cmake'], '--build', build, '--parallel', args.jobs], 'build')
        check_compile_sources(build, output)
        provider = build / ('telemetry_static_qml_provider'+suffix)
        contracts = build / ('telemetry_static_qml_contracts'+suffix)
        image = output / 'window.png'
        report['executions'] = [dict(path=str(provider), sha256=digest(provider), bytes=provider.stat().st_size)]
        version = run([provider, '--qt-version'], 'qt-runtime-version').strip()
        if not re.fullmatch(r'\d+\.\d+\.\d+(?:[-+].*)?', version):
            raise RuntimeError('The actual GUI executable did not report its Qt runtime version')
        report['qt_runtime_version'] = version
        texture_exception = (args.sanitize and sys.platform.startswith('linux') and
                             version == '6.4.2' and env['QT_QUICK_BACKEND'] == 'software')
        report['qt_software_texture_exception'] = dict(eligible=texture_exception, applied=False)
        gui_env = env
        text = None
        if texture_exception:
            suppression = HERE / 'qt64-software-textures.lsan'
            if suppression.read_text(encoding='utf-8') != QT_TEXTURE_RULE:
                raise RuntimeError('The Qt 6.4.2 exception must contain only the proved texture-cache rule')
            unfiltered = run([provider, output/'window-unsuppressed.png'],
                             'provider-runtime-unsuppressed', timeout=120,
                             expected_returncode=(0, 86))
            unfiltered_counts = gui_counts(unfiltered)
            unfiltered_exit = report['commands'][-1]['returncode']
            if unfiltered_exit == 0:
                if any(marker in unfiltered for marker in
                       ('AddressSanitizer', 'LeakSanitizer', 'UndefinedBehaviorSanitizer',
                        'runtime error:')):
                    raise RuntimeError('The zero-exit GUI emitted a sanitizer diagnostic')
                # The extracted standalone renderer may no longer retain a Qt
                # texture. Prefer that fully unfiltered successful execution.
                text = unfiltered
                shutil.copyfile(output/'window-unsuppressed.png', image)
                report['qt_software_texture_exception']['unsuppressed'] = dict(
                    **unfiltered_counts, returncode=0, leaked_bytes=0, allocations=0)
            else:
                leak = re.search(r'SUMMARY: AddressSanitizer: (\d+) byte\(s\) leaked in (\d+) allocation\(s\)',
                                 unfiltered)
                if (not leak or not int(leak[1]) or not int(leak[2]) or
                        'LeakSanitizer: detected memory leaks' not in unfiltered or
                        'QSGRenderContext::textureForFactory' not in unfiltered):
                    raise RuntimeError('Exit 86 did not reproduce the established Qt texture leak')
                gui_env = dict(env, LSAN_OPTIONS=env['LSAN_OPTIONS']+':suppressions='+str(suppression))
                report['qt_software_texture_exception'].update(
                    applied=True,
                    scope='Only this Qt 6.4.2 Linux/software GUI execution; all other steps unfiltered',
                    rule=QT_TEXTURE_RULE.strip(), suppression_sha256=digest(suppression),
                    primary_sources=QT_TEXTURE_SOURCES, executable_sha256=digest(provider),
                    unsuppressed=dict(**unfiltered_counts, returncode=86,
                                      leaked_bytes=int(leak[1]), allocations=int(leak[2])))
        if text is None:
            text = run([provider, image], 'provider-runtime', selected_env=gui_env, timeout=120)
        counts = gui_counts(text)
        report['provider_checks'], report['qml_warnings'] = counts['checks'], counts['warnings']
        if report['qt_software_texture_exception']['applied']:
            if 'QSGRenderContext::textureForFactory' not in text or 'Suppressions used:' not in text:
                raise RuntimeError('The filtered GUI did not report the exact Qt texture exception')
        if args.sanitize:
            injected = run([provider, output/'window-injected-leak.png', '--inject-nonqt-leak'],
                           'provider-runtime-nonqt-leak-control', selected_env=gui_env,
                           timeout=120, expected_returncode=86)
            injected_counts = gui_counts(injected)
            if ('injectedNonQtLeak' not in injected or
                    not re.search(r'SUMMARY: AddressSanitizer: 123 byte\(s\) leaked in 1 allocation\(s\)',
                                  injected)):
                raise RuntimeError('The same GUI executable did not retain its fatal non-Qt leak control')
            report['qt_software_texture_exception']['nonqt_leak_control'] = dict(
                **injected_counts, returncode=86, leaked_bytes=123, allocations=1,
                allocation_frame='injectedNonQtLeak', executable_sha256=digest(provider))
        report['executions'].append(dict(path=str(contracts), sha256=digest(contracts), bytes=contracts.stat().st_size))
        text = run([contracts], 'contracts-runtime', timeout=120)
        contract = json.loads(text.strip())
        if not contract.get('passed') or contract.get('checks', 0) < CONTRACT_CHECK_FLOOR:
            raise RuntimeError('Presentation contracts did not meet their qualification floor')
        report['contracts'] = contract
        png = image.read_bytes()
        if not png.startswith(b'\x89PNG\r\n\x1a\n') or len(png) < 24:
            raise RuntimeError('Actual Qt window screenshot was not retained')
        width, height = struct.unpack('>II', png[16:24])
        report['screenshot'] = dict(path=str(image), sha256=digest(image), bytes=len(png),
                                    width=width, height=height)
        report['passed'] = True
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        report['error'] = str(error)
        print(str(error), file=sys.stderr)
    finally:
        report['source_sha256_after'] = candidate_inputs()
        report['installed_sdk_sha256_after'] = sdk_inputs(prefix)
        report['installed_ux_sha256_after'] = sdk_inputs(ux_prefix)
        try:
            report['codegen_after'] = codegen_identity(python, env, output)
            report['tools_sha256_after'] = {str(path): digest(path) for path in observed_tools}
            for before, after in (('source_sha256', 'source_sha256_after'),
                                  ('installed_sdk_sha256', 'installed_sdk_sha256_after'),
                                  ('installed_ux_sha256', 'installed_ux_sha256_after'),
                                  ('codegen_before', 'codegen_after'), ('tools_sha256', 'tools_sha256_after')):
                if report.get(before) != report.get(after):
                    raise RuntimeError('Qualification inputs changed during execution: ' + before)
            for command in report['commands']:
                if digest(output / command['log']) != command['log_sha256']:
                    raise RuntimeError('A retained execution log changed: ' + command['name'])
            for name, value in report.get('nested_logs_sha256', {}).items():
                if digest(output / name) != value:
                    raise RuntimeError('A retained nested execution log changed: ' + name)
            if 'generated_sha256' in report:
                report['generated_sha256_after'] = snapshot((child_output/'sdk').rglob('*'), child_output/'sdk')
                if report['generated_sha256_after'] != report['generated_sha256']:
                    raise RuntimeError('Generated SDK inputs changed during provider qualification')
            for execution in report.get('executions', []):
                if digest(Path(execution['path'])) != execution['sha256']:
                    raise RuntimeError('An executed binary changed: ' + execution['path'])
        except (OSError, ValueError, RuntimeError) as error:
            report['passed'] = False
            report['error'] = str(error)
        report['status'] = 'passed' if report['passed'] else 'failed'
        report['finished_utc'] = dt.datetime.now(dt.timezone.utc).isoformat()
        (output/'summary.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
