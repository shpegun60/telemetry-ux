#!/usr/bin/env python3
"""Run the installed custom element in actual Chromium; no device is used. MIT."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import threading

from playwright.sync_api import sync_playwright


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--public-root', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    args = parser.parse_args()
    root, output = args.public_root.resolve(), args.build_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    files = {'/': (root / 'index.html', 'text/html'),
             '/Element.mjs': (root / 'Element.mjs', 'text/javascript')}
    ux = root / 'node_modules/telemetry-ux/web'
    for name in ('index.js', 'Provider.js', 'Configurator.js', 'ProviderContract.js', 'Element.js'):
        files['/ux/' + name] = (ux / name, 'text/javascript')
    files['/ux/Configurator.css'] = (ux / 'Configurator.css', 'text/css')
    sdk = root / 'node_modules/telemetry-sdk'
    files['/sdk/Client.js'] = (sdk / 'lib/client/web/Client.js', 'text/javascript')
    files['/sdk/runtime.js'] = (sdk / 'web/telemetry.js', 'text/javascript')

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            if self.path == '/favicon.ico':
                self.send_response(204)
                self.end_headers()
                return
            item = files.get(self.path)
            if item is None:
                self.send_error(404)
                return
            path, mime = item
            data = path.read_bytes()
            self.send_response(200)
            self.send_header('Content-Type', mime + '; charset=utf-8')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)

    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    report = dict(passed=False, hardware=False, errors=[], warnings=[], requests=[])
    try:
        with sync_playwright() as playwright:
            browser = playwright.chromium.launch(headless=True)
            try:
                report['browser'] = dict(name='Chromium', version=browser.version)
                page = browser.new_page()
                page.on('pageerror', lambda error: report['errors'].append(str(error)))
                page.on('console', lambda message: report['warnings'].append(message.text)
                        if message.type in ('warning', 'error') else None)
                page.on('request', lambda request: report['requests'].append(
                    dict(url=request.url, method=request.method)))
                page.goto('http://127.0.0.1:' + str(server.server_port), wait_until='networkidle')
                result = page.evaluate('async () => (await import("./Element.mjs")).runElementChecks()')
                if (result.get('passed') is not True or result.get('checks', 0) < 136 or
                        result.get('fixture') != 'presentation-only' or
                        result.get('actualSdkJourneys') is not False):
                    raise AssertionError('Element lifecycle checks incomplete: ' + repr(result))
                if report['errors'] or report['warnings']:
                    raise AssertionError('Unexpected browser diagnostics: ' + repr(report))
                if any(item['method'] != 'GET' for item in report['requests']):
                    raise AssertionError('Element lifecycle caused a non-asset request')
                report['element'] = result
                report['passed'] = True
            finally:
                browser.close()
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)
        (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')


if __name__ == '__main__':
    main()
