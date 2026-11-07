// Public installed Session mounts over the unchanged reference WebSocket server. MIT.
import {connectStatic, connectDynamic, demoWebSocket} from 'telemetry-sdk';
import {createSessionProvider, mountWatchlist} from 'telemetry-ux/web';
import * as sdk from './generated/js/device.js';
import {structuralMetadata} from './generated/js/device.metadata.js';

const mode = new URL(location.href).searchParams.get('mode') || 'static';
const transport = demoWebSocket(location.origin, {enableReadMany : true});
const session =
    mode === 'static'
        ? await connectStatic({sdk, metadata : structuralMetadata, transport, timeoutMs : 5000})
        : await connectDynamic({transport, timeoutMs : 5000});
const automatic = mountWatchlist(document.querySelector('#automatic'), {session});
const manual = mountWatchlist(document.querySelector('#manual'), {session, autoPoll : false});
const invalidMounts = [];
for (const autoPoll of [1, 'true', null]) {
	const host = document.createElement('div');
	try {
		mountWatchlist(host, {session, autoPoll});
	} catch (cause) {
		invalidMounts.push(cause instanceof TypeError && host.childNodes.length === 0);
	}
}
const fill = provider => {
	const fields = provider.snapshot().endpoints.filter(row => row.catalog === 'scaling' &&
	                                                           row.category === 1);
	return fields.length === 65 && fields.every(row => provider.addWatch(row.key));
};
if (!fill(automatic.provider) || !fill(manual.provider))
	throw new Error('The actual scaling Model or unrestricted host Watchlist is unavailable');
// A provider mount stays manually driven by default, including a provider that
// happens to borrow a high-level Session.
const borrowed = createSessionProvider({session});
fill(borrowed);
const borrowedView = mountWatchlist(document.querySelector('#borrowed'), {provider : borrowed});
window.scaling = {
	session,
	automatic,
	manual,
	borrowed,
	borrowedView,
	invalidMounts,
	snapshot() {
	    return automatic.provider.snapshot();
	},
	async close() {
	    automatic.unmount();
	    manual.unmount();
	    borrowedView.unmount();
	    borrowed.dispose();
	    await session.close();
	}
};
document.body.dataset.ready = 'true';
