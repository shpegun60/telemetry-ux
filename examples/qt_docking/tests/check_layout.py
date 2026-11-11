#!/usr/bin/env python3
"""Layout and title-bar checks for the Qt docking example (MIT).

Runs the built example several times against layout files in a fresh
temporary directory, never the user's settings, and never connects to a
device. Every restore check has a control run that must fail, so a restore
that silently does nothing cannot pass.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def run(exe, args, env, label, expect_exit):
    result = subprocess.run([str(exe), *args], env=env, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, timeout=120)
    output = result.stdout.strip()
    ok = result.returncode == expect_exit
    print(f"{'ok  ' if ok else 'FAIL'} {label}: exit {result.returncode} (expected {expect_exit})")
    if not ok or os.environ.get('CHECK_LAYOUT_VERBOSE'):
        for line in output.splitlines()[-12:]:
            print('     ' + line)
    return ok


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True, type=Path, help='built telemetry-qt-docking executable')
    parser.add_argument('--platform', default='', help='QT_QPA_PLATFORM for the runs (default: native)')
    args = parser.parse_args()
    exe = args.exe.resolve()
    if not exe.is_file():
        parser.error(f'not an executable: {exe}')
    env = dict(os.environ)
    if args.platform:
        env['QT_QPA_PLATFORM'] = args.platform

    results = []
    with tempfile.TemporaryDirectory(prefix='qt-docking-check-') as directory:
        layout = Path(directory) / 'layout.json'
        missing = Path(directory) / 'missing.json'
        corrupt = Path(directory) / 'corrupt.json'
        corrupt.write_text('{"this is": "not a KDDockWidgets layout"', encoding='utf-8')
        results.append(run(exe, ['--smoke-test'], env, 'smoke: open Connection, float and dock, reset', 0))
        results.append(run(exe, ['--layout-file', str(layout), '--prepare-layout'], env,
                           'prepare: Connection, Compare open; Device tree and Trend floating; save', 0))
        results.append(layout.is_file() and layout.stat().st_size > 0)
        print(f"{'ok  ' if results[-1] else 'FAIL'} the prepared layout file exists")
        results.append(run(exe, ['--layout-file', str(layout), '--expect-layout', 'restored'], env,
                           'restart restores the prepared layout', 0))
        results.append(run(exe, ['--layout-file', str(missing), '--expect-layout', 'restored'], env,
                           'control: no saved file cannot pass as restored', 1))
        results.append(run(exe, ['--layout-file', str(layout), '--expect-layout', 'default'], env,
                           'control: the prepared layout is not the default', 1))
        results.append(run(exe, ['--layout-file', str(missing), '--expect-layout', 'default'], env,
                           'no saved file starts from the default', 0))
        results.append(run(exe, ['--layout-file', str(corrupt), '--expect-layout', 'default'], env,
                           'a damaged layout file falls back to the default', 0))
        results.append(run(exe, ['--layout-file', str(missing), '--title-bar-check'], env,
                           'title-bar button floats the Device tree and docks it back', 0))
    passed = all(results)
    print(f"Qt docking layout checks: {sum(results)}/{len(results)} passed")
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
