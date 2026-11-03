#!/usr/bin/env python3
"""Local HTTP assets/bootstrap and one persistent binary Encoded WebSocket lease.

The native SDK dispatcher owns Telemetry bytes. Only the initial session greeting
is application JSON. Requires websockets==15.0.1; this is a host demonstration,
not a firmware server or an Internet deployment. SPDX-License-Identifier: MIT.
"""
import argparse
from http import HTTPStatus
import json
from pathlib import Path
import queue
import re
import threading
import time

import websockets
from websockets.exceptions import ConnectionClosed
from websockets.sync.server import serve

import serve as http_demo

PROTOCOL = 'telemetry.encoded.v1'
FRAME_LIMIT = 65536


class WebSocketDemo:
    def __init__(self, device, public_root, data_dir, port=0):
        if websockets.__version__ != '15.0.1':
            raise RuntimeError('This example requires websockets==15.0.1')
        self.http = http_demo.create_server(device, public_root, data_dir, port, leases=True)
        self.peer = self.http.peer
        self.server_port = self.http.server_port
        self.reply_delay_seconds = 0
        self.drop_reply_once = False
        self._condition = threading.Condition()
        self._workers = set()
        self._connection = None
        self._stopping = False
        self._serving = False
        self._ws_thread = None
        origin = 'http://127.0.0.1:' + str(self.server_port)
        try:
            self.websocket = serve(
                self._handle, '127.0.0.1', 0, origins=[origin], subprotocols=[PROTOCOL],
                compression=None, process_request=self._request,
                open_timeout=5, close_timeout=2, ping_interval=20, ping_timeout=20,
                max_size=FRAME_LIMIT, max_queue=1)
        except BaseException:
            self.http.server_close()
            self.peer.close()
            raise
        # The pinned synchronous Server otherwise stops only its listening socket.
        # Track its full connection handler, including bounded opening handshakes,
        # so application cleanup never closes the native process under a worker.
        connection_handler = self.websocket.handler

        def tracked(socket, address):
            worker = threading.current_thread()
            with self._condition:
                self._workers.add(worker)
            try:
                connection_handler(socket, address)
            finally:
                with self._condition:
                    self._workers.remove(worker)
                    self._condition.notify_all()

        self.websocket.handler = tracked
        ws_port = self.websocket.socket.getsockname()[1]
        self.http.transport = dict(kind='websocket',
                                   url='ws://127.0.0.1:' + str(ws_port) + '/encoded',
                                   protocol=PROTOCOL)

    @property
    def exchange_count(self):
        return self.http.exchange_count

    def _request(self, connection, request):
        if request.path != '/encoded':
            return connection.respond(HTTPStatus.NOT_FOUND, 'Unknown demonstration path\n')
        with self._condition:
            if self._stopping:
                return connection.respond(HTTPStatus.SERVICE_UNAVAILABLE, 'Demonstration closing\n')
        return None

    def _greet(self, connection, session):
        connection.send(json.dumps({'session': session}, separators=(',', ':')))

    def _handle(self, connection):
        token = object()
        claimed = False
        try:
            with self._condition:
                stopping = self._stopping
            if stopping:
                connection.close(1001, 'Demonstration closing')
                return
            # claim() is nonblocking and serializes trusted admission with
            # every legacy HTTP connect/exchange under the same peer lock.
            try:
                session = self.peer.claim(token)
            except http_demo.PeerBusy:
                connection.close(1013, 'Native demonstration peer busy')
                return
            claimed = True
            with self._condition:
                stopping = self._stopping
                if not stopping:
                    self._connection = connection
            if stopping:
                connection.close(1001, 'Demonstration closing')
                return
            self._greet(connection, session)
            for packet in connection:
                if not isinstance(packet, bytes) or not 32 <= len(packet) <= FRAME_LIMIT:
                    connection.close(1003, 'One bounded binary TEQ1 frame is required')
                    return
                # line() rechecks this exact owner token atomically before I/O.
                # It returns only after actual native work and owns its reply.
                answer = self.peer.line(packet.hex(), token)
                if not re.fullmatch(r'(?:[0-9a-f]{2}){32,65536}', answer):
                    raise RuntimeError('Invalid native reply')
                response = bytes.fromhex(answer)
                self.http.exchange_count += 1
                if self.reply_delay_seconds:
                    time.sleep(self.reply_delay_seconds)
                if self.drop_reply_once:
                    self.drop_reply_once = False
                    connection.close(1011, 'Demonstration reply discarded')
                    return
                connection.send(response)
        except ConnectionClosed:
            pass
        except (RuntimeError, OSError, queue.Empty, ValueError):
            connection.close(1011, 'Native demonstration peer failed')
        finally:
            if claimed:
                # A local socket close did not cancel synchronous native work.
                # This point is reached only after that work actually returned.
                self.peer.release(token)
                with self._condition:
                    if self._connection is connection:
                        self._connection = None
                    self._condition.notify_all()

    def serve_forever(self):
        with self._condition:
            if self._stopping or self._serving:
                raise RuntimeError('The demonstration can be started only once')
            self._serving = True
        self._ws_thread = threading.Thread(target=self.websocket.serve_forever,
                                           name='telemetry-demo-ws-listener')
        self._ws_thread.start()
        try:
            self.http.serve_forever()
        finally:
            with self._condition:
                self._serving = False
            self.shutdown()

    def shutdown(self):
        with self._condition:
            first = not self._stopping
            self._stopping = True
            self.peer.stop_admission()
            connection = self._connection
            serving = self._serving
        if first:
            self.websocket.shutdown()
        if connection is not None:
            connection.close(1001, 'Demonstration closing')
        if serving:
            self.http.shutdown()

    def server_close(self):
        self.shutdown()
        self.http.server_close()
        if self._ws_thread is not None:
            self._ws_thread.join(timeout=5)
            if self._ws_thread.is_alive():
                raise RuntimeError('The WebSocket listener did not stop')
        deadline = time.monotonic() + 30
        with self._condition:
            while self._workers:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise RuntimeError('Connection workers still own native I/O; retain the peer')
                self._condition.wait(remaining)


def create_server(device, public_root, data_dir, port=0):
    """Unstarted dual-listener demo; close/drain server before closing its peer."""
    return WebSocketDemo(device, public_root, data_dir, port)


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
