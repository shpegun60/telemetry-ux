"""Deploy preflight, deterministic bytes, and immutable flash-table contracts. MIT."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('export_web_assets',
    Path(__file__).resolve().parents[2] / 'tools/export_web_assets.py')
tool = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(tool)


class ExportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.public = self.root / 'public'
        self.public.mkdir()
        self.output = self.root / 'export'
        for name, paths in (('telemetry-sdk', tool.SDK_FILES), ('telemetry-ux', tool.UX_FILES)):
            package = self.public / 'node_modules' / name
            package.mkdir(parents=True)
            (package / 'package.json').write_text(json.dumps(
                dict(name=name, version='0.1.0', type='module')), encoding='utf8')
            for path in paths:
                file = package / path
                file.parent.mkdir(parents=True, exist_ok=True)
                file.write_text('/* installed runtime */\n' if path.endswith('.js') else '', encoding='utf8')
        self.html = ('<!doctype html><html><head><link rel="stylesheet" href="./node_modules/'
            'telemetry-ux/web/Configurator.css"><script type="importmap">' + json.dumps({'imports':
                {key: './node_modules/' + value for key, value in tool.IMPORTS.items()}}) +
            '</script></head><body><script type="module" src="./app.js"></script></body></html>')
        self.write('index.html', self.html)
        self.write('app.js', "import {DeviceClient} from 'telemetry-sdk/client';\n")

    def tearDown(self):
        self.temp.cleanup()

    def write(self, name, text):
        path = self.public / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf8')

    def export(self, **kwargs):
        return tool.export_assets(self.public, self.output,
                                  kwargs.pop('assets', ['index.html', 'app.js']), **kwargs)

    def refused(self, **kwargs):
        with self.assertRaises(ValueError):
            self.export(**kwargs)
        self.assertFalse(self.output.exists(), 'Validation failure created output')

    def test_runtime_closure_and_determinism(self):
        first = self.export(cpp=True)
        second = self.root / 'elsewhere'
        self.assertEqual(first, tool.export_assets(self.public, second, ['app.js', 'index.html'], cpp=True))
        left = {str(p.relative_to(self.output)): p.read_bytes() for p in self.output.rglob('*') if p.is_file()}
        right = {str(p.relative_to(second)): p.read_bytes() for p in second.rglob('*') if p.is_file()}
        self.assertEqual(left, right)
        self.assertNotIn('node_modules', (self.output / 'index.html').read_text())
        self.assertTrue((self.output / 'vendor/telemetry-sdk/lib/client/web/Client.js').is_file())
        self.assertEqual(first['totalBytes'], sum(asset['bytes'] for asset in first['assets']))
        self.assertTrue(all(asset['path'].startswith('/') for asset in first['assets']))
        self.assertNotIn('new DeviceClient', (self.output / 'telemetry_web_assets.hpp').read_text())

    def test_existing_destination_is_never_reused(self):
        self.output.mkdir()
        marker = self.output / 'mine'
        marker.write_bytes(b'keep')
        with self.assertRaises(ValueError):
            self.export()
        self.assertEqual(marker.read_bytes(), b'keep')

    def test_traversal_alias_and_reserved_paths(self):
        for path in ('../outside.js', '/absolute.js', 'x\\escape.js', 'a//b.js', 'a/./b.js',
                     'CON.js', 'x./a.js', 'vendor/a.js', 'node_modules/a.js',
                     'Vendor/telemetry-sdk/web/telemetry.js', 'NODE_MODULES/a.js',
                     'assets.json', 'Assets.json', 'telemetry_web_assets.hpp',
                     'Telemetry_Web_Assets.hpp', 'a.py'):
            with self.subTest(path=path):
                self.refused(assets=['index.html', 'app.js', path])
        self.refused(assets=['index.html', 'app.js', 'APP.JS'])
        self.refused(assets=['app.js'])

    def test_source_output_overlap(self):
        with self.assertRaises(ValueError):
            tool.export_assets(self.public, self.public / 'export', ['index.html', 'app.js'])
        self.assertFalse((self.public / 'export').exists())

    def test_budgets_before_output(self):
        self.refused(max_bytes=1)
        self.refused(max_files=1)
        self.refused(max_bytes=0)
        self.refused(max_files=0)

    def test_aggregate_budget_is_checked_during_reads(self):
        self.write('large.bin', 'x' * 1024)
        # The first file fills the budget; the next file is refused by stat
        # rather than accumulating N independently max-sized payloads.
        self.refused(assets=['large.bin', 'index.html', 'app.js'], max_bytes=1024)

    def test_missing_runtime_and_package_identity(self):
        file = self.public / 'node_modules/telemetry-sdk/web/DynamicClient.js'
        file.unlink()
        self.refused()
        file.write_text('', encoding='utf8')
        metadata = self.public / 'node_modules/telemetry-sdk/package.json'
        metadata.write_text('{"name":"wrong","type":"module","version":"0.1.0"}', encoding='utf8')
        self.refused()

    def test_module_reference_preflight(self):
        for value in ('./absent.js', '../escape.js', 'https://cdn.invalid/sdk.js',
                      'other-package', './%61pp.js'):
            with self.subTest(value=value):
                self.write('app.js', "import {x} from '" + value + "';\n")
                self.refused()
        self.write('app.js', "export {x} from './child.js';\n")
        self.write('child.js', 'export const x=1;\n')
        self.export(assets=['index.html', 'app.js', 'child.js'])

    def test_html_and_css_reference_preflight(self):
        self.write('index.html', self.html.replace('./app.js', 'https://cdn.invalid/app.js'))
        self.refused()
        self.write('index.html', self.html)
        self.write('index.html', self.html.replace('</body>',
            '<script type="module">import "https://cdn.invalid/module.js";</script></body>'))
        self.refused()
        self.write('index.html', self.html.replace('<head>', '<head><base href="/index.html">'))
        self.refused()
        self.write('index.html', self.html)
        self.write('node_modules/telemetry-ux/web/Configurator.css', 'body{background:url(missing.png)}')
        self.refused()

    def test_import_map_preflight(self):
        for mapping in ('{}', '{"scopes":{},"imports":{}}', '{"imports":{"telemetry-sdk/client":"https://bad"}}'):
            self.write('index.html', tool.MAP.sub(lambda match: match[1] + mapping + match[3], self.html))
            self.refused()
        self.write('index.html', self.html.replace('type="importmap"', 'type="other"'))
        self.refused()

    def test_empty_assets_have_logical_zero_length(self):
        self.write('empty.bin', '')
        manifest = self.export(cpp=True, assets=['index.html', 'app.js', 'empty.bin'])
        self.assertEqual(next(a['bytes'] for a in manifest['assets'] if a['path'] == '/empty.bin'), 0)
        self.assertIn('"/empty.bin","application/octet-stream",data_',
                      (self.output / 'telemetry_web_assets.hpp').read_text())

    def test_symlinks_if_platform_permits(self):
        link = self.public / 'linked.js'
        try:
            link.symlink_to(self.public / 'app.js')
        except OSError:
            self.skipTest('Creating test symlinks needs Windows Developer Mode or privilege')
        self.refused(assets=['index.html', 'app.js', 'linked.js'])


if __name__ == '__main__':
    unittest.main()
