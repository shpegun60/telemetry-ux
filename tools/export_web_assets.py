#!/usr/bin/env python3
"""Export prepared, installed Telemetry browser assets for a firmware HTTP stack.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
This deploy-time tool supplies files, not an HTTP/WebSocket server or admission.
"""
import argparse
import hashlib
from html.parser import HTMLParser
import json
from pathlib import Path, PurePosixPath
import re
import stat
from urllib.parse import unquote, urlsplit


SDK_FILES = ('LICENSE', 'lib/client/web/Client.js', 'web/telemetry.js', 'web/DynamicClient.js')
UX_FILES = ('LICENSE', 'web/index.js', 'web/Provider.js', 'web/ProviderContract.js',
            'web/Configurator.js', 'web/Element.js', 'web/WebSocketTransport.js',
            'web/Configurator.css')
IMPORTS = {
    'telemetry-sdk/client': 'telemetry-sdk/lib/client/web/Client.js',
    'telemetry-sdk/runtime': 'telemetry-sdk/web/telemetry.js',
    'telemetry-sdk/dynamic': 'telemetry-sdk/web/DynamicClient.js',
    'telemetry-ux/web': 'telemetry-ux/web/index.js',
    'telemetry-ux/transports/websocket': 'telemetry-ux/web/WebSocketTransport.js',
}
MIME = {'.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8',
        '.mjs': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8',
        '.json': 'application/json', '.bin': 'application/octet-stream',
        '.svg': 'image/svg+xml', '.png': 'image/png', '.ico': 'image/x-icon',
        '.woff2': 'font/woff2'}
MAP = re.compile(r'(<script\b[^>]*\btype\s*=\s*[\'"]importmap[\'"][^>]*>)([\s\S]*?)(</script\s*>)', re.I)
MODULE = re.compile(r'(?:(?:^|(?<=;))\s*(?:import|export)\s+[^;]*?\bfrom\s*|'
                    r'(?:^|(?<=;))\s*import\s*|\bimport\s*\(\s*)[\'"]([^\'"\r\n]+)[\'"]', re.M)
RESERVED = {'CON', 'PRN', 'AUX', 'NUL', *(f'COM{i}' for i in range(1, 10)),
            *(f'LPT{i}' for i in range(1, 10))}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def asset_name(value):
    if not isinstance(value, str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._/-]*', value):
        raise ValueError('Asset paths must be plain relative ASCII paths')
    parts = value.split('/')
    if any(not part or part in ('.', '..') or part.endswith('.') or
           part.split('.')[0].upper() in RESERVED for part in parts):
        raise ValueError('Unsafe asset path: ' + value)
    return value


def no_links(path):
    """Reject symlinks and Windows reparse points in existing path components."""
    path = Path(path).absolute()
    for part in (path, *path.parents):
        try:
            info = part.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
            raise ValueError('Symlink/reparse path is not a deploy input/output: ' + str(part))


def read_file(root, relative, limit):
    path = root.joinpath(*PurePosixPath(asset_name(relative)).parts)
    no_links(path)
    if not path.is_file() or root not in path.resolve().parents:
        raise ValueError('Missing regular input file: ' + str(path))
    if path.stat().st_size > limit:
        raise ValueError('Asset byte budget exceeded: ' + relative)
    data = path.read_bytes()
    if len(data) > limit:
        raise ValueError('Asset byte budget exceeded: ' + relative)
    return data


def local_reference(value, source, files):
    """Resolve an exported URL, refusing remote, encoded and escaping paths."""
    if not value or value.startswith('#'):
        return
    parsed = urlsplit(value)
    if parsed.scheme or parsed.netloc or unquote(parsed.path) != parsed.path or '\\' in value:
        raise ValueError('Only plain local asset references are exported: ' + value)
    parts = [] if parsed.path.startswith('/') else list(PurePosixPath(source).parent.parts)
    for part in parsed.path.split('/'):
        if part in ('', '.'):
            continue
        if part == '..':
            if not parts:
                raise ValueError('Asset reference escapes export: ' + value)
            parts.pop()
        else:
            parts.append(part)
    target = '/'.join(parts)
    if target not in files:
        raise ValueError('Referenced asset missing from explicit export: ' + source + ' -> ' + value)


class References(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.values = []
        self.inline_modules = []
        self.module = False
        self.styles = []
        self.style = False

    def handle_starttag(self, tag, attrs):
        if tag == 'base':
            raise ValueError('base elements would change local module resolution')
        if tag == 'script':
            self.module = dict(attrs).get('type') == 'module'
        if tag == 'style':
            self.style = True
        for name, value in attrs:
            # Hyperlinks remain application navigation, not runtime assets.
            if name == 'src' or (name == 'href' and tag in ('link', 'base')):
                self.values.append(value or '')
            if name == 'style':
                self.styles.append(value or '')
            if name == 'srcset' and value:
                for candidate in value.split(','):
                    parts = candidate.split()
                    if not 1 <= len(parts) <= 2 or (len(parts) == 2 and not
                            re.fullmatch(r'(?:[0-9]+w|[0-9]+(?:\.[0-9]+)?x)', parts[1])):
                        raise ValueError('Unsupported srcset; use explicit local URL candidates')
                    self.values.append(parts[0])

    def handle_endtag(self, tag):
        if tag == 'script':
            self.module = False
        if tag == 'style':
            self.style = False

    def handle_data(self, data):
        if self.module:
            self.inline_modules.append(data)
        if self.style:
            self.styles.append(data)


def css_references(source):
    """Recognize normal literal CSS URLs, refusing nonliteral @import forms."""
    source = re.sub(r'/\*[\s\S]*?\*/', '', source)
    for match in re.finditer(r'url\(\s*(?:"([^"]*)"|\'([^\']*)\'|([^\s)]*))\s*\)', source, re.I):
        yield next(group for group in match.groups() if group is not None)
    for match in re.finditer(r'@import\b([^;]*)(?:;|$)', source, re.I):
        value = match[1].lstrip()
        quoted = re.match(r'([\'"])(.*?)\1', value)
        if quoted:
            yield quoted[2]
        elif not re.match(r'url\(', value, re.I):
            raise ValueError('CSS @import requires a literal local URL')


def cpp_table(files):
    lines = ['// Generated immutable HTTP assets. SPDX-License-Identifier: MIT.',
             '#ifndef TELEMETRY_UX_WEB_ASSETS_HPP', '#define TELEMETRY_UX_WEB_ASSETS_HPP',
             '#include <cstddef>', '#include <string_view>', 'namespace telemetry_ux_assets {',
             'struct Asset { const char* path; const char* mime; const unsigned char* data; std::size_t size; };']
    for index, (_, payload) in enumerate(files.items()):
        lines.append('inline constexpr unsigned char data_' + str(index) + '[] = {')
        for at in range(0, len(payload), 24):
            lines.append(','.join('0x' + format(value, '02x') for value in payload[at:at + 24]) + ',')
        if not payload:
            lines.append('0,')  # Portable nonempty storage, logical length remains zero.
        lines.append('};')
    lines.append('inline constexpr Asset assets[] = {')
    for index, (path, payload) in enumerate(files.items()):
        lines.append('{' + json.dumps('/' + path) + ',' + json.dumps(mime(path)) +
                     ',data_' + str(index) + ',' + str(len(payload)) + '},')
    lines += ['};', 'inline constexpr std::size_t count = sizeof(assets) / sizeof(assets[0]);',
              'inline const Asset* find(std::string_view path) noexcept {',
              'for (const auto& asset : assets) if (path == asset.path) return &asset;',
              'return nullptr;', '}', '}', '#endif', '']
    return '\n'.join(lines).encode('ascii')


def mime(path):
    return MIME.get(PurePosixPath(path).suffix.lower(), 'text/plain; charset=utf-8')


def export_assets(public_root, output, assets, *, cpp=False, max_bytes=4 * 1024 * 1024,
                  max_files=256):
    """Preflight all bytes/references before creating a fresh output directory.

    Callers keep source and destination parent directories trusted and stable.
    A disk error can leave an incomplete fresh directory; no existing path is replaced.
    """
    source, target = Path(public_root).absolute(), Path(output).absolute()
    no_links(source)
    no_links(target)
    source, target = source.resolve(), target.resolve()
    if not source.is_dir() or target.exists():
        raise ValueError('Public root must exist and output must not exist; nothing is overwritten')
    if source == target or source in target.parents or target in source.parents:
        raise ValueError('Source and output directories must be disjoint')
    if max_bytes < 1 or not 1 <= max_files <= 4096:
        raise ValueError('Positive byte budget and file limit 1..4096 required')
    names = [asset_name(name) for name in assets]
    if 'index.html' not in names or len({name.casefold() for name in names}) != len(names):
        raise ValueError('Explicit assets must include index.html without duplicates/case aliases')
    if any(name.casefold().startswith(('vendor/', 'node_modules/')) or name.casefold() in
           ('assets.json', 'telemetry_web_assets.hpp') for name in names):
        raise ValueError('Application assets cannot occupy reserved deploy paths')
    if any(PurePosixPath(name).suffix.lower() not in MIME for name in names):
        raise ValueError('Application assets must have an explicitly supported web MIME type')
    planned = names + ['vendor/' + package + '/' + name
                       for package, paths in (('telemetry-sdk', SDK_FILES), ('telemetry-ux', UX_FILES))
                       for name in paths] + ['assets.json'] + (['telemetry_web_assets.hpp'] if cpp else [])
    lowered = {name.casefold() for name in planned}
    if len(lowered) != len(planned):
        raise ValueError('Planned deploy paths have a duplicate or case alias')
    if any(str(parent).casefold() in lowered for name in planned
           for parent in PurePosixPath(name).parents if str(parent) != '.'):
        raise ValueError('A file cannot also be an exported directory')
    if len(planned) - 1 - int(cpp) > max_files:
        raise ValueError('Total deployed file budget exceeded')
    files = {}
    total = 0

    def append_file(name, root, relative):
        nonlocal total
        payload = read_file(root, relative, max_bytes - total)
        total += len(payload)
        files[name] = payload

    for name in names:
        append_file(name, source, name)
    packages = {}
    for package, required in (('telemetry-sdk', SDK_FILES), ('telemetry-ux', UX_FILES)):
        root = source / 'node_modules' / package
        metadata_bytes = read_file(root, 'package.json', min(max_bytes, 65536))
        metadata = json.loads(metadata_bytes)
        if (metadata.get('name') != package or metadata.get('type') != 'module' or
                not isinstance(metadata.get('version'), str) or not metadata['version']):
            raise ValueError('Require the actual installed ESM ' + package + ' package')
        packages[package] = dict(version=metadata['version'], packageJsonSha256=digest(metadata_bytes))
        for name in required:
            append_file('vendor/' + package + '/' + name, root, name)

    html = files['index.html'].decode('utf8')
    maps = list(MAP.finditer(html))
    if len(maps) != 1:
        raise ValueError('index.html requires exactly one inline JSON import map')
    mapping = json.loads(maps[0][2])
    if set(mapping) != {'imports'} or not isinstance(mapping['imports'], dict):
        raise ValueError('Only an explicit imports map is supported')
    for key, value in list(mapping['imports'].items()):
        if key not in IMPORTS or value not in ('./node_modules/' + IMPORTS[key],
                                              '/node_modules/' + IMPORTS[key]):
            raise ValueError('Unsupported installed package import-map entry: ' + key)
        mapping['imports'][key] = './vendor/' + IMPORTS[key]
    html = html[:maps[0].start(2)] + '\n' + json.dumps(mapping, sort_keys=True, indent=2) + '\n' + html[maps[0].end(2):]
    html = html.replace('./node_modules/telemetry-ux/web/Configurator.css',
                        './vendor/telemetry-ux/web/Configurator.css')
    html = html.replace('/node_modules/telemetry-ux/web/Configurator.css',
                        './vendor/telemetry-ux/web/Configurator.css')
    files['index.html'] = html.encode('utf8')
    for name, data in files.items():
        suffix = PurePosixPath(name).suffix.lower()
        modules = []
        styles = []
        if suffix == '.html':
            refs = References()
            refs.feed(data.decode('utf8'))
            for value in refs.values:
                local_reference(value, name, files)
            modules += refs.inline_modules
            styles += refs.styles
        if suffix in ('.js', '.mjs'):
            modules.append(data.decode('utf8'))
        for module in modules:
            for value in MODULE.findall(module):
                if value.startswith(('.', '/')):
                    local_reference(value, name, files)
                elif value not in mapping['imports']:
                    raise ValueError('Bare module is absent from import map: ' + value)
        if suffix == '.css':
            styles.append(data.decode('utf8'))
        for style in styles:
            for value in css_references(style):
                local_reference(value, name, files)
    for value in mapping['imports'].values():
        local_reference(value, 'index.html', files)
    files = dict(sorted(files.items()))
    if sum(map(len, files.values())) > max_bytes:
        raise ValueError('Rewritten deployed asset byte budget exceeded')
    manifest = dict(format='telemetry-ux-web-assets-v1', entry='/index.html', packages=packages,
                    totalBytes=sum(map(len, files.values())),
                    assets=[dict(path='/' + name, mime=mime(name), bytes=len(payload),
                                 sha256=digest(payload)) for name, payload in files.items()])
    artifacts = dict(files)
    artifacts['assets.json'] = (json.dumps(manifest, sort_keys=True, indent=2) + '\n').encode('utf8')
    if cpp:
        artifacts['telemetry_web_assets.hpp'] = cpp_table(files)
    # No partial result is created for a failed validation. Filesystem failures
    # after this point are reported; an incomplete new output is never reused.
    no_links(target)
    target.mkdir(parents=True, exist_ok=False)
    for name, payload in artifacts.items():
        path = target.joinpath(*PurePosixPath(name).parts)
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open('xb') as stream:
            stream.write(payload)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--public-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--asset', action='append', required=True,
                        help='Explicit application file relative to public root (repeatable)')
    parser.add_argument('--cpp', action='store_true', help='Also emit immutable C++17 flash asset table')
    parser.add_argument('--max-bytes', type=int, default=4 * 1024 * 1024)
    parser.add_argument('--max-files', type=int, default=256)
    args = parser.parse_args()
    try:
        manifest = export_assets(args.public_root, args.output, args.asset, cpp=args.cpp,
                                 max_bytes=args.max_bytes, max_files=args.max_files)
    except (ValueError, OSError) as error:
        parser.exit(1, 'Export refused: ' + str(error) + '\n')
    print('Exported ' + str(len(manifest['assets'])) + ' assets, ' +
          str(manifest['totalBytes']) + ' bytes; no HTTP server or hardware was installed.')


if __name__ == '__main__':
    main()
