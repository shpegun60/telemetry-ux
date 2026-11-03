// Application-owned demo connection, one supplied SDK client, one view. MIT.
import {DeviceClient, EncodedBackend, Status} from 'telemetry-sdk/client';
import {DynamicClient} from 'telemetry-sdk/dynamic';
import {parseDescriptor} from 'telemetry-sdk/runtime';
import {createDynamicProvider, mountConfigurator} from 'telemetry-ux/web';
import * as sdk from './generated/js/device.js';
import {mountDevice} from './generated/ui/DevicePage.js';

const element = document.querySelector('#configurator');
const mode = document.querySelector('#mode');
const connectButton = document.querySelector('#connect-demo');
const disconnectButton = document.querySelector('#disconnect-demo');
const detachButton = document.querySelector('#detach-view');
const status = document.querySelector('#demo-status');
let connection;
let connecting = false;

function controls()
{
	connectButton.disabled = connecting;
	mode.disabled = connecting;
	disconnectButton.disabled = !connection?.client.ready;
	detachButton.disabled = !connection;
	detachButton.textContent = connection?.view ? 'Hide view' : 'Show view';
}

function disconnect()
{
	if (!connection)
		return;
	// Retire interpretation before the host changes its admitted connection.
	// Keep the provider/view alive to publish the SDK's operation completion.
	connection.provider.invalidate();
	connection.client.close();
	connection.provider.sync();
	status.textContent = 'Disconnected. The previous operation outcome remains below.';
	controls();
}

async function connect()
{
	if (connecting)
		return;
	connecting = true;
	disconnect();
	controls();
	let candidate;
	try {
		const old = connection;
		const admitted = await fetch('/demo/connect', {method : 'POST', body : ''});
		if (!admitted.ok)
			throw Error('The local demo could not establish a session.');
		const {session} = await admitted.json();
		const descriptorReply =
		    await fetch(mode.value === 'static' ? '/schema-header.bin' : '/descriptor.bin');
		if (!descriptorReply.ok)
			throw Error('The local demo Descriptor is unavailable.');
		const descriptor = new Uint8Array(await descriptorReply.arrayBuffer());
		const header = descriptor.subarray(0, 64);
		// This illustrative HTTP route is an application transport, not a new
		// Telemetry protocol or a production firmware server. No retry is used.
		const backend = EncodedBackend.fromExchange(async (packet, context) => {
			const response = await fetch('/exchange', {
				method : 'POST',
				body : packet,
				signal : context?.signal,
				headers : {'Content-Type' : 'application/octet-stream'}
			});
			if (!response.ok)
				throw Error('The local Encoded exchange failed.');
			return new Uint8Array(await response.arrayBuffer());
		});
		let client, install;
		if (mode.value === 'static') {
			client =
			    new DeviceClient({backend, matchesHeader : sdk.matchesHeader, timeoutMs : 5000});
			candidate = client;
			if (!client.openSession(session, header))
				throw Error('Static schema admission refused.');
			install = () => mountDevice(element, {client});
		} else {
			client = new DynamicClient({backend, timeoutMs : 5000});
			candidate = client;
			if (client.admit(parseDescriptor(descriptor), header, session) !== Status.Ok)
				throw Error('Dynamic schema admission refused.');
			const provider = createDynamicProvider({client});
			install = () => ({provider, unmount : mountConfigurator(element, {provider}).unmount});
		}
		// Each connection gets a fresh presentation owner. Never transfer
		// drafts, bindings or pending requests between sessions or modes.
		old?.view?.unmount();
		const page = install();
		connection = {client, provider : page.provider, view : page};
		candidate = undefined;
		status.textContent = mode.value === 'static'
		                         ? 'Connected · static generated SDK · simulated device'
		                         : 'Connected · runtime Descriptor · simulated device';
	} catch (error) {
		candidate?.close();
		status.textContent = error instanceof Error ? error.message : 'Demo connection failed.';
	} finally {
		connecting = false;
		controls();
	}
}

connectButton.addEventListener('click', connect);
disconnectButton.addEventListener('click', disconnect);
detachButton.addEventListener('click', () => {
	if (!connection)
		return;
	if (connection.view) {
		connection.view.unmount();
		connection.view = undefined;
	} else {
		connection.view = mountConfigurator(element, {provider : connection.provider});
	}
	controls();
});
controls();
