#!/usr/bin/env python3
"""Real 65-Field Watchlist controls over the maintained reference WebSocket server. MIT."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
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
    sys.path.insert(0, str(ROOT / 'examples/web_demo'))
    spec = importlib.util.spec_from_file_location('scaling_ws_demo', ROOT / 'examples/web_demo/serve_ws.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    server = module.WebSocketDemo(args.device, args.public_root, output / 'peer')
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    report = dict(passed=False, hardware=False, checks=0, modes={}, errors=[], warnings=[])

    def check(value, message):
        report['checks'] += 1
        if not value:
            raise AssertionError(message)

    try:
        with sync_playwright() as playwright:
            browser = playwright.chromium.launch()
            report['browser'] = dict(name='Chromium', version=browser.version)
            try:
                for mode in ('static', 'dynamic'):
                    start = report['checks']
                    page = browser.new_page(viewport={'width': 1400, 'height': 1000})
                    page.on('pageerror', lambda error: report['errors'].append(str(error)))
                    page.on('console', lambda msg: report['warnings'].append(msg.text) if msg.type in ('warning', 'error') else None)
                    page.goto('http://127.0.0.1:' + str(server.server_port) + '/?mode=' + mode)
                    expect(page.locator('body')).to_have_attribute('data-ready', 'true')
                    check(page.evaluate('scaling.invalidMounts') == [True] * 3, 'Invalid polling options reject before mounting DOM')
                    automatic = page.locator('#automatic')
                    manual = page.locator('#manual')
                    borrowed = page.locator('#borrowed')
                    def action(view, name):
                        return view.locator('[data-action="' + name + '"]')
                    rows = automatic.locator('.tux-watch-table tbody tr')
                    expect(rows).to_have_count(65)
                    viewport = automatic.locator('.tux-watch-values')
                    check(viewport.evaluate('(node) => node.scrollHeight > node.clientHeight && node.clientHeight > 0'),
                          'A long list scrolls inside its pane without hiding polling controls')
                    viewport.focus(); viewport.press('End')
                    page.wait_for_function('(node) => node.scrollTop > 0', arg=viewport.element_handle())
                    check(viewport.evaluate('(node) => node.scrollTop > 0'),
                          'Keyboard users can reach the final watched rows')
                    expect(action(automatic, 'startWatch')).to_be_visible()
                    check(page.evaluate('scaling.snapshot().watch.rows.length') == 65, 'The actual mounted Watchlist retains all 65 selected Fields')
                    interval = action(automatic, 'watch-interval')
                    expect(interval).to_have_attribute('min', '1')
                    expect(interval).to_have_attribute('max', '60000')
                    expect(interval).to_have_attribute('step', '1')
                    interval.fill('1'); interval.press('Tab')
                    check(page.evaluate('scaling.snapshot().watch.intervalMs') == 1, 'Real user input accepts a one-millisecond interval')
                    before = server.exchange_count
                    action(automatic, 'refreshWatch').click()
                    expect(automatic.locator('[data-role="watch-status"]')).to_contain_text('ReadMany complete')
                    check(server.exchange_count == before + 3, '65 mounted Fields produce exactly three bounded ReadMany requests')
                    expect(rows.nth(0)).to_contain_text('18446744073709551615')
                    expect(rows.nth(32)).to_contain_text('Unavailable')
                    expect(rows.nth(64)).to_contain_text('18446744073709551615')
                    # Session mounts schedule only after the previous pending
                    # cycle completes; no per-field timer or missed-tick queue.
                    before = server.exchange_count
                    action(automatic, 'startWatch').click()
                    deadline = time.monotonic() + 5
                    while server.exchange_count < before + 6 and time.monotonic() < deadline:
                        page.wait_for_timeout(5)
                    check(server.exchange_count >= before + 6, 'Session Start automatically completes at least two real cycles')
                    action(automatic, 'stopWatch').click()
                    expect(action(automatic, 'clearWatch')).to_be_enabled()
                    after_stop = server.exchange_count
                    page.wait_for_timeout(60)
                    check(server.exchange_count == after_stop, 'Stop drains the accepted chunk and schedules no later request')
                    check(not page.evaluate('scaling.snapshot().watch.active'), 'Stopped mounted Watchlist is logically drained')
                    # Explicit manual mode and legacy provider mounts never add
                    # hidden progression on behalf of the advanced owner.
                    for view, expression in ((manual, 'scaling.manual.provider'), (borrowed, 'scaling.borrowed')):
                        action(view, 'watch-interval').fill('1')
                        action(view, 'watch-interval').press('Tab')
                        check(page.evaluate(expression + '.snapshot().watch.intervalMs') == 1,
                              'Manual-mode observation uses the same one-millisecond interval')
                        before = server.exchange_count
                        action(view, 'startWatch').click()
                        expect(view.locator('[data-role="watch-status"]')).to_contain_text('ReadMany complete')
                        check(server.exchange_count == before + 3, 'Manual mode executes one explicitly started cycle')
                        page.wait_for_timeout(60)
                        check(server.exchange_count == before + 3, 'Manual/borrowed provider mount creates no automatic polling timer')
                        action(view, 'stopWatch').click()
                        check(not page.evaluate(expression + '.snapshot().watch.active'), 'Explicit owner Stop remains usable')
                    # Unmount a timer-owned view while it is active, preserving
                    # the Session and the other independent presentation.
                    action(automatic, 'startWatch').click()
                    page.evaluate('scaling.automatic.unmount()')
                    page.wait_for_function('!scaling.automatic.provider.snapshot().watch.pending')
                    after_remove = server.exchange_count
                    page.wait_for_timeout(60)
                    check(server.exchange_count == after_remove, 'Unmount never restarts a retired poll cycle')
                    check(page.evaluate('scaling.session.ready && scaling.manual.provider.snapshot().current'), 'Removing a polling pane does not close its shared Session')
                    screenshot = output / (mode + '.png')
                    page.screenshot(path=str(screenshot), full_page=True)
                    report['modes'][mode] = dict(checks=report['checks'] - start, screenshot_sha256=hashlib.sha256(screenshot.read_bytes()).hexdigest())
                    page.evaluate('scaling.close()')
                    page.close()
                check(not report['errors'], 'No browser runtime errors: ' + repr(report['errors']))
                check(not report['warnings'], 'No browser warnings: ' + repr(report['warnings']))
            finally:
                browser.close()
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        server.shutdown(); server.server_close(); thread.join(timeout=5)
        (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')


if __name__ == '__main__':
    main()
