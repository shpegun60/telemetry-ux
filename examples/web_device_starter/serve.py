#!/usr/bin/env python3
"""Run the shared reference HTTP/WS server with this application's own Model. MIT."""
import argparse
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
# Reuse the maintained reference transport, including its exclusive peer lease.
sys.path.insert(0, str(HERE.parent / 'web_demo'))
from serve_ws import create_server


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepared', required=True, type=Path)
    parser.add_argument('--port', type=int, default=8080)
    args = parser.parse_args()
    if not 0 <= args.port <= 65535:
        parser.error('--port must be between 0 and 65535')
    root = args.prepared.resolve()
    device = root / 'native' / ('telemetry_device_starter.exe' if sys.platform == 'win32' else 'telemetry_device_starter')
    server = create_server(device, root / 'public', root / 'state', args.port)
    print('Own-device host starter: http://127.0.0.1:' + str(server.server_port), flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        server.peer.close()


if __name__ == '__main__':
    main()
