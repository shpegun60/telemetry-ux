#!/usr/bin/env python3
"""Local web demonstration transport to a persistent native Encoded dispatcher.

Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
This example owns HTTP routing. The SDK owns every Telemetry request/reply byte.
It listens only on localhost and deliberately serves one demo session at a time.
"""
import argparse
from contextlib import contextmanager
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import queue
import re
import subprocess
import threading
import time


class PeerBusy(RuntimeError):
    """The optional persistent transport owns this peer or a call is executing."""


class NativePeer:
    def __init__(self, executable, directory, *, leases=False):
        self.executable = Path(executable).resolve()
        self.directory = Path(directory).resolve()
        self.directory.mkdir(parents=True, exist_ok=True)
        self.descriptor = self.directory / 'descriptor.bin'
        subprocess.run([str(self.executable), '--descriptor', str(self.descriptor)],
                       check=True, timeout=30)
        self.lock = threading.RLock()
        self.leases = leases
        self._lease_owner = None
        self._closing = False
        self._io_uncertain = False
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

    @contextmanager
    def _access(self, owner=None, *, diagnostic=False):
        if not self.lock.acquire(blocking=not self.leases):
            raise PeerBusy('Native peer is executing another call')
        try:
            if self._closing:
                raise RuntimeError('Native peer is closing')
            if self.leases and self._lease_owner is not owner and not diagnostic:
                raise PeerBusy('Native peer belongs to another connection')
            yield
        finally:
            self.lock.release()

    def line(self, text, owner=None, *, diagnostic=False):
        if diagnostic and text not in ('state', 'readmany'):
            raise ValueError('Only state and ReadMany capability are read-only native diagnostics')
        with self._access(owner, diagnostic=diagnostic):
            if self.process.poll() is not None:
                self.stop_admission()
                raise RuntimeError('Native peer exited')
            complete = False
            try:
                self.process.stdin.write(text + '\n')
                self.process.stdin.flush()
                answer = self.lines.get(timeout=15)
                if answer is None:
                    raise RuntimeError('Native peer closed its reply stream')
                complete = True
                return answer
            finally:
                if not complete:
                    # A pipe timeout/EOF/write error isn't evidence that native
                    # execution ended. Never relabel a late line as a new reply.
                    self._io_uncertain = True
                    self.stop_admission()

    def connect(self, owner=None):
        with self._access(owner):
            if self.session == 0xffffffff:
                raise RuntimeError('Demo session IDs exhausted; restart the demonstration')
            self.session += 1
            if json.loads(self.line('session ' + str(self.session), owner)) != {'session': self.session}:
                raise RuntimeError('Native peer refused the new demo session')
            return self.session

    def claim(self, owner):
        if not self.leases or owner is None:
            raise ValueError('A persistent connection requires an explicit lease owner')
        with self._access():
            self._lease_owner = owner
            try:
                return self.connect(owner)
            except BaseException:
                if not self._io_uncertain:
                    self._lease_owner = None
                raise

    def release(self, owner):
        # Cleanup waits for any actual native call; no new session can overtake it.
        with self.lock:
            if owner is None or self._lease_owner is not owner:
                return False
            if self._io_uncertain:
                return False
            self._lease_owner = None
            return True

    def stop_admission(self):
        # Logical revocation only. A call already inside the lock still finishes.
        self._closing = True

    def close(self):
        with self.lock:
            self._closing = True
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
            # Owned process exit, not a logical timeout, retires uncertain I/O.
            self._lease_owner = None
            return self.process.returncode


def create_server(device, public_root, data_dir, port=0, *, leases=False):
    """Return an unstarted localhost server; caller owns shutdown and peer close."""
    root = Path(public_root).resolve()
    if not root.is_dir():
        raise ValueError('--public-root must contain the prepared browser application')
    peer = NativePeer(device, data_dir, leases=leases)

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
            except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                # The peer may already have executed a mutation. Never repeat it.
                pass

        def do_GET(self):
            if self.path == '/favicon.ico':
                self.reply(204, b'')
            elif self.path == '/descriptor.bin':
                self.reply(200, peer.descriptor.read_bytes())
            elif self.path == '/schema-header.bin':
                self.reply(200, peer.descriptor.read_bytes()[:64])
            elif self.path == '/demo/transport':
                self.reply(200, json.dumps(self.server.transport).encode('ascii'), 'application/json')
            elif self.path == '/demo/state':
                try:
                    self.reply(200, peer.line('state', diagnostic=True).encode('utf8'), 'application/json')
                except PeerBusy:
                    self.send_error(409, 'Native demonstration peer is busy')
                except (RuntimeError, OSError, queue.Empty, ValueError):
                    self.send_error(502, 'Native demonstration peer failed')
            elif self.path == '/capabilities/read-many.bin':
                try:
                    capability = peer.line('readmany', diagnostic=True)
                    if not re.fullmatch(r'[0-9a-f]{48}', capability):
                        raise ValueError('Invalid native ReadMany capability')
                    self.reply(200, bytes.fromhex(capability))
                except PeerBusy:
                    self.send_error(409, 'Native demonstration peer is busy')
                except (RuntimeError, OSError, queue.Empty, ValueError):
                    self.send_error(502, 'Native demonstration peer failed')
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
            except PeerBusy:
                self.send_error(409, 'Native demonstration peer is busy')
            except (RuntimeError, OSError, queue.Empty, ValueError):
                self.send_error(502, 'Native demonstration peer failed')

    try:
        server = ThreadingHTTPServer(('127.0.0.1', port), Handler)
    except Exception:
        peer.close()
        raise
    server.daemon_threads = True
    server.peer = peer
    server.transport = {'kind': 'http'}
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
