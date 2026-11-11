#!/usr/bin/env python3
"""Run pr49_header_probe.qml against a telemetry-ux checkout with one Qt kit.

Usage: run_pr49_probe.py --ux <checkout> --qt <Qt prefix> --out <empty dir>
Copies the checkout's lib/qml/Telemetry/UX/*.qml beside the probe as "ux",
runs Qt's qml tool offscreen with the Basic style, and prints its output.
"""
import argparse
import os
import shutil
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ux', type=Path, required=True)
    parser.add_argument('--qt', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    if out.exists() and any(out.iterdir()):
        parser.error('output must be new or empty')
    (out / 'ux').mkdir(parents=True)
    for qml in (args.ux / 'lib/qml/Telemetry/UX').glob('*.qml'):
        shutil.copy2(qml, out / 'ux' / qml.name)
    shutil.copy2(HERE / 'pr49_header_probe.qml', out / 'probe.qml')
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software',
               QT_QUICK_CONTROLS_STYLE='Basic', QT_FORCE_STDERR_LOGGING='1')
    env['PATH'] = os.pathsep.join((str(args.qt / 'bin'), env.get('PATH', '')))
    if os.name == 'nt':
        env['QT_QPA_FONTDIR'] = str(Path(os.environ.get('WINDIR', r'C:\Windows')) / 'Fonts')
    result = subprocess.run([str(args.qt / 'bin' / 'qml'), str(out / 'probe.qml')], cwd=out, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=300)
    (out / 'probe.log').write_text(result.stdout, encoding='utf-8')
    print(result.stdout)
    return result.returncode


if __name__ == '__main__':
    raise SystemExit(main())
