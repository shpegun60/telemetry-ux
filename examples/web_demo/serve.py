#!/usr/bin/env python3
"""Local web demonstration transport to a persistent native Encoded dispatcher.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
This example owns HTTP routing. The SDK owns every Telemetry request/reply byte.
It listens only on localhost and deliberately serves one demo session at a time.
"""
import argparse
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import queue
import re
import subprocess
import threading
import time


class NativePeer:
    def __init__(self, executable, directory):
        self.executable = Path(executable).resolve()
        self.directory = Path(directory).resolve()
        self.directory.mkdir(parents=True, exist_ok=True)
        self.descriptor = self.directory / 'descriptor.bin'
        subprocess.run([str(self.executable), '--descriptor', str(self.descriptor)],
                       check=True, timeout=30)
        self.lock = threading.RLock()
        self.lines = queue.Queue()
        self.log = (self.directory / 'native-stderr.log').open('w', encoding='utf8')
        self.process = subprocess.Popen([str(self.executable)], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=self.log, text=True, bufsize=1)
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()
        self.session = 16

    def _read(self):
        try:
            for line in self.process.stdout:
                self.lines.put(line.rstrip('\r\n'))
        finally:
            self.lines.put(None)

    def line(self, text):
        with self.lock:
            if self.process.poll() is not None:
                raise RuntimeError('Native peer exited')
            self.process.stdin.write(text + '\n')
            self.process.stdin.flush()
            answer = self.lines.get(timeout=15)
            if answer is None:
                raise RuntimeError('Native peer closed its reply stream')
            return answer

    def connect(self):
        with self.lock:
            if self.session == 0xffffffff:
                raise RuntimeError('Demo session IDs exhausted; restart the demonstration')
            self.session += 1
            if json.loads(self.line('session ' + str(self.session))) != {'session': self.session}:
                raise RuntimeError('Native peer refused the new demo session')
            return self.session

    def close(self):
        with self.lock:
            if self.process.poll() is None:
                self.process.stdin.close()
                try:
                    self.process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    self.process.terminate()
                    self.process.wait(timeout=5)
            self.reader.join(timeout=5)
            self.process.stdout.close()
            self.log.close()
            return self.process.returncode


def create_server(device, public_root, data_dir, port=0):
    """Return an unstarted localhost server; caller owns shutdown and peer close."""
    root = Path(public_root).resolve()
    if not root.is_dir():
        raise ValueError('--public-root must contain the prepared browser application')
    peer = NativePeer(device, data_dir)

    class Handler(SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=str(root), **kwargs)

        def log_message(self, *_):
            pass

        def reply(self, code, body, kind='application/octet-stream'):
            self.send_response(code)
            self.send_header('Content-Type', kind)
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Cache-Control', 'no-store')
            self.end_headers()
            try:
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                # The peer may already have executed a mutation. Never repeat it.
                pass

        def do_GET(self):
            if self.path == '/favicon.ico':
                self.reply(204, b'')
            elif self.path == '/descriptor.bin':
                self.reply(200, peer.descriptor.read_bytes())
            elif self.path == '/schema-header.bin':
                self.reply(200, peer.descriptor.read_bytes()[:64])
            elif self.path == '/demo/state':
                self.reply(200, peer.line('state').encode('utf8'), 'application/json')
            else:
                super().do_GET()

        def do_POST(self):
            length = self.headers.get('Content-Length', '')
            if not re.fullmatch(r'[0-9]{1,6}', length) or int(length) > 65536:
                self.send_error(400, 'A bounded complete request body is required')
                return
            self.connection.settimeout(20)
            body = self.rfile.read(int(length))
            if len(body) != int(length):
                self.send_error(400, 'Incomplete request body')
                return
            try:
                if self.path == '/demo/connect' and not body:
                    value = json.dumps({'session': peer.connect()}).encode('ascii')
                    self.reply(200, value, 'application/json')
                elif self.path == '/exchange' and body:
                    # The native peer validates the whole TEQ1 request and calls
                    # the existing SDK dispatcher under its admitted session.
                    answer = peer.line(body.hex())
                    if not re.fullmatch(r'(?:[0-9a-f]{2})+', answer):
                        raise RuntimeError('Invalid native reply')
                    response = bytes.fromhex(answer)
                    self.server.exchange_count += 1
                    if self.server.reply_delay_seconds:
                        time.sleep(self.server.reply_delay_seconds)
                    self.reply(200, response)
                else:
                    self.send_error(404, 'Unknown demonstration route')
            except (RuntimeError, OSError, queue.Empty, ValueError):
                self.send_error(502, 'Native demonstration peer failed')

    try:
        server = ThreadingHTTPServer(('127.0.0.1', port), Handler)
    except Exception:
        peer.close()
        raise
    server.daemon_threads = True
    server.peer = peer
    server.exchange_count = 0
    # Qualification can retain replies after actual execution. No retry occurs.
    server.reply_delay_seconds = 0
    return server


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', type=Path, required=True)
    parser.add_argument('--public-root', type=Path, required=True)
    parser.add_argument('--data-dir', type=Path, required=True)
    parser.add_argument('--port', type=int, default=8080)
    args = parser.parse_args()
    if not 0 <= args.port <= 65535:
        parser.error('--port must be between 0 and 65535')
    server = create_server(args.device, args.public_root, args.data_dir, args.port)
    print('Demo only: http://127.0.0.1:' + str(server.server_port), flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        server.peer.close()


if __name__ == '__main__':
    main()
