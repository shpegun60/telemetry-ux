#!/usr/bin/env python3
"""Exercise the shipped own-device starter over real HTTP/WebSocket and native dispatch. MIT."""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import threading
import time
from playwright.sync_api import sync_playwright, expect

ROOT = Path(__file__).resolve().parents[2]
STARTER = ROOT / 'examples/web_device_starter'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepared', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    prepared, output = args.prepared.resolve(), args.output.resolve()
    if output.exists() or output == ROOT or ROOT in output.parents:
        parser.error('--output must be a fresh directory outside the checkout')
    receipt = json.loads((prepared / 'prepared.json').read_text(encoding='utf8'))
    if not receipt.get('passed'):
        parser.error('--prepared must contain a successful prepare.py result')
    if not receipt.get('source_sha256') or any(sha(ROOT / name) != value for name, value in receipt['source_sha256'].items()):
        parser.error('Prepared inputs do not match current starter sources; prepare a fresh application')
    for name in ('DeviceApp.js', 'index.html'):
        if sha(prepared / 'public' / name) != sha(STARTER / name):
            parser.error('Prepared browser application changed: ' + name)
    output.mkdir(parents=True)
    sys.path.insert(0, str(ROOT / 'examples/web_demo'))
    spec = importlib.util.spec_from_file_location('starter_ws', ROOT / 'examples/web_demo/serve_ws.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    device = prepared / 'native' / ('telemetry_device_starter.exe' if sys.platform == 'win32' else 'telemetry_device_starter')
    if sha(device) != receipt['device_sha256']:
        parser.error('Prepared native executable changed')
    server = module.create_server(device, prepared / 'public', output / 'state')
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    paths = sorted(p for p in STARTER.rglob('*') if p.is_file() and '__pycache__' not in p.parts)
    sources = lambda: {str(p.relative_to(ROOT)): sha(p) for p in paths}
    report = dict(passed=False, hardware=False, checks=0, modes={}, errors=[], warnings=[],
                  source_sha256_before=sources(), started_utc=datetime.now(timezone.utc).isoformat())

    def check(condition, label):
        report['checks'] += 1
        if not condition:
            raise AssertionError(label)

    def native():
        return json.loads(server.peer.line('state', diagnostic=True))

    def wait_native(predicate):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            try:
                value = native()
            except RuntimeError:
                time.sleep(0.01)
                continue
            if predicate(value):
                return value
            time.sleep(0.01)
        raise AssertionError('Native state did not reach the expected boundary')

    try:
        with sync_playwright() as playwright:
            browser = playwright.chromium.launch()
            report['browser_version'] = browser.version
            try:
                modes = ('dynamic', 'static') if receipt.get('static_generated') else ('dynamic',)
                for mode in modes:
                    start = report['checks']
                    page = browser.new_page(viewport={'width':1280, 'height':1100})
                    page.on('pageerror', lambda error: report['errors'].append(str(error)))
                    page.on('console', lambda msg: report['warnings'].append(msg.text) if msg.type in ('error','warning') else None)
                    page.goto('http://127.0.0.1:' + str(server.server_port))
                    check(page.locator('#mode option[value="static"]').is_disabled() == (not receipt.get('static_generated')), 'Static option matches actual generated files')
                    page.locator('#mode').select_option(mode)
                    before = native()
                    page.locator('#connect').click()
                    expect(page.locator('#status')).to_have_text('ready')
                    view = page.locator('#device')
                    action = lambda name: view.locator('[data-action="' + name + '"]')
                    select = lambda name: view.locator('button[data-endpoint]').filter(has_text=name).click()
                    idle = lambda: expect(view.locator('[data-role="configurator"]')).to_have_attribute('aria-busy','false')
                    check(view.locator('button[data-endpoint]').count() == 4, mode + ': discovers the application Model')
                    check(native()['reads'] == before['reads'], 'Connecting and mounting do not call Field getters')
                    select('SerialNumber')
                    action('refresh').click(); idle()
                    expect(view.locator('.tux-row[data-source="value"] .tux-value')).to_have_text('9007199254740993')
                    check(True, 'Exact U64 is rendered without a JavaScript Number conversion')
                    select('Config'); action('refresh').click(); idle()
                    action('writes').check(); action('beginEdit').click()
                    period = view.locator('input[data-source="draft"][data-path=\'["periodMs"]\']')
                    period.fill('1')
                    previous = native(); action('apply').click(); idle()
                    now = native()
                    check(now['writes'] == previous['writes'] + 1, 'Apply sends exactly one FieldWrite')
                    check(now['reads'] == previous['reads'] + 1, 'Apply performs separate explicit readback')
                    check(now['periodMs'] == 10, 'Device normalization is observable')
                    expect(view.locator('.tux-row[data-source="value"][data-path=\'["periodMs"]\'] .tux-value')).to_have_text('10')
                    check(True, 'GUI shows normalized readback rather than the local draft')
                    select('Scale')
                    check(action('call').is_enabled(), 'Selecting a Service prepares its runtime request automatically')
                    view.locator('input[data-source="draft"][data-path=\'["input"]\']').fill('2.5')
                    previous = native(); action('call').click(); idle()
                    check(native()['services'] == previous['services'] + 1, 'Service uses the same Session')
                    expect(view.locator('.tux-row[data-source="response"][data-path=\'["output"]\'] .tux-value')).to_have_text('2.5')
                    check(True, 'Runtime service response is rendered without generated UI')
                    select('Reset')
                    previous = native(); action('execute').click(); idle()
                    check(native()['commands'] == previous['commands'] + 1, 'Explicit Execute sends one Command')
                    expect(view.locator('[data-role="outcome"][data-operation="command"]')).to_contain_text('Executed')
                    # Actual native execution occurs, then the reference server discards
                    # its acknowledgement. The app must never replay that mutation.
                    server.drop_reply_once = True
                    previous = native(); action('execute').click()
                    wait_native(lambda value: value['commands'] == previous['commands'] + 1)
                    expect(page.locator('#reconnect')).to_be_enabled()
                    expect(view.locator('[data-role="outcome"][data-operation="command"]')).to_contain_text('OutcomeUnknown')
                    check(True, 'Dropped Command acknowledgement retains uncertainty')
                    retired = native()
                    page.locator('#reconnect').click()
                    expect(page.locator('#status')).to_have_text('ready')
                    current = native()
                    check(current['session'] > retired['session'], 'Explicit reconnect obtains a fresh monotonic lease')
                    check(all(current[key] == retired[key] for key in ('reads','writes','commands','services')), 'Reconnect never replays endpoint work')
                    select('Config'); action('refresh').click(); idle()
                    page.locator('#disconnect').click()
                    expect(page.locator('#status')).to_have_text('disconnected')
                    check(view.locator('.tux-row[data-source="value"] .tux-value').count() == 0, 'Disconnect invalidates visible data')
                    retired = native(); page.locator('#reconnect').click()
                    expect(page.locator('#status')).to_have_text('ready')
                    check(native()['reads'] == retired['reads'], 'Reconnect does not silently refresh')
                    page.locator('#close').click()
                    expect(page.locator('#status')).to_have_text('closed')
                    check(page.locator('#reconnect').is_disabled(), 'Close is terminal for this Session')
                    check(page.locator('#connect').is_enabled(), 'A new explicit Connect is available')
                    check(page.locator('#error').inner_text() == '', 'No connection-owner error remains')
                    page.screenshot(path=str(output / (mode + '.png')), full_page=True)
                    report['modes'][mode] = dict(checks=report['checks']-start, final_state=current)
                    page.close()
            finally:
                browser.close()
        check(not report['errors'] and not report['warnings'], 'No browser errors or warnings')
        report['source_sha256_after'] = sources()
        check(report['source_sha256_before'] == report['source_sha256_after'], 'Starter sources did not change during qualification')
        check(sha(device) == receipt['device_sha256'], 'Native executable did not change during qualification')
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        server.shutdown(); thread.join(timeout=10)
        server.server_close(); server.peer.close()
        report['finished_utc'] = datetime.now(timezone.utc).isoformat()
        (output / 'summary.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf8')


if __name__ == '__main__':
    main()
