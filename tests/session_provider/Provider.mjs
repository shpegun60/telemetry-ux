// Installed SDK Session -> installed UX -> unchanged native demo Model. MIT.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {readFile} from 'node:fs/promises';
import {connectStatic, connectDynamic, EncodedBackend, Status} from 'telemetry-sdk';
import {descriptorFingerprint, parseDescriptor} from 'telemetry-sdk/runtime';
import {createSessionProvider} from 'telemetry-ux/web';
import * as sdk from './generated/js/device.js';
import {structuralMetadata} from './generated/js/device.metadata.js';

const [executable] = process.argv.slice(2);
const descriptor = new Uint8Array(await readFile(new URL('./descriptor.bin', import.meta.url)));
let checks = 0, scenarios = 0;
const eq = (a, b, why = '') => {
	checks++;
	assert.deepEqual(a, b, why);
};
const yes = (a, why = '') => {
	checks++;
	assert(a, why);
};
const flush = async () => {
	for (let i = 0; i < 35; i++)
		await Promise.resolve();
};
const tick = () => new Promise(resolve => setImmediate(resolve));
const results = [];

for (const mode of ['static', 'dynamic']) {
	const child = spawn(executable, [], {stdio : [ 'pipe', 'pipe', 'pipe' ]});
	let pending, errors = '', active = 0, peak = 0, packets = 0, generation = 16;
	let hold = false, release, transformRead, lastWritten, closes = 0, opens = 0;
	let bootstrapDescriptor = descriptor;
	const drained = new Set();
	child.stderr.on('data', bytes => errors += bytes);
	const exit = new Promise((resolve, reject) => {
		child.once('error', reject);
		child.once('exit', code => {
			pending?.reject(new Error('Native peer ended ' + code + ': ' + errors));
			resolve(code);
		});
	});
	createInterface({input : child.stdout}).on('line', line => {
		assert(pending, 'Unsolicited native output');
		const {resolve} = pending;
		pending = undefined;
		resolve(line);
	});
	const native = command => new Promise((resolve, reject) => {
		assert(!pending, 'Physical exchange bypassed the existing FIFO');
		pending = {resolve, reject};
		child.stdin.write(command + '\n');
	});
	const backend = EncodedBackend.fromExchange(async packet => {
		active++;
		peak = Math.max(peak, active);
		packets++;
		if (packet[6] === 1)
			lastWritten = packet.slice(32);
		try {
			const reply = new Uint8Array(
			    Buffer.from(await native(Buffer.from(packet).toString('hex')), 'hex'));
			if (packet[6] === 4 && transformRead) {
				const change = transformRead;
				transformRead = undefined;
				eq(reply[7], 0);
				change(reply.subarray(32));
			}
			if (hold)
				await new Promise(resolve => release = resolve);
			return reply;
		} finally {
			active--;
			if (!active) {
				for (const resolve of drained)
					resolve();
				drained.clear();
			}
		}
	});
	const profile = {
		async open(context) {
		    opens++;
		    generation++;
		    await native('session ' + generation);
		    return {
			    peer : 'native-demo',
			    session : generation,
			    header : bootstrapDescriptor.slice(0, 64),
			    descriptor : bootstrapDescriptor,
			    backend,
			    supersedes : context.previous,
			    async close() {
			        closes++;
			        if (active)
				        await new Promise(resolve => drained.add(resolve));
			    }
		    };
		}
	};
	const clock = {now : () => 0, setTimeout : () => undefined, clearTimeout() {}};
	const session =
	    mode === 'static'
	        ? await connectStatic(
	              {sdk, metadata : structuralMetadata, transport : profile, clock, maxPending : 3})
	        : await connectDynamic({transport : profile, clock, maxPending : 3});
	const binding = object => mode === 'static' ? object : object.id;
	const config = {category : 'field', catalog : 'control', name : 'Config'};
	const p = createSessionProvider({session, endpoint : config});
	const q = createSessionProvider(
	    {session, endpoint : {category : 1, id : sdk.control.fields.Config.id}});
	const r = createSessionProvider(
	    {session, endpoint : {category : 'service', catalog : 'control', name : 'Inspect'}});
	const all = [ p, q, r ];
	const endpoint = (provider, name, category) =>
	    provider.snapshot().endpoints.find(e => e.name === name && e.category === category);
	const text = (provider, source, name) =>
	    provider.rows([], {source}).rows.find(row => row.name === name)?.text;

	async function idle(provider)
	{
		for (let i = 0; provider.snapshot().busy && i < 1000; i++)
			await tick();
		eq(provider.snapshot().busy, false, 'Provider did not finish');
		await flush();
	}

	async function held()
	{
		for (let i = 0; !release && i < 1000; i++)
			await tick();
		yes(release, 'No controlled backend response boundary');
	}

	function unhold()
	{
		hold = false;
		const done = release;
		release = undefined;
		done?.();
	}

	async function read(provider)
	{
		eq(provider.refresh(), true);
		await idle(provider);
	}

	function edit(provider, name, value)
	{
		eq(provider.setText([ name ], value, provider.snapshot().revision), true);
	}

	try {
		scenarios++;
		eq(packets, 0, 'Mount/selection must not submit');
		eq(opens, 1);
		eq(p.snapshot().mode, mode);
		eq(p.snapshot().current, true);
		eq(p.snapshot().connection.state, 'ready');
		eq(p.snapshot().disposed, false);
		eq(p.snapshot().selected.name, 'Config');
		eq(r.snapshot().selected.name, 'Inspect');
		eq('watch' in p.snapshot(), false);
		eq('batch' in p.snapshot(), false);
		for (const name of ['addWatch', 'refreshWatch', 'tickWatch', 'addBatch', 'executeBatch'])
			eq(name in p, false);
		eq(Object.isFrozen(p.snapshot()), true);
		eq(Object.isFrozen(p.snapshot().connection), true);
		checks++;
		assert.throws(
		    () => createSessionProvider(
		        {session, endpoint : {category : 'field', catalog : 'none', name : 'Config'}}),
		    TypeError);
		checks++;
		assert.throws(() => createSessionProvider({session, endpoint : {category : 9, id : 0}}),
		              TypeError);
		eq(packets, 0);

		scenarios++;
		await read(p);
		await read(q);
		eq(text(p, 'value', 'large'), '18446744073709551615');
		eq(text(p, 'value', 'negative'), '-9223372036854775808');
		eq(text(p, 'value', 'mode'), '-123');
		eq(text(p, 'value', 'zero'), '-0');
		for (const provider of [p, q]) {
			eq(provider.setWritesEnabled(true), true);
			eq(provider.beginEdit(), true);
		}
		edit(p, 'large', '9007199254740993');
		edit(q, 'gain', '6.');
		eq(text(p, 'draft', 'large'), '9007199254740993');
		eq(text(q, 'draft', 'large'), '18446744073709551615');
		eq(q.select(endpoint(q, 'ReadOnly', 1).key), true);
		eq(p.snapshot().selected.name, 'Config');
		eq(q.snapshot().selected.name, 'ReadOnly');
		eq(q.select(endpoint(q, 'Config', 1).key), true);
		await read(q);
		eq(q.beginEdit(), true);
		edit(q, 'gain', '6.');

		scenarios++;
		// Capacity rejection is an actual Session/core refusal and sends no mutation.
		hold = true;
		const pendingReads =
		    Array.from({length : 3}, () => session.read(binding(sdk.control.fields.Config)));
		yes(pendingReads.every(request => request.requestId > 0));
		await held();
		const before = packets;
		eq(p.apply(), false);
		await flush();
		eq(p.snapshot().outcomes.write.status, Status.QueueFull);
		eq(q.snapshot().dataStale, false);
		eq(text(q, 'draft', 'gain'), '6.');
		eq(packets, before);
		unhold();
		await Promise.all(pendingReads);
		await flush();
		eq(p.snapshot().hasDraft, true);
		eq(text(p, 'draft', 'large'), '9007199254740993');

		scenarios++;
		eq(p.apply(), true);
		eq(q.snapshot().dataStale, true, 'Accepted mutation immediately revokes sibling data');
		eq(text(q, 'draft', 'gain'), '6.');
		yes(q.snapshot().dataInvalidationReason);
		eq(q.snapshot().actions.apply, false);
		eq(q.apply(), false);
		await idle(p);
		eq(p.snapshot().outcomes.write.display, 'Applied');
		eq(p.snapshot().outcomes.readback.succeeded, true);
		eq(text(p, 'value', 'large'), '9007199254740993');
		eq(q.snapshot().selected.name, 'Config');
		eq(r.snapshot().selected.name, 'Inspect');
		eq(q.cancelEdit(), true);
		await read(q);
		eq(q.snapshot().dataStale, false);

		scenarios++;
		// Real read payloads carry selected valid float encodings. Editing another
		// leaf must preserve every untouched wire byte, through the Session API.
		const metadataEndpoint =
		    structuralMetadata.endpoints.find(e => e.category === 1 && e.name === 'Config');
		const type = structuralMetadata.types[metadataEndpoint.valueType];
		const offset = name => type.members.find(member => member.name === name).wireOffset;
		for (const [f32, f64] of [[ 0x7fa12345, 0x7ff0123456789abcn ],
		                          [ 0xffa12345, 0xfff0123456789abcn ],
		                          [ 0x80000000, 0x8000000000000000n ]]) {
			let original;
			transformRead = payload => {
				const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
				view.setUint32(offset('f32'), f32, true);
				view.setBigUint64(offset('zero'), f64, true);
				original = payload.slice();
			};
			await read(p);
			eq(p.beginEdit(), true);
			edit(p, 'gain', '6.25');
			const expected = original.slice();
			new DataView(expected.buffer).setFloat64(offset('gain'), 6.25, true);
			eq(p.apply(), true);
			await idle(p);
			eq(lastWritten, expected);
			eq(new DataView(lastWritten.buffer, lastWritten.byteOffset)
			       .getUint32(offset('f32'), true),
			   f32);
			eq(new DataView(lastWritten.buffer, lastWritten.byteOffset)
			       .getBigUint64(offset('zero'), true),
			   f64);
		}

		scenarios++;
		await read(q);
		eq(q.beginEdit(), true);
		edit(q, 'gain', '8.5');
		eq(r.cancelEdit(), true);
		eq(r.beginEdit(), true);
		eq(r.setWritesEnabled(true), true);
		eq(r.call(), true);
		eq(q.snapshot().dataStale, true);
		await idle(r);
		eq(r.snapshot().outcomes.service.succeeded, true);
		eq(r.snapshot().hasResponse, true);
		eq(text(q, 'draft', 'gain'), '8.5');
		eq(p.select(endpoint(p, 'Accepted', 2).key), true);
		eq(p.execute(), true);
		await idle(p);
		eq(p.snapshot().outcomes.command.accepted, true);
		eq(p.snapshot().outcomes.command.executed, false);
		eq(r.snapshot().hasResponse, false);
		eq(p.select(endpoint(p, 'Config', 1).key), true);
		await read(p);
		await session.command(binding(sdk.control.commands.Accepted));
		eq(p.snapshot().hasValue, true,
		   'Direct headless work is an explicit coordination boundary');
		p.invalidateData('Host mutation completed');
		eq(p.snapshot().hasValue, false);
		eq(q.snapshot().dataStale, true);
		eq(q.snapshot().dataInvalidationReason, 'Host mutation completed');

		scenarios++;
		// A real read already in flight cannot restore pre-mutation data when a
		// sibling mutation is accepted into the same Session FIFO.
		eq(q.cancelEdit(), true);
		await read(q);
		hold = true;
		eq(q.refresh(), true);
		await held();
		eq(p.select(endpoint(p, 'Accepted', 2).key), true);
		eq(p.execute(), true);
		eq(q.snapshot().dataStale, true);
		unhold();
		await idle(q);
		await idle(p);
		eq(q.snapshot().hasValue, false);
		eq(q.snapshot().outcomes.read.succeeded, true);
		eq(q.snapshot().outcomes.read.valueStatus, 'Stale');
		eq(p.select(endpoint(p, 'Config', 1).key), true);

		scenarios++;
		await read(p);
		await read(q);
		const selected = p.snapshot().selected.key, previous = session.session;
		let revocations = 0;
		const phases = [];
		const unsubscribe = p.subscribe(state => {
			phases.push(state.connection.state);
			if (!state.connection.ready) {
				revocations++;
				eq(state.actions.refresh, false);
			}
		});
		const disconnect = session.disconnect();
		eq(p.snapshot().current, false);
		eq(q.snapshot().hasValue, false);
		eq(p.snapshot().actions.refresh, false);
		eq(p.snapshot().outcomes.read.succeeded, true);
		eq(p.snapshot().outcomes.read.historical, true);
		await disconnect;
		yes(revocations > 0);
		const deferred = createSessionProvider(
		    {session, endpoint : {category : 'field', catalog : 'missing', name : 'Unknown'}});
		eq(deferred.snapshot().current, false);
		await session.reconnect();
		await flush();
		eq(p.snapshot().current, true);
		eq(p.snapshot().selected.key, selected);
		yes(session.session > previous);
		eq(p.snapshot().hasValue, false);
		eq(p.snapshot().hasDraft, false);
		unsubscribe();
		for (const phase
		         of ['disconnecting', 'disconnected', 'connecting', 'bootstrapping', 'ready'])
			yes(phases.includes(phase), 'Missing automatic connection phase ' + phase);
		eq(deferred.snapshot().selected, null);
		yes(deferred.snapshot().error);
		deferred.dispose();

		if (mode === 'dynamic') {
			scenarios++;
			// Reuse the actual native Descriptor, with one same-width endpoint
			// name change and the SDK's own exported fingerprint/parser helpers.
			// No new descriptor producer or wire codec is implemented here.
			const changed = descriptor.slice();
			const needle = Buffer.from('ReadOnly'), at = Buffer.from(changed).indexOf(needle);
			yes(at >= 64);
			changed[at] = 'r'.charCodeAt(0);
			new DataView(changed.buffer).setBigUint64(16, descriptorFingerprint(changed), true);
			yes(parseDescriptor(changed).fingerprint !== session.fingerprint);
			await session.disconnect();
			bootstrapDescriptor = changed;
			await session.reconnect();
			await flush();
			eq(q.snapshot().current, true);
			eq(q.snapshot().selected, null, 'Changed fingerprint requires explicit reselect');
			eq(q.snapshot().hasValue, false);
			eq(q.select(endpoint(q, 'Config', 1).key), true);
			eq(q.snapshot().selected.name, 'Config');
		}

		scenarios++;
		// A provider's disposal releases only its presentation subscription.
		const victim = createSessionProvider(
		    {session, endpoint : {category : 'command', catalog : 'control', name : 'Accepted'}});
		victim.setWritesEnabled(true);
		hold = true;
		eq(victim.execute(), true);
		await held();
		const beforeClose = closes;
		victim.dispose();
		victim.dispose();
		eq(victim.snapshot().disposed, true);
		eq(victim.snapshot().current, false);
		eq(session.ready, true);
		eq(closes, beforeClose);
		eq(victim.execute(), false);
		unhold();
		await idle(victim);
		eq(victim.snapshot().outcomes.command.accepted, true);
		eq(victim.snapshot().outcomes.command.executed, false);
		eq(victim.snapshot().outcomes.command.historical, true);
		let disposedEvents = 0;
		const stopDisposed = p.subscribe(state => {
			if (state.disposed)
				disposedEvents++;
		});
		await session.disconnect();
		p.dispose();
		eq(p.snapshot().disposed, true);
		eq(disposedEvents, 1, 'Dispose after revocation is observable');
		stopDisposed();
		await session.reconnect();
		await flush();
		const other = session.read(binding(sdk.control.fields.Config));
		eq((await other).valueStatus, 'Ok');
		eq(peak, 1);
		yes(packets > 15);
		results.push({mode, packets, peak, opens, closes, checks});
	} finally {
		unhold();
		for (const provider of all)
			provider.dispose();
		await session.close();
		child.stdin.end();
		eq(await exit, 0);
	}
}
console.log(JSON.stringify({passed : true, hardware : false, checks, scenarios, results}));
