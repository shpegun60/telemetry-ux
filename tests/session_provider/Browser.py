#!/usr/bin/env python3
"""Chromium exercises packed Session mounts through the existing native WebSocket demo. MIT."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import threading
import time
from playwright.sync_api import sync_playwright, expect

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', required=True, type=Path)
    parser.add_argument('--public-root', required=True, type=Path)
    parser.add_argument('--data-dir', required=True, type=Path)
    args = parser.parse_args()
    output = args.data_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(ROOT / 'examples/web_demo'))
    spec = importlib.util.spec_from_file_location('session_provider_ws_demo', ROOT / 'examples/web_demo/serve_ws.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    server = module.WebSocketDemo(args.device, args.public_root, output / 'peer')
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    report = dict(passed=False, hardware=False, checks=0, modes={}, errors=[], warnings=[])

    def check(condition, message):
        report['checks'] += 1
        if not condition:
            raise AssertionError(message)

    def state():
        return json.loads(server.peer.line('state', diagnostic=True))

    try:
        with sync_playwright() as playwright:
            browser = playwright.chromium.launch()
            report['browser'] = dict(name='Chromium', version=browser.version)
            try:
                for mode in ('static', 'dynamic'):
                    start = report['checks']
                    server.peer.line('reset')
                    page = browser.new_page(viewport={'width':1440,'height':1100})
                    page.on('pageerror', lambda error: report['errors'].append(str(error)))
                    page.on('console', lambda msg: report['warnings'].append(msg.text) if msg.type in ('warning','error') else None)
                    page.goto('http://127.0.0.1:' + str(server.server_port) + '/?mode=' + mode)
                    expect(page.locator('body')).to_have_attribute('data-ready','true')
                    a,b,full,command,service = (page.locator('#'+name) for name in ('field-a','field-b','full','command','service'))
                    def action(view,name): return view.locator('[data-action="'+name+'"]')
                    def idle(view): expect(view.locator('[data-role="configurator"]')).to_have_attribute('aria-busy','false')
                    def row(view,source,name): return view.locator('.tux-row[data-source="'+source+'"][data-path=\'["'+name+'"]\'] .tux-value')
                    def edit(view,name,text):
                        field=view.locator('input[data-source="draft"][data-path=\'["'+name+'"]\']')
                        expect(field).to_be_visible(); field.fill(text); return field
                    def outcome(view,operation): return view.locator('[data-role="outcome"][data-operation="'+operation+'"]')
                    snap=lambda name:page.evaluate('name=>suite.snapshot(name)',name)
                    check(page.evaluate('suite.invalidMounts') == [True]*2,'Invalid ownership mounts reject before DOM')
                    check(state()['reads']==0 and state()['writes']==0,'Mounts do not send endpoint traffic')
                    check(page.evaluate('suite.providers.a!==suite.providers.b&&suite.providers.a!==suite.providers.full'),'Session mounts own independent providers')
                    check(snap('full')['selected'] is None,'Full view starts without coupled selection')
                    full.locator('button[data-endpoint]').filter(has_text='ReadOnly').first.click()
                    check(snap('full')['selected']['name']=='ReadOnly' and snap('a')['selected']['name']=='Config','Full selection is independent')
                    check(state()['reads']==0,'Full selection also sends no read')
                    for view in (a,b):
                        action(view,'refresh').click(); idle(view)
                        action(view,'writes').check(); action(view,'beginEdit').click()
                    expect(row(a,'value','large')).to_have_text('18446744073709551615')
                    expect(row(a,'value','negative')).to_have_text('-9223372036854775808')
                    expect(row(a,'value','zero')).to_have_text('-0')
                    expect(row(a,'value','mode')).to_contain_text('-123')
                    edit(b,'gain','6.')
                    overflow=edit(a,'large','18446744073709551616')
                    expect(overflow).to_have_attribute('aria-invalid','true')
                    check(action(a,'apply').is_disabled(),'U64 overflow never submits')
                    edit(a,'large','9007199254740993'); edit(a,'negative','-9223372036854775808')
                    gain=edit(a,'gain','4.'); expect(gain).to_have_value('4.'); gain.fill('25')
                    before=state(); action(a,'apply').click(); idle(a)
                    check(state()['writes']==before['writes']+1 and state()['reads']==before['reads']+1,'One write and explicit readback')
                    check(state()['large']=='9007199254740993' and state()['gain']==10,'Exact U64 and native normalization')
                    expect(row(a,'value','gain')).to_have_text('10')
                    check(snap('b')['dataStale'] and not snap('b')['actions']['apply'],'Sibling stale draft cannot submit')
                    stale=b.locator('input[data-source="draft"][data-path=\'["gain"]\']')
                    expect(stale).to_have_value('6.')
                    check(bool(snap('b')['dataInvalidationReason']),'Stale draft has a reason')
                    expect(b.locator('[data-role="error"]')).to_contain_text('Data changed')
                    check(snap('a')['selected']['name']=='Config' and snap('b')['selected']['name']=='Config','Independent endpoint selections survive invalidation')
                    action(b,'cancelEdit').click(); action(b,'refresh').click(); idle(b)
                    check(not snap('b')['dataStale'],'Explicit refresh recovers readable data')
                    check(page.evaluate('suite.seedNaN()'),'Headless Session initializes a valid NaN')
                    action(a,'refresh').click(); idle(a)
                    expect(row(a,'value','f32')).to_have_text('NaN')
                    expect(row(a,'value','zero')).to_have_text('-0')
                    action(a,'beginEdit').click(); edit(a,'large','18446744073709551615')
                    action(a,'apply').click(); idle(a)
                    check(page.evaluate('suite.hasNaN()'),'Untouched NaN and signed zero survive UI edit through Session')
                    check(state()['large']=='18446744073709551615','NaN UI edit also preserves exact U64 max')
                    action(command,'cancelEdit').click(); action(command,'beginEdit').click()
                    action(command,'writes').check(); action(command,'execute').click(); idle(command)
                    expect(outcome(command,'command')).to_contain_text('Accepted')
                    check('Executed' not in outcome(command,'command').inner_text(),'Accepted is displayed separately')
                    action(service,'cancelEdit').click(); action(service,'beginEdit').click()
                    action(service,'writes').check(); action(service,'call').click(); idle(service)
                    check(snap('service')['hasResponse'],'Inspect publishes the current response')
                    # The same high-level Session now supplies both optional
                    # panels. All UI actions use the existing native FIFO.
                    check(page.locator('#watch [data-role="configurator"]').count()==1 and
                          page.locator('#batch [data-role="configurator"]').count()==1,
                          'Standalone Watchlist and Batch mount directly from Session')
                    for name in ('Wide','Signed','Mode','Unavailable'):
                        full.locator('button[data-endpoint]').filter(has_text=name).first.click()
                        action(full,'addWatch').click()
                    exchanges=server.exchange_count
                    action(full,'refreshWatch').click()
                    expect(full.locator('[data-role="watch-status"]')).to_contain_text('ReadMany complete')
                    idle(full)
                    check(server.exchange_count==exchanges+1,'Session Watchlist sends one native ReadMany, not four reads')
                    watched=full.locator('.tux-watch-table tbody tr')
                    expect(watched).to_have_count(4)
                    expect(watched.nth(0)).to_contain_text('18446744073709551615')
                    expect(watched.nth(1)).to_contain_text('-9223372036854775808')
                    expect(watched.nth(3)).to_contain_text('Unavailable')
                    check(snap('full')['watch']['rows'][3]['text']=='','Unavailable item does not publish a value')
                    action(full,'watch-individual').check()
                    exchanges=server.exchange_count
                    action(full,'refreshWatch').click()
                    expect(full.locator('[data-role="watch-status"]')).to_contain_text('Individual FieldRead complete')
                    idle(full)
                    check(server.exchange_count==exchanges+4,'Individual fallback remains an explicit user choice')
                    action(full,'startWatch').click()
                    expect(action(full,'stopWatch')).to_be_enabled()
                    action(full,'stopWatch').click()
                    expect(action(full,'clearWatch')).to_be_enabled()
                    exchanges=server.exchange_count
                    page.wait_for_timeout(150)
                    check(server.exchange_count==exchanges,'Stopped Session polling does not retain missed ticks')
                    action(full,'clearWatch').click()
                    def add_batch(name,operation):
                        full.locator('button[data-endpoint]').filter(has_text=name).first.click()
                        action(full,'batch-operation').select_option(operation)
                        action(full,'addBatch').click()
                    def batch_text(name,value):
                        control=full.locator('input[data-source="batchDraft"][data-path=\'["'+name+'"]\']')
                        expect(control).to_be_visible();control.fill(value)
                    add_batch('Config','write')
                    batch_text('gain','6.25');batch_text('large','9007199254740993')
                    batch_text('negative','-9223372036854775808')
                    add_batch('Accepted','command')
                    add_batch('Echo','service')
                    batch_text('gain','2.5');batch_text('large','18446744073709551615')
                    add_batch('Wide','read')
                    batch_rows=full.locator('.tux-batch-table tbody tr')
                    expect(batch_rows).to_have_count(4)
                    check(action(full,'executeBatch').is_disabled(),'Session Batch needs its own mutation opt-in')
                    action(full,'writes').check()
                    action(b,'refresh').click();idle(b)
                    action(b,'beginEdit').click();edit(b,'gain','7.')
                    before=state();exchanges=server.exchange_count
                    action(full,'executeBatch').click()
                    expect(full.locator('[data-role="batch-status"]')).to_contain_text('Batch reply received')
                    idle(full)
                    after=state()
                    check(server.exchange_count==exchanges+1,'Mixed Session Batch uses exactly one native exchange')
                    check(all(after[key]==before[key]+1 for key in ('reads','writes','commands','services')),
                          'Each mixed Batch item executes once in this measured request')
                    check(after['gain']==6.25 and after['large']=='9007199254740993','Batch editor preserves exact U64 and request DTO')
                    expect(batch_rows.nth(0)).to_contain_text('Applied')
                    expect(batch_rows.nth(1)).to_contain_text('Accepted')
                    check('Executed' not in batch_rows.nth(1).inner_text(),'Batch Accepted is not reported as Executed')
                    check(snap('b')['dataStale'] and not snap('b')['actions']['apply'],'Accepted Batch mutation invalidates peer draft')
                    expect(b.locator('input[data-source="draft"][data-path=\'["gain"]\']')).to_have_value('7.')
                    batch_rows.nth(2).locator('[data-action="selectBatch"]').click()
                    expect(row(full,'batchResponse','gain')).to_have_text('2.5')
                    expect(row(full,'batchResponse','large')).to_have_text('18446744073709551615')
                    batch_rows.nth(3).locator('[data-action="selectBatch"]').click()
                    expect(full.locator('.tux-row[data-source="batchResponse"] .tux-value')).to_have_text('9007199254740993')
                    page.wait_for_timeout(150)
                    check(server.exchange_count==exchanges+1,'Session Batch never adds a hidden retry or readback')
                    action(b,'cancelEdit').click();action(b,'refresh').click();idle(b)
                    action(full,'clearBatch').click()
                    # Every standalone panel keeps historical checked facts after revocation.
                    action(a,'refresh').click(); idle(a)
                    # Own-category facts stay hidden while a ready provider's
                    # selected endpoint belongs to another category. Switching
                    # selection and synthetic old-control clicks submit no work.
                    categories=(('a',a,'Accepted',2,'read'),
                                ('command',command,'Inspect',3,'command'),
                                ('service',service,'Config',1,'service'))
                    before=state()
                    for name,view,target,category,operation in categories:
                        selected=page.evaluate('''({name,target,category})=>{
                            const provider=suite.providers[name];
                            const endpoint=provider.snapshot().endpoints.find(row=>row.name===target&&row.category===category);
                            return provider.select(endpoint.key);
                        }''',dict(name=name,target=target,category=category))
                        check(selected and view.locator('[data-role="outcome"]').count()==0,
                              name+' hides own history for a current category mismatch')
                        view.locator('button[data-action]').evaluate_all(
                            'buttons=>buttons.forEach(button=>button.dispatchEvent(new Event("click")))')
                    check(state()==before,'Category reselection and retired controls never submit/replay work')
                    page.locator('#disconnect').click(); expect(page.locator('#status')).to_have_text('disconnected')
                    for view,operation in ((a,'read'),(command,'command'),(service,'service')):
                        expect(outcome(view,operation)).to_have_attribute('data-historical','true')
                        expect(outcome(view,operation)).to_contain_text('Historical')
                        operations=view.locator('[data-role="outcome"]').evaluate_all(
                            'nodes=>nodes.map(node=>node.dataset.operation)')
                        allowed=['read','write','readback'] if operation=='read' else [operation]
                        check(bool(operations) and all(item in allowed for item in operations),
                              'Revoked standalone panel exposes only its own historical category')
                    check(not snap('a')['hasValue'] and not snap('service')['hasResponse'],'Revoked values and responses stay hidden')
                    check(a.locator('.tux-row[data-source="value"]').count()==0 and service.locator('.tux-row[data-source="response"]').count()==0,'Standalone DOM hides revoked values')
                    before=state(); page.locator('#reconnect').click(); expect(page.locator('#status')).to_have_text('ready')
                    check(snap('a')['selected']['name']=='Accepted' and not snap('a')['hasValue'],'Same fingerprint restores the independently changed selection without data')
                    check(state()['reads']==before['reads'],'Reconnect causes no hidden read')
                    for name,view,target,category,operation in categories:
                        check(view.locator('[data-role="outcome"]').count()==0,
                              name+' reconnect keeps old category history hidden under mismatched current selection')
                    counts=lambda value:{key:value[key] for key in ('reads','writes','commands','services')}
                    previous=state()
                    for name,target,category in (('a','Config',1),('command','Accepted',2),('service','Inspect',3)):
                        check(page.evaluate('''({name,target,category})=>{
                            const provider=suite.providers[name];
                            return provider.select(provider.snapshot().endpoints.find(row=>row.name===target&&row.category===category).key);
                        }''',dict(name=name,target=target,category=category)), 'Explicit category reselect succeeds')
                    check(counts(state())==counts(previous),'Restoring panel categories sends no read/Command/Service')
                    # Unmount during accepted work does not cancel the borrowed Session.
                    server.reply_delay_seconds=.3
                    action(command,'writes').check()
                    page.evaluate('''async()=>{
                        const {mountCommandPanel}=await import('telemetry-ux/web');
                        const host=document.createElement('section');host.id='disposed-command';document.body.append(host);
                        suite.outcomeObserver=mountCommandPanel(host,{provider:suite.providers.command});
                    }''')
                    before=state(); action(command,'execute').click()
                    for _ in range(100):
                        if state()['commands']>before['commands']: break
                        time.sleep(.01)
                    check(state()['commands']==before['commands']+1,'Accepted command reached the native Model')
                    page.evaluate('suite.views.command.unmount()')
                    check(page.evaluate('suite.providers.command.snapshot().disposed&&suite.session.ready'),'Unmount owns presentation only')
                    page.wait_for_function('suite.providers.command.snapshot().outcomes.command?.accepted===true')
                    check(snap('command')['outcomes']['command']['accepted'],'Pending facts finish after owned unmount')
                    observer=page.locator('#disposed-command')
                    expect(outcome(observer,'command')).to_have_attribute('data-historical','true')
                    expect(outcome(observer,'command')).to_contain_text('Accepted')
                    check(snap('command')['connection']['ready'] and not snap('command')['current'],
                          'Disposed provider retains history while the shared Session is ready')
                    terminal=state()
                    page.evaluate('suite.providers.a.dispose();suite.providers.service.dispose()')
                    for name,view,operation in (('a',a,'read'),('service',service,'service')):
                        expect(outcome(view,operation)).to_have_attribute('data-historical','true')
                        check(snap(name)['disposed'] and snap(name)['connection']['ready'],
                              name+' disposed history is independent of live connection readiness')
                    check(counts(state())==counts(terminal) and terminal['commands']==before['commands']+1 and
                          terminal['services']==before['services'], 'History projection/disposal never repeats a Command or Service')
                    page.evaluate('suite.outcomeObserver.unmount()')
                    server.reply_delay_seconds=0
                    # Lost ACK for a prepared mixed mutation preserves global
                    # uncertainty; reconnect must not replay its recipe.
                    full.locator('button[data-endpoint]').filter(has_text='Accepted').first.click()
                    action(full,'batch-operation').select_option('command')
                    action(full,'addBatch').click();action(full,'writes').check()
                    server.reply_delay_seconds=.3
                    before=state();exchanges=server.exchange_count
                    action(full,'executeBatch').click()
                    for _ in range(100):
                        if state()['commands']>before['commands']:break
                        time.sleep(.01)
                    check(state()['commands']==before['commands']+1,'Prepared Session Batch reached native Command')
                    page.locator('#disconnect').click()
                    expect(page.locator('#status')).to_have_text('recovery-required')
                    expect(full.locator('[data-role="batch-status"]')).to_contain_text('OutcomeUnknown')
                    check(snap('full')['batch']['outcome']['historical'],'Disconnected Batch keeps its operation facts')
                    check(not page.evaluate('suite.providers.full.rows([], {source:"batchResponse"}).available'),
                          'Uncertain Batch publishes no response')
                    server.reply_delay_seconds=0
                    page.wait_for_timeout(350)
                    terminal=state()
                    page.locator('#reconnect').click();expect(page.locator('#status')).to_have_text('ready')
                    check(snap('full')['batch']['items']==[] and not snap('full')['watch']['running'],
                          'New admission does not restore Batch or polling recipes')
                    check(counts(state())==counts(terminal) and server.exchange_count==exchanges+1,
                          'Reconnect sends no automatic Batch mutation or read')
                    page.evaluate('suite.views.borrowed.unmount()')
                    check(page.evaluate('!suite.borrowed.snapshot().disposed&&suite.session.ready'),'Explicit provider mounts borrow presentation')
                    screenshot=output/(mode+'.png'); page.screenshot(path=str(screenshot),full_page=True)
                    report['modes'][mode]=dict(checks=report['checks']-start,state=state(),screenshot_sha256=hashlib.sha256(screenshot.read_bytes()).hexdigest())
                    page.evaluate('suite.close()'); page.close()
                    # The shipped Session demo is exercised as an unmodified
                    # application as well as the purpose-built boundary fixture.
                    demo=browser.new_page(viewport={'width':1440,'height':1000})
                    demo.on('pageerror',lambda error:report['errors'].append(str(error)))
                    demo.goto('http://127.0.0.1:'+str(server.server_port)+'/demo.html')
                    demo.locator('#mode').select_option(mode); demo.locator('#connect').click()
                    expect(demo.locator('#connection-status')).to_have_text('ready')
                    left,right=demo.locator('#left'),demo.locator('#right')
                    before=state()
                    left.locator('button[data-endpoint]').filter(has_text='Config').first.click()
                    right.locator('button[data-endpoint]').filter(has_text='ReadOnly').first.click()
                    check(state()['reads']==before['reads'],'Shipped demo selection sends no I/O')
                    check(left.locator('button[data-endpoint][aria-current="true"]').inner_text()=='Config' and
                          right.locator('button[data-endpoint][aria-current="true"]').inner_text()=='ReadOnly','Shipped demo has independent selections')
                    action(left,'refresh').click(); idle(left)
                    check(state()['reads']==before['reads']+1,'Shipped demo reaches native peer once')
                    demo.locator('#disconnect').click(); expect(demo.locator('#connection-status')).to_have_text('disconnected')
                    expect(outcome(left,'read')).to_have_attribute('data-historical','true')
                    demo.locator('#reconnect').click(); expect(demo.locator('#connection-status')).to_have_text('ready')
                    check(state()['reads']==before['reads']+1,'Shipped demo reconnect does not replay reads')
                    demo.locator('#close').click(); expect(demo.locator('#connection-status')).to_have_text('closed')
                    check(action(left,'refresh').is_disabled(),'Shipped demo terminal close disables endpoint actions')
                    report['modes'][mode]['shipped_demo']=True
                    demo.close()
                check(not report['errors'],'No browser runtime errors: '+repr(report['errors']))
                check(not report['warnings'],'No browser warnings: '+repr(report['warnings']))
            finally:
                browser.close()
        report['passed']=True
    except Exception as error:
        report['error']=str(error)
        raise
    finally:
        server.shutdown(); server.server_close(); thread.join(timeout=5)
        (output/'summary.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')


if __name__=='__main__':
    main()
