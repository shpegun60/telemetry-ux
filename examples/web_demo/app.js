// Application-owned demo connection, one supplied SDK client, one view. MIT.
import {DeviceClient, EncodedBackend, Status} from 'telemetry-sdk/client';
import {DynamicClient} from 'telemetry-sdk/dynamic';
import {parseDescriptor} from 'telemetry-sdk/runtime';
import {createDynamicProvider, mountConfigurator} from 'telemetry-ux/web';
import * as sdk from './generated/js/device.js';
import {mountDevice} from './generated/ui/DevicePage.js';
import {bindWebSocket} from 'telemetry-ux/transports/websocket';

const element = document.querySelector('#configurator');
const mode = document.querySelector('#mode');
const connectButton = document.querySelector('#connect-demo');
const disconnectButton = document.querySelector('#disconnect-demo');
const detachButton = document.querySelector('#detach-view');
const status = document.querySelector('#demo-status');
let connection;
let connecting = false;
let candidate;
let revision = 0;
const sockets = new Set();

function controls()
{
	connectButton.disabled = connecting;
	mode.disabled = connecting;
	disconnectButton.disabled = !connecting && !sockets.size && (!connection || connection.retired);
	detachButton.disabled = !connection;
	detachButton.textContent = connection?.view ? 'Hide view' : 'Show view';
}

function retire(owner)
{
	if (!owner)
		return;
	if (!owner.retired) {
		owner.retired = true;
		owner.abort.abort();
		// Keep the provider/view alive for completion, including OutcomeUnknown.
		owner.provider?.invalidate();
		owner.client?.close();
		owner.provider?.sync();
		owner.port?.dispose();
	}
	if (owner.socket && owner.socket.readyState !== WebSocket.CLOSED &&
	    owner.socket.readyState !== WebSocket.CLOSING)
		owner.socket.close(1000, 'Host disconnected');
}

function disconnect()
{
	++revision;
	retire(candidate);
	candidate = undefined;
	retire(connection);
	for (const owner of sockets)
		retire(owner);
	connecting = false;
	status.textContent = 'Disconnected. The previous operation outcome remains below.';
	controls();
}

function current(owner)
{
	if (candidate !== owner || owner.retired || owner.revision !== revision)
		throw Error('Connection attempt retired.');
}

async function awaitSocketRelease()
{
	if (!sockets.size)
		return;
	let timer;
	try {
		await Promise.race([
			Promise.all([...sockets ].map(owner => owner.closed)), new Promise((_, reject) => {
			    timer = setTimeout(() => reject(Error('Previous socket has not closed.')), 5000);
			})
		]);
	} finally {
		clearTimeout(timer);
	}
}

function greeting(owner, transport)
{
	if (transport.protocol !== 'telemetry.encoded.v1')
		throw Error('Unsupported demonstration WebSocket protocol.');
	const url = new URL(transport.url, location.href);
	if (url.protocol !== 'ws:' && url.protocol !== 'wss:')
		throw Error('Invalid demonstration WebSocket URL.');
	const socket = owner.socket = new WebSocket(url, transport.protocol);
	socket.binaryType = 'arraybuffer';
	sockets.add(owner);
	owner.closed = new Promise(resolve => {
		socket.addEventListener('close', () => {
			sockets.delete(owner);
			controls();
			resolve();
		}, {once : true});
	});
	return new Promise((resolve, reject) => {
		let settled = false;
		const finish = (error, session) => {
			if (settled)
				return;
			settled = true;
			clearTimeout(timer);
			socket.removeEventListener('message', message);
			socket.removeEventListener('close', closed);
			socket.removeEventListener('error', failed);
			owner.abort.signal.removeEventListener('abort', aborted);
			error ? reject(error) : resolve(session);
		};
		const message = event => {
			try {
				if (socket.protocol !== transport.protocol || typeof event.data !== 'string' ||
				    event.data.length > 256)
					throw Error('Invalid demonstration session greeting.');
				const value = JSON.parse(event.data);
				if (!value || Object.keys(value).length !== 1 ||
				    Object.keys(value)[0] !== 'session' || !Number.isInteger(value.session) ||
				    value.session <= 0 || value.session > 0xffffffff)
					throw Error('Invalid demonstration session greeting.');
				finish(undefined, value.session);
			} catch {
				finish(Error('Invalid demonstration session greeting.'));
			}
		};
		const closed = () => finish(Error('Socket closed before its session greeting.'));
		const failed = () => finish(Error('Demonstration WebSocket failed.'));
		const aborted = () => finish(Error('Connection attempt retired.'));
		const timer = setTimeout(() => finish(Error('Session greeting timed out.')), 5000);
		socket.addEventListener('message', message);
		socket.addEventListener('close', closed, {once : true});
		socket.addEventListener('error', failed, {once : true});
		owner.abort.signal.addEventListener('abort', aborted, {once : true});
		if (owner.abort.signal.aborted)
			aborted();
	});
}

async function connect()
{
	if (connecting)
		return;
	disconnect();
	const owner = {revision : ++revision, abort : new AbortController(), retired : false};
	candidate = owner;
	connecting = true;
	const selectedMode = mode.value;
	controls();
	try {
		const old = connection;
		await awaitSocketRelease();
		current(owner);
		const discovery = await fetch('/demo/transport', {signal : owner.abort.signal});
		if (!discovery.ok)
			throw Error('The demonstration transport is unavailable.');
		const transport = await discovery.json();
		current(owner);
		let session, backend;
		if (transport.kind === 'websocket') {
			session = await greeting(owner, transport);
			current(owner);
			owner.port = bindWebSocket(owner.socket, {
				onDisconnect : event => {
				    if (owner.retired)
					    return;
				    retire(owner);
				    if (candidate === owner) {
					    candidate = undefined;
					    connecting = false;
				    }
				    if (connection === owner || owner.revision === revision)
					    status.textContent = 'Disconnected (' + event.kind +
					                         '). The previous operation outcome remains below.';
				    controls();
				},
				maxFrameBytes : 65536,
				maxBufferedBytes : 65536
			});
			backend = owner.port.backend;
		} else if (transport.kind === 'http') {
			const admitted = await fetch('/demo/connect',
			                             {method : 'POST', body : '', signal : owner.abort.signal});
			if (!admitted.ok)
				throw Error('The local demo could not establish a session.');
			({session} = await admitted.json());
			backend = EncodedBackend.fromExchange(async (packet, context) => {
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
		} else {
			throw Error('Unsupported demonstration transport.');
		}
		current(owner);
		const descriptorReply =
		    await fetch(selectedMode === 'static' ? '/schema-header.bin' : '/descriptor.bin',
		                {signal : owner.abort.signal});
		if (!descriptorReply.ok)
			throw Error('The local demo Descriptor is unavailable.');
		const descriptor = new Uint8Array(await descriptorReply.arrayBuffer());
		current(owner);
		const header = descriptor.subarray(0, 64);
		let client, install;
		if (selectedMode === 'static') {
			client =
			    new DeviceClient({backend, matchesHeader : sdk.matchesHeader, timeoutMs : 5000});
			owner.client = client;
			if (!client.openSession(session, header))
				throw Error('Static schema admission refused.');
			install = () => mountDevice(element, {client});
		} else {
			client = new DynamicClient({backend, timeoutMs : 5000});
			owner.client = client;
			if (client.admit(parseDescriptor(descriptor), header, session) !== Status.Ok)
				throw Error('Dynamic schema admission refused.');
			const provider = createDynamicProvider({client});
			owner.provider = provider;
			install = () => ({provider, unmount : mountConfigurator(element, {provider}).unmount});
		}
		// Each connection gets a fresh presentation owner. Never transfer
		// drafts, bindings or pending requests between sessions or modes.
		const page = install();
		owner.provider = page.provider;
		owner.view = page;
		current(owner);
		old?.view?.unmount();
		connection = owner;
		candidate = undefined;
		status.textContent = selectedMode === 'static'
		                         ? 'Connected · static generated SDK · simulated device'
		                         : 'Connected · runtime Descriptor · simulated device';
	} catch (error) {
		retire(owner);
		owner.view?.unmount();
		if (candidate === owner)
			status.textContent = 'Demo connection failed: ' +
			                     (error instanceof Error ? error.message : 'Connection refused.');
	} finally {
		if (candidate === owner)
			candidate = undefined;
		if (owner.revision === revision)
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
