#!/usr/bin/env python3
"""Drive the real structural editor and encoded C++ fake Device in a browser.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
Requires Playwright; uses a caller-selected browser or its pinned Chromium.
"""
import argparse
import os
from pathlib import Path
import struct
import sys
import threading
import time

from playwright.sync_api import sync_playwright

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'examples/structured_client'))
from serve import create_server


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--sdk-package-root', type=Path, required=True)
    args = parser.parse_args()
    out = args.build_dir.resolve()
    server = create_server(args.device, out, args.sdk_package_root)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    checks = 0

    def check(condition):
        nonlocal checks
        assert condition
        checks += 1

    try:
        with sync_playwright() as playwright:
            launch = dict(headless=True)
            if os.environ.get('TELEMETRY_BROWSER_EXECUTABLE'):
                launch['executable_path'] = os.environ['TELEMETRY_BROWSER_EXECUTABLE']
            browser = playwright.chromium.launch(**launch)
            page = browser.new_page(viewport={'width': 1180, 'height': 900})
            errors = []
            page.on('pageerror', lambda error: errors.append(str(error)))
            base = f'http://127.0.0.1:{server.server_address[1]}'
            for path in ('/serve.py', '/Device.hpp', '/CMakeLists.txt', '/node_modules/',
                         '/vendor/telemetry-sdk/package.json', '/web/telemetry.js',
                         '/../Device.hpp', '/%2e%2e/Device.hpp'):
                check(page.request.get(base + path).status == 404)
            runtime = page.request.get(base + '/vendor/telemetry-sdk/runtime.js')
            check(runtime.status == 200)
            check(runtime.body() == (args.sdk_package_root / 'web/telemetry.js').read_bytes())
            page.goto(base + '/')
            page.locator('#load-device').click()
            page.wait_for_function("document.querySelector('#values').textContent.includes('18446744073709551615')")
            check('3 services' in page.locator('#model-info').inner_text())
            check(page.locator('#editor input[min], #editor input[max], #editor input[step]').count() == 0)
            for member, value in {
                'serial': '18446744073709551615', 'offset': '-9223372036854775808',
                'gain': '-0', 'precise': '0.125', 'mode': '-2', 'multiplier': '4',
                '0': '1', '1': '256', '2': '65535',
            }.items():
                page.locator(f'input[data-member="{member}"]').fill(value)
            page.locator('input[data-member="enabled"]').check()
            with page.expect_download() as download:
                page.locator('#download').click()
            check(Path(download.value.path()).read_bytes() == (out / 'request.bin').read_bytes())
            page.locator('#call').click()
            page.wait_for_function("document.querySelector('#message').textContent.startsWith('Ok')")
            check('263168' in page.locator('#response').inner_text())
            check('18446744073709551615' in page.locator('#response').inner_text())
            check(server.request_count == 1 and server.application_calls == 1)
            page.screenshot(path=str(out / 'client-desktop.png'), full_page=True)

            for value, status in [('0', 'InvalidArgument'), ('1', 'Busy'), ('2', 'Unavailable'), ('3', 'Failed')]:
                page.locator('input[data-member="multiplier"]').fill(value)
                page.locator('#call').click()
                page.wait_for_function('(status) => document.querySelector("#message").textContent === status', arg=status)
                check(page.locator('#message').inner_text() == status)
            page.locator('#service').select_option('1')
            check(page.locator('#editor input').count() == 0)
            page.locator('#call').click()
            page.wait_for_function("document.querySelector('#response').textContent.includes('Void')")
            check(page.locator('#message').inner_text().startswith('Ok'))
            page.locator('#service').select_option('2')
            page.locator('#call').click()
            page.wait_for_function("document.querySelector('#message').textContent === 'Unavailable'")
            check(server.request_count == 7 and server.application_calls == 6)

            # A delayed operation can still execute after a client timeout.
            # Only one request is sent, and no automatic retry is performed.
            page.locator('#service').select_option('0')
            page.locator('input[data-member="multiplier"]').fill('4')
            server.delay_seconds = 5.3
            page.locator('#call').click()
            page.wait_for_function("document.querySelector('#message').textContent.startsWith('Timeout')", timeout=10000)
            deadline = time.monotonic() + 5
            while server.application_calls < 7 and time.monotonic() < deadline:
                time.sleep(0.05)
            check(server.request_count == 8 and server.application_calls == 7)
            check('not repeated' in page.locator('#message').inner_text())
            server.delay_seconds = 0

            wrong_values = bytearray((out / 'values.bin').read_bytes())
            wrong_values[16] ^= 1
            (out / 'wrong-values.bin').write_bytes(wrong_values)
            page.locator('#values-file').set_input_files(str(out / 'wrong-values.bin'))
            page.wait_for_function("document.querySelector('#message').textContent.includes('fingerprint')")
            check('fingerprint' in page.locator('#message').inner_text())

            # File I/O can complete after selection changes, including an ABA
            # switch or reloading identical DescriptorCache bytes. Exercise the
            # real File event path and codec with native-generated fixtures.
            page.evaluate('''() => {
                window.deferredFiles = new Map();
                window.openDeferredFile = (id, name, bytes) => {
                    const file = new File([new Uint8Array(bytes)], name);
                    file.arrayBuffer = () => new Promise((resolve, reject) => {
                        window.deferredFiles.set(name, {resolve, reject, bytes});
                    });
                    const transfer = new DataTransfer();
                    transfer.items.add(file);
                    const input = document.getElementById(id);
                    input.files = transfer.files;
                    input.dispatchEvent(new Event('change', {bubbles: true}));
                };
                window.finishDeferredFile = async (name, fail = false) => {
                    const pending = window.deferredFiles.get(name);
                    window.deferredFiles.delete(name);
                    if (fail) pending.reject(new Error('stale file failure'));
                    else pending.resolve(new Uint8Array(pending.bytes).buffer);
                    await new Promise(requestAnimationFrame);
                };
            }''')

            def defer_file(element, name, fixture):
                page.evaluate('([id, name, bytes]) => window.openDeferredFile(id, name, bytes)',
                              [element, name, list((out / fixture).read_bytes())])

            def finish_file(name, fail=False):
                page.evaluate('([name, fail]) => window.finishDeferredFile(name, fail)', [name, fail])

            def reload_descriptor():
                page.evaluate('window.previousServiceOption = document.querySelector("#service").firstElementChild')
                page.locator('#descriptor-file').set_input_files([])
                page.locator('#descriptor-file').set_input_files(str(out / 'descriptor.bin'))
                page.wait_for_function('document.querySelector("#service").firstElementChild !== window.previousServiceOption')

            defer_file('response-file', 'selection-response.bin', 'response.bin')
            page.locator('#service').select_option('2')
            page.locator('#service').select_option('0')
            finish_file('selection-response.bin')
            check(page.locator('#response').inner_text() == 'No response yet.')
            check(page.locator('#message').inner_text() != 'Response decoded.')

            defer_file('response-file', 'descriptor-response.bin', 'response.bin')
            reload_descriptor()
            finish_file('descriptor-response.bin')
            check(page.locator('#response').inner_text() == 'No response yet.')
            check(page.locator('#message').inner_text() == 'Descriptor validated.')

            defer_file('values-file', 'descriptor-values.bin', 'values.bin')
            reload_descriptor()
            finish_file('descriptor-values.bin')
            check(page.locator('#values').inner_text() == 'No values loaded.')

            defer_file('response-file', 'failed-old-response.bin', 'response.bin')
            page.locator('#service').select_option('2')
            page.locator('#service').select_option('0')
            finish_file('failed-old-response.bin', fail=True)
            check(page.locator('#message').inner_text() == 'Descriptor validated.')
            check(page.locator('#response').inner_text() == 'No response yet.')

            page.locator('#response-file').set_input_files(str(out / 'response.bin'))
            page.wait_for_function("document.querySelector('#message').textContent === 'Response decoded.'")
            check('263168' in page.locator('#response').inner_text())
            page.locator('#values-file').set_input_files(str(out / 'values.bin'))
            page.wait_for_function("document.querySelector('#message').textContent === 'Values decoded.'")
            check('18446744073709551615' in page.locator('#values').inner_text())
            check(server.request_count == 8 and server.application_calls == 7)

            # A reflected name is literal text, never HTML or an expression.
            descriptor = bytearray((out / 'descriptor.bin').read_bytes())
            # The application fixture has exactly one ten-byte member name.
            # Replace only that literal and recompute the public v3 FNV hash;
            # the actual installed browser parser validates the resulting bytes.
            original = b'multiplier'
            check(descriptor.count(original) == 1)
            position = descriptor.index(original)
            name = b'<b>hi</b> '
            check(len(name) == len(original))
            descriptor[position:position + len(name)] = name
            fingerprint = 0xcbf29ce484222325
            for index, byte in enumerate(descriptor):
                fingerprint = ((fingerprint ^ (0 if 16 <= index < 24 else byte)) *
                               0x100000001b3) & 0xffffffffffffffff
            struct.pack_into('<Q', descriptor, 16, fingerprint)
            (out / 'literal-name.bin').write_bytes(descriptor)
            page.locator('#descriptor-file').set_input_files(str(out / 'literal-name.bin'))
            page.wait_for_function("document.querySelector('#editor').textContent.includes('<b>hi</b>')")
            check(page.locator('#editor b').count() == 0)
            check(page.locator('#call').is_disabled())
            check(not errors)
            browser.close()
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)
    print(f'Structured browser: {checks} checks, 0 failures', flush=True)


if __name__ == '__main__':
    main()
