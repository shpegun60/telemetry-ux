#!/usr/bin/env python3
"""Check optional generated page boundaries using public immutable SDK IR (MIT)."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk-root', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--node', default='node')
    args = parser.parse_args()
    sdk, output = args.sdk_root.resolve(), args.build_dir.resolve()
    if any(output == path or path in output.parents or output in path.parents
           for path in (ROOT, sdk)):
        parser.error('Use a build directory outside both repositories')
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        parser.error('Build directory must be fresh and empty; nothing is deleted')
    node = Path(shutil.which(args.node) or args.node).absolute()
    if not node.is_file():
        parser.error('Node executable not found')
    fixtures = [sdk / 'tests/structured/descriptor' / (name + '.hex')
                for name in ('empty', 'mixed')]
    files = [ROOT / 'python/telemetry_ux_codegen' / name for name in
             ('__init__.py', 'starter.py')] + sorted(HERE.glob('*')) + fixtures
    files += [sdk / 'tools/telemetry_codegen' / name for name in
              ('emitter_api.py', 'schema.py', 'parser.py', 'naming.py')]
    files = [path for path in files if path.is_file()]
    before = {str(path): sha(path) for path in files}
    output.mkdir(parents=True, exist_ok=True)
    report = dict(passed=False, hardware=False, checks=0,
                  started_utc=datetime.now(timezone.utc).isoformat(),
                  source_sha256_before=before)

    def check(condition, message):
        report['checks'] += 1
        if not condition:
            raise RuntimeError(message)

    try:
        sys.path[:0] = [str(ROOT / 'python'), str(sdk)]
        from tools.telemetry_codegen.emitter_api import GeneratedTarget, Schema
        from tools.telemetry_codegen.schema import load_schema
        from telemetry_ux_codegen import generate_web_starter
        check(not any(name.startswith('telemetry_ux_codegen.qml') for name in sys.modules),
              'Optional web hook imported QML')
        for fixture in fixtures:
            schema = load_schema(bytes.fromhex(fixture.read_text()), namespace='web_starter')
            check(isinstance(schema, Schema), 'Fixture did not use the public SDK IR')
            target = generate_web_starter(schema)
            check(isinstance(target, GeneratedTarget), 'Wrong extension return contract')
            check(set(target.files) == {'ui/DevicePage.js', 'ui/DevicePage.d.ts', 'ui/README.md'},
                  'Starter must emit only the three thin entry files')
            check(target == generate_web_starter(schema), 'Non-deterministic starter')
            check(target.symbol_mapping == {'namespace': schema.namespace, 'mount': 'mountDevice',
                'provider': 'static', 'lifecycle': 'host-owned'}, 'Wrong symbol receipt')
            check('provider: BatchPresentationProvider;' in target.files['ui/DevicePage.d.ts'],
                  'Generated page must retain the typed Watchlist and Batch provider API')
            page = target.files['ui/DevicePage.js']
            check(all(marker in page for marker in ("'../js/device.js'",
                "'../js/device.metadata.js'", "'telemetry-ux/web'", 'createStaticProvider',
                'mountConfigurator', 'Object.freeze({provider, unmount: view.unmount})')),
                'Page stopped reusing public generated/package exports')
            check(all(marker not in page for marker in ('new DeviceClient', 'new DynamicClient',
                'fetch(', 'Descriptor', 'createElement', 'innerHTML', 'Uint8Array')),
                'Starter duplicated client, transport, parser, or form code')
        check(not any(name.startswith('telemetry_ux_codegen.qml') for name in sys.modules),
              'Web generation imported QML')
        for name, content in target.files.items():
            path = output / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding='utf8')
        (output / 'package.json').write_text('{"type":"module"}', encoding='utf8')
        (output / 'js').mkdir()
        (output / 'js/device.js').write_text('export const fingerprint = 17n;\n'
            'export const codecs = Object.freeze({});\n', encoding='utf8')
        (output / 'js/device.metadata.js').write_text('export const structuralMetadata = '
            'Object.freeze({format:"test-metadata"});\n', encoding='utf8')
        package = output / 'node_modules/telemetry-ux'
        package.mkdir(parents=True)
        (package / 'package.json').write_text('{"type":"module","exports":{"./web":"./index.js"}}',
                                           encoding='utf8')
        (package / 'index.js').write_text('export const calls = [];\n'
            'export const expectedProvider = Object.freeze({provider:true});\n'
            'export function createStaticProvider(options) { calls.push({kind:"provider", options}); '
            'return expectedProvider; }\n'
            'export function unmount() { calls.push({kind:"unmount"}); }\n'
            'export function mountConfigurator(host, options) { calls.push({kind:"mount",host,options}); '
            'return Object.freeze({unmount}); }\n', encoding='utf8')
        shutil.copyfile(HERE / 'Check.mjs', output / 'Check.mjs')
        result = subprocess.run([str(node), str(output / 'Check.mjs')], cwd=output,
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            env=dict(os.environ, NODE_NO_WARNINGS='0'), timeout=30)
        (output / 'node.log').write_text(result.stdout, encoding='utf8')
        report['node'] = dict(path=str(node), sha256=sha(node), exit=result.returncode,
                              log='node.log', log_sha256=sha(output / 'node.log'))
        check(result.returncode == 0, result.stdout)
        check('10 checks passed' in result.stdout, 'Node assertions did not complete')
        report['checks'] += 10
        report['source_sha256_after'] = {str(path): sha(path) for path in files}
        check(before == report['source_sha256_after'], 'Sources changed during the check')
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print('Web starter checks=' + str(report['checks']))


if __name__ == '__main__':
    sys.dont_write_bytecode = True
    main()
