#!/usr/bin/env python3
"""Qualify deploy export from actual npm-installed SDK/UX packages. MIT."""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
APP = ['index.html', 'app.js', 'generated/js/device.js', 'generated/js/device.metadata.js',
       'generated/ui/DevicePage.js']
SESSION_APP = ['index.html', 'SessionDemo.js', 'generated/js/device.js', 'generated/js/device.metadata.js']


def sha(data):
    return hashlib.sha256(data).hexdigest()


def verify_cpp_table(root, manifest, output, compiler, command):
    """Compile the real table and independently compare every published byte."""
    source = output / 'all-assets.cpp'
    source.write_text('#include "telemetry_web_assets.hpp"\n#include <cstdio>\n'
        'int main(){using namespace telemetry_ux_assets; if(find("/absent"))return 1;'
        'for(const auto& asset:assets){if(find(asset.path)!=&asset)return 2;'
        'std::printf("%s\\t%s\\t%zu\\t",asset.path,asset.mime,asset.size);'
        'for(std::size_t i=0;i<asset.size;++i)std::printf("%02x",unsigned(asset.data[i]));'
        'std::putchar(\'\\n\');}return 0;}\n', encoding='ascii')
    compiler = Path(shutil.which(compiler) or compiler).absolute()
    if not compiler.is_file():
        raise RuntimeError('Actual compiler missing: ' + str(compiler))
    executable = output / ('all-assets.exe' if os.name == 'nt' else 'all-assets')
    command('table-compile', [compiler, '-std=c++17', '-Os', '-Wall', '-Wextra', '-Werror',
                              '-I', root, source, '-o', executable])
    environment = dict(os.environ, PATH=str(compiler.parent) + os.pathsep + os.environ.get('PATH', ''))
    text = command('table-run', [executable], env=environment)
    rows = text.splitlines()
    if len(rows) != len(manifest['assets']):
        raise RuntimeError('Compiled table asset count differs from its manifest')
    for line, item in zip(rows, manifest['assets']):
        path, mime, length, hexadecimal = line.split('\t')
        payload = bytes.fromhex(hexadecimal)
        if (path != item['path'] or mime != item['mime'] or int(length) != item['bytes'] or
                len(payload) != item['bytes'] or sha(payload) != item['sha256'] or
                payload != (root / path.lstrip('/')).read_bytes()):
            raise RuntimeError('Compiled table path, MIME or bytes differ: ' + path)
    return dict(assets=len(rows), bytes=manifest['totalBytes'], cxx17=True,
                executable_sha256=sha(executable.read_bytes()))


def prepared_export(args, output):
    """Verify and relocate an already prepared own-device export without rebuilding it."""
    root = args.export_root.resolve()
    inventory = lambda directory: {path.relative_to(directory).as_posix(): sha(path.read_bytes())
                                   for path in sorted(directory.rglob('*')) if path.is_file()}
    before = inventory(root)
    report = dict(passed=False, hardware=False, commands=[], exported_sha256_before=before,
                  started_utc=datetime.now(timezone.utc).isoformat())

    def command(label, argv, env=None):
        result = subprocess.run(list(map(str, argv)), cwd=output, env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
        log = output / (label + '.log')
        log.write_text(result.stdout, encoding='utf8')
        report['commands'].append(dict(label=label, argv=list(map(str, argv)), exit=result.returncode,
                                       log=log.name, sha256=sha(log.read_bytes())))
        if result.returncode:
            raise RuntimeError(label + ' failed; see ' + str(log))
        return result.stdout

    try:
        manifest = json.loads((root / 'assets.json').read_text(encoding='utf8'))
        if manifest.get('format') != 'telemetry-ux-web-assets-v1':
            raise RuntimeError('Prepared export has an unsupported manifest')
        expected = {'assets.json'}
        for item in manifest['assets']:
            path = root / item['path'].lstrip('/')
            if not path.resolve().is_relative_to(root) or path.is_symlink():
                raise RuntimeError('Manifest asset escapes the prepared export')
            data = path.read_bytes()
            if len(data) != item['bytes'] or sha(data) != item['sha256']:
                raise RuntimeError('Prepared manifest bytes differ: ' + item['path'])
            expected.add(item['path'].lstrip('/'))
        if 'telemetry_web_assets.hpp' in before:
            expected.add('telemetry_web_assets.hpp')
        if set(before) != expected or any('node_modules' in name or name.endswith(('.d.ts', '.ts')) for name in before):
            raise RuntimeError('Prepared export contains undeclared development files')
        if manifest['totalBytes'] != sum(item['bytes'] for item in manifest['assets']):
            raise RuntimeError('Prepared manifest total byte count differs')
        relocated = output / 'relocated' / 'www'
        shutil.copytree(root, relocated)
        if before != inventory(relocated):
            raise RuntimeError('Relocation changed prepared asset bytes')
        if args.cxx and (relocated / 'telemetry_web_assets.hpp').is_file():
            report['compiled_table'] = verify_cpp_table(relocated, manifest, output, args.cxx, command)
        report.update(assets=len(manifest['assets']), bytes=manifest['totalBytes'],
                      relocated_sha256=inventory(relocated))
        report['exported_sha256_after'] = inventory(root)
        if report['exported_sha256_after'] != before or report['relocated_sha256'] != before:
            raise RuntimeError('Prepared or relocated exports changed during qualification')
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
    finally:
        report['finished_utc'] = datetime.now(timezone.utc).isoformat()
        (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(json.dumps(dict(passed=report['passed'], assets=report.get('assets'), bytes=report.get('bytes'))))
    return 0 if report['passed'] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--public-root', type=Path)
    parser.add_argument('--export-root', type=Path,
                        help='Verify/relocate an existing prepared www bundle and compile all C++ table bytes')
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--session-demo', action='store_true',
                        help='Export the prepared examples/session_web app using its explicit SDK root import')
    parser.add_argument('--cxx', help='Optional host C++17 compiler; builds and executes table probe')
    parser.add_argument('--arm-cxx', help='Optional actual ARM C++ compiler; object-only table probe')
    parser.add_argument('--browser-python', type=Path,
                        help='Installed Playwright/websockets environment for deployed browser journeys')
    parser.add_argument('--device', type=Path, help='Actual native SDK peer; required with --browser-python')
    args = parser.parse_args()
    if bool(args.public_root) == bool(args.export_root):
        parser.error('Supply exactly one of --public-root or --export-root')
    if args.export_root:
        if args.session_demo or args.arm_cxx or args.browser_python or args.device:
            parser.error('--export-root takes only --build-dir and optional --cxx')
        output = args.build_dir.resolve()
        root = args.export_root.resolve()
        if output.exists() or any(path == output or path in output.parents or output in path.parents for path in (ROOT, root)):
            parser.error('Use a fresh build directory outside sources and prepared export')
        output.mkdir(parents=True)
        return prepared_export(args, output)
    if bool(args.browser_python) != bool(args.device):
        parser.error('--browser-python and --device must be supplied together')
    public, output = args.public_root.resolve(), args.build_dir.resolve()
    if output.exists() or any(path == output or path in output.parents or output in path.parents
                              for path in (ROOT, public)):
        parser.error('Use a fresh build directory outside source and prepared public trees')
    output.mkdir(parents=True)
    spec = importlib.util.spec_from_file_location('asset_export', ROOT / 'tools/export_web_assets.py')
    tool = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tool)
    app = SESSION_APP if args.session_demo else APP
    sdk_files = tool.SDK_FILES + (tool.SESSION_SDK_FILES if args.session_demo else ())
    browser_script = HERE / 'SessionBrowser.py' if args.session_demo else ROOT / 'tests/websocket_transport/browser.py'
    sources = [ROOT / 'tools/export_web_assets.py', HERE / 'TestExport.py', HERE / 'run.py']
    sources += [public / name for name in app]
    if args.browser_python:
        sources += [browser_script,
                    ROOT / 'examples/web_demo/serve.py', ROOT / 'examples/web_demo/serve_ws.py',
                    args.device.resolve()]
    for name, paths in (('telemetry-sdk', sdk_files), ('telemetry-ux', tool.UX_FILES)):
        sources += [public / 'node_modules' / name / file for file in (*paths, 'package.json')]
    before = {str(path): sha(path.read_bytes()) for path in sources}
    report = dict(passed=False, hardware=False, started_utc=datetime.now(timezone.utc).isoformat(),
                  source_sha256_before=before, commands=[], session_demo=args.session_demo)
    (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')

    def command(label, argv, cwd=output, env=None):
        result = subprocess.run([str(arg) for arg in argv], cwd=cwd, env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
        log = output / (label + '.log')
        log.write_text(result.stdout, encoding='utf8')
        report['commands'].append(dict(label=label, argv=list(map(str, argv)), exit=result.returncode,
                                       log=log.name, sha256=sha(log.read_bytes())))
        if result.returncode:
            raise RuntimeError(label + ' failed; see ' + str(log))
        return result.stdout

    try:
        command('unit', [sys.executable, HERE / 'TestExport.py', '-v'])
        a, b = output / 'export-a', output / 'relocated' / 'www'
        first = tool.export_assets(public, a, app, cpp=True)
        second = tool.export_assets(public, b, list(reversed(app)), cpp=True)
        inventory = lambda directory: {path.relative_to(directory).as_posix(): sha(path.read_bytes())
                                      for path in directory.rglob('*') if path.is_file()}
        if first != second or inventory(a) != inventory(b):
            raise RuntimeError('Relocated export is not deterministic')
        for item in first['assets']:
            data = (b / item['path'].lstrip('/')).read_bytes()
            if len(data) != item['bytes'] or sha(data) != item['sha256']:
                raise RuntimeError('Manifest content mismatch')
        if any('node_modules' in name or name.endswith(('.d.ts', '.ts')) for name in inventory(b)):
            raise RuntimeError('A development/package-manager path escaped into deploy output')
        extra_sdk_paths = {'vendor/telemetry-sdk/web/' + name for name in
                           ('index.js', 'Session.js', 'WebSocketConnection.js')}
        deployed_session_paths = extra_sdk_paths.intersection(inventory(b))
        if deployed_session_paths != (extra_sdk_paths if args.session_demo else set()):
            raise RuntimeError('Session modules must be exported only for the explicit SDK root layout')
        report.update(assets=len(first['assets']), bytes=first['totalBytes'],
                      exported_sha256=inventory(b), packages=first['packages'])
        (output / 'probe.cpp').write_text('#include "telemetry_web_assets.hpp"\n'
            '#include <cstring>\nint main(){using namespace telemetry_ux_assets;'
            'const auto* p=find("/index.html");'
            'if(!p||p->size<10||p->data[0]!=\'<\'||find("/absent"))return 1;'
            f'if(count!={len(first["assets"])})return 2;'
            'return std::strcmp(p->mime,"text/html; charset=utf-8")==0?0:3;}\n', encoding='ascii')
        for option, arm in ((args.cxx, False), (args.arm_cxx, True)):
            if not option:
                continue
            compiler = Path(shutil.which(option) or option).absolute()
            if not compiler.is_file():
                raise RuntimeError('Actual compiler missing: ' + str(compiler))
            command('arm-version' if arm else 'host-version', [compiler, '--version'])
            executable = output / ('probe-arm.o' if arm else 'probe.exe' if os.name == 'nt' else 'probe')
            argv = [compiler, '-std=c++17', '-Os', '-Wall', '-Wextra', '-Werror', '-I', b,
                    output / 'probe.cpp', '-o', executable]
            if arm:
                argv += ['-c', '-mcpu=cortex-m7', '-mthumb', '-fno-exceptions', '-fno-rtti']
            command('arm-compile' if arm else 'host-compile', argv)
            if not arm:
                env = dict(os.environ, PATH=str(compiler.parent) + os.pathsep + os.environ['PATH'])
                command('host-run', [executable], env=env)
            report['arm_compiled' if arm else 'host_executed'] = True
        if args.cxx:
            report['compiled_table'] = verify_cpp_table(b, first, output, args.cxx, command)
        if args.browser_python:
            # Use only shipped runtime assets. The separate Renderer.js test
            # fixture is deliberately absent from a firmware deployment.
            command('deployed-browser', [args.browser_python.absolute(), '-B',
                    browser_script, '--device', args.device.resolve(),
                    '--public-root', b, '--data-dir', output / 'browser'])
            browser = json.loads((output / 'browser/summary.json').read_text(encoding='utf8'))
            if (not browser.get('passed') or not browser.get('static') or
                    not browser.get('dynamic') or browser.get('checks', 0) < (38 if args.session_demo else 88) or
                    browser.get('errors') or browser.get('warnings') or
                    browser.get('native_exit') != 0 or browser.get('native_stderr')):
                raise RuntimeError('Relocated runtime did not pass actual warning-free browser journeys')
            report['browser'] = browser
        if inventory(b) != report['exported_sha256']:
            raise RuntimeError('Deployed assets changed during qualification')
        for entry in report['commands']:
            if sha((output / entry['log']).read_bytes()) != entry['sha256']:
                raise RuntimeError('A retained execution log changed')
        after = {str(path): sha(path.read_bytes()) for path in sources}
        report['source_sha256_after'] = after
        if before != after:
            raise RuntimeError('Sources or installed inputs changed during qualification')
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
    finally:
        (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(json.dumps(dict(passed=report['passed'], assets=report.get('assets'),
                          bytes=report.get('bytes'), report=str(output / 'summary.json'))))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
