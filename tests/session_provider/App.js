// Public programmer journey; the existing demo owns all transport/model bytes. MIT.
import {connectStatic, connectDynamic, demoWebSocket} from 'telemetry-sdk';
import {
	createSessionProvider,
	mountConfigurator,
	mountFieldEditor,
	mountCommandPanel,
	mountServicePanel,
	mountWatchlist,
	mountBatchEditor
} from 'telemetry-ux/web';
import * as sdk from './generated/js/device.js';
import {structuralMetadata} from './generated/js/device.metadata.js';

const mode = new URL(location.href).searchParams.get('mode') || 'static';
const transport = demoWebSocket(location.origin);
const session =
    mode === 'static'
        ? await connectStatic({sdk, metadata : structuralMetadata, transport, timeoutMs : 5000})
        : await connectDynamic({transport, timeoutMs : 5000});
const field = {
	category : 'field',
	catalog : 'control',
	name : 'Config'
};
const views = {
	full : mountConfigurator(document.querySelector('#full'), {session}),
	a : mountFieldEditor(document.querySelector('#field-a'), {session, endpoint : field}),
	b : mountFieldEditor(document.querySelector('#field-b'), {session, endpoint : field}),
	command : mountCommandPanel(
	    document.querySelector('#command'),
	    {session, endpoint : {category : 'command', catalog : 'control', name : 'Accepted'}}),
	service : mountServicePanel(
	    document.querySelector('#service'),
	    {session, endpoint : {category : 'service', catalog : 'control', name : 'Inspect'}})
};
const borrowed = createSessionProvider({session, endpoint : field});
views.borrowed = mountFieldEditor(document.querySelector('#borrowed'), {provider : borrowed});
const invalidMounts = [];
for (const options of [{}, {session, provider : borrowed}]) {
	const host = document.createElement('div');
	try {
		mountConfigurator(host, options);
	} catch (error) {
		invalidMounts.push(error instanceof TypeError && host.childNodes.length === 0);
	}
}
for (const mount of [mountWatchlist, mountBatchEditor]) {
	const host = document.createElement('div');
	try {
		mount(host, {session});
	} catch (error) {
		invalidMounts.push(error instanceof TypeError && host.childNodes.length === 0);
	}
}
const providers =
    Object.fromEntries(Object.entries(views).map(([ name, view ]) => [name, view.provider]));
session.subscribe(state => document.querySelector('#status').textContent = state.state);
document.querySelector('#disconnect').onclick = () => session.disconnect();
document.querySelector('#reconnect').onclick = () => session.reconnect();
window.suite = {
	session,
	views,
	providers,
	borrowed,
	invalidMounts,
	mode,
	async seedNaN() {
	    const target = mode === 'static' ? sdk.control.fields.Config : sdk.control.fields.Config.id;
	    const before = await session.read(target);
	    before.value.f32 = NaN;
	    before.value.zero = -0;
	    const result = await session.write(target, before.value);
	    providers.a.invalidateData('Host initialized NaN fixture');
	    return result.succeeded;
	},
	async hasNaN() {
	    const target = mode === 'static' ? sdk.control.fields.Config : sdk.control.fields.Config.id;
	    const result = await session.read(target);
	    return result.valueStatus === 'Ok' && Number.isNaN(result.value.f32) &&
	           Object.is(result.value.zero, -0);
	},
	snapshot(name) {
	    return JSON.parse(
	        JSON.stringify(providers[name].snapshot(),
	                       (_, value) => typeof value === 'bigint' ? value.toString() : value));
	},
	async close() {
	    for (const view of Object.values(views))
		    view.unmount();
	    borrowed.dispose();
	    await session.close();
	}
};
document.body.dataset.ready = 'true';
