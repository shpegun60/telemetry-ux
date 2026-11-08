#!/usr/bin/env python3
"""Run the installed custom element in actual Chromium; no device is used. MIT."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import hashlib
import json
from pathlib import Path
import threading

from playwright.sync_api import sync_playwright, expect


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
    for name in ('index.js', 'Provider.js', 'Configurator.js', 'ProviderContract.js', 'Presentation.js', 'Element.js'):
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
                page = browser.new_page(viewport={'width': 1360, 'height': 1000})
                page.on('pageerror', lambda error: report['errors'].append(str(error)))
                page.on('console', lambda message: report['warnings'].append(message.text)
                        if message.type in ('warning', 'error') else None)
                page.on('request', lambda request: report['requests'].append(
                    dict(url=request.url, method=request.method)))
                page.goto('http://127.0.0.1:' + str(server.server_port), wait_until='networkidle')
                result = page.evaluate('async () => (await import("./Element.mjs")).runElementChecks()')
                if (result.get('passed') is not True or result.get('checks', 0) < 283 or
                        result.get('fixture') != 'presentation-only' or
                        result.get('actualSdkJourneys') is not False):
                    raise AssertionError('Element lifecycle checks incomplete: ' + repr(result))
                if report['errors'] or report['warnings']:
                    raise AssertionError('Unexpected browser diagnostics: ' + repr(report))
                if any(item['method'] != 'GET' for item in report['requests']):
                    raise AssertionError('Element lifecycle caused a non-asset request')
                report['element'] = result
                workspace = dict(checks=0, fixture='presentation-only', screenshots={})
                report['workspace'] = workspace

                def check(condition, message):
                    workspace['checks'] += 1
                    if not condition:
                        raise AssertionError(message)

                def geometry():
                    return page.evaluate('''() => {
                        const root = document.querySelector('[data-panel="configurator"]');
                        const nav = root.querySelector('.tux-navigation');
                        const work = root.querySelector('.tux-workspace');
                        const batch = root.querySelector('.tux-batch');
                        return {nav:nav.getBoundingClientRect().toJSON(),
                            work:work.getBoundingClientRect().toJSON(),
                            position:getComputedStyle(nav).position,
                            width:innerWidth,height:innerHeight,
                            batchClientWidth:batch.clientWidth,batchScrollWidth:batch.scrollWidth,
                            batchLabelsUnbroken:[...batch.querySelectorAll('th,td:nth-child(-n+2)')]
                                .every(cell=>getComputedStyle(cell).whiteSpace==='nowrap'),
                            scrollWidth:document.documentElement.scrollWidth};
                    }''')

                def screenshot(name):
                    path = output / (name + '.png')
                    page.screenshot(path=str(path))
                    workspace['screenshots'][name] = dict(path=str(path),
                        sha256=hashlib.sha256(path.read_bytes()).hexdigest())

                page.evaluate('''async () => {
                    const {mountWorkspaceFixture} = await import('./Element.mjs');
                    window.__workspace = mountWorkspaceFixture();
                    window.scrollTo(0, 0);
                }''')
                try:
                    desktop = geometry()
                    check(desktop['nav']['right'] <= desktop['work']['left'] and
                          abs(desktop['nav']['top']-desktop['work']['top']) <= 1,
                          'Desktop endpoint navigation starts beside Watchlist and Batch')
                    check(desktop['nav']['top'] < 200 and desktop['position'] == 'sticky',
                          'Endpoint selection is available at the beginning of the workspace')
                    check(page.locator('.tux-workspace > .tux-watchlist').count() == 1 and
                          page.locator('.tux-workspace > .tux-batch').count() == 1 and
                          page.locator('.tux-workspace > .tux-content').count() == 1,
                          'The full Configurator owns one shared right-hand workspace')
                    for index in (6, 13, 27, 38):
                        page.locator('nav [data-endpoint="1:'+str(index)+'"]').click()
                        page.locator('[data-action="addBatch"]').click()
                    expect(page.locator('.tux-batch-table tbody tr')).to_have_count(8)
                    counters = page.evaluate('window.__workspace.counters')
                    check(counters['adds'] == 4 and counters['io'] == 0,
                          'Selecting four endpoints and adding arguments starts no device operation')
                    screenshot('desktop-workspace')
                    focused = page.evaluate('''() => {
                        const search=document.querySelector('[data-role="endpoint-search"]');
                        search.focus(); window.__workspace.resizeBatch(32);
                        return document.activeElement===search && search.isConnected;
                    }''')
                    check(focused, 'Batch publication keeps the existing endpoint search and focus')
                    expect(page.locator('.tux-batch-table tbody tr')).to_have_count(32)
                    page.evaluate('''async () => {
                        window.scrollTo(0, 700);
                        await new Promise(requestAnimationFrame);
                    }''')
                    scrolled = geometry()
                    check(abs(scrolled['nav']['top']-12) <= 1 and
                          scrolled['nav']['height'] <= scrolled['height']-24+1,
                          'Desktop navigation remains inside the viewport while the Batch scrolls')
                    check(scrolled['nav']['right'] <= scrolled['work']['left'],
                          'Sticky endpoint navigation never overlaps the request workspace')
                    screenshot('desktop-scrolled')
                    page.set_viewport_size({'width': 390, 'height': 844})
                    page.evaluate('''async () => {
                        window.scrollTo(0, 0);
                        await new Promise(requestAnimationFrame);
                    }''')
                    mobile = geometry()
                    check(mobile['position'] == 'static' and
                          mobile['work']['top'] >= mobile['nav']['bottom'],
                          'Mobile navigation stacks above the workspace without sticky positioning')
                    check(mobile['nav']['height'] <= 17*16+1,
                          'The mobile endpoint selector retains its bounded scroll area')
                    check(mobile['scrollWidth'] <= mobile['width']+1,
                          'A full 32-item Batch causes no horizontal mobile viewport overflow')
                    check(mobile['batchScrollWidth'] > mobile['batchClientWidth'] and
                          mobile['batchLabelsUnbroken'],
                          'Mobile Batch columns retain readable widths inside their own horizontal scroller')
                    page.evaluate('document.querySelector(".tux-batch").scrollIntoView({block:"start"})')
                    screenshot('mobile-batch')
                    check(page.evaluate('window.__workspace.counters.io') == 0,
                          'Resizing, scrolling and presentation updates cause no device I/O')
                    workspace.update(passed=True, desktop=desktop, scrolled=scrolled, mobile=mobile)
                finally:
                    counters = page.evaluate('''() => {
                        window.__workspace.dispose(); return window.__workspace.counters;
                    }''')
                    check(counters['subscriptions'] == counters['unsubscriptions'] == 1 and
                          counters['lifecycle'] == 0,
                          'Workspace cleanup removes only its existing presentation subscription')
                if report['errors'] or report['warnings']:
                    raise AssertionError('Unexpected workspace diagnostics: ' + repr(report))
                if any(item['method'] != 'GET' for item in report['requests']):
                    raise AssertionError('Workspace layout caused a non-asset request')
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
