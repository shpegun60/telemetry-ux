#!/usr/bin/env python3
"""Qualify packed Session -> UX without source imports or copied native protocol. MIT."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inventory(root):
    return {p.relative_to(root).as_posix():digest(p) for p in sorted(root.rglob('*'))
            if p.is_file() and '__pycache__' not in p.parts}


def sources():
    paths=[ROOT/'package.json', ROOT/'tests/web_configurator/Provider.mjs']
    for directory in (ROOT/'web', ROOT/'examples/web_demo', ROOT/'examples/session_web', HERE):
        paths.extend(p for p in directory.rglob('*') if p.is_file() and '__pycache__' not in p.parts)
    return {p.relative_to(ROOT).as_posix():digest(p) for p in sorted(paths) if p.is_file()}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',required=True,type=Path)
    parser.add_argument('--sdk-prefix',required=True,type=Path)
    parser.add_argument('--sdk-package',type=Path)
    parser.add_argument('--sdk-root',type=Path,default=ROOT.parent/'telemetry-sdk')
    parser.add_argument('--codegen-python',required=True)
    parser.add_argument('--browser-python',required=True)
    parser.add_argument('--node',required=True)
    parser.add_argument('--tsc',required=True,type=Path)
    parser.add_argument('--npm-js',required=True,type=Path)
    parser.add_argument('--cxx',default='g++')
    parser.add_argument('--cmake',default='cmake')
    parser.add_argument('--ninja',default='ninja')
    parser.add_argument('--sanitize',action='store_true')
    args=parser.parse_args()
    output=args.build_dir.resolve()
    sdk=args.sdk_prefix.resolve()
    if any(output==p or p in output.parents or output in p.parents for p in (ROOT,sdk,args.sdk_root.resolve())):
        parser.error('Build directory must be outside source and installed SDK')
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        if output.is_dir() and (output/'summary.json').is_file():
            old=json.loads((output/'summary.json').read_text(encoding='utf8'))
            old.update(passed=False,error='Refused nonempty output reuse; previous success is stale')
            (output/'summary.json').write_text(json.dumps(old,indent=2)+'\n',encoding='utf8')
        parser.error('Build directory must be fresh and empty; nothing is deleted')
    output.mkdir(parents=True)
    environment=dict(os.environ,PYTHONDONTWRITEBYTECODE='1',PYTHONNOUSERSITE='1',npm_config_update_notifier='false')
    environment.pop('PYTHONPATH',None)
    report=dict(passed=False,hardware=False,checks=0,commands=[],started_utc=datetime.now(timezone.utc).isoformat(),source_sha256_before=sources())
    (output/'summary.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')

    def check(condition,message):
        report['checks']+=1
        if not condition: raise RuntimeError(message)

    def run(name,command,cwd=output):
        start=time.monotonic()
        result=subprocess.run(list(map(str,command)),cwd=cwd,env=environment,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=900)
        log=output/(name+'.log'); log.write_text(result.stdout,encoding='utf8')
        report['commands'].append(dict(name=name,command=list(map(str,command)),exit=result.returncode,seconds=time.monotonic()-start,log=log.name,sha256=digest(log)))
        print(name+': exit '+str(result.returncode),flush=True)
        check(result.returncode==0,name+' failed\n'+result.stdout[-12000:])
        return result.stdout

    try:
        tools={}
        for name in ('node','codegen_python','browser_python','cxx','cmake','ninja'):
            path=Path(shutil.which(str(getattr(args,name))) or getattr(args,name)).absolute()
            check(path.is_file(),'Missing tool '+str(path)); tools[name]=path
        environment['PATH']=os.pathsep.join((str(tools['node'].parent),str(tools['cxx'].parent),environment.get('PATH','')))
        if args.sanitize:
            environment['ASAN_OPTIONS']='detect_leaks=1:halt_on_error=1'
            environment['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
        report['tools']={name:dict(path=str(path),sha256=digest(path)) for name,path in tools.items()}
        check(args.npm_js.is_file() and args.tsc.is_file(),'Missing npm or TypeScript')
        configs=list(sdk.rglob('TelemetryConfig.cmake'))
        check(len(configs)==1,'Require one installed SDK CMake package')
        report['installed_cpp_sha256_before']=inventory(sdk)
        configure=[tools['cmake'],'-S',ROOT/'examples/web_demo/native','-B',output/'native','-G','Ninja',
            '-DCMAKE_PREFIX_PATH='+str(sdk),'-DTelemetry_DIR='+str(configs[0].parent),
            '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF','-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF',
            '-DCMAKE_CXX_COMPILER='+str(tools['cxx']),'-DCMAKE_MAKE_PROGRAM='+str(tools['ninja']),'-DCMAKE_BUILD_TYPE=Release']
        if args.sanitize:
            configure+=['-DCMAKE_CXX_FLAGS_RELEASE=-O1 -g -DNDEBUG -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer']
        run('native-configure',configure)
        run('native-build',[tools['cmake'],'--build',output/'native','--parallel','2'])
        device=output/'native'/('telemetry_web_demo_peer.exe' if os.name=='nt' else 'telemetry_web_demo_peer')
        check(device.is_file(),'Missing native peer')
        run('native-descriptor',[device,'--descriptor',output/'descriptor.bin'])
        public=output/'public'; public.mkdir()
        (public/'package.json').write_text('{"private":true,"type":"module"}\n',encoding='utf8')
        run('installed-codegen-identity',[tools['codegen_python'],'-c',
            'import json,tools.telemetry_codegen as c; print(json.dumps(dict(version=c.__version__,path=c.__file__)))'])
        identity=json.loads((output/'installed-codegen-identity.log').read_text(encoding='utf8'))
        check(identity['version']=='0.1.0' and 'site-packages' in Path(identity['path']).parts,'Require matching installed codegen')
        report['codegen']=identity
        run('generate',[tools['codegen_python'],'-m','tools.telemetry_codegen','generate',output/'descriptor.bin',
            '--targets','js','--structural-metadata','--wire-editing','--namespace','web_demo_api','--output',public/'generated'])
        dist=output/'dist'; dist.mkdir()
        def pack(name,root):
            packed=json.loads(run('pack-'+name,[tools['node'],args.npm_js,'pack','--json','--ignore-scripts',
                '--pack-destination',dist,'--cache',output/'npm-cache'],cwd=root))
            check(len(packed)==1 and packed[0]['name']=='telemetry-'+name,'Unexpected '+name+' package')
            return dist/packed[0]['filename']
        sdk_package=args.sdk_package.resolve() if args.sdk_package else pack('sdk',args.sdk_root.resolve())
        ux_package=pack('ux',ROOT)
        report['packages']={name:dict(path=str(path),sha256=digest(path)) for name,path in [('sdk',sdk_package),('ux',ux_package)]}
        run('install',[tools['node'],args.npm_js,'install','--offline','--ignore-scripts','--no-audit','--no-fund',
            '--package-lock=false','--cache',output/'npm-cache','--prefix',public,sdk_package,ux_package])
        report['installed_npm_sha256_before']={name:inventory(public/'node_modules'/name) for name in ('telemetry-sdk','telemetry-ux')}
        for name in ('Consumer.ts','Provider.mjs','App.js','index.html'):
            shutil.copy2(HERE/name,public/name)
        shutil.copy2(ROOT/'examples/session_web/index.html',public/'demo.html')
        shutil.copy2(ROOT/'examples/session_web/SessionDemo.js',public/'SessionDemo.js')
        shutil.copy2(output/'descriptor.bin',public/'descriptor.bin')
        run('strict-installed-types',[tools['node'],args.tsc,'--strict','--noEmit','--target','ES2022',
            '--module','NodeNext','--moduleResolution','NodeNext',public/'Consumer.ts'])
        provider=json.loads(run('installed-session-providers',[tools['node'],public/'Provider.mjs',device]).strip().splitlines()[-1])
        check(provider.get('passed') and provider.get('checks',0)>=697 and provider.get('scenarios',0)>=33,'Provider assertion floor')
        report['providers']=provider
        shutil.copy2(ROOT/'tests/web_configurator/Provider.mjs',public/'LegacyProvider.mjs')
        legacy=json.loads(run('unchanged-low-level-providers',[tools['node'],public/'LegacyProvider.mjs',
            '--device',device,'--descriptor',output/'descriptor.bin','--generated',public/'generated/js']).strip().splitlines()[-1])
        check(legacy.get('passed') and legacy.get('checks',0)>=1305,'Existing low-level provider regression')
        report['legacy_providers']=legacy
        run('chromium-real-websocket',[tools['browser_python'],'-B',HERE/'Browser.py','--device',device,
            '--public-root',public,'--data-dir',output/'browser'])
        browser=json.loads((output/'browser/summary.json').read_text(encoding='utf8'))
        check(browser.get('passed') and set(browser.get('modes',{}))=={'static','dynamic'} and browser.get('checks',0)>=134,'Both browser modes and assertion floor')
        report['browser']=browser
        report['source_sha256_after']=sources()
        report['installed_cpp_sha256_after']=inventory(sdk)
        report['installed_npm_sha256_after']={name:inventory(public/'node_modules'/name) for name in ('telemetry-sdk','telemetry-ux')}
        check(report['source_sha256_before']==report['source_sha256_after'],'Source input drift')
        check(report['installed_cpp_sha256_before']==report['installed_cpp_sha256_after'],'Installed C++ SDK drift')
        check(report['installed_npm_sha256_before']==report['installed_npm_sha256_after'],'Installed npm input drift')
        for name,path in [('sdk',sdk_package),('ux',ux_package)]: check(digest(path)==report['packages'][name]['sha256'],'Package input drift')
        report['device']=dict(path=str(device),sha256=digest(device))
        report['checks']+=provider['checks']+browser['checks']+legacy['checks']
        report['passed']=True
    except Exception as error:
        report['error']=str(error)
        raise
    finally:
        report['finished_utc']=datetime.now(timezone.utc).isoformat()
        (output/'summary.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')


if __name__=='__main__':
    main()
