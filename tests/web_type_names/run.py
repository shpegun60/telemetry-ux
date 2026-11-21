#!/usr/bin/env python3
"""The web Configurator over descriptors v3.1 in headless Chromium. MIT.

Serves the SDK's web modules (its source checkout), this repository's web
modules and the golden descriptors copied in tests/qml_layout/fixtures from
one local HTTP server, opens page.html in Chromium through Playwright, and
checks what Page.mjs found: a descriptor with advisory attributes gives the
same endpoints, headings and value rows as the same model without them; a
group tree's groups read as their paths; named types show by their names.
"""
import argparse
import functools
import http.server
import json
from pathlib import Path
import subprocess
import sys
import threading

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent

BROWSER = r'''
import json, sys
from playwright.sync_api import sync_playwright
url, shot = sys.argv[1], sys.argv[2]
with sync_playwright() as playwright:
    browser = playwright.chromium.launch()
    page = browser.new_page(viewport={'width': 1100, 'height': 900})
    errors = []
    page.on('pageerror', lambda error: errors.append(str(error)))
    page.on('console', lambda message: errors.append(message.text) if message.type == 'error' else None)
    page.goto(url)
    try:
        page.wait_for_function('window.__result !== undefined', timeout=20000)
        result = page.evaluate('window.__result')
    except Exception as failure:
        result = {'ok': False, 'error': 'no result: ' + str(failure).splitlines()[0]}
    page.screenshot(path=shot, full_page=True)
    result['browser'] = browser.version
    result['errors'] = errors
    browser.close()
print(json.dumps(result))
'''


class Handler(http.server.SimpleHTTPRequestHandler):
    roots = {}

    def translate_path(self, path):
        clean = path.split('?', 1)[0].split('#', 1)[0]
        for prefix, root in self.roots.items():
            if clean.startswith(prefix):
                target = (root / clean[len(prefix):]).resolve()
                if root.resolve() in target.parents or target == root.resolve():
                    return str(target)
        return str(HERE / '__nothing__')

    def guess_type(self, path):
        if str(path).endswith(('.js', '.mjs')):
            return 'text/javascript'
        return super().guess_type(path)

    def log_message(self, *args):
        pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--browser-python', required=True, help='a Python with Playwright and its Chromium')
    parser.add_argument('--sdk-source', type=Path, default=ROOT.parent / 'telemetry-sdk')
    args = parser.parse_args()
    output = args.build_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if not (args.sdk_source / 'web/telemetry.js').is_file():
        print('Web type names: no SDK web modules under ' + str(args.sdk_source) + '; pass --sdk-source', file=sys.stderr)
        return 2
    Handler.roots = {'/sdk/': args.sdk_source.resolve(), '/ux/': ROOT, '/fixtures/': ROOT / 'tests/qml_layout/fixtures'}
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    checks = 0
    report = dict(passed=False)

    def check(condition, message):
        nonlocal checks
        checks += 1
        if not condition:
            raise AssertionError(message)

    try:
        url = 'http://127.0.0.1:%d/ux/tests/web_type_names/page.html' % server.server_port
        shot = output / 'web-type-names.png'
        done = subprocess.run([args.browser_python, '-I', '-c', BROWSER, url, str(shot)], text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=180)
        if done.returncode:
            raise RuntimeError('browser failed:\n' + done.stderr[-4000:])
        result = json.loads(done.stdout.strip().splitlines()[-1])
        report['result'] = result
        check(result.get('ok'), 'page failed: ' + str(result.get('error')))
        check(not result['errors'], 'page errors: ' + '; '.join(result['errors']))
        named, attrs, tree = result['looks']
        check(named['minor'] == 1 and attrs['minor'] == 1, 'v3.1 descriptors')
        check(named['fingerprint'] != attrs['fingerprint'], 'the attributes are fingerprinted')
        # Attributes are skipped: the same tree, the same rows.
        check(len(named['endpoints']) > 3 and named['endpoints'] == attrs['endpoints'], 'same endpoints')
        check(named['headings'] and named['headings'] == attrs['headings'], 'same headings')
        check(named['struct'] and named['struct'] == attrs['struct'], 'the same struct Field')
        check(named['rows'] and named['rows'] == attrs['rows'], 'same value rows')
        check(named['shown'] == attrs['shown'], 'the same rows on the page')
        # Named types by their names, in the provider's rows and on the page.
        check(named['enumRows'] and named['enumRows'] == attrs['enumRows'], 'the same enum row')
        check(any('| fixture::Mode | fixture::Mode' in row for row in named['enumRows']), 'an enum Field by its name')
        check(any('| metrology::Window | metrology::Window' in row for row in tree['rows']), 'a member by its name')
        check(any('metrology::Window' in text for text in tree['shown']), 'the page shows a named type')
        # A group tree: its groups as paths, nested ones included.
        check(tree['groupTree'], 'tree.hex is a group tree')
        check('Fields / adc/channel1' in tree['headings'] and 'Fields / adc' in tree['headings'], 'group paths')
        check(any(row.startswith('1 / adc/channel2 / Gain') for row in tree['endpoints']), 'nested endpoint')
        check(any(row.startswith('3 / adc / Calibrate') for row in tree['endpoints']), 'a Service of a tree')
        report.update(passed=True, checks=checks, browser=result['browser'])
    except Exception as error:
        report['error'] = str(error)
    finally:
        server.shutdown()
        (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    if not report['passed']:
        print('Web type names failed: ' + report.get('error', ''), file=sys.stderr)
        return 1
    print('Web type names checks=%d browser=%s' % (checks, report['browser']))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
