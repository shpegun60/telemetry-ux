#!/usr/bin/env python3
"""Run the shared reference HTTP/WS server with this application's own Model. MIT."""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import sys

HERE = Path(__file__).resolve().parent
# Reuse the maintained reference transport, including its exclusive peer lease.
sys.path.insert(0, str(HERE.parent / 'web_demo'))
from serve_ws import create_server


def verified_export(root):
    """Verify the recorded deploy files using the exporter's own path rules."""
    exporter = runpy.run_path(str(HERE.parents[1] / 'tools/export_web_assets.py'))
    public = root / 'www'
    for path in (public, root / 'prepared.json', public / 'assets.json'):
        exporter['no_links'](path)
    prepared = json.loads((root / 'prepared.json').read_text(encoding='utf8'))
    receipt = prepared.get('web_export', {})
    manifest_bytes = (public / 'assets.json').read_bytes()
    if (prepared.get('passed') is not True or receipt.get('directory') != 'www' or
            hashlib.sha256(manifest_bytes).hexdigest() != receipt.get('manifest_sha256')):
        raise ValueError('Successful preparation and its unchanged www/assets.json are required')
    manifest = json.loads(manifest_bytes)
    assets = manifest.get('assets')
    if (manifest.get('format') != 'telemetry-ux-web-assets-v1' or not isinstance(assets, list) or
            len(assets) != receipt.get('asset_count')):
        raise ValueError('Exported manifest does not match the preparation receipt')
    total = 0
    seen = set()
    for asset in assets:
        path, size = asset['path'], asset['bytes']
        if not isinstance(path, str) or not path.startswith('/') or type(size) is not int or size < 0:
            raise ValueError('Invalid exported asset path or length')
        name = exporter['asset_name'](path[1:])
        if name.casefold() in seen:
            raise ValueError('Duplicate exported asset: ' + name)
        seen.add(name.casefold())
        payload = exporter['read_file'](public, name, size)
        if len(payload) != size or hashlib.sha256(payload).hexdigest() != asset['sha256']:
            raise ValueError('Exported asset changed: ' + name)
        total += len(payload)
    if total != manifest.get('totalBytes') or total != receipt.get('total_bytes'):
        raise ValueError('Exported runtime byte count does not match the preparation receipt')
    return public


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepared', required=True, type=Path)
    parser.add_argument('--port', type=int, default=8080)
    parser.add_argument('--exported', action='store_true',
                        help='Serve only prepared/www deploy assets instead of the development public tree')
    args = parser.parse_args()
    if not 0 <= args.port <= 65535:
        parser.error('--port must be between 0 and 65535')
    root = args.prepared.resolve()
    public = root / ('www' if args.exported else 'public')
    if args.exported:
        try:
            public = verified_export(root)
        except (OSError, ValueError, AttributeError, TypeError, KeyError) as error:
            parser.error('--exported asset check failed: ' + str(error))
    device = root / 'native' / ('telemetry_device_starter.exe' if sys.platform == 'win32' else 'telemetry_device_starter')
    server = create_server(device, public, root / 'state', args.port)
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
