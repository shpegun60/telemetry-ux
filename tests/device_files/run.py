#!/usr/bin/env python3
"""Installed Session -> installed UX: the Device files models and panel over
localhost TCP. Builds tests/device_files against the given prefixes and runs
the model check and the off-screen panel check. No board. MIT."""
import argparse
import locale
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True, type=Path)
    parser.add_argument('--sdk-prefix', required=True, type=Path)
    parser.add_argument('--ux-prefix', required=True, type=Path)
    parser.add_argument('--qt-prefix', required=True, type=Path)
    parser.add_argument('--cmake', default='cmake')
    parser.add_argument('--ninja', default='ninja')
    parser.add_argument('--cxx', default='g++')
    # Against sanitized SDK/UX archives the checks must be instrumented too, or
    # the link misses the __asan_ symbols the archives call.
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    output = args.build_dir.resolve()
    if output == ROOT or ROOT in output.parents:
        parser.error('Use a build directory outside the repository')
    if output.exists() and any(output.iterdir()):
        parser.error('Use a fresh empty build directory; nothing is deleted')
    output.mkdir(parents=True, exist_ok=True)
    sdk, ux, qt = (p.resolve() for p in (args.sdk_prefix, args.ux_prefix, args.qt_prefix))
    sdk_configs = list(sdk.rglob('TelemetryConfig.cmake'))
    ux_configs = list(ux.rglob('TelemetryUXConfig.cmake'))
    if len(sdk_configs) != 1 or len(ux_configs) != 1:
        parser.error('Each prefix must hold exactly one package configuration')
    tools = {name: shutil.which(getattr(args, name)) or getattr(args, name) for name in ('cmake', 'ninja', 'cxx')}
    environment = dict(os.environ, QT_QPA_PLATFORM='offscreen')
    environment['PATH'] = os.pathsep.join([str(qt / 'bin'), str(Path(tools['cxx']).parent), environment.get('PATH', '')])

    def run(label, command, cwd=output, timeout=600):
        result = subprocess.run(list(map(str, command)), cwd=cwd, env=environment,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
        # Native qPrintable() uses the Windows system code page even when
        # Python itself is in UTF-8 mode. Retain the exact bytes as evidence
        # before decoding; a reader-thread decoding error must not lose them.
        (output / (label + '.raw.log')).write_bytes(result.stdout)
        try:
            text = result.stdout.decode('utf-8')
        except UnicodeDecodeError:
            text = result.stdout.decode(locale.getencoding(), errors='replace')
        (output / (label + '.log')).write_text(text, encoding='utf-8')
        print(label + ': ' + str(result.returncode), flush=True)
        if result.returncode:
            raise RuntimeError(label + ' failed:\n' + text[-8000:])
        return text

    flags = '-Wall -Wextra -Werror'
    if args.sanitize:
        flags += ' -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
        # The narrowly named upstream software-texture exception the other
        # installed gates use; no application frames.
        suppression = output / 'qt-software-textures.lsan'
        suppression.write_text('leak:QSGRenderContext::textureForFactory\n', encoding='ascii')
        environment.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',
                           UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1',
                           LSAN_OPTIONS='exitcode=86:print_suppressions=1:suppressions=' + str(suppression))
    build = output / 'build'
    configure = [tools['cmake'], '-S', HERE, '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
                 '-DCMAKE_CXX_COMPILER=' + tools['cxx'], '-DCMAKE_MAKE_PROGRAM=' + tools['ninja'],
                 '-DCMAKE_CXX_FLAGS=' + flags,
                 '-DCMAKE_PREFIX_PATH=' + ';'.join(map(str, (sdk, ux, qt))),
                 '-DTelemetry_DIR=' + str(sdk_configs[0].parent),
                 '-DTelemetryUX_DIR=' + str(ux_configs[0].parent),
                 '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF']
    if args.sanitize:
        configure.append('-DCMAKE_CXX_FLAGS_RELEASE=-O1 -g -DNDEBUG')
    run('configure', configure)
    run('build', [tools['cmake'], '--build', build])
    suffix = '.exe' if os.name == 'nt' else ''
    for name, pattern, floor in (('telemetry_event_log_check', r'Event log: (\d+) checks, 0 failures', 41),
                                 ('telemetry_device_files_check', r'Device files UX: (\d+) checks, 0 failures', 1056),
                                 ('telemetry_device_files_panel', r'Device files panel: (\d+) checks, warnings=0', 490)):
        text = run(name, [build / (name + suffix)], cwd=build, timeout=300)
        found = re.findall(pattern, text)
        if len(found) != 1 or int(found[0]) < floor:
            raise RuntimeError(name + ' did not reach its assertion floor')
    print('Device files checks passed', flush=True)


if __name__ == '__main__':
    main()
