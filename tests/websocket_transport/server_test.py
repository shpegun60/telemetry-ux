#!/usr/bin/env python3
"""Real WebSocket/HTTP boundaries with fake native I/O, not SDK operation proof. MIT."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import queue
import socket
import sys
import threading
import time
from unittest.mock import patch
from urllib.error import HTTPError
from urllib.request import Request, urlopen

from websockets.exceptions import ConnectionClosed, InvalidHandshake
from websockets.sync.client import connect

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'examples/web_demo'))
import serve as http_demo
import serve_ws


class FakeProcess:
    """Only pipe replies/counters are fake; lease/access code is NativePeer itself."""
    def __init__(self, peer):
        self.peer = peer
        self.stdin = self
        self.stdout = io.StringIO()
        self.returncode = None
        self.pending = ''
        self.exchanges = []
        self.native_session = 0
        self.block_next = False
        self.pending_physical = False
        self.defer_reply = False
        self.started = threading.Event()
        self.release = threading.Event()

    def write(self, text):
        self.pending = text.rstrip('\n')

    def flush(self):
        text = self.pending
        if text.startswith('session '):
            self.native_session = int(text[8:])
            answer = json.dumps({'session': self.native_session})
        elif text == 'state':
            answer = json.dumps({'session': self.native_session, 'calls': len(self.exchanges)})
        else:
            self.exchanges.append(text)
            if self.defer_reply:
                self.pending_physical = True
                return
            if self.block_next:
                self.block_next = False
                self.started.set()
                if not self.release.wait(timeout=10):
                    raise RuntimeError('Fake native I/O was not released')
            answer = text
        self.peer.lines.put(answer)

    def poll(self):
        return self.returncode

    def close(self):
        self.returncode = 0
        self.pending_physical = False

    def wait(self, timeout):
        return self.returncode

    def terminate(self):
        self.returncode = 0


def fake_peer(_device, directory, *, leases=False):
    peer = http_demo.NativePeer.__new__(http_demo.NativePeer)
    peer.lock = threading.RLock()
    peer.leases, peer._lease_owner, peer._closing, peer._io_uncertain = leases, None, False, False
    peer.session = 16
    peer.lines = queue.Queue()
    peer.descriptor = Path(directory) / 'descriptor.bin'
    peer.descriptor.parent.mkdir(parents=True, exist_ok=True)
    peer.descriptor.write_bytes(bytes(range(64)))
    peer.process = FakeProcess(peer)
    peer.reader = threading.Thread(target=lambda: None)
    peer.reader.start()
    peer.log = io.StringIO()
    return peer


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists() or ROOT == output or ROOT in output.parents:
        parser.error('--output must be new and outside the checkout')
    output.parent.mkdir(parents=True, exist_ok=True)
    paths = [ROOT/'examples/web_demo/serve.py', ROOT/'examples/web_demo/serve_ws.py', Path(__file__).resolve()]
    hashes = lambda: {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    report = dict(passed=False, checks=0, hardware=False,
                  scope='Actual NativePeer admission/lease functions, fake native pipe execution, real WebSocket/HTTP clients',
                  source_sha256_before=hashes(), python=sys.version,
                  websockets_version=serve_ws.websockets.__version__)
    failures = []

    def check(value, label):
        report['checks'] += 1
        if not value:
            failures.append(label)
            raise AssertionError(label)

    def until(predicate, label):
        deadline = time.monotonic() + 5
        while not predicate() and time.monotonic() < deadline:
            time.sleep(0.01)
        check(predicate(), label)

    # Preserve the class while replacing only its constructor for fake pipe setup.
    native_class = http_demo.NativePeer
    def factory(*values, **options):
        with patch.object(http_demo, 'NativePeer', native_class):
            return fake_peer(*values, **options)

    def http(server, path, body=None):
        request = Request('http://127.0.0.1:'+str(server.server_port)+path,
                          data=body, method='GET' if body is None else 'POST')
        try:
            with urlopen(request, timeout=3) as response:
                return response.status, response.read()
        except HTTPError as error:
            return error.code, error.read()

    server = thread = None
    try:
        public = output.parent / (output.stem+'-public')
        public.mkdir()
        (public/'index.html').write_text('Demonstration fixture')
        with patch.object(http_demo, 'NativePeer', factory):
            server = serve_ws.create_server(None, public, output.parent/(output.stem+'-peer'))
        thread = threading.Thread(target=server.serve_forever)
        thread.start()
        code, data = http(server, '/demo/transport')
        info = json.loads(data)
        check(code == 200 and info == server.http.transport, 'Actual advertised transport')
        check(info['kind'] == 'websocket' and info['protocol'] == serve_ws.PROTOCOL, 'Protocol admission')
        check(info['url'].startswith('ws://127.0.0.1:') and info['url'].endswith('/encoded'), 'Actual loopback listener')
        origin = 'http://127.0.0.1:'+str(server.server_port)

        def ws(url=None, selected_origin=origin, protocols=None):
            return connect(url or info['url'], origin=selected_origin,
                           subprotocols=[serve_ws.PROTOCOL] if protocols is None else protocols,
                           compression=None, open_timeout=3, close_timeout=1, proxy=None,
                           max_size=65536, max_queue=1)

        def closed(connection, label, expected=None):
            try:
                connection.recv(timeout=3)
            except ConnectionClosed as error:
                check(expected is None or (error.rcvd is not None and error.rcvd.code == expected), label)
            else:
                check(False, label)

        def greet(connection):
            text = connection.recv(timeout=3)
            check(isinstance(text, str) and len(text) <= 32, 'Bounded text greeting')
            hello = json.loads(text)
            check(list(hello) == ['session'] and type(hello['session']) is int and
                  0 < hello['session'] <= 0xffffffff, 'Exact session greeting')
            return hello['session']

        def idle():
            until(lambda: server.peer._lease_owner is None, 'Actual native lease released')

        # No WS lease: the legacy HTTP calls retain their original meaning.
        check(http(server, '/demo/connect', b'')[0] == 200, 'Legacy connect before WS')
        packet = b'T' * 32  # Opaque fake-pipe data; no claim of a valid Telemetry operation.
        check(http(server, '/exchange', packet) == (200, packet), 'Legacy exchange before WS')
        with ws() as first:
            session = greet(first)
            calls = len(server.peer.process.exchanges)
            check(http(server, '/demo/connect', b'')[0] == 409, 'HTTP cannot retag WS session')
            check(http(server, '/exchange', packet)[0] == 409, 'HTTP cannot execute under WS lease')
            check(server.peer.process.native_session == session and
                  len(server.peer.process.exchanges) == calls, 'HTTP refusals have no native side effect')
            check(json.loads(http(server, '/demo/state')[1]) == {'session': session, 'calls': calls},
                  'Idle leased read-only diagnostic')
            with ws() as second:
                closed(second, 'Second WS owner refused', 1013)
            first.send(packet)
            check(first.recv(timeout=3) == packet, 'One binary message forwarded unchanged')
            large = b'B' * 65536
            first.send(large)
            check(first.recv(timeout=3) == large, 'Exact maximum binary message forwarded')
            check(len(server.peer.process.exchanges) == calls + 2, 'Exactly two fake native dispatches')
        idle()

        for url, selected_origin, protocols, label in (
                (info['url'], 'http://unrelated.invalid', None, 'Wrong Origin'),
                (info['url'], None, None, 'Missing Origin'),
                (info['url'], info['url'].replace('ws:', 'http:').removesuffix('/encoded'), None, 'WS port is not page Origin'),
                (info['url']+'?other', origin, None, 'Exact WS path'),
                (info['url'], origin, ['unrelated'], 'Wrong subprotocol'),
                (info['url'], origin, [], 'Empty subprotocol header')):
            before = server.peer.session
            try:
                with ws(url, selected_origin, protocols):
                    check(False, label+' accepted')
            except InvalidHandshake:
                check(server.peer.session == before, label+' refused before native admission')
        before = server.peer.session
        try:
            with connect(info['url'], origin=origin, subprotocols=None, compression=None,
                         open_timeout=3, close_timeout=1, proxy=None):
                check(False, 'Absent subprotocol accepted')
        except InvalidHandshake:
            check(server.peer.session == before, 'Absent subprotocol refused before native admission')

        for value, code, label in [('text', 1003, 'Text data'), (b'X'*31, 1003, 'Short frame'),
                                   (b'X'*65537, 1009, 'Oversized message')]:
            with ws() as client:
                greet(client)
                before = len(server.peer.process.exchanges)
                client.send(value)
                closed(client, label+' refused', code)
                check(len(server.peer.process.exchanges) == before, label+' has no fake dispatch')
            idle()

        server.drop_reply_once = True
        with ws() as client:
            old_session = greet(client)
            before = len(server.peer.process.exchanges)
            client.send(packet)
            closed(client, 'Executed fake I/O with discarded response', 1011)
        idle()
        with ws() as client:
            check(greet(client) > old_session, 'Reconnect has a fresh trusted session')
            check(len(server.peer.process.exchanges) == before+1, 'No replay after reply loss')
        idle()

        # Real NativePeer lock remains held by a synchronous pipe call after WS close.
        with ws() as client:
            greet(client)
            token = server.peer._lease_owner
            process = server.peer.process
            process.block_next = True
            process.started.clear()
            process.release.clear()
            before = len(process.exchanges)
            client.send(packet)
            check(process.started.wait(timeout=3), 'Fake native work entered actual peer lock')
            client.close()
            check(server.peer._lease_owner is token, 'Socket close cannot release running native work')
            started = time.monotonic()
            check(http(server, '/demo/state')[0] == 409, 'Busy diagnostic is not queued')
            check(http(server, '/demo/connect', b'')[0] == 409, 'Busy HTTP connect is not queued')
            check(time.monotonic()-started < 2, 'Busy legacy calls refuse before fake native release')
            with ws() as other:
                closed(other, 'Busy native work prevents takeover', 1013)
            process.release.set()
        idle()
        check(len(process.exchanges) == before+1, 'Closed native operation executed only once')
        try:
            server.peer.line(packet.hex(), token)
            check(False, 'Retired token accepted')
        except http_demo.PeerBusy:
            check(len(process.exchanges) == before+1, 'Captured stale token cannot call native pipe')

        # A legacy call already holding the real lock must also defeat WS admission.
        process.block_next = True
        process.started.clear()
        process.release.clear()
        call_errors = []
        def legacy_call():
            try:
                server.peer.line(packet.hex())
            except Exception as error:
                call_errors.append(str(error))
        worker = threading.Thread(target=legacy_call)
        worker.start()
        check(process.started.wait(timeout=3), 'Legacy native work entered')
        before_session = server.peer.session
        with ws() as other:
            closed(other, 'WS claim refuses an executing legacy call', 1013)
        check(server.peer.session == before_session, 'Busy claim cannot increment session')
        process.release.set()
        worker.join(timeout=3)
        check(not worker.is_alive() and not call_errors, 'Original legacy call finishes normally')

        # A post-write timeout doesn't physically stop native work. Exercise the
        # actual NativePeer functions with an immediate deterministic queue fault.
        poisoned = fake_peer(None, output.parent/(output.stem+'-uncertain'), leases=True)
        owner = object()
        old = poisoned.claim(owner)
        poisoned.process.defer_reply = True
        with patch.object(poisoned.lines, 'get', side_effect=queue.Empty):
            try:
                poisoned.line(packet.hex(), owner)
                check(False, 'Post-write timeout was ignored')
            except queue.Empty:
                check(poisoned.process.pending_physical, 'Fake native work remains physically pending')
        check(poisoned._closing and poisoned._io_uncertain, 'Ambiguous I/O permanently revokes admission')
        check(not poisoned.release(owner) and poisoned._lease_owner is owner,
              'Timeout cannot release the uncertain native lease')
        before_calls = list(poisoned.process.exchanges)
        for operation in (lambda: poisoned.claim(object()), lambda: poisoned.connect(),
                          lambda: poisoned.line(packet.hex()),
                          lambda: poisoned.line('state', diagnostic=True)):
            try:
                operation()
                check(False, 'Uncertain native I/O was reused')
            except RuntimeError:
                check(poisoned.process.native_session == old and
                      poisoned.process.exchanges == before_calls,
                      'No new session or exchange can overtake an uncertain reply')
        check(poisoned.close() == 0 and poisoned._lease_owner is None and
              not poisoned.process.pending_physical, 'Only actual owned-process exit clears uncertain lease')

        before = len(process.exchanges)
        server.peer.session = 0xffffffff
        with ws() as client:
            closed(client, 'Session exhaustion refuses instead of wrapping', 1011)
        idle()
        check(server.peer.session == 0xffffffff and len(process.exchanges) == before,
              'Session exhaustion leaves native execution unchanged')

        # Include a partial opening handshake in tracked, bounded shutdown.
        host, port = '127.0.0.1', int(info['url'].split(':')[2].split('/')[0])
        raw = socket.create_connection((host, port), timeout=3)
        try:
            until(lambda: bool(server._workers), 'Opening handshake worker tracked')
            started = time.monotonic()
            server.shutdown()
            try:
                server.peer.line(packet.hex())
                check(False, 'Shutdown accepted new native work')
            except RuntimeError:
                check(len(process.exchanges) == before, 'Shutdown revokes late HTTP/native entry')
            server.server_close()
            thread.join(timeout=3)
            check(not thread.is_alive() and not server._workers and
                  time.monotonic()-started < 12, 'Bounded listener and tracked-handshake cleanup')
            check(server.peer._lease_owner is None, 'Shutdown retains no native lease')
        finally:
            raw.close()
        check(server.peer.close() == 0, 'Actual NativePeer close handles fake pipe cleanup')
        check(server.peer._closing and process.returncode == 0, 'Peer is closed only after worker drain')
        server = None
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        report['failures'] = failures
    finally:
        if server is not None:
            server.peer.process.release.set()
            server.shutdown()
            server.server_close()
            if thread is not None:
                thread.join(timeout=3)
            server.peer.close()
        report['source_sha256_after'] = hashes()
        if report['source_sha256_before'] != report['source_sha256_after']:
            report.update(passed=False, error='Host test inputs changed')
        output.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
