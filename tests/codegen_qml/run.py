"""Installed SDK/generated Qt API qualification with no SDK source-tree inputs.

All outputs stay in --build-dir; fixtures belong to this UX repository.
Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
"""
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

from support import (ROOT, check_cache, check_compile_sources, codegen_identity, digest,
                     installed_sdk, sdk_inputs, ux_inputs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--sdk-prefix', type=Path, required=True)
    parser.add_argument('--codegen-python', type=Path, required=True)
    parser.add_argument('--qt-prefix', type=Path, required=True)
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ninja', default='ninja')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    output, prefix, qt = (value.resolve() for value in
                          (args.build_dir, args.sdk_prefix, args.qt_prefix))
    if output == ROOT or ROOT in output.parents or output == prefix or prefix in output.parents:
        parser.error('--build-dir must be outside UX and the installed SDK')
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    output.mkdir(parents=True, exist_ok=True)
    config, metadata = installed_sdk(prefix)
    tools = {key: Path(shutil.which(getattr(args, key)) or getattr(args, key)).absolute()
             for key in ('cxx', 'cmake', 'ninja')}
    python = args.codegen_python.absolute()
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', PYTHONNOUSERSITE='1',
               QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software')
    env.pop('PYTHONPATH', None)
    env.pop('PYTHONHOME', None)
    env['PATH'] = os.pathsep.join((str(tools['cxx'].parent), str(qt / 'bin'), env.get('PATH', '')))
    if args.sanitize:
        env['ASAN_OPTIONS'] = 'detect_leaks=1:halt_on_error=1'
        env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
    report = dict(passed=False, status='running', hardware=False, started_utc=dt.datetime.now(dt.timezone.utc).isoformat(),
                  sdk_prefix=str(prefix), sdk_metadata=metadata, qt_prefix=str(qt), sanitize=args.sanitize,
                  commands=[], executions=[], source_sha256=ux_inputs(), installed_sdk_sha256=sdk_inputs(prefix))

    def run(command, label, selected_env=None, timeout=240):
        command = list(map(str, command))
        started = time.monotonic()
        result = subprocess.run(command, cwd=output, env=selected_env or env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
        (output / (label + '.log')).write_text(result.stdout, encoding='utf-8')
        report['commands'].append(dict(name=label, command=command, returncode=result.returncode,
                                       seconds=time.monotonic()-started, log=label+'.log'))
        print(label, 'exit', result.returncode, flush=True)
        if result.returncode:
            raise RuntimeError(label+' failed\n'+result.stdout[-12000:])
        return result.stdout

    flags = '-Wall -Wextra -Werror'
    if args.sanitize:
        flags += ' -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
    common = ['-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
              '-DCMAKE_CXX_COMPILER='+str(tools['cxx']), '-DCMAKE_MAKE_PROGRAM='+str(tools['ninja']),
              '-DCMAKE_CXX_FLAGS='+flags, '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
              '-DCMAKE_PREFIX_PATH='+str(prefix)+';'+str(qt), '-DTelemetry_DIR='+str(config),
              '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF', '-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF']
    suffix = '.exe' if os.name == 'nt' else ''
    native, sdk, build = output/'native', output/'sdk', output/'build'
    binaries = []
    try:
        report['codegen_before'] = codegen_identity(python, env, output)
        report['tools_sha256'] = {str(path): digest(path) for path in (*tools.values(), python)}
        for name, path in (*tools.items(), ('codegen-python', python)):
            run([path, '--version'], name+'-version')
        fixture_build = output/'fixture-build'
        run([tools['cmake'], '-S', ROOT/'examples/codegen_qml/fixture', '-B', fixture_build,
             *common, '-DTELEMETRY_QML_FIXTURE_OUTPUT='+str(native)], 'fixture-configure')
        check_cache(fixture_build, config)
        run([tools['cmake'], '--build', fixture_build, '--target', 'telemetry_qml_fixture_export',
             '--parallel', args.jobs], 'fixture-build-export')
        check_compile_sources(fixture_build, output)
        binaries.append(fixture_build/('telemetry_qml_fixture_producer'+suffix))
        run([python, '-B', '-m', 'unittest', 'discover', '-s', ROOT/'tests/codegen_qml',
             '-p', 'test_*.py'], 'generator-unit')
        run([python, '-B', '-m', 'tools.telemetry_codegen', 'generate', native/'descriptor.bin',
             '--targets', 'cpp,qml', '--output', sdk], 'generate')
        report['input_sha256'] = {p.name: digest(p) for p in sorted(native.glob('*.bin'))}
        report['generated_sha256'] = {p.relative_to(sdk).as_posix(): digest(p)
                                      for p in sorted(sdk.rglob('*')) if p.is_file()}
        alternate = output/'alternate-sdk'
        shutil.copytree(prefix, alternate)
        configure_env = dict(env, Telemetry_ROOT=str(alternate))
        run([tools['cmake'], '-S', ROOT/'tests/codegen_qml', '-B', build, *common,
             '-DGENERATED_SDK='+str(sdk)], 'configure-competing-sdk-root', configure_env)
        check_cache(build, config)
        report['competing_sdk_root_ignored'] = True
        run([tools['cmake'], '--build', build, '--parallel', args.jobs], 'build')
        check_compile_sources(build, output)
        runtime = run([build/('telemetry_qml_check'+suffix), native], 'runtime')
        counts = re.search(r'QML checks=(\d+) RPC packets=(\d+)', runtime)
        if not counts or int(counts[1]) < 1234 or int(counts[2]) < 40:
            raise RuntimeError('Runtime did not execute the QML/RPC qualification floor')
        report['qml_checks'], report['rpc_packets'] = map(int, counts.groups())
        queued = run([build/('telemetry_qml_queued'+suffix)], 'queued-runtime')
        counts = re.search(r'QML queued checks=(\d+) packets=(\d+)', queued)
        if not counts or int(counts[1]) < 277 or int(counts[2]) < 26:
            raise RuntimeError('Queued adapter did not execute its qualification floor')
        report['qml_queued_checks'], report['qml_queued_packets'] = map(int, counts.groups())
        binaries.extend(build/(name+suffix) for name in ('telemetry_qml_check', 'telemetry_qml_queued'))
        probe_args = ['--sanitize'] if args.sanitize else []
        run([sys.executable, '-B', ROOT/'tests/codegen_qml/probes/run.py', '--build-dir', output/'probes',
             '--sdk-prefix', prefix, '--codegen-python', python, '--qt-prefix', qt,
             '--cxx', tools['cxx'], '--cmake', tools['cmake'], '--ninja', tools['ninja'],
             '--jobs', args.jobs, *probe_args], 'conversion-probes')
        probes = json.loads((output/'probes/summary.json').read_text(encoding='utf-8'))
        if not probes.get('passed'):
            raise RuntimeError('Conversion probes did not complete')
        report['conversion_probes'] = probes
        example = output/'example'
        run([tools['cmake'], '-S', ROOT/'examples/codegen_qml', '-B', example, *common,
             '-DGENERATED_SDK='+str(sdk)], 'example-configure')
        check_cache(example, config)
        run([tools['cmake'], '--build', example, '--parallel', args.jobs], 'example-build')
        check_compile_sources(example, output)
        executable = example/('telemetry_qml_example'+suffix)
        binaries.append(executable)
        for name, mode in (('example-runtime', []), ('example-rpc-runtime', ['--rpc'])):
            run([executable, native/'descriptor.bin', native/'config.bin', *mode, '--smoke'], name)
        report['executions'] = [dict(path=str(p), sha256=digest(p), bytes=p.stat().st_size) for p in binaries]
        report['passed'] = True
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        report['error'] = str(error)
        print(str(error), file=sys.stderr)
    finally:
        report['source_sha256_after'] = ux_inputs()
        report['installed_sdk_sha256_after'] = sdk_inputs(prefix)
        try:
            report['codegen_after'] = codegen_identity(python, env, output)
            report['tools_sha256_after'] = {str(path): digest(path) for path in (*tools.values(), python)}
            if (report['source_sha256'] != report['source_sha256_after'] or
                    report['installed_sdk_sha256'] != report['installed_sdk_sha256_after'] or
                    report.get('codegen_before') != report['codegen_after'] or
                    report.get('tools_sha256') != report['tools_sha256_after']):
                raise RuntimeError('Qualification inputs changed during execution')
        except (OSError, ValueError, RuntimeError) as error:
            report['passed'] = False
            report['error'] = str(error)
        report['status'] = 'passed' if report['passed'] else 'failed'
        report['finished_utc'] = dt.datetime.now(dt.timezone.utc).isoformat()
        (output/'summary.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
