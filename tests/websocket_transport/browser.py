#!/usr/bin/env python3
"""Real Chromium, installed packages, persistent native Model and WebSocket. MIT."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys
import threading
import time

from playwright.sync_api import sync_playwright, expect

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', type=Path, required=True)
    parser.add_argument('--public-root', type=Path, required=True)
    parser.add_argument('--data-dir', type=Path, required=True)
    args = parser.parse_args()
    output = args.data_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(ROOT/'examples/web_demo'))
    spec = importlib.util.spec_from_file_location('websocket_demo', ROOT/'examples/web_demo/serve_ws.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    server = module.create_server(args.device, args.public_root, output/'peer')
    native_line = server.peer.line
    wire_replies, faults = [], dict(late=None, late_sent=0, corrupt=False)

    def controlled_line(text, owner=None, *, diagnostic=False):
        answer = native_line(text, owner, diagnostic=diagnostic)
        if owner is not None and re.fullmatch(r'(?:[0-9a-f]{2}){32,65536}', text):
            wire_replies.append(bytes.fromhex(answer))
            if faults['late'] is not None:
                # Replay an opaque previously captured native reply, not a test codec.
                server._connection.send(faults['late'])
                faults.update(late=None, late_sent=faults['late_sent']+1)
            if faults['corrupt']:
                faults['corrupt'] = False
                malformed = bytearray.fromhex(answer)
                malformed[0] ^= 1
                return malformed.hex()
        return answer
    server.peer.line = controlled_line
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    report = dict(passed=False, hardware=False, checks=0, static=False, dynamic=False,
                  errors=[], warnings=[], modes={}, greetings={}, screenshots={})

    def check(condition, message):
        report['checks'] += 1
        if not condition:
            raise AssertionError(message)

    def state():
        deadline = time.monotonic()+5
        while True:
            try:
                return json.loads(server.peer.line('state', diagnostic=True))
            except module.http_demo.PeerBusy:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(.01)

    def lease_idle():
        deadline = time.monotonic()+5
        while server._connection is not None and time.monotonic() < deadline:
            time.sleep(.01)
        check(server._connection is None, 'Old native lease must finish before another admission')

    try:
        with sync_playwright() as playwright:
            browser = playwright.chromium.launch()
            report['browser'] = dict(name='Chromium', version=browser.version)
            try:
                for mode in ('static', 'dynamic'):
                    lease_idle()
                    server.peer.line('reset')
                    wire_replies.clear()
                    page = browser.new_page(viewport={'width': 1360, 'height': 1000})
                    page.on('pageerror', lambda error: report['errors'].append(str(error)))
                    page.on('console', lambda message: report['warnings'].append(message.text)
                            if message.type in ('warning', 'error') else None)
                    requests, frames = [], dict(sent=0, received=0, greetings=[])
                    page.on('request', lambda request: requests.append(request.url))

                    def socket_open(socket):
                        def sent(_):
                            frames['sent'] += 1
                        def received(value):
                            frames['received'] += 1
                            if isinstance(value, str):
                                frames['greetings'].append(json.loads(value))
                        socket.on('framesent', sent)
                        socket.on('framereceived', received)
                    page.on('websocket', socket_open)
                    page.goto('http://127.0.0.1:'+str(server.server_port), wait_until='networkidle')
                    page.locator('#mode').select_option(mode)
                    page.locator('#connect-demo').click()
                    expect(page.locator('#demo-status')).to_contain_text('Connected')
                    view = page.locator('[data-role="configurator"]')
                    expect(view).to_have_attribute('data-mode', mode)

                    def endpoint(name):
                        page.locator('button[data-endpoint]').filter(has_text=re.compile('^'+re.escape(name)+'$')).first.click()
                    def action(name):
                        return view.locator('[data-action="'+name+'"]')
                    def row(source, path):
                        encoded = json.dumps(path, separators=(',', ':'))
                        return view.locator('.tux-row[data-source='+json.dumps(source)+'][data-path='+json.dumps(encoded)+'] .tux-value')
                    def edit(path, text):
                        encoded = json.dumps(path, separators=(',', ':'))
                        field = view.locator('input[data-source="draft"][data-path='+json.dumps(encoded)+']')
                        field.fill(text)
                        return field
                    def idle():
                        expect(view).to_have_attribute('aria-busy', 'false')
                    def outcome(name):
                        return view.locator('[data-role="outcome"][data-operation="'+name+'"]')

                    endpoint('Config')
                    check(state()['reads'] == 0, 'Selecting an endpoint issues no hidden Read')
                    action('refresh').click()
                    idle()
                    expect(row('value', ['large'])).to_have_text('18446744073709551615')
                    expect(row('value', ['negative'])).to_have_text('-9223372036854775808')
                    expect(row('value', ['zero'])).to_have_text('-0')
                    check(state()['reads'] == 1, 'One binary request reads the real native Model')
                    old_reply = wire_replies[-1]
                    action('writes').check()
                    action('beginEdit').click()
                    edit(['large'], '9007199254740993')
                    edit(['gain'], '25')
                    action('apply').click()  # Real mouse; no Enter before Apply.
                    idle()
                    check(state()['writes'] == 1 and state()['gain'] == 10, 'Device normalizes the actual Write')
                    check(state()['reads'] == 2 and state()['large'] == '9007199254740993',
                          'Separate readback retains exact U64 and the normalized value')
                    expect(row('value', ['gain'])).to_have_text('10')
                    endpoint('Accepted')
                    action('execute').click()
                    idle()
                    expect(outcome('command')).to_contain_text('Accepted')
                    check('Executed' not in outcome('command').inner_text(), 'Accepted is not Executed')
                    endpoint('SetConfig')
                    edit(['gain'], '1.5')
                    action('execute').click()
                    idle()
                    expect(outcome('command')).to_contain_text('Executed')
                    check(state()['gain'] == 1.5 and state()['commands'] == 2, 'Explicit Command reaches native Model once')
                    endpoint('Echo')
                    edit(['gain'], '-1')
                    action('call').click()
                    idle()
                    expect(outcome('service')).to_contain_text('InvalidArgument')
                    check(not view.locator('.tux-row[data-source="response"]').count(), 'Refusal publishes no response')
                    edit(['gain'], '2.5')
                    edit(['large'], '18446744073709551615')
                    action('call').click()
                    idle()
                    expect(row('response', ['large'])).to_have_text('18446744073709551615')
                    expect(row('response', ['gain'])).to_have_text('2.5')
                    check(state()['services'] == 2, 'Service error and success both reach the real endpoint')
                    endpoint('Config')
                    action('refresh').click()
                    idle()
                    before, sent = state(), frames['sent']
                    page.locator('#detach-view').click()
                    check(page.locator('[data-role="configurator"]').count() == 0, 'Hide removes only the view')
                    page.locator('#detach-view').click()
                    view = page.locator('[data-role="configurator"]')
                    check(state() == before and frames['sent'] == sent and len(frames['greetings']) == 1,
                          'Show reuses the exact socket, session and client without I/O')
                    screenshot = output/(mode+'.png')
                    page.screenshot(path=str(screenshot), full_page=True)
                    report['screenshots'][mode] = dict(path=str(screenshot), sha256=hashlib.sha256(screenshot.read_bytes()).hexdigest())

                    # Native execution completes, but its reply is deliberately discarded.
                    action('beginEdit').click()
                    edit(['gain'], '3.25')
                    writes = state()['writes']
                    server.drop_reply_once = True
                    action('apply').click()
                    expect(outcome('write')).to_contain_text('OutcomeUnknown')
                    expect(page.locator('#demo-status')).to_contain_text('Disconnected')
                    check(state()['writes'] == writes+1 and state()['gain'] == 3.25,
                          'Lost reply preserves uncertainty after exactly one native mutation')
                    check(not view.locator('.tux-row[data-source="value"]').count(), 'Disconnected values are stale')
                    lease_idle()
                    page.locator('#connect-demo').click()
                    expect(page.locator('#demo-status')).to_contain_text('Connected')
                    endpoint('Config')
                    action('refresh').click()
                    idle()
                    expect(row('value', ['gain'])).to_have_text('3.25')
                    check(state()['writes'] == writes+1, 'Reconnect does not replay the uncertain mutation')
                    check(len(frames['greetings']) == 2 and frames['greetings'][1]['session'] > frames['greetings'][0]['session'],
                          'An explicit reconnect obtains a distinct lease')
                    # An actual old native reply arrives while a new request is pending.
                    faults['late'] = old_reply
                    action('refresh').click()
                    idle()
                    expect(row('value', ['gain'])).to_have_text('3.25')
                    check(faults['late'] is None and faults['late_sent'] > 0,
                          'A late old-session reply cannot publish over the current read')
                    action('writes').check()
                    action('beginEdit').click()
                    edit(['gain'], '4.5')
                    writes = state()['writes']
                    faults['corrupt'] = True
                    action('apply').click()
                    expect(outcome('write')).to_contain_text('OutcomeUnknown')
                    expect(view.locator('[data-role="status"]')).to_contain_text('Disconnected')
                    check(server._connection is not None and page.locator('#disconnect-demo').is_enabled(),
                          'Disconnect remains enabled when admission is lost but the physical socket is open')
                    page.locator('#disconnect-demo').click()
                    lease_idle()
                    check(state()['writes'] == writes+1, 'Physical Disconnect never replays a malformed-ACK mutation')
                    page.locator('#connect-demo').click()
                    expect(page.locator('#demo-status')).to_contain_text('Connected')
                    endpoint('Accepted')
                    action('writes').check()
                    action('execute').click()
                    idle()
                    expect(outcome('command')).to_contain_text('Accepted')
                    known = state()
                    server._connection.close(1001, 'Known-result test closure')
                    expect(page.locator('#demo-status')).to_contain_text('Disconnected')
                    expect(outcome('command')).to_contain_text('Accepted')
                    check('OutcomeUnknown' not in outcome('command').inner_text() and state() == known,
                          'A known Accepted outcome survives remote closure without re-execution')
                    check(any('/descriptor.bin' in url for url in requests) == (mode == 'dynamic'),
                          'Static mode never downloads the Descriptor body')
                    check(not any('/exchange' in url or '/demo/connect' in url for url in requests),
                          'WebSocket mode has no HTTP exchange fallback')
                    check(not report['errors'] and not report['warnings'], 'Actual browser journeys have no diagnostics')
                    report[mode] = True
                    report['modes'][mode] = dict(native_state=state(), frames=frames)
                    expect(page.locator('#disconnect-demo')).to_be_disabled()
                    page.close()
                    lease_idle()

                # Test-only greeting faults exercise candidate cleanup before client creation.
                original_greet = server._greet
                for name, greeting in [('zero', '{"session":0}'), ('extra', '{"session":20,"extra":1}'),
                                       ('fraction', '{"session":1.5}'), ('binary', b'bad')]:
                    server._greet = lambda socket, session, payload=greeting: socket.send(payload)
                    page = browser.new_page()
                    page.on('pageerror', lambda error: report['errors'].append(str(error)))
                    page.goto('http://127.0.0.1:'+str(server.server_port), wait_until='networkidle')
                    page.locator('#connect-demo').click()
                    expect(page.locator('#demo-status')).to_contain_text('greeting')
                    expect(page.locator('#connect-demo')).to_be_enabled()
                    lease_idle()
                    check(not page.locator('[data-role="configurator"]').count(), 'Bad greeting creates no client view')
                    report['greetings'][name] = 'refused and physical lease released'
                    page.close()
                server._greet = lambda socket, session: None
                page = browser.new_page()
                page.on('pageerror', lambda error: report['errors'].append(str(error)))
                page.goto('http://127.0.0.1:'+str(server.server_port), wait_until='networkidle')
                page.locator('#connect-demo').click()
                expect(page.locator('#demo-status')).to_contain_text('greeting timed out', timeout=7000)
                lease_idle()
                check(not page.locator('[data-role="configurator"]').count(), 'Greeting deadline cleans the physical candidate')
                page.close()
                page = browser.new_page()
                page.on('pageerror', lambda error: report['errors'].append(str(error)))
                page.goto('http://127.0.0.1:'+str(server.server_port), wait_until='networkidle')
                page.locator('#connect-demo').click()
                expect(page.locator('#disconnect-demo')).to_be_enabled()
                # Wait for the server lease, so this is a genuinely open unadmitted socket.
                deadline = time.monotonic()+5
                while server._connection is None and time.monotonic() < deadline:
                    page.wait_for_timeout(10)
                check(server._connection is not None, 'Missing greeting retains a real open candidate socket')
                page.locator('#disconnect-demo').click()
                expect(page.locator('#demo-status')).to_contain_text('Disconnected.')
                lease_idle()
                check(page.locator('#connect-demo').is_enabled(), 'Disconnect works before client.ready can exist')
                page.close()
                server._greet = original_greet
                def duplicate_greeting(socket, session):
                    original_greet(socket, session)
                    socket.send(json.dumps({'session': session}))
                server._greet = duplicate_greeting
                page = browser.new_page()
                page.on('pageerror', lambda error: report['errors'].append(str(error)))
                page.goto('http://127.0.0.1:'+str(server.server_port), wait_until='networkidle')
                page.locator('#connect-demo').click()
                expect(page.locator('#demo-status')).to_contain_text('Disconnected (')
                lease_idle()
                check(not page.locator('[data-role="configurator"]').count(), 'A second JSON greeting is not a binary reply')
                page.close()
                server._greet = original_greet
                for mode, resource, body in [('static', 'schema-header.bin', bytes(64)),
                                             ('dynamic', 'descriptor.bin', b'bad')]:
                    page = browser.new_page()
                    page.on('pageerror', lambda error: report['errors'].append(str(error)))
                    page.goto('http://127.0.0.1:'+str(server.server_port), wait_until='networkidle')
                    page.route('**/'+resource, lambda route, request, value=body: route.fulfill(
                        status=200, content_type='application/octet-stream', body=value))
                    page.locator('#mode').select_option(mode)
                    page.locator('#connect-demo').click()
                    expect(page.locator('#demo-status')).to_contain_text('Demo connection failed:')
                    lease_idle()
                    check(not page.locator('[data-role="configurator"]').count(), 'Admission failure closes its borrowed socket')
                    page.close()
                check(not report['errors'], 'Greeting controls produce no uncaught browser exceptions')
            except Exception:
                if 'page' in locals() and not page.is_closed():
                    try:
                        page.screenshot(path=str(output/'failure.png'), full_page=True)
                        (output/'failure.html').write_text(page.content(), encoding='utf8')
                    except Exception as diagnostic:
                        report['diagnostic_error'] = str(diagnostic)
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
        report['native_stderr'] = (output/'peer/native-stderr.log').read_text(encoding='utf8')
        if native_exit != 0 or report['native_stderr'] or thread.is_alive():
            report['passed'] = False
            report['cleanup_error'] = 'Native exit/stderr or listener cleanup failed'
        (output/'summary.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf8')
        if not report['passed'] and report.get('cleanup_error'):
            raise RuntimeError(report['cleanup_error'])


if __name__ == '__main__':
    main()
