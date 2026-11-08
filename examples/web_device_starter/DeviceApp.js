// One Session owns connection and execution. The Configurator owns presentation. MIT.
import {connectDynamic, connectStatic, demoWebSocket} from 'telemetry-sdk';
import {mountConfigurator} from 'telemetry-ux/web';
import {staticAvailable} from './starter-options.js';
import {loadStaticContext} from './static-context.js';

const element = id => document.getElementById(id);
element('mode').querySelector('[value="static"]').disabled = !staticAvailable;
element('mode').title = staticAvailable ? 'Both SDK modes are prepared'
                                        : 'Static mode requires prepare.py --codegen-python';
// This starter implements the reference-server contract documented in README.md.
const transport = demoWebSocket(location.origin, {enableReadMany : true, enableBatch : true});
let session, view, unsubscribe, working = false;

function render()
{
	const exists = session && session.state !== 'closed';
	element('mode').disabled = working || exists;
	element('connect').disabled = working || exists;
	element('disconnect').disabled = working || !session?.ready;
	element('reconnect').disabled = working || !exists || session.ready;
	element('close').disabled = working || !exists;
	element('status').textContent = working ? 'Working…' : session?.state ?? 'Not connected';
}

async function run(action)
{
	if (working)
		return;
	working = true;
	element('error').textContent = '';
	render();
	try {
		await action();
	} catch (error) {
		element('error').textContent = `${error.code ?? error.name}: ${error.message}`;
	} finally {
		working = false;
		render();
	}
}

element('connect').onclick = () => run(async () => {
	let sdk, metadata;
	if (element('mode').value === 'static') {
		({sdk, metadata} = await loadStaticContext());
	}
	view?.unmount();
	unsubscribe?.();
	session =
	    sdk ? await connectStatic({sdk, metadata, transport}) : await connectDynamic({transport});
	unsubscribe = session.subscribe(render);
	try {
		view = mountConfigurator(element('device'), {session});
		element('empty').hidden = true;
	} catch (error) {
		await session.close();
		throw error;
	}
});
element('disconnect').onclick = () => run(() => session.disconnect());
element('reconnect').onclick = () => run(() => session.reconnect());
element('close').onclick = () => run(() => session.close());

// Host-owned teardown; removing a view alone must not close a shared connection.
export async function dispose()
{
	view?.unmount();
	unsubscribe?.();
	await session?.close();
}

render();
