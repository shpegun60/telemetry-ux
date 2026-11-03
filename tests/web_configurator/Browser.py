#!/usr/bin/env python3
"""Actual Chromium input/click journeys through installed providers and native dispatch. MIT."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import threading
import time

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
    spec = importlib.util.spec_from_file_location('telemetry_ux_web_demo', ROOT / 'examples/web_demo/serve.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    server = module.create_server(args.device, args.public_root, output / 'peer')
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    report = dict(passed=False, hardware=False, checks=0, static=False, dynamic=False,
                  errors=[], warnings=[], requests=[], modes={}, screenshots={})

    def check(condition, message):
        report['checks'] += 1
        if not condition:
            raise AssertionError(message)

    def state():
        return json.loads(server.peer.line('state'))

    try:
        with sync_playwright() as playwright:
            browser = playwright.chromium.launch()
            report['browser'] = dict(name='Chromium', version=browser.version)
            try:
                for mode in ('static', 'dynamic'):
                    server.peer.line('reset')
                    start_checks = report['checks']
                    page = browser.new_page(viewport={'width': 1360, 'height': 1000})
                    page.on('pageerror', lambda error: report['errors'].append(str(error)))
                    page.on('console', lambda message: report['warnings'].append(message.text)
                            if message.type in ('warning', 'error') else None)
                    requests = []
                    page.on('request', lambda request: requests.append(request.url))
                    page.goto('http://127.0.0.1:' + str(server.server_port), wait_until='networkidle')
                    page.locator('#mode').select_option(mode)
                    page.locator('#connect-demo').click()
                    expect(page.locator('#demo-status')).to_contain_text('Connected')
                    view = page.locator('[data-role="configurator"]')
                    expect(view).to_have_attribute('data-mode', mode)

                    def endpoint(name):
                        page.locator('button[data-endpoint]').filter(has_text=re.compile('^' + re.escape(name) + '$')).first.click()

                    def action(name):
                        return view.locator('[data-action="' + name + '"]')

                    def edit(path, text):
                        selector = json.dumps(path, separators=(',', ':'))
                        field = view.locator(
                            'input[data-source="draft"][data-path=' + json.dumps(selector) + ']')
                        expect(field).to_be_visible()
                        field.fill(text)
                        return field

                    def row(source, path):
                        selector = json.dumps(path, separators=(',', ':'))
                        return view.locator('.tux-row[data-source=' + json.dumps(source) +
                            '][data-path=' + json.dumps(selector) + '] .tux-value')

                    def idle():
                        expect(view).to_have_attribute('aria-busy', 'false')

                    def outcome(kind):
                        return view.locator('[data-role="outcome"][data-operation="' + kind + '"]')

                    config_button = page.locator('button[data-endpoint]').filter(has_text=re.compile('^Config$')).first
                    config_button.focus()
                    config_button.press('Enter')
                    expect(config_button).to_be_focused()
                    check(True, 'Keyboard endpoint selection preserves focus')
                    check(state()['reads'] == 0, 'Selection must not issue a read')
                    check(action('beginEdit').is_disabled(), 'Writes require an explicit opt-in')
                    action('refresh').click()
                    idle()
                    expect(row('value', ['large'])).to_have_text('18446744073709551615')
                    expect(row('value', ['negative'])).to_have_text('-9223372036854775808')
                    expect(row('value', ['mode'])).to_contain_text('-123')
                    expect(row('value', ['zero'])).to_have_text('-0')
                    check(state()['reads'] == 1, 'Read reaches native Model once')
                    action('writes').check()
                    action('beginEdit').click()
                    big = edit(['large'], '18446744073709551616')
                    expect(big).to_have_attribute('aria-invalid', 'true')
                    check(action('apply').is_disabled() and state()['writes'] == 0, 'Overflow never reaches native write')
                    edit(['large'], '18446744073709551615')
                    gain = edit(['gain'], '4.')
                    edit(['negative'], '-9223372036854775808')
                    expect(gain).to_have_value('4.')
                    gain.fill('25')
                    action('apply').click()  # Physical pointer input; no Enter or manual blur event.
                    idle()
                    check(state()['writes'] == 1 and state()['gain'] == 10, 'Write is normalized by the device')
                    check(state()['reads'] == 2, 'A separate native Read publishes readback')
                    expect(row('value', ['gain'])).to_have_text('10')
                    expect(row('value', ['large'])).to_have_text('18446744073709551615')

                    action('beginEdit').click()
                    edit(['large'], '9007199254740993')
                    action('apply').click()
                    idle()
                    check(state()['large'] == '9007199254740993', 'U64 never crosses JavaScript Number')
                    expect(row('value', ['large'])).to_have_text('9007199254740993')

                    action('beginEdit').click()
                    old_input = edit(['gain'], '4.5')
                    old_input.evaluate('(input) => { window.__retiredInput = input; }')
                    endpoint('ReadOnly')
                    endpoint('Config')
                    action('refresh').click()
                    idle()
                    action('beginEdit').click()
                    new_input = edit(['gain'], '6.5')
                    before = state()
                    page.evaluate('''() => {
                        const input = window.__retiredInput;
                        input.value = '9.5';
                        input.dispatchEvent(new Event('input', {bubbles:true}));
                        delete window.__retiredInput;
                    }''')
                    expect(new_input).to_have_value('6.5')
                    check(state() == before, 'Detached stale input never edits or sends a new draft')
                    action('cancelEdit').click()

                    endpoint('Accepted')
                    before = state()
                    action('execute').click()
                    idle()
                    expect(outcome('command')).to_contain_text('Accepted')
                    check('Executed' not in outcome('command').inner_text(), 'Accepted is not Executed')
                    check(state()['commands'] == before['commands'] + 1, 'One explicit Command submission')
                    check(state()['reads'] == before['reads'], 'Command does not trigger hidden Field reads')
                    endpoint('SetConfig')
                    edit(['gain'], '1.5')
                    edit(['large'], '18446744073709551615')
                    edit(['negative'], '-9223372036854775808')
                    action('execute').click()
                    idle()
                    expect(outcome('command')).to_contain_text('Executed')
                    check(state()['gain'] == 1.5, 'Command uses the edited request')

                    endpoint('Echo')
                    edit(['gain'], '-1')
                    action('call').click()
                    idle()
                    expect(outcome('service')).to_contain_text('InvalidArgument')
                    check(not view.locator('.tux-row[data-source="response"]').count(), 'Refused Service publishes no response')
                    edit(['gain'], '2.5')
                    edit(['large'], '18446744073709551615')
                    action('call').click()
                    idle()
                    expect(row('response', ['gain'])).to_have_text('2.5')
                    expect(row('response', ['large'])).to_have_text('18446744073709551615')
                    endpoint('Ping')
                    action('call').click()
                    idle()
                    check(not view.locator('.tux-row[data-source="response"]').count(), 'Void response has no snapshot rows')
                    expect(view.locator('section[data-source="response"]')).to_contain_text('Void')
                    endpoint('Empty')
                    action('call').click()
                    idle()
                    expect(outcome('service')).to_contain_text('Ok')
                    expect(view.locator('section[data-source="response"]')).to_contain_text('Empty structure')

                    endpoint('Config')
                    action('refresh').click()
                    idle()
                    nested = view.locator('[data-expand=' + json.dumps('value:["nested"]') + ']')
                    nested.click()
                    matrix = view.locator('[data-expand=' + json.dumps('value:["nested","matrix"]') + ']')
                    matrix.click()
                    check(view.locator('.tux-row[data-source="value"]').count() < 128,
                          'Collapsed nested arrays do not eagerly expand the value tree')
                    before = state()
                    page.locator('#detach-view').click()
                    check(page.locator('[data-role="configurator"]').count() == 0, 'Unmount removes only the view')
                    page.locator('#detach-view').click()
                    view = page.locator('[data-role="configurator"]')
                    expect(row('value', ['large'])).to_have_text('18446744073709551615')
                    check(state() == before, 'Hide/show keeps the same client and performs no I/O')
                    screenshot = output / (mode + '.png')
                    page.screenshot(path=str(screenshot), full_page=True)
                    report['screenshots'][mode] = dict(file=screenshot.name,
                        sha256=hashlib.sha256(screenshot.read_bytes()).hexdigest())

                    action('beginEdit').click()
                    edit(['gain'], '3.25')
                    server.reply_delay_seconds = 0.8
                    writes = state()['writes']
                    action('apply').click()
                    deadline = time.monotonic() + 5
                    while state()['writes'] == writes and time.monotonic() < deadline:
                        page.wait_for_timeout(10)
                    check(state()['writes'] == writes + 1, 'Write executed while its ACK is withheld')
                    page.locator('#disconnect-demo').click()
                    expect(outcome('write')).to_contain_text('OutcomeUnknown')
                    expect(view.locator('[data-role="status"]')).to_contain_text('Disconnected')
                    check(not view.locator('.tux-row[data-source="value"]').count(), 'Disconnect removes stale value rows')
                    server.reply_delay_seconds = 0
                    page.wait_for_timeout(900)
                    check(state()['writes'] == writes + 1, 'Late response never replays the write')
                    page.locator('#connect-demo').click()
                    expect(page.locator('#demo-status')).to_contain_text('Connected')
                    endpoint('Config')
                    action('refresh').click()
                    idle()
                    expect(row('value', ['gain'])).to_have_text('3.25')
                    check(state()['writes'] == writes + 1, 'Reconnect performs no automatic mutation')
                    check(any('/descriptor.bin' in request for request in requests) == (mode == 'dynamic'),
                          'Static mode uses only the observed header, without downloading a Descriptor')
                    check(not report['errors'] and not report['warnings'], 'Browser console must stay clean')
                    # Actual Watchlist buttons -> one existing SDK FIFO -> native
                    # ReadMany. This complements deterministic provider-clock tests.
                    for name in ('Wide', 'Signed', 'Mode', 'Unavailable'):
                        endpoint(name)
                        action('addWatch').click()
                    before_exchanges = server.exchange_count
                    action('refreshWatch').click()
                    expect(view.locator('[data-role="watch-status"]')).to_contain_text('ReadMany complete')
                    idle()
                    check(server.exchange_count == before_exchanges + 1,
                          'Four watched Fields use exactly one native exchange')
                    watch_rows = view.locator('.tux-watch-table tbody tr')
                    expect(watch_rows).to_have_count(4)
                    expect(watch_rows.nth(0)).to_contain_text('18446744073709551615')
                    expect(watch_rows.nth(1)).to_contain_text('-9223372036854775808')
                    expect(watch_rows.nth(3)).to_contain_text('Unavailable')
                    check('18446744073709551615' not in watch_rows.nth(3).inner_text(),
                          'Unavailable Field publishes no stale scalar')
                    action('watch-individual').check()
                    before_exchanges = server.exchange_count
                    action('refreshWatch').click()
                    expect(view.locator('[data-role="watch-status"]')).to_contain_text('Individual FieldRead complete')
                    idle()
                    check(server.exchange_count == before_exchanges + 4,
                          'Explicit Individual mode sends four separate reads')
                    action('startWatch').click()
                    expect(action('stopWatch')).to_be_enabled()
                    action('stopWatch').click()
                    expect(action('clearWatch')).to_be_enabled()
                    before_exchanges = server.exchange_count
                    page.wait_for_timeout(150)
                    check(server.exchange_count == before_exchanges,
                          'Stopped owner polling queues no further exchange')
                    action('clearWatch').click()
                    expect(watch_rows).to_have_count(0)

                    # Build a real mixed request with the same editor and FIFO.
                    # Full replacement FieldWrite arguments are explicit; the
                    # UI never pretends that canonical zeros are device defaults.
                    def add_batch(name, operation):
                        endpoint(name)
                        action('batch-operation').select_option(operation)
                        action('addBatch').click()

                    def edit_batch(path, text):
                        selector = json.dumps(path, separators=(',', ':'))
                        field = view.locator(
                            'input[data-source="batchDraft"][data-path=' + json.dumps(selector) + ']')
                        expect(field).to_be_visible()
                        field.fill(text)
                        return field

                    add_batch('Config', 'write')
                    edit_batch(['gain'], '6.25')
                    edit_batch(['large'], '9007199254740993')
                    edit_batch(['negative'], '-9223372036854775808')
                    add_batch('Accepted', 'command')
                    add_batch('Echo', 'service')
                    edit_batch(['gain'], '2.5')
                    edit_batch(['large'], '18446744073709551615')
                    add_batch('Wide', 'read')
                    batch_rows = view.locator('.tux-batch-table tbody tr')
                    expect(batch_rows).to_have_count(4)
                    check(action('executeBatch').is_disabled(),
                          'A reconnected session requires fresh mutation permission for Batch')
                    action('writes').check()
                    before = state()
                    before_exchanges = server.exchange_count
                    action('executeBatch').click()
                    expect(view.locator('[data-role="batch-status"]')).to_contain_text('Batch reply received')
                    idle()
                    check(server.exchange_count == before_exchanges + 1,
                          'Mixed FieldWrite/Command/Service/FieldRead uses one native exchange')
                    after = state()
                    check(after['writes'] == before['writes'] + 1 and
                          after['commands'] == before['commands'] + 1 and
                          after['services'] == before['services'] + 1 and
                          after['reads'] == before['reads'] + 1,
                          'Each requested mixed item reaches the native Model')
                    check(after['gain'] == 6.25 and after['large'] == '9007199254740993',
                          'Batch FieldWrite uses the edited full DTO without numeric precision loss')
                    expect(batch_rows.nth(0)).to_contain_text('Applied')
                    expect(batch_rows.nth(1)).to_contain_text('Accepted')
                    check('Executed' not in batch_rows.nth(1).inner_text(),
                          'Batch Command Accepted is not reported as Executed')
                    batch_rows.nth(2).locator('[data-action="selectBatch"]').click()
                    expect(row('batchResponse', ['gain'])).to_have_text('2.5')
                    expect(row('batchResponse', ['large'])).to_have_text('18446744073709551615')
                    batch_rows.nth(3).locator('[data-action="selectBatch"]').click()
                    expect(row('batchResponse', [])).to_have_text('9007199254740993')
                    before_exchanges = server.exchange_count
                    page.wait_for_timeout(150)
                    check(server.exchange_count == before_exchanges,
                          'Completed Batch has no automatic retry or readback')
                    action('clearBatch').click()
                    expect(batch_rows).to_have_count(0)
                    report[mode] = True
                    report['modes'][mode] = dict(checks=report['checks']-start_checks, native_state=state())
                    report['requests'] += requests
                    page.locator('#disconnect-demo').click()
                    page.close()
                page = browser.new_page(viewport={'width': 1100, 'height': 850})
                page.on('pageerror', lambda error: report['errors'].append(str(error)))
                page.on('console', lambda message: report['warnings'].append(message.text)
                        if message.type in ('warning', 'error') else None)
                page.goto('http://127.0.0.1:' + str(server.server_port), wait_until='networkidle')
                before = state()
                renderer = page.evaluate('''async () => {
                    const test = await import('./Renderer.js');
                    return await test.runRendererChecks();
                }''')
                check(renderer.get('passed') and renderer.get('checks', 0) >= 38,
                      'Independent renderer pagination and lifecycle controls')
                check(state() == before, 'Presentation-only controls perform no device I/O')
                check(not report['errors'] and not report['warnings'], 'Final browser console must stay clean')
                report['renderer'] = renderer
                page.close()
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
        native_exit = server.peer.close()
        report['native_exit'] = native_exit
        report['native_stderr'] = (output / 'peer/native-stderr.log').read_text(encoding='utf8')
        cleanup_failed = native_exit != 0 or bool(report['native_stderr'])
        if cleanup_failed:
            report['passed'] = False
            report['cleanup_error'] = 'Native peer exit/stderr check failed'
        (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
        if cleanup_failed:
            raise RuntimeError(report['cleanup_error'])


if __name__ == '__main__':
    main()
