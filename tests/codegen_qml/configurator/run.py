"""Installed static QML binding/preserving-cache qualification; no hardware. MIT."""
from __future__ import annotations
import argparse
import datetime as dt
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from support import (ROOT, check_cache, check_compile_sources, codegen_identity, digest,
                     installed_sdk, sdk_inputs, ux_inputs)


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
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    output.mkdir(parents=True, exist_ok=True)
    config, metadata = installed_sdk(prefix)
    ux_configs = list(ux_prefix.rglob('TelemetryUXConfig.cmake'))
    if len(ux_configs) != 1:
        parser.error('--ux-prefix must contain one installed TelemetryUXConfig.cmake')
    tools = {name: Path(shutil.which(getattr(args, name)) or getattr(args, name)).absolute()
             for name in ('cxx', 'cmake', 'ninja')}
    # Preserve venv and compiler-driver spelling; resolving symlinks loses them.
    python = args.codegen_python.expanduser().absolute()
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', PYTHONNOUSERSITE='1',
               QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software')
    env.pop('PYTHONPATH', None)
    env.pop('PYTHONHOME', None)
    env['PATH'] = os.pathsep.join((str(tools['cxx'].parent), str(qt / 'bin'), env.get('PATH', '')))
    if args.sanitize:
        env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                   UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    report = dict(passed=False, hardware=False, started_utc=dt.datetime.now(dt.timezone.utc).isoformat(),
                  sdk_metadata=metadata, sdk_prefix=str(prefix), ux_prefix=str(ux_prefix),
                  sanitize=args.sanitize, commands=[], source_sha256=ux_inputs(),
                  installed_sdk_sha256=sdk_inputs(prefix), installed_ux_sha256=sdk_inputs(ux_prefix))

    def run(command, label, selected_env=None, timeout=240):
        started = time.monotonic()
        command = list(map(str, command))
        result = subprocess.run(command, cwd=output, env=selected_env or env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
        (output / (label + '.log')).write_text(result.stdout, encoding='utf-8')
        report['commands'].append(dict(name=label, command=command, returncode=result.returncode,
                                       seconds=time.monotonic()-started, log=label+'.log'))
        print(label, 'exit', result.returncode, flush=True)
        if result.returncode:
            raise RuntimeError(label + ' failed\n' + result.stdout[-16000:])
        return result.stdout

    flags = '-Wall -Wextra -Werror'
    if args.sanitize:
        flags += ' -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
    common = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
              '-DCMAKE_CXX_COMPILER='+str(tools['cxx']), '-DCMAKE_MAKE_PROGRAM='+str(tools['ninja']),
              '-DCMAKE_CXX_FLAGS='+flags, '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
              '-DCMAKE_PREFIX_PATH='+';'.join(map(str, (prefix, ux_prefix, qt))),
              '-DTelemetry_DIR='+str(config), '-DTelemetryUX_DIR='+str(ux_configs[0].parent),
              '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF', '-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF']
    suffix = '.exe' if os.name == 'nt' else ''
    native, generated, naming, build = (output / name for name in ('native', 'sdk', 'naming-sdk', 'build'))
    try:
        report['codegen_before'] = codegen_identity(python, env, output)
        report['tools_sha256'] = {str(path): digest(path) for path in (*tools.values(), python)}
        for name, path in (*tools.items(), ('codegen-python', python)):
            run([path, '--version'], name+'-version')
        fixture_build = output / 'fixture-build'
        run([tools['cmake'], '-S', ROOT/'examples/codegen_qml/fixture', '-B', fixture_build,
             *common, '-DTELEMETRY_QML_FIXTURE_OUTPUT='+str(native)], 'fixture-configure')
        check_cache(fixture_build, config)
        run([tools['cmake'], '--build', fixture_build, '--target', 'telemetry_qml_fixture_export',
             '--parallel', args.jobs], 'fixture-export')
        check_compile_sources(fixture_build, output)
        run([python, '-B', '-m', 'unittest', 'discover', '-s', HERE, '-p', 'test_generator.py'], 'generator-unit')
        generate = [python, '-B', '-m', 'tools.telemetry_codegen', 'generate']
        options = ['--targets', 'cpp,qml', '--structural-metadata', '--ux-starter', 'qml']
        run([*generate, native/'descriptor.bin', *options, '--output', generated], 'generate-native')
        run([python, '-B', HERE/'test_generator.py', '--write-fixture', output/'naming.bin'], 'naming-fixture')
        run([*generate, output/'naming.bin', *options, '--namespace', 'naming_api', '--output', naming], 'generate-naming')
        report['generated_sha256'] = {p.relative_to(output).as_posix(): digest(p)
                                      for directory in (generated, naming) for p in sorted(directory.rglob('*')) if p.is_file()}
        alternate = output / 'alternate-sdk'
        shutil.copytree(prefix, alternate)
        run([tools['cmake'], '-S', HERE, '-B', build, *common,
             '-DGENERATED_SDK='+str(generated), '-DGENERATED_NAMING_SDK='+str(naming)],
            'configure-competing-sdk-root', dict(env, Telemetry_ROOT=str(alternate)))
        check_cache(build, config)
        ux_cache = [line.split('=', 1)[1] for line in (build/'CMakeCache.txt').read_text().splitlines()
                    if line.startswith('TelemetryUX_DIR:')]
        if len(ux_cache) != 1 or Path(ux_cache[0]).resolve() != ux_configs[0].parent:
            raise RuntimeError('CMake selected a different installed UX prefix')
        run([tools['cmake'], '--build', build, '--parallel', args.jobs], 'build')
        check_compile_sources(build, output)
        executable = build / ('telemetry_static_qml_generated'+suffix)
        result = run([executable], 'runtime')
        counts = re.search(r'Static QML generated checks=(\d+)', result)
        if not counts or int(counts[1]) < 340:
            raise RuntimeError('Generated preserving/cache qualification floor was not met')
        report['generated_checks'] = int(counts[1])
        naming_executable = build / ('telemetry_static_qml_naming'+suffix)
        result = run([naming_executable], 'naming-runtime')
        if 'Static QML naming endpoints=36' not in result:
            raise RuntimeError('Hostile naming fixture did not execute')
        report['naming_endpoints'] = 36
        report['executions'] = [dict(path=str(path), sha256=digest(path), bytes=path.stat().st_size)
                                for path in (fixture_build/('telemetry_qml_fixture_producer'+suffix),
                                             executable, naming_executable)]
        report['passed'] = True
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        report['error'] = str(error)
        print(str(error), file=sys.stderr)
    finally:
        report['source_sha256_after'] = ux_inputs()
        report['installed_sdk_sha256_after'] = sdk_inputs(prefix)
        report['installed_ux_sha256_after'] = sdk_inputs(ux_prefix)
        try:
            report['codegen_after'] = codegen_identity(python, env, output)
            report['tools_sha256_after'] = {str(path): digest(path) for path in (*tools.values(), python)}
            if any(report[before] != report[after] for before, after in (
                    ('source_sha256', 'source_sha256_after'), ('installed_sdk_sha256', 'installed_sdk_sha256_after'),
                    ('installed_ux_sha256', 'installed_ux_sha256_after'), ('codegen_before', 'codegen_after'),
                    ('tools_sha256', 'tools_sha256_after'))):
                raise RuntimeError('Qualification inputs changed during execution')
        except (OSError, ValueError, RuntimeError, KeyError) as error:
            report['passed'] = False
            report['error'] = str(error)
        report['status'] = 'passed' if report['passed'] else 'failed'
        report['finished_utc'] = dt.datetime.now(dt.timezone.utc).isoformat()
        (output/'summary.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
