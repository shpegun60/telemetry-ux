#!/usr/bin/env python3
"""Compile strict Qt converters from an installed generator; no SDK sources. MIT."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from support import (ROOT, check_compile_sources, codegen_identity, digest,
                     installed_sdk, sdk_inputs, ux_inputs)
import fixture


def main(argv=None):
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
    args = parser.parse_args(argv)
    output, qt, prefix = (p.resolve() for p in (args.build_dir, args.qt_prefix, args.sdk_prefix))
    if output == ROOT or ROOT in output.parents or output == prefix or prefix in output.parents:
        parser.error('--build-dir must be outside UX and the installed SDK')
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    output.mkdir(parents=True, exist_ok=True)
    _, metadata = installed_sdk(prefix)
    python = args.codegen_python.absolute()
    tools = {name: Path(shutil.which(getattr(args, name)) or getattr(args, name)).absolute()
             for name in ('cxx', 'cmake', 'ninja')}
    env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', PYTHONNOUSERSITE='1',
               QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software')
    env.pop('PYTHONPATH', None)
    env.pop('PYTHONHOME', None)
    env['PATH'] = os.pathsep.join((str(tools['cxx'].parent), str(qt/'bin'), env.get('PATH', '')))
    if args.sanitize:
        env['ASAN_OPTIONS'] = 'detect_leaks=1:halt_on_error=1'
        env['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
    selected = {str(path): path for path in (*tools.values(), python)}
    for directory, names in ((qt/'bin', ('Qt6Core.dll', 'Qt6Qml.dll', 'qmake.exe', 'qmake', 'qmake6')),
                              (qt/'lib', ('libQt6Core.so', 'libQt6Qml.so')),
                              (qt/'lib/x86_64-linux-gnu', ('libQt6Core.so', 'libQt6Qml.so'))):
        for name in names:
            path = directory/name
            if path.is_file():
                selected[str(path.resolve())] = path.resolve()
    report = dict(passed=False, hardware=False, started_utc=datetime.now(timezone.utc).isoformat(),
                  commands=[], executions=[], source_sha256=ux_inputs(),
                  installed_sdk_sha256=sdk_inputs(prefix), sdk_metadata=metadata,
                  qt_prefix=str(qt), sanitize=args.sanitize)

    def run(command, name):
        command = list(map(str, command))
        start = time.monotonic()
        result = subprocess.run(command, cwd=output, env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=240)
        (output/(name+'.log')).write_text(result.stdout, encoding='utf-8')
        report['commands'].append(dict(name=name, command=command, returncode=result.returncode,
                                       seconds=time.monotonic()-start, log=name+'.log'))
        if result.returncode:
            raise RuntimeError(name+' failed\n'+result.stdout[-12000:])
        print(name+': pass', flush=True)
        return result.stdout

    def generate(data, namespace, folder):
        folder.mkdir(parents=True, exist_ok=True)
        source = folder/'descriptor.bin'
        source.write_bytes(data)
        run([python, '-B', '-m', 'tools.telemetry_codegen', 'generate', source,
             '--namespace', namespace, '--targets', 'cpp,qml', '--output', folder],
            'generate-'+namespace.replace('::', '-'))
        return json.loads((folder/'manifest.json').read_text(encoding='utf-8'))

    try:
        report['codegen_before'] = codegen_identity(python, env, output)
        report['tool_sha256'] = {name: digest(path) for name, path in selected.items()}
        for name, path in tools.items():
            run([path, '--version'], name+'-version')
        sdk = output/'sdk'
        manifest = fixture.write(sdk, generate)
        report['compiled_modules'] = fixture.write_modules(sdk/'modules', generate)
        report['descriptor_sha256'] = manifest['descriptor_sha256']
        report['descriptor_fingerprint'] = manifest['descriptor_fingerprint']
        report['generated_sha256'] = {p.relative_to(sdk).as_posix(): digest(p)
                                      for p in sorted(sdk.rglob('*')) if p.is_file()}
        build = output/'build'
        flags = '-Wall -Wextra -Werror'
        if args.sanitize:
            flags += ' -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
        run([tools['cmake'], '-S', HERE, '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
             '-DCMAKE_CXX_COMPILER='+str(tools['cxx']), '-DCMAKE_MAKE_PROGRAM='+str(tools['ninja']),
             '-DCMAKE_CXX_FLAGS='+flags, '-DCMAKE_PREFIX_PATH='+str(qt),
             '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', '-DGENERATED_SDK='+str(sdk),
             '-DGENERATED_MODULES='+str(sdk/'modules')], 'configure')
        run([tools['cmake'], '--build', build, '--parallel', args.jobs], 'build')
        check_compile_sources(build, output)
        suffix = '.exe' if os.name == 'nt' else ''
        for name, minimum in (('values', 61), ('snapshot', 48), ('legacy', 3)):
            executable = build/('qml_'+name+'_probe'+suffix)
            stdout = run([executable], 'execute-'+name)
            matches = re.findall(r'(?m)^qml '+name+r' checks=(\d+) failures=0\s*$', stdout)
            if len(matches) != 1 or int(matches[0]) < minimum:
                raise RuntimeError(name+' did not report its required positive checks')
            report['executions'].append(dict(name=name, checks=int(matches[0]), failures=0,
                                              executable=str(executable), executable_sha256=digest(executable)))
        report['checks'] = sum(row['checks'] for row in report['executions'])
        report['passed'] = True
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        report['error'] = str(error)
        print(str(error), file=sys.stderr)
    finally:
        report['source_sha256_after'] = ux_inputs()
        report['installed_sdk_sha256_after'] = sdk_inputs(prefix)
        try:
            report['codegen_after'] = codegen_identity(python, env, output)
            report['tool_sha256_after'] = {name: digest(path) for name, path in selected.items()}
            if (report['source_sha256'] != report['source_sha256_after'] or
                    report['installed_sdk_sha256'] != report['installed_sdk_sha256_after'] or
                    report.get('codegen_before') != report['codegen_after'] or
                    report.get('tool_sha256') != report['tool_sha256_after']):
                raise RuntimeError('Qualification source/installed/tool inputs changed during execution')
        except (OSError, ValueError, RuntimeError) as error:
            report['passed'] = False
            report['error'] = str(error)
        report['finished_utc'] = datetime.now(timezone.utc).isoformat()
        (output/'summary.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
