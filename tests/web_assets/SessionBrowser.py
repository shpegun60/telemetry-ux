#!/usr/bin/env python3
"""Load the unchanged Session demo solely from relocated vendor assets. MIT."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import threading
import time
from urllib.parse import urlsplit
from playwright.sync_api import sync_playwright, expect

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', required=True, type=Path)
    parser.add_argument('--public-root', required=True, type=Path)
    parser.add_argument('--data-dir', required=True, type=Path)
    args = parser.parse_args()
    output = args.data_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(ROOT / 'examples/web_demo'))
    spec = importlib.util.spec_from_file_location('session_asset_demo', ROOT / 'examples/web_demo/serve_ws.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    server = module.create_server(args.device, args.public_root, output / 'peer')
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    report = dict(passed=False, hardware=False, checks=0, static=False, dynamic=False,
                  errors=[], warnings=[], modes={})

    def check(condition, message):
        report['checks'] += 1
        if not condition:
            raise AssertionError(message)

    def state():
        deadline = time.monotonic() + 5
        while True:
            try:
                return json.loads(server.peer.line('state', diagnostic=True))
            except module.http_demo.PeerBusy:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(.01)

    def lease_idle():
        deadline = time.monotonic() + 5
        while server._connection is not None and time.monotonic() < deadline:
            time.sleep(.01)
        check(server._connection is None, 'Previous exclusive native lease finished')

    try:
        with sync_playwright() as playwright:
            browser = playwright.chromium.launch()
            report['browser'] = dict(name='Chromium', version=browser.version)
            try:
                for mode in ('static', 'dynamic'):
                    lease_idle()
                    server.peer.line('reset')
                    start = report['checks']
                    page = browser.new_page(viewport={'width':1440, 'height':1000})
                    page.on('pageerror', lambda error: report['errors'].append(str(error)))
                    page.on('console', lambda msg: report['warnings'].append(msg.text)
                            if msg.type in ('warning', 'error') else None)
                    requests = []
                    page.on('request', lambda request: requests.append(urlsplit(request.url).path))
                    page.goto('http://127.0.0.1:' + str(server.server_port))
                    page.locator('#mode').select_option(mode)
                    page.locator('#connect').click()
                    expect(page.locator('#connection-status')).to_have_text('ready')
                    left, right = page.locator('#left'), page.locator('#right')
                    action = lambda view, name: view.locator('[data-action="' + name + '"]')
                    def idle(view):
                        expect(view.locator('[data-role="configurator"]')).to_have_attribute('aria-busy', 'false')
                    def row(view, source, name):
                        return view.locator('.tux-row[data-source="' + source + '"][data-path=\'["' + name + '"]\'] .tux-value')
                    def edit(view, name, text):
                        field = view.locator('input[data-source="draft"][data-path=\'["' + name + '"]\']')
                        expect(field).to_be_visible()
                        field.fill(text)
                        return field
                    for view in (left, right):
                        view.locator('button[data-endpoint]').filter(has_text='Config').first.click()
                    check(state()['reads'] == 0, 'Mount and selection submit no read')
                    for view in (left, right):
                        action(view, 'refresh').click()
                        idle(view)
                        action(view, 'writes').check()
                        action(view, 'beginEdit').click()
                    expect(row(left, 'value', 'large')).to_have_text('18446744073709551615')
                    expect(row(left, 'value', 'negative')).to_have_text('-9223372036854775808')
                    expect(row(left, 'value', 'zero')).to_have_text('-0')
                    check(state()['reads'] == 2, 'Two independent explicit native reads')
                    edit(right, 'gain', '6.')
                    overflow = edit(left, 'large', '18446744073709551616')
                    expect(overflow).to_have_attribute('aria-invalid', 'true')
                    check(action(left, 'apply').is_disabled(), 'U64 overflow cannot submit')
                    edit(left, 'large', '9007199254740993')
                    edit(left, 'gain', '25')
                    before = state()
                    action(left, 'apply').click()
                    idle(left)
                    after = state()
                    check(after['writes'] == before['writes'] + 1 and after['reads'] == before['reads'] + 1,
                          'One native write plus explicit readback')
                    check(after['large'] == '9007199254740993' and after['negative'] == '-9223372036854775808',
                          'Exact U64/S64 through deployed Session edit')
                    expect(row(left, 'value', 'gain')).to_have_text('10')
                    stale = right.locator('input[data-source="draft"][data-path=\'["gain"]\']')
                    expect(stale).to_have_value('6.')
                    expect(row(right, 'draft', 'gain')).to_have_text('6.')
                    check(stale.is_hidden() and action(right, 'apply').is_disabled(), 'Sibling draft text stays visible without an editable input')
                    expect(right.locator('[data-role="error"]')).to_contain_text('another view')
                    check(not right.locator('.tux-row[data-source="value"]').count(), 'Sibling cached value is hidden')
                    check(left.locator('button[data-endpoint][aria-current="true"]').inner_text() == 'Config',
                          'Sibling invalidation preserves selection')
                    page.locator('#disconnect').click()
                    expect(page.locator('#connection-status')).to_have_text('disconnected')
                    expect(left.locator('[data-role="outcome"][data-operation="write"]')).to_have_attribute('data-historical', 'true')
                    check(not left.locator('.tux-row[data-source="value"]').count(), 'Disconnect hides current values')
                    before = state()
                    page.locator('#reconnect').click()
                    expect(page.locator('#connection-status')).to_have_text('ready')
                    check(state()['reads'] == before['reads'] and state()['writes'] == before['writes'], 'Reconnect does not replay endpoint work')
                    check(not action(left, 'writes').is_checked(), 'Fresh admission requires permission again')
                    screenshot = output / (mode + '.png')
                    page.screenshot(path=str(screenshot), full_page=True)
                    page.locator('#close').click()
                    expect(page.locator('#connection-status')).to_have_text('closed')
                    check(action(left, 'refresh').is_disabled(), 'Terminal close disables endpoint actions')
                    check(not any('/node_modules/' in path for path in requests), 'Deployed page requests no package-manager files')
                    for path in ('/vendor/telemetry-sdk/web/index.js', '/vendor/telemetry-sdk/web/Session.js',
                                 '/vendor/telemetry-sdk/web/WebSocketConnection.js', '/vendor/telemetry-ux/web/Provider.js'):
                        check(path in requests, 'Deployed required module loaded: ' + path)
                    report['modes'][mode] = dict(checks=report['checks'] - start, state=state(), requests=requests,
                        screenshot_sha256=hashlib.sha256(screenshot.read_bytes()).hexdigest())
                    report[mode] = True
                    page.close()
                check(not report['errors'], 'No uncaught deployed browser exceptions')
                check(not report['warnings'], 'No deployed browser warnings/errors')
            except Exception:
                if 'page' in locals() and not page.is_closed():
                    page.screenshot(path=str(output / 'failure.png'), full_page=True)
                    (output / 'failure.html').write_text(page.content(), encoding='utf8')
                raise
            finally:
                browser.close()
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=10)
        report['native_exit'] = server.peer.close()
        report['native_stderr'] = (output / 'peer/native-stderr.log').read_text(encoding='utf8')
        if report['native_exit'] != 0 or report['native_stderr'] or thread.is_alive():
            report['passed'] = False
            report['cleanup_error'] = 'Native exit/stderr or listener cleanup failed'
        (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
        if report.get('cleanup_error'):
            raise RuntimeError(report['cleanup_error'])


if __name__ == '__main__':
    main()
