#!/usr/bin/env python3
"""Exercise the shipped own-device starter over real HTTP/WebSocket and native dispatch. MIT."""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
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
    parser.add_argument('--exported', action='store_true',
                        help='Relocate and serve only prepared www assets, without node_modules')
    parser.add_argument('--extra-json', help='Explicit application JSON asset that the created-project journey must fetch')
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
    inventory = lambda directory: {path.relative_to(directory).as_posix(): sha(path)
                                   for path in sorted(directory.rglob('*')) if path.is_file()}
    public = prepared / 'public'
    exported = None
    if args.exported:
        exported = receipt.get('web_export')
        if not exported or exported.get('directory') != 'www':
            parser.error('--exported requires a successful prepare.py --export-web result')
        original = prepared / 'www'
        manifest = json.loads((original / 'assets.json').read_text(encoding='utf8'))
        if sha(original / 'assets.json') != exported['manifest_sha256']:
            parser.error('Prepared deploy manifest changed')
        for asset in manifest['assets']:
            path = original / asset['path'].lstrip('/')
            if not path.is_file() or path.stat().st_size != asset['bytes'] or sha(path) != asset['sha256']:
                parser.error('Prepared deploy asset changed: ' + asset['path'])
        public = output / 'relocated-www'
        shutil.copytree(original, public)
    sys.path.insert(0, str(ROOT / 'examples/web_demo'))
    spec = importlib.util.spec_from_file_location('starter_ws', ROOT / 'examples/web_demo/serve_ws.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    device = prepared / 'native' / ('telemetry_device_starter.exe' if sys.platform == 'win32' else 'telemetry_device_starter')
    if sha(device) != receipt['device_sha256']:
        parser.error('Prepared native executable changed')
    server = module.create_server(device, public, output / 'state')
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    paths = sorted(p for p in STARTER.rglob('*') if p.is_file() and '__pycache__' not in p.parts)
    sources = lambda: {str(p.relative_to(ROOT)): sha(p) for p in paths}
    report = dict(passed=False, hardware=False, checks=0, modes={}, errors=[], warnings=[],
                  asset_errors=[], exported=bool(args.exported),
                  assets_sha256_before=inventory(public) if args.exported else None,
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
        report['negative_controls'] = []

        def refused(label, command, expected_message):
            result = subprocess.run(list(map(str, command)), encoding='utf8', errors='replace',
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
            log = output / (label + '.log')
            log.write_text(result.stdout, encoding='utf8')
            report['negative_controls'].append(dict(name=label, exit=result.returncode,
                                                    log=log.name, sha256=sha(log)))
            check(result.returncode != 0 and expected_message in result.stdout and 'Traceback' not in result.stdout,
                  label + ' fails before startup with an actionable diagnostic')

        if args.exported:
            for name in ('changed', 'missing'):
                invalid = output / ('refused-' + name)
                invalid.mkdir()
                shutil.copy2(prepared / 'prepared.json', invalid / 'prepared.json')
                shutil.copytree(prepared / 'www', invalid / 'www')
                asset = invalid / 'www' / 'DeviceApp.js'
                if name == 'changed':
                    asset.write_bytes(asset.read_bytes() + b'\n')
                else:
                    asset.unlink()
                refused('serve-' + name, [sys.executable, '-B', STARTER / 'serve.py',
                    '--prepared', invalid, '--exported', '--port', '0'], '--exported asset check failed:')
                check(not (invalid / 'state').exists(), 'Rejected ' + name + ' asset starts no native peer or server')
            for name, options in (
                    ('budget-without-export', ['--web-max-bytes', '1']),
                    ('zero-byte-budget', ['--export-web', 'cpp', '--web-max-bytes', '0']),
                    ('zero-file-budget', ['--export-web', 'files', '--web-max-files', '0']),
                    ('excess-file-budget', ['--export-web', 'files', '--web-max-files', '4097'])):
                destination = output / ('refused-' + name)
                refused(name, [sys.executable, '-B', STARTER / 'prepare.py', '--output', destination,
                    '--sdk-prefix', output / 'unused-sdk', '--sdk-package', receipt['packages']['sdk']['path'],
                    '--ux-package', receipt['packages']['ux']['path'], *options], '--web-')
                check(not destination.exists(), 'Invalid export option creates no preparation output')
        with sync_playwright() as playwright:
            browser = playwright.chromium.launch()
            report['browser_version'] = browser.version
            try:
                modes = ('dynamic', 'static') if receipt.get('static_generated') else ('dynamic',)
                for mode in modes:
                    start = report['checks']
                    page = browser.new_page(viewport={'width':1280, 'height':1100})
                    requests, sockets = [], []
                    page.on('request', lambda request: requests.append(request.url))
                    page.on('websocket', lambda socket: sockets.append(socket.url))
                    page.on('pageerror', lambda error: report['errors'].append(str(error)))
                    page.on('console', lambda msg: report['warnings'].append(msg.text) if msg.type in ('error','warning') else None)
                    page.on('response', lambda response: report['asset_errors'].append(
                        dict(url=response.url, status=response.status)) if response.status >= 400 and
                        response.request.resource_type in ('document', 'script', 'stylesheet', 'image', 'font') else None)
                    page.goto('http://127.0.0.1:' + str(server.server_port))
                    check(page.locator('#mode option[value="static"]').is_disabled() == (not receipt.get('static_generated')), 'Static option matches actual generated files')
                    if not receipt.get('static_generated'):
                        page.wait_for_load_state('networkidle')
                        check(not (public / 'generated').exists(), 'Default Dynamic application contains no generated DTO module tree')
                        check('import(' not in (public / 'static-context.js').read_text(encoding='utf8'),
                              'Dynamic-only context contains no conditional generated-module imports')
                        before_static = native()
                        request_count, socket_count = len(requests), len(sockets)
                        page.evaluate('document.getElementById("mode").value="static"')
                        page.locator('#connect').click()
                        expect(page.locator('#error')).to_contain_text('Static mode needs generated files')
                        expect(page.locator('#connect')).to_be_enabled()
                        check(native() == before_static, 'Forced unsupported Static never acquires a Session or invokes an endpoint')
                        check(len(requests) == request_count and len(sockets) == socket_count,
                              'Forced unsupported Static performs no HTTP/bootstrap/WebSocket or Dynamic fallback')
                    page.locator('#mode').select_option(mode)
                    if args.extra_json:
                        extra = page.evaluate('''async name => {
                            const response = await fetch('./' + name);
                            return {ok:response.ok, mime:response.headers.get('Content-Type'), value:await response.json()};
                        }''', args.extra_json)
                        expected_extra = json.loads((prepared / 'public' / args.extra_json).read_text(encoding='utf8'))
                        check(extra['ok'] and extra['mime'].startswith('application/json') and extra['value'] == expected_extra,
                              'Explicit extra application JSON loads from the same deployed tree byte-exactly')
                    before = native()
                    page.locator('#connect').click()
                    expect(page.locator('#status')).to_have_text('ready')
                    view = page.locator('#device')
                    action = lambda name: view.locator('[data-action="' + name + '"]')
                    select = lambda name: view.locator('button[data-endpoint]').filter(has_text=name).click()
                    idle = lambda: expect(view.locator('[data-role="configurator"]')).to_have_attribute('aria-busy','false')
                    check(view.locator('button[data-endpoint]').count() == 4, mode + ': discovers the application Model')
                    check(native()['reads'] == before['reads'], 'Connecting and mounting do not call Field getters')
                    if args.exported:
                        check(not any('node_modules' in request or 'file:' in request
                                      for request in page.evaluate('performance.getEntriesByType("resource").map(row=>row.name)')),
                              'Relocated page resolves runtime assets without source or package-manager paths')
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
                    # Optional Session operations remain usable from the same
                    # shipped page after package-manager paths are removed.
                    select('SerialNumber'); action('addWatch').click()
                    watched = view.locator('.tux-watch-table tbody tr')
                    expect(watched).to_have_count(1)
                    exchanges = server.exchange_count
                    action('refreshWatch').click()
                    expect(view.locator('[data-role="watch-status"]')).to_contain_text('ReadMany complete')
                    idle()
                    expect(watched.nth(0)).to_contain_text('9007199254740993')
                    check(server.exchange_count == exchanges + 1, 'Watchlist uses one real ReadMany exchange through Session')
                    action('clearWatch').click()
                    expect(watched).to_have_count(0)

                    def add_batch(name, operation):
                        select(name)
                        action('batch-operation').select_option(operation)
                        action('addBatch').click()

                    def batch_text(path, value):
                        control = view.locator('input[data-source="batchDraft"][data-path=' +
                                               json.dumps(json.dumps(path, separators=(',', ':'))) + ']')
                        expect(control).to_be_visible()
                        control.fill(value)

                    add_batch('Config', 'write')
                    batch_text(['enabled'], 'true')
                    batch_text(['periodMs'], '1')
                    batch_text(['gain'], '3')
                    add_batch('Scale', 'service')
                    batch_text(['input'], '2.5')
                    add_batch('Reset', 'command')
                    add_batch('Config', 'read')
                    batch_rows = view.locator('.tux-batch-table tbody tr')
                    expect(batch_rows).to_have_count(4)
                    previous = native(); exchanges = server.exchange_count
                    action('executeBatch').click()
                    expect(view.locator('[data-role="batch-status"]')).to_contain_text('Batch reply received')
                    idle(); now = native()
                    check(server.exchange_count == exchanges + 1, 'Mixed Batch uses one existing Session exchange')
                    check(all(now[key] == previous[key] + 1 for key in ('reads','writes','commands','services')),
                          'Measured mixed request executes each selected endpoint once')
                    expect(batch_rows.nth(0)).to_contain_text('Applied')
                    expect(batch_rows.nth(2)).to_contain_text('Executed')
                    batch_rows.nth(1).locator('[data-action="selectBatch"]').click()
                    expect(view.locator('.tux-row[data-source="batchResponse"][data-path=\'["output"]\'] .tux-value')).to_have_text('7.5')
                    batch_rows.nth(3).locator('[data-action="selectBatch"]').click()
                    expect(view.locator('.tux-row[data-source="batchResponse"][data-path=\'["periodMs"]\'] .tux-value')).to_have_text('1000')
                    check(now['periodMs'] == 1000, 'Batch response reflects execution order, including Command normalization')
                    page.wait_for_timeout(100)
                    check(server.exchange_count == exchanges + 1, 'Batch adds no automatic retry or readback')
                    action('clearBatch').click()
                    select('Reset')
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
        check(not report['errors'] and not report['warnings'] and not report['asset_errors'], 'No browser errors, warnings or missing runtime assets')
        if args.exported:
            report['assets_sha256_after'] = inventory(public)
            check(report['assets_sha256_before'] == report['assets_sha256_after'], 'Relocated deploy assets stayed byte-identical')
        report['source_sha256_after'] = sources()
        check(report['source_sha256_before'] == report['source_sha256_after'], 'Starter sources did not change during qualification')
        check(sha(device) == receipt['device_sha256'], 'Native executable did not change during qualification')
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        server.shutdown(); thread.join(timeout=10)
        server.server_close()
        report['native_exit'] = server.peer.close()
        report['native_stderr'] = (output / 'state' / 'native-stderr.log').read_text(encoding='utf8')
        native_failure = report['native_exit'] != 0 or bool(report['native_stderr'].strip())
        if native_failure and report['passed']:
            report.update(passed=False, error='Native peer did not exit cleanly: ' + str(report['native_exit']))
        report['finished_utc'] = datetime.now(timezone.utc).isoformat()
        (output / 'summary.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf8')
        if native_failure and report.get('error', '').startswith('Native peer did not exit cleanly'):
            raise RuntimeError(report['error'])


if __name__ == '__main__':
    main()
