#!/usr/bin/env python3
"""Serve the payload tools and a native demonstration on localhost only.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
Routing, timeout policy and one native process per service belong to this
example. The installed SDK supplies the transport-independent payload codec.
"""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import re
import subprocess
import tempfile
import time

HERE = Path(__file__).resolve().parent


def installed_runtime(package_root):
    """Validate the explicitly installed npm package; never search SDK sources."""
    package_root = Path(package_root).resolve()
    if package_root.name != 'telemetry-sdk' or package_root.parent.name != 'node_modules':
        raise ValueError('--sdk-package-root must name installed node_modules/telemetry-sdk')
    metadata = json.loads((package_root / 'package.json').read_text(encoding='utf8'))
    if (metadata.get('name') != 'telemetry-sdk' or metadata.get('version') != '0.1.0' or
            metadata.get('type') != 'module' or
            metadata.get('exports', {}).get('./runtime', {}).get('default') != './web/telemetry.js'):
        raise ValueError('Require the installed telemetry-sdk 0.1.0 runtime export')
    runtime = (package_root / 'web/telemetry.js').resolve()
    declaration = (package_root / 'web/telemetry.d.ts').resolve()
    for path in (runtime, declaration):
        if package_root not in path.parents or not path.is_file():
            raise ValueError('Installed telemetry-sdk runtime files are missing or outside the package')
    return runtime


def create_server(device, data_dir, sdk_package_root, port=0):
    runtime = installed_runtime(sdk_package_root)
    device, data_dir = Path(device).absolute(), Path(data_dir).resolve()
    if not device.is_file():
        raise ValueError('Provide the built telemetry_structured_device executable')
    data_dir.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(device), str(data_dir)], check=True, timeout=30)
    routes = {
        '/': (HERE / 'index.html', 'text/html; charset=utf-8'),
        '/index.html': (HERE / 'index.html', 'text/html; charset=utf-8'),
        '/app.js': (HERE / 'app.js', 'text/javascript; charset=utf-8'),
        '/style.css': (HERE / 'style.css', 'text/css; charset=utf-8'),
        '/vendor/telemetry-sdk/runtime.js': (runtime, 'text/javascript; charset=utf-8'),
        '/descriptor.bin': (data_dir / 'descriptor.bin', 'application/octet-stream'),
        '/values.bin': (data_dir / 'values.bin', 'application/octet-stream'),
    }

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            route = routes.get(self.path)
            if route is None:
                self.send_error(404, 'Unknown example route')
                return
            path, mime = route
            payload = path.read_bytes()
            self.send_response(200)
            self.send_header('Content-Type', mime)
            self.send_header('Content-Length', str(len(payload)))
            self.send_header('Cache-Control', 'no-store')
            self.end_headers()
            self.wfile.write(payload)

        def do_POST(self):
            match = re.fullmatch(r'/service/(0|[1-9][0-9]{0,9})', self.path)
            if not match or int(match[1]) > 0xffffffff:
                self.send_error(404, 'Unknown endpoint route')
                return
            length = self.headers.get('Content-Length', '')
            if not re.fullmatch(r'[0-9]{1,7}', length) or int(length) > 1048576:
                self.send_error(400, 'Invalid content length')
                return
            self.connection.settimeout(10)
            payload = self.rfile.read(int(length))
            if len(payload) != int(length):
                self.send_error(400, 'Short payload')
                return
            self.server.request_count += 1
            if self.server.delay_seconds:
                time.sleep(self.server.delay_seconds)
            with tempfile.TemporaryDirectory(prefix='request-', dir=data_dir) as temporary:
                source, destination = Path(temporary) / 'input.bin', Path(temporary) / 'output.bin'
                source.write_bytes(payload)
                result = subprocess.run(
                    [str(device), 'service', match[1], str(source), str(destination)],
                    capture_output=True, text=True, timeout=30)
                if result.returncode:
                    self.send_error(500, 'Demonstration Device failed')
                    return
                dispatch, endpoint, written, calls = map(int, result.stdout.split())
                response = destination.read_bytes()
                if written != len(response):
                    self.send_error(500, 'Demonstration Device length mismatch')
                    return
                self.server.application_calls += calls
                try:
                    self.send_response(200)
                    self.send_header('Content-Type', 'application/octet-stream')
                    self.send_header('Content-Length', str(written))
                    self.send_header('X-Dispatch-Status', str(dispatch))
                    self.send_header('X-Endpoint-Status', str(endpoint))
                    self.end_headers()
                    self.wfile.write(response)
                except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                    pass  # The caller may time out after the operation executes.

    server = ThreadingHTTPServer(('127.0.0.1', port), Handler)
    server.delay_seconds = 0
    server.request_count = server.application_calls = 0
    return server


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', type=Path, required=True)
    parser.add_argument('--data-dir', type=Path, required=True)
    parser.add_argument('--sdk-package-root', type=Path, required=True)
    parser.add_argument('--port', type=int, default=8080)
    args = parser.parse_args()
    if not 0 <= args.port <= 65535:
        parser.error('--port must be between 0 and 65535')
    server = create_server(args.device, args.data_dir, args.sdk_package_root, args.port)
    print(f'Open http://127.0.0.1:{server.server_address[1]}/', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == '__main__':
    main()
