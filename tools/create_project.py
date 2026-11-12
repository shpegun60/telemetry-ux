#!/usr/bin/env python3
"""Create a complete installed-package QML or Web application. MIT."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import tarfile


ROOT = Path(__file__).absolute().parents[1]
QML = ('CMakeLists.txt', 'Device.hpp', 'main.cpp', 'Main.qml')
QT_CONNECTION = ('CMakeLists.txt', 'main.cpp', 'Main.qml')
WEB = ('prepare.py', 'serve.py', 'DeviceApp.js', 'index.html', 'favicon.svg',
       'native/CMakeLists.txt', 'native/Device.hpp', 'native/Peer.cpp')
RESERVED = {'con', 'prn', 'aux', 'nul', 'clock$'} | {
    prefix + str(index) for prefix in ('com', 'lpt') for index in range(1, 10)}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def encoded(value):
    return (json.dumps(value, indent=2, sort_keys=True) + '\n').encode('utf8')


def checked_path(value, *, exists=False):
    """Keep lexical paths; do not silently accept links or differently-cased aliases."""
    raw = os.fspath(value)
    if not raw or any(ord(char) < 32 for char in raw) or any(char in raw for char in ';$'):
        raise ValueError('Paths must not contain control characters, semicolons or dollar signs')
    if '..' in raw.replace('\\', '/').split('/') or raw.startswith('\\\\'):
        raise ValueError('Parent traversal and network paths are not accepted')
    path = Path(raw).absolute()
    parent = Path(path.anchor)
    for part in path.parts[1:]:
        if part.endswith(('.', ' ')) or part.split('.')[0].casefold() in RESERVED:
            raise ValueError('Reserved or ambiguous path component: ' + part)
        if any(char in part for char in '<>:"|?*'):
            raise ValueError('Unsupported path component: ' + part)
        if parent.exists():
            if not parent.is_dir():
                raise ValueError('Path parent is not a directory: ' + str(parent))
            matches = [entry.name for entry in parent.iterdir()
                       if entry.name.casefold() == part.casefold()]
            if matches and matches != [part]:
                raise ValueError('Case alias is not accepted: ' + str(parent / part))
        parent /= part
        try:
            info = parent.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
            raise ValueError('Symlink or reparse path is not accepted: ' + str(parent))
    if exists and not path.exists():
        raise ValueError('Input does not exist: ' + str(path))
    return path


def overlaps(left, right):
    left_parts = tuple(part.casefold() for part in left.parts)
    right_parts = tuple(part.casefold() for part in right.parts)
    size = min(len(left_parts), len(right_parts))
    return left_parts[:size] == right_parts[:size]


def package(path, expected):
    if not path.is_file():
        raise ValueError('Package must be an existing npm tarball: ' + str(path))
    with tarfile.open(path, 'r:gz') as archive:
        entries = [item for item in archive.getmembers() if item.name == 'package/package.json']
        if len(entries) != 1 or not entries[0].isfile() or entries[0].size > 65536:
            raise ValueError('Package requires one bounded package/package.json')
        with archive.extractfile(entries[0]) as stream:
            metadata = json.load(stream)
        if not isinstance(metadata, dict):
            raise ValueError('Package metadata must be a JSON object')
        if metadata.get('name') != expected or metadata.get('version') != '0.1.0':
            raise ValueError('Expected matching ' + expected + '@0.1.0 package')
    return sha(path.read_bytes())


def installed(path, relative):
    if not path.is_dir():
        raise ValueError('Installed prefix must be a directory: ' + str(path))
    config = checked_path(path / relative, exists=True)
    if not config.is_file():
        raise ValueError('Installed CMake package missing: ' + str(config))


def installed_qt(path):
    # Qt SDKs use lib/cmake; system Linux packages commonly use lib/<triplet>/cmake.
    candidates = [path / 'lib/cmake/Qt6/Qt6Config.cmake', path / 'lib64/cmake/Qt6/Qt6Config.cmake']
    candidates += sorted(path.glob('lib/*/cmake/Qt6/Qt6Config.cmake'))
    candidates = [candidate for candidate in candidates if candidate.is_file()]
    if len(candidates) != 1:
        raise ValueError('--qt-prefix must contain exactly one supported installed Qt6Config.cmake')
    checked_path(candidates[0], exists=True)


def wrapper(script, defaults):
    return ('''#!/usr/bin/env python3
"""Run this project's maintained application entry point. MIT."""
import json
from pathlib import Path
import runpy
import sys

HERE = Path(__file__).resolve().parent
options = json.loads((HERE / 'project-options.json').read_text(encoding='utf8'))
arguments = list(sys.argv[1:])
''' + defaults + '''
sys.argv = [str(HERE / ''' + repr(script) + '''), *arguments]
runpy.run_path(sys.argv[0], run_name='__main__')
''').encode('utf8')


def cmake_presets(options):
    return encoded({
        'version': 3, 'cmakeMinimumRequired': {'major': 3, 'minor': 21, 'patch': 0},
        'configurePresets': [{'name': 'local', 'generator': 'Ninja',
            'binaryDir': '${sourceDir}/build', 'cacheVariables': {
                'CMAKE_BUILD_TYPE': 'Release', 'CMAKE_FIND_USE_PACKAGE_REGISTRY': False,
                'CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY': False,
                'CMAKE_PREFIX_PATH': ';'.join(options[key] for key in ('sdk_prefix', 'ux_prefix', 'qt_prefix'))}}],
        'buildPresets': [{'name': 'local', 'configurePreset': 'local', 'jobs': 2}]})


def render(target, name, options):
    files, templates = {}, {}

    def copy(relative):
        source = checked_path(ROOT / relative, exists=True)
        if not source.is_file() or source.stat().st_size > 1024 * 1024:
            raise ValueError('Template is not a bounded ordinary file: ' + relative)
        data = source.read_bytes()
        templates[relative] = sha(data)
        return data

    if target == 'dynamic-qml':
        for file in QML:
            files[file] = copy('examples/dynamic_qml/' + file)
        old = b'project(TelemetryOwnDynamicQml LANGUAGES CXX)'
        if files['CMakeLists.txt'].count(old) != 1:
            raise ValueError('QML template project declaration changed; update the creator')
        files['CMakeLists.txt'] = files['CMakeLists.txt'].replace(
            old, ('project(' + name + ' LANGUAGES CXX)').encode('ascii'))
        files['Main.qml'] = files['Main.qml'].replace(b'My Telemetry device', name.encode('ascii'))
        if options:
            files['CMakePresets.json'] = cmake_presets(options)
        instructions = '''Open CMakeLists.txt in Qt Creator with the same Qt/compiler kit as your installed packages.
Set CMAKE_PREFIX_PATH to the matching SDK, UX and Qt prefixes if no preset was supplied.
With supplied prefixes: `cmake --preset local` then `cmake --build --preset local`.
Run the `telemetry_dynamic_example` target (add `.exe` on Windows).

Press Refresh in both panels. Enable Allow changes in one panel, choose Edit,
change gain and Apply. Enter is not required. Apply displays a separate actual
device readback; the sibling is invalidated until you explicitly Refresh it.
Change Device.hpp for your Model and Main.qml for your own composition.

Installed SDK components: core, workbench, encoded_backend, encoded_server.
Install UX with TELEMETRY_UX_BUILD_DYNAMIC_CONFIGURATOR=ON and link
TelemetryUX::dynamic_configurator. Generated DTOs, Python and the Inspector
application sources are not required to build or run this application.

The example owns a small local native Encoded loop. For a real connection keep
your one admitted Workbench/transport owner and call binding.poll() after its
progress. Panels own independent selection, draft, snapshot and response state;
they share that owner's one DynamicClient/FIFO. Removing a panel does not close
the connection. Retire/drain/poll before destroying native backing; read the
lifetime comments in main.cpp before substituting an asynchronous transport.
'''
    elif target == 'qt-connection':
        for file in QT_CONNECTION:
            files[file] = copy('examples/qt_connection/' + file)
        old = b'project(TelemetryQtConnection LANGUAGES CXX)'
        if files['CMakeLists.txt'].count(old) != 1:
            raise ValueError('Qt connection template project declaration changed; update the creator')
        files['CMakeLists.txt'] = files['CMakeLists.txt'].replace(
            old, ('project(' + name + ' LANGUAGES CXX)').encode('ascii'))
        files['Main.qml'] = files['Main.qml'].replace(
            b'Telemetry - one TCP connection, two panels',
            name.encode('ascii') + b' - one TCP connection, two panels')
        if options:
            files['CMakePresets.json'] = cmake_presets(options)
        instructions = '''Open CMakeLists.txt in Qt Creator with the same Qt/compiler kit as your installed packages.
Set CMAKE_PREFIX_PATH to the matching SDK, UX and Qt prefixes if no preset was supplied.
With supplied prefixes: `cmake --preset local` then `cmake --build --preset local`.
Run the `telemetry-qt-connection` target (add `.exe` on Windows).

Install the SDK with its optional qt_connection component and UX with
TELEMETRY_UX_BUILD_QT_CONNECTION=ON. Link TelemetryUX::qt_connection; Qt 6.12+
Core, Network, Gui, Qml, Quick and QuickControls2 are required by this application.
Python, generated DTOs and private Inspector sources are not runtime dependencies.

Connect to your matching device, or start an existing Telemetry Inspector binary
with `telemetry-inspector-qml --serve-demo 5577` in another terminal. That local
demo is read-only; installing this starter does not install a server or firmware.
Enter its host/port and press Connect. The initial address is 127.0.0.1:5577.
Both Configurator panels expose their own endpoint navigation and share one
ConnectionSession, DynamicClient and FIFO, while
keeping independent selection, draft, snapshot and response state. There is no
application-owned transport pump, Descriptor indexing or second connection.
Change Main.qml to compose your own panels; call presentation.createProvider()
for another panel. A provider borrows the connection and does not close it.

This explicitly selects the exclusive Resource-bootstrap/Encoded TCP reference
profile, not arbitrary TCP or UART. Use a matching trusted server. A fresh full
bootstrap from the same peer supplies remote supersession under that server's
contract; socket closure alone does not prove remote retirement. Authentication,
authorization, TLS and firmware networking belong to your application.

Read-only is the initial panel policy. The example host permits explicit changes;
the configuration panel still requires its Allow changes checkbox. Write uses
actual device readback. Disconnect retains terminal mutation facts, including
OutcomeUnknown; reconnect is explicit and never replays a mutation. Remove panels
freely. Destroy the connection outside a custom native callback stack; use
deleteLater() there. Ordinary Qt UI notifications arrive after native processing.
'''
    else:
        for file in WEB:
            relative = 'examples/web_device_starter/' + file
            files[relative] = copy(relative)
        for file in ('serve.py', 'serve_ws.py'):
            relative = 'examples/web_demo/' + file
            files[relative] = copy(relative)
        files['tools/export_web_assets.py'] = copy('tools/export_web_assets.py')
        files['examples/web_device_starter/index.html'] = files[
            'examples/web_device_starter/index.html'].replace(b'My Telemetry Device', name.encode('ascii'))
        files['prepare.py'] = wrapper('examples/web_device_starter/prepare.py', '''
for key in ('sdk_prefix', 'sdk_package', 'ux_package'):
    option = '--' + key.replace('_', '-')
    if key in options and not any(item == option or item.startswith(option + '=') for item in arguments):
        arguments += [option, options[key]]
if not any(item == '--output' or item.startswith('--output=') for item in arguments):
    arguments += ['--output', str(HERE.with_name(HERE.name + '-prepared'))]
''')
        files['serve.py'] = wrapper('examples/web_device_starter/serve.py', '''
if not any(item == '--prepared' or item.startswith('--prepared=') for item in arguments):
    arguments += ['--prepared', str(HERE.with_name(HERE.name + '-prepared'))]
''')
        instructions = '''Change examples/web_device_starter/native/Device.hpp for your own Model.
Change examples/web_device_starter/index.html and DeviceApp.js for presentation.
The maintained local transport lives in examples/web_demo; no producer checkout
or private Inspector source is required after project creation.

Install C++20, CMake 3.21+, Ninja, Node/npm and Python 3.10+. Install the matching
SDK prefix with core, encoded_server and batch_server. Prepare matching local
telemetry-sdk/telemetry-ux npm tarballs with `npm pack --ignore-scripts`.
If paths were supplied when creating the project, run `python -B prepare.py`.
Otherwise run:

```
python -B prepare.py --sdk-prefix /installed/sdk --sdk-package /packages/telemetry-sdk-0.1.0.tgz --ux-package /packages/telemetry-ux-0.1.0.tgz
```

Pass --cmake, --ninja, --cxx, --node or --npm-cli to prepare.py if tools are not
on PATH. Optional --codegen-python enables Static mode with the matching installed
core generator. Dynamic mode needs no generator. By default preparation writes
to a fresh sibling `<project-directory>-prepared`; your application sources are
separate from native builds, installed runtime packages and generated DTOs.

For deployable browser files add --export-web files, or --export-web cpp for the
same files plus a C++17 Flash asset table. Output is <prepared>/www/ with a
MIME/length/SHA-256 manifest; pass --web-max-bytes and --web-max-files to enforce
your target's runtime budgets. Dynamic export needs no generated SDK. All
exporter code is included in this project; no producer checkout is needed.
Add --web-asset hints/presentation.json for each extra file under
examples/web_device_starter/. Names are source-relative, stay the same in www/,
and must be plain local paths; no file is loaded or applied automatically.

Create an external Python environment with websockets==15.0.1, then use its Python
to run `python -B serve.py`. On Windows its interpreter is Scripts/python.exe;
on Linux bin/python. Open http://127.0.0.1:8080 and press Connect. Read Config,
enable changes, Edit periodMs to 1 and Apply + readback: the Model normalizes it
to 10. Command Reset and Service Scale use the same Session/FIFO. Disconnect and
Reconnect are explicit and never replay an uncertain operation.

To run only the exported browser tree, use `python -B serve.py --exported` after
preparing with --export-web. This host test uses the same Model and Session;
the bundle itself needs no Python or Node on the serving device.

The server is a localhost reference HTTP/WebSocket host, not STM32 networking.
Its bootstrap routes, monotonically increasing exclusive session lease and
one-complete-TEQ1-frame-per-WS-message contract must be preserved when adapting
your own server. Socket closure alone does not establish remote retirement.
The reference supplies no production authentication/authorization policy.
'''
    files['project-options.json'] = encoded(options)
    files['README.md'] = ('# ' + name + '\n\n' + instructions + '''
This directory is application-owned. Regeneration is refused over an existing
directory; keep your edits here and generate a new directory to compare upgrades.
project-manifest.json records copied template hashes and the initial file hashes.
Do not place user code inside a code-generator-owned output directory.
''').encode('utf8')
    if len({relative.casefold() for relative in files}) != len(files):
        raise ValueError('Template destinations contain a case alias')
    manifest = {'format': 1, 'target': target, 'name': name,
                'templates': templates, 'files': {key: sha(value) for key, value in sorted(files.items())}}
    files['project-manifest.json'] = encoded(manifest)
    return files


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', required=True, choices=('dynamic-qml', 'qt-connection', 'web-device'))
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--name', required=True)
    for option in ('sdk-prefix', 'ux-prefix', 'qt-prefix', 'sdk-package', 'ux-package'):
        parser.add_argument('--' + option, type=Path)
    args = parser.parse_args(argv)
    try:
        if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_-]{0,63}', args.name) or args.name.casefold() in RESERVED:
            raise ValueError('--name must be a non-reserved ASCII identifier of 1..64 characters')
        output = checked_path(args.output)
        if output.exists():
            raise ValueError('--output must not exist; no existing file or directory is overwritten')
        if overlaps(output, checked_path(ROOT, exists=True)):
            raise ValueError('--output must be outside the creator checkout')
        allowed = ('sdk_prefix', 'ux_prefix', 'qt_prefix') if args.target in ('dynamic-qml', 'qt-connection') else (
            'sdk_prefix', 'sdk_package', 'ux_package')
        options = {}
        for key in ('sdk_prefix', 'ux_prefix', 'qt_prefix', 'sdk_package', 'ux_package'):
            value = getattr(args, key)
            if value is None:
                continue
            if key not in allowed:
                raise ValueError('--' + key.replace('_', '-') + ' does not apply to this target')
            path = checked_path(value, exists=True)
            if overlaps(output, path):
                raise ValueError('--output must not overlap an installed prefix or package input')
            options[key] = str(path)
        if options and set(options) != set(allowed):
            raise ValueError('Supply all matching target paths together, or omit them all')
        if 'sdk_prefix' in options:
            installed(Path(options['sdk_prefix']), 'lib/cmake/Telemetry/TelemetryConfig.cmake')
        if 'ux_prefix' in options:
            installed(Path(options['ux_prefix']), 'lib/cmake/TelemetryUX/TelemetryUXConfig.cmake')
            installed_qt(Path(options['qt_prefix']))
        if 'sdk_package' in options:
            package(Path(options['sdk_package']), 'telemetry-sdk')
            package(Path(options['ux_package']), 'telemetry-ux')
        files = render(args.target, args.name, options)
        # All names, inputs and destination bytes have been checked before mkdir.
        output.mkdir(parents=True, exist_ok=False)
        for relative, data in sorted(files.items()):
            destination = output / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            with destination.open('xb') as stream:
                stream.write(data)
    except (ValueError, OSError, tarfile.TarError, json.JSONDecodeError) as error:
        parser.error(str(error))
    print('Created ' + args.target + ' project: ' + str(output))
    print('Start with ' + str(output / 'README.md'))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
