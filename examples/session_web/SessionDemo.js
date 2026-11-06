// Reference-server connection owner; the shared views own presentation only. MIT.
import {connectDynamic, connectStatic, demoWebSocket} from 'telemetry-sdk';
import {mountConfigurator} from 'telemetry-ux/web';
import * as sdk from './generated/js/device.js';
import {structuralMetadata} from './generated/js/device.metadata.js';

const get = id => document.getElementById(id);
const transport = demoWebSocket(location.origin);
let session, unsubscribe, views = [], working = false, ownerError = '';

function state()
{
	get('mode').disabled = working || !!session && session.state !== 'closed';
	get('connect').disabled = working || !!session && session.state !== 'closed';
	get('disconnect').disabled = working || !session || session.state !== 'ready';
	get('reconnect').disabled = working || !session || session.state === 'closed';
	get('close').disabled = working || !session || session.state === 'closed';
	get('connection-status').textContent =
	    ownerError ||
	    (session
	         ? session.state + (session.lastError
	                                ? ` / ${session.lastError.code}: ${session.lastError.message}`
			                        : '')
		 : working ? 'Connecting...'
			       : 'Not connected');
}

async function action(run)
{
	if (working)
		return;
	ownerError = '';
	working = true;
	state();
	try {
		await run();
	} catch (error) {
		ownerError = `${error.code ?? error.name}: ${error.message}`;
	} finally {
		working = false;
		state();
	}
}

get('connect').addEventListener(
    'click', () => action(async () => {
	             unsubscribe?.();
	             for (const view of views)
		             view.unmount();
	             views = [];
	             session =
	                 get('mode').value === 'static'
	                     ? await connectStatic({sdk, metadata : structuralMetadata, transport})
	                     : await connectDynamic({transport});
	             unsubscribe = session.subscribe(state);
	             try {
		             for (const id of ['left', 'right'])
			             views.push(mountConfigurator(get(id), {session}));
	             } catch (error) {
		             for (const view of views)
			             view.unmount();
		             views = [];
		             await session.close();
		             throw error;
	             }
             }));
get('disconnect').addEventListener('click', () => action(() => session.disconnect()));
get('reconnect').addEventListener('click', () => action(() => session.reconnect()));
get('close').addEventListener('click', () => action(() => session.close()));
