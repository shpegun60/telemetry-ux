"""Local qualification receipts for installed SDK and generator inputs. MIT."""
from pathlib import Path
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def snapshot(paths, base):
    return {path.relative_to(base).as_posix(): digest(path)
            for path in sorted(paths) if path.is_file() and '__pycache__' not in path.parts}


def ux_inputs():
    return snapshot([path for directory in ('tests/codegen_qml', 'examples/codegen_qml')
                     for path in (ROOT / directory).rglob('*')], ROOT)


def sdk_inputs(prefix):
    return snapshot(prefix.rglob('*'), prefix)


def installed_sdk(prefix):
    configs = list(prefix.rglob('TelemetryConfig.cmake'))
    metadata = list(prefix.rglob('SDKMetadata.json'))
    if len(configs) != 1 or len(metadata) != 1:
        raise ValueError('--sdk-prefix needs one installed TelemetryConfig.cmake and SDKMetadata.json')
    identity = json.loads(metadata[0].read_text(encoding='utf-8'))
    if identity.get('sdkVersion') != '0.1.0':
        raise ValueError('This consumer requires installed Telemetry SDK 0.1.0')
    return configs[0].parent, identity


def codegen_identity(python, env, cwd):
    script = '''import hashlib,json
from pathlib import Path
from importlib.metadata import distribution
import tools.telemetry_codegen as core
import telemetry_ux_codegen as ux
result={'core_module':str(Path(core.__file__).resolve()),'ux_module':str(Path(ux.__file__).resolve()),'distributions':{}}
for name in ('telemetry-codegen-core','telemetry-ux-codegen'):
    dist=distribution(name)
    root=Path(dist.locate_file('')).resolve()
    module=Path(result['core_module' if name=='telemetry-codegen-core' else 'ux_module'])
    if root not in module.parents:
        raise RuntimeError('Generator must use installed wheel-owned modules, not an editable/source checkout: '+name)
    files={str(p):hashlib.sha256(dist.locate_file(p).read_bytes()).hexdigest()
           for p in dist.files if dist.locate_file(p).is_file() and (str(p).endswith('.py') or str(p).endswith(('METADATA','entry_points.txt')))}
    if module.relative_to(root).as_posix() not in files:
        raise RuntimeError('The imported module is not owned by the installed distribution: '+name)
    result['distributions'][name]={'version':dist.version,'root':str(root),'files_sha256':files}
print(json.dumps(result,sort_keys=True))
'''
    result = subprocess.run([str(python), '-B', '-c', script], cwd=cwd, env=env,
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
    if result.returncode:
        raise RuntimeError('Installed generator identity failed: ' + result.stderr)
    identity = json.loads(result.stdout)
    for name in ('core_module', 'ux_module'):
        path = Path(identity[name]).resolve()
        if ROOT == path or ROOT in path.parents:
            raise RuntimeError('Generator must be installed outside the UX checkout: ' + name)
    versions = [item['version'] for item in identity['distributions'].values()]
    if versions != ['0.1.0', '0.1.0']:
        raise RuntimeError('Matching installed core and UX codegen 0.1.0 are required')
    return identity


def check_cache(build, config):
    entries = (build / 'CMakeCache.txt').read_text(encoding='utf-8').splitlines()
    selected = [line.split('=', 1)[1] for line in entries
                if line.startswith('Telemetry_DIR:') and '=' in line]
    if len(selected) != 1 or Path(selected[0]).resolve() != config.resolve():
        raise RuntimeError('CMake selected a different installed SDK prefix')


def check_compile_sources(build, output):
    commands = json.loads((build / 'compile_commands.json').read_text(encoding='utf-8'))
    for entry in commands:
        path = Path(entry['file']).resolve()
        if not any(base == path or base in path.parents for base in (ROOT, output)):
            raise RuntimeError('Consumer compiled a source outside UX/generated output: ' + str(path))
