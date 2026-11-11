#!/usr/bin/env python3
"""Exercise the exported browser UI against the actual Qt emulator. MIT.

Requires Playwright in the invoking Python environment. All processes belong to
this runner, assets stay external, and native reply bytes are independently read
from the emulator's bounded trace. No firmware or remote repository is touched.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import socket
import struct
import time
from urllib.parse import urlsplit
from urllib.request import urlopen

from playwright.sync_api import sync_playwright, expect
from check_process_lifecycle import Capture, demand, digest, qt_test_environment
from check_batch import decode_batch


def request(port, target='/', host=None, extra='', method='GET'):
    with socket.create_connection(('127.0.0.1', port), timeout=3) as stream:
        stream.settimeout(3)
        stream.sendall((f'{method} {target} HTTP/1.1\r\nHost: {host or "127.0.0.1:" + str(port)}\r\n'
                        f'{extra}Connection: close\r\n\r\n').encode('ascii'))
        data = bytearray()
        while True:
            part = stream.recv(65536)
            if not part:
                return bytes(data)
            data.extend(part)
            demand(len(data) <= 5 * 1024 * 1024, 'HTTP response exceeds asset budget')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emulator', required=True, type=Path)
    parser.add_argument('--www', required=True, type=Path)
    parser.add_argument('--output-dir', required=True, type=Path)
    parser.add_argument('--browser-executable', type=Path)
    args = parser.parse_args()
    output, www, emulator = args.output_dir.resolve(), args.www.resolve(), args.emulator.resolve()
    workspace = Path(__file__).resolve().parents[2]
    demand(not output.exists() and workspace not in output.parents, 'Use a fresh external output directory')
    demand(emulator.is_file() and (www / 'assets.json').is_file(), 'Built web-enabled emulator and exported www required')
    output.mkdir(parents=True)
    assets = lambda: {path.relative_to(www).as_posix(): digest(path)
                      for path in sorted(www.rglob('*')) if path.is_file()}
    before = assets()
    report = dict(passed=False, hardware=False, checks=0, warnings=[], errors=[], requests=[],
                  asset_sha256_before=before, emulator_sha256=digest(emulator))
    peer = None
    started = time.monotonic()

    def check(value, label):
        report['checks'] += 1
        demand(value, label)
        report.setdefault('completed', []).append(label)

    try:
        peer = Capture('emulator', [str(emulator), '--pause', '--trace-encoded', '--port', '0',
            '--web-root', str(www), '--http-port', '0', '--quit-after', '70000'], output, qt_test_environment())
        ready = web = None
        while time.monotonic() - started < 10:
            peer.refresh()
            ready = next((row for row in peer.records if row.get('ready')), None)
            web = next((row for row in peer.records if row.get('webReady')), None)
            if ready and web:
                break
            demand(peer.process.poll() is None, 'Emulator exited before HTTP readiness')
            time.sleep(0.01)
        check(bool(ready and web), 'Real Qt emulator advertises TCP and HTTP listeners')
        origin, http_port = web['url'].rstrip('/'), web['httpPort']
        discovery = json.loads(urlopen(origin + '/demo/transport', timeout=3).read())
        ws_port = urlsplit(discovery['url']).port
        check(discovery['kind'] == 'websocket' and discovery['protocol'] == 'telemetry.encoded.v1',
              'Web discovery selects the existing Encoded WebSocket contract')
        for label, response, expected in (
            ('Foreign HTTP Host', request(http_port, host='attacker.invalid'), b' 400 '),
            ('Unknown asset', request(http_port, '/not-exported.bin'), b' 404 '),
            ('Encoded traversal', request(http_port, '/%2e%2e/Device.cpp'), b' 400 '),
            ('HTTP mutation method', request(http_port, method='POST'), b' 405 '),
            ('HTTP request body', request(http_port, extra='Content-Length: 1\r\n'), b' 400 '),
        ):
            check(expected in response.split(b'\r\n', 1)[0], label + ' refused before dispatch')
        header = urlopen(origin + '/schema-header.bin', timeout=3).read()
        descriptor = urlopen(origin + '/descriptor.bin', timeout=3).read()
        check(len(header) == 64 and descriptor[:64] == header, 'Descriptor header matches the exact native Model')
        with sync_playwright() as playwright:
            browser = playwright.chromium.launch(**({'executable_path': str(args.browser_executable)}
                                                   if args.browser_executable else {}))
            try:
                page = browser.new_page(viewport={'width': 1440, 'height': 1200})
                page.on('pageerror', lambda error: report['errors'].append(str(error)))
                page.on('console', lambda msg: report['warnings'].append(msg.text)
                        if msg.type in ('error', 'warning') else None)
                page.on('request', lambda item: report['requests'].append(item.url))
                page.goto(origin)
                page.locator('#connect').click()
                expect(page.locator('#status')).to_have_text('ready')
                view = page.locator('#device')
                action = lambda name: view.locator('[data-action="' + name + '"]')
                select = lambda name: view.locator('button[data-endpoint]').filter(has_text=name).click()
                idle = lambda: expect(view.locator('[data-role="configurator"]')).to_have_attribute('aria-busy', 'false')

                def text(source, path, value):
                    selector = f'input[data-source="{source}"][data-path={json.dumps(json.dumps(path, separators=(",", ":")))}]'
                    control = view.locator(selector)
                    expect(control).to_be_visible()
                    control.fill(str(value))

                check(view.locator('button[data-endpoint]').count() == 73, 'Dynamic Configurator discovers all 73 native endpoints')
                check(not any('node_modules' in url or url.startswith('file:') for url in report['requests']),
                      'Exported browser uses only validated HTTP assets')
                peer.refresh()
                check(not any(row.get('encoded') for row in peer.records), 'Bootstrap and mount do not execute native endpoints')
                # A second browser and raw TCP must not acquire a parallel owner.
                other = browser.new_page()
                other.goto(origin)
                other.locator('#connect').click()
                expect(other.locator('#error')).not_to_have_text('')
                report['second_client_error'] = other.locator('#error').inner_text()
                check('other client' in report['second_client_error'] and
                      'BootstrapFailed' in report['second_client_error'],
                      'Connection refusal gives a useful next step and keeps technical details')
                check(other.locator('#status').inner_text() != 'ready', 'Second browser cannot admit a concurrent session')
                other.close()
                with socket.create_connection(('127.0.0.1', ready['port']), timeout=3) as tcp:
                    tcp.settimeout(3)
                    check(tcp.recv(1) == b'', 'Native TCP is refused while the browser owns the same device')

                select('Config'); action('refresh').click(); idle()
                action('writes').check(); action('beginEdit').click()
                text('draft', ['Gain'], '2.5')
                text('draft', ['Enabled'], 'true')
                text('draft', ['SamplePeriodMs'], '25')
                action('apply').click(); idle()
                expect(view.locator('.tux-row[data-source="value"][data-path=\'["Gain"]\'] .tux-value')).to_have_text('2.5')
                check(True, 'Browser FieldWrite is followed by actual native readback')
                select('Scale')
                text('draft', ['value'], '3'); text('draft', ['factor'], '4')
                action('call').click(); idle()
                expect(view.locator('.tux-row[data-source="response"][data-path=\'["result"]\'] .tux-value')).to_have_text('12')
                check(True, 'Service response comes from the native device Model')
                select('SetEnabled'); text('draft', ['enabled'], 'false')
                action('execute').click(); idle()
                expect(view.locator('[data-role="outcome"][data-operation="command"]')).to_contain_text('Executed')
                select('Power'); action('refresh').click(); idle()
                expect(view.locator('.tux-row[data-source="value"] .tux-value')).to_have_text('0')
                check(True, 'Confirmed Command changes a subsequent real FieldRead')
                for name in ('Voltage', 'Current', 'Power'):
                    select(name); action('addWatch').click()
                action('refreshWatch').click()
                expect(view.locator('[data-role="watch-status"]')).to_contain_text('ReadMany complete')
                idle()
                expect(view.locator('.tux-watch-table tbody tr')).to_have_count(3)
                check(True, 'Watchlist reads three selected Fields through one ReadMany exchange')
                action('clearWatch').click()

                def add(name, operation):
                    select(name)
                    action('batch-operation').select_option(operation)
                    action('addBatch').click()

                add('Config', 'write')
                text('batchDraft', ['Gain'], '3'); text('batchDraft', ['Enabled'], 'true')
                text('batchDraft', ['SamplePeriodMs'], '100')
                add('SetEnabled', 'command'); text('batchDraft', ['enabled'], 'false')
                add('Scale', 'service'); text('batchDraft', ['value'], '2'); text('batchDraft', ['factor'], '3')
                add('Power', 'read'); add('Config', 'read')
                rows = view.locator('.tux-batch-table tbody tr')
                expect(rows).to_have_count(5)
                action('executeBatch').click()
                expect(view.locator('[data-role="batch-status"]')).to_contain_text('Batch reply received')
                idle()
                expect(rows.nth(0)).to_contain_text('Applied')
                expect(rows.nth(1)).to_contain_text('Executed')
                rows.nth(2).locator('[data-action="selectBatch"]').click()
                expect(view.locator('.tux-row[data-source="batchResponse"][data-path=\'["result"]\'] .tux-value')).to_have_text('6')
                rows.nth(4).locator('[data-action="selectBatch"]').click()
                expect(view.locator('.tux-row[data-source="batchResponse"][data-path=\'["Enabled"]\'] .tux-value')).to_have_text('false')
                check(True, 'Mixed Batch exposes ordered per-item facts and owned native responses')
                page.screenshot(path=str(output / 'browser-batch.png'), full_page=True)
                peer.refresh()
                trace_before = [row for row in peer.records if row.get('encoded')]
                page.locator('#disconnect').click()
                expect(page.locator('#status')).to_have_text('disconnected')
                check(view.locator('.tux-row[data-source="batchResponse"] .tux-value').count() == 0,
                      'Disconnect retires cached response values')
                page.locator('#reconnect').click()
                expect(page.locator('#status')).to_have_text('ready')
                page.wait_for_timeout(100)
                peer.refresh()
                check([row for row in peer.records if row.get('encoded')] == trace_before,
                      'Explicit reconnect does not replay or silently refresh native operations')
                select('Config'); action('refresh').click(); idle()
                expect(view.locator('.tux-row[data-source="value"][data-path=\'["Gain"]\'] .tux-value')).to_have_text('3')
                page.locator('#close').click()
                expect(page.locator('#status')).to_have_text('closed')
                check(page.locator('#reconnect').is_disabled() and page.locator('#connect').is_enabled(),
                      'Close ends this Session and permits a new explicit connection')
                check(not report['errors'] and not report['warnings'], 'No browser errors or warnings in the positive journey')
                report['browser_version'] = browser.version
                page.close()
            finally:
                browser.close()
        demand(peer.process.wait(timeout=max(1, 80 - (time.monotonic() - started))) == 0, 'Native emulator did not exit gracefully')
        peer.refresh()
        native = [row for row in peer.records if row.get('summary')]
        check(len(native) == 1 and all(native[0][key] == 2 for key in ('writes', 'commands', 'services')),
              'Native mutation counters prove exactly the explicit journey without reconnect replay')
        trace = [row for row in peer.records if row.get('encoded')]
        batches = [row for row in trace if row['operation'] == 6]
        check(len(batches) == 1, 'The five-step Batch is one native Encoded exchange')
        records = decode_batch(bytes.fromhex(batches[0]['replyHex']))
        check([(row['operation'], row['endpoint']) for row in records] ==
              [(1, 65536), (2, 1), (3, 0), (4, 2), (4, 65536)] and
              all(row['dispatch'] == row['endpointStatus'] == 0 for row in records),
              'Independent wire decoding confirms all five ordered successful results')
        check(struct.unpack('<f', records[2]['payload'])[0] == 6 and
              struct.unpack('<f', records[3]['payload'])[0] == 0 and
              struct.unpack('<f?I', records[4]['payload']) == (3, False, 100),
              'Batch payload bytes reflect native Write then Command then reads')
        check(len([row for row in trace if row['operation'] == 5]) == 1,
              'Watchlist uses one native ReadMany rather than hidden FieldRead fan-out')
        check(len({row['session'] for row in trace}) == 2, 'Reconnect uses a fresh native session')
        report.update(native=native[0], trace=trace, asset_sha256_after=assets())
        check(before == report['asset_sha256_after'] and digest(emulator) == report['emulator_sha256'],
              'Exported assets and tested executable stayed byte-identical')
        report['passed'] = True
    except Exception as error:
        report['error'] = f'{type(error).__name__}: {error}'
    finally:
        if peer:
            try:
                peer.stop_owned(); peer.refresh()
            except Exception as error:
                report.update(passed=False, cleanupError=str(error))
        report.update(seconds=time.monotonic() - started, runner_sha256=digest(Path(__file__)))
        report['files'] = {path.name: digest(path) for path in output.iterdir() if path.is_file()}
        (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(json.dumps({key: report.get(key) for key in ('passed', 'checks', 'seconds', 'error')}))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
