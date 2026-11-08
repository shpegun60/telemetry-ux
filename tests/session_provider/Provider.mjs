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
import {publication, waitForPublication, finishDriver} from './Publication.mjs';

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
const results = [];

for (const mode of ['static', 'dynamic']) {
	const child = spawn(executable, [], {stdio : [ 'pipe', 'pipe', 'pipe' ]});
	let pending, errors = '', active = 0, peak = 0, packets = 0, generation = 16;
	let hold = false, release, transformRead, lastWritten, closes = 0, opens = 0;
	let bootstrapDescriptor = descriptor;
	const discovery = {readMany : true, batch : true};
	const drained = new Set();
	const peerEvents = publication(), responseEvents = publication();
	let peerFailure, primaryFailure;
	const failPeer = error => {
		peerFailure ??= error;
		pending?.reject(peerFailure);
		peerEvents.publish();
	};
	child.stderr.on('data', bytes => errors += bytes);
	child.stdin.on('error', failPeer);
	const exit = new Promise((resolve, reject) => {
		child.once('error', error => {
			failPeer(error);
			reject(error);
		});
		child.once('exit', (code, signal) => {
			failPeer(new Error('Native peer ended ' + code + ' signal=' + signal + ': ' + errors));
			resolve(code);
		});
	});
	void exit.catch(() => {
	                }); // A phase barrier reports spawn failure before final cleanup awaits exit.
	createInterface({input : child.stdout}).on('line', line => {
		assert(pending, 'Unsolicited native output');
		const {resolve} = pending;
		pending = undefined;
		resolve(line);
	});
	const native = command => new Promise((resolve, reject) => {
		if (peerFailure) {
			reject(peerFailure);
			return;
		}
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
				await new Promise(resolve => {
					release = resolve;
					responseEvents.publish();
				});
			return reply;
		} finally {
			active--;
			if (!active) {
				for (const resolve of drained)
					resolve();
				drained.clear();
			}
		}
	}, {readMany : true, batch : true});
	const profile = {
		async open(context) {
		    opens++;
		    generation++;
		    await native('session ' + generation);
		    const capabilities = {};
		    for (const [key, command] of [[ 'readMany', 'readmany' ], [ 'batch', 'mixedbatch' ]])
			    if (discovery[key])
				    capabilities[key] = new Uint8Array(Buffer.from(await native(command), 'hex'));
		    return {
			    ...capabilities,
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
	const presentation = {format: 'telemetry-presentation', version: 1,
		fingerprint: '0x' + session.fingerprint.toString(16).padStart(16, '0'), entries: [
			{category: 1, id: sdk.control.fields.Config.id, role: 'value', path: ['large'], label: 'Total', unit: 'ticks', maximum: '18446744073709551615'}
		]};
	const p = createSessionProvider({session, endpoint : config, presentation});
	const q = createSessionProvider(
	    {session, endpoint : {category : 1, id : sdk.control.fields.Config.id}, presentation});
	const r = createSessionProvider(
	    {session, endpoint : {category : 'service', catalog : 'control', name : 'Inspect'}});
	const all = [ p, q, r ];
	const endpoint = (provider, name, category) =>
	    provider.snapshot().endpoints.find(e => e.name === name && e.category === category);
	const text = (provider, source, name) =>
	    provider.rows([], {source}).rows.find(row => row.name === name)?.text;

	const diagnostics = provider => {
		const state = provider?.snapshot();
		return {
			mode,
			packets,
			active,
			nativePending : !!pending,
			hold,
			held : !!release,
			peerExit : child.exitCode,
			peerError : peerFailure?.message,
			connection : session.state,
			busy : state?.busy,
			current : state?.current,
			selected : state?.selected?.key,
			error : state?.error,
			watchPending : state?.watch.pending,
			watchActive : state?.watch.active,
			watchStatus : state?.watch.status,
			batchStatus : state?.batch.status
		};
	};

	async function idle(provider)
	{
		await waitForPublication({
			label : mode + ' provider completion',
			ready : () => !provider.snapshot().busy,
			subscribe(changed) {
			    const state = provider.subscribe(changed), peer = peerEvents.subscribe(changed);
			    return () => {
				    state();
				    peer();
			    };
			},
			failure : () => peerFailure,
			diagnostics : () => diagnostics(provider)
		});
		eq(provider.snapshot().busy, false, 'Provider did not finish');
		await flush();
	}

	async function held()
	{
		await waitForPublication({
			label : mode + ' controlled backend response boundary',
			ready : () => typeof release === 'function',
			subscribe(changed) {
			    const response = responseEvents.subscribe(changed),
			          peer = peerEvents.subscribe(changed);
			    return () => {
				    response();
				    peer();
			    };
			},
			failure : () => peerFailure,
			diagnostics : () => diagnostics()
		});
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
		eq('watch' in p.snapshot(), true);
		eq('batch' in p.snapshot(), true);
		for (const name of ['addWatch', 'refreshWatch', 'tickWatch', 'addBatch', 'executeBatch'])
			eq(typeof p[name], 'function');
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
			const hints = provider.rows().rows.find(row => row.name === 'large').presentation;
			eq(hints.label, 'Total');
			eq(hints.maximum, '18446744073709551615');
		}
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

		// Optional UI operations borrow this same Session. Capability discovery is
		// installed once for each admission; polling creates no second scheduler.
		const watch = createSessionProvider({session});
		const batch = createSessionProvider({session});
		all.push(watch, batch);
		const key = (provider, name, category = 1) => endpoint(provider, name, category).key;
		const watchValues = () => watch.snapshot().watch.rows.map(row => [row.status, row.text]);
		scenarios++;
		const optionalBefore = packets;
		eq(watch.addWatch(key(watch, 'Config')), false, 'ReadMany does not flatten DTO members');
		for (const name of ['Wide', 'Signed', 'Mode', 'Unavailable'])
			eq(watch.addWatch(key(watch, name)), true);
		eq(watch.addWatch(key(watch, 'Wide')), false);
		eq(packets, optionalBefore, 'Watch selection is local');
		eq(watch.refreshWatch(), true, 'Session discovery supplies TRM1 without caller wiring');
		await idle(watch);
		eq(packets, optionalBefore + 1, 'Four watched Fields use one existing FIFO request');
		eq(watchValues(), [
			[ 'Ok', '9007199254740993' ], [ 'Ok', '-9223372036854775808' ], [ 'Ok', '-123' ],
			[ 'Unavailable', '' ]
		]);
		eq(Object.isFrozen(watch.snapshot().watch.rows[0]), true);
		hold = true;
		eq(watch.refreshWatch(), true);
		await held();
		eq(p.select(key(p, 'Accepted', 2)), true);
		eq(p.setWritesEnabled(true), true);
		eq(p.execute(), true);
		eq(watch.snapshot().watch.rows.every(row => row.status === 'Stale' && row.text === ''),
		   true);
		unhold();
		await idle(watch);
		await idle(p);
		eq(watch.snapshot().watch.rows.every(row => row.status === 'Stale' && row.text === ''),
		   true,
		   'Late Watchlist response cannot restore data revoked by an accepted peer mutation');
		eq(p.select(key(p, 'Config')), true);

		scenarios++;
		eq(watch.setWatchInterval(100), true);
		hold = true;
		const pollingBefore = packets;
		eq(watch.startWatch(0), true);
		await held();
		for (const now of [100, 200, 300, 500])
			eq(watch.tickWatch(now), false, 'Missed ticks must not queue another request');
		eq(packets, pollingBefore + 1);
		eq(watch.clearWatch(), false);
		eq(watch.stopWatch(), true);
		eq(watch.snapshot().watch.active, true, 'Stop retains the pending lifetime fence');
		eq(watch.clearWatch(), false);
		unhold();
		await idle(watch);
		eq(watch.snapshot().watch.active, false);
		eq(watch.tickWatch(1000), false);
		eq(packets, pollingBefore + 1, 'Stopped poll never replays missed ticks');
		eq(watch.setWatchIndividual(true), true);
		const individualBefore = packets;
		eq(watch.refreshWatch(), true);
		await idle(watch);
		eq(packets, individualBefore + 4, 'Individual fallback is explicit');
		eq(watchValues(), [
			[ 'Ok', '9007199254740993' ], [ 'Ok', '-9223372036854775808' ], [ 'Ok', '-123' ],
			[ 'Unavailable', '' ]
		]);
		eq(watch.setWatchIndividual(false), true);

		scenarios++;
		eq(batch.addBatch('read', key(batch, 'Wide')), true);
		eq(batch.addBatch('read', key(batch, 'Signed')), true);
		const signedBatch = batch.snapshot().batch.selectedKey;
		const readBatchBefore = packets;
		const nonMutatedWatch = watchValues();
		eq(batch.executeBatch(), true, 'Session discovery supplies TBM1 independently');
		await idle(batch);
		eq(packets, readBatchBefore + 1);
		eq(watchValues(), nonMutatedWatch, 'Read-only Batch does not invalidate sibling values');
		eq(batch.snapshot().batch.items.map(item => item.outcome.succeeded), [ true, true ]);
		eq(batch.rows([], {source : 'batchResponse'}).rows[0].text, '-9223372036854775808');
		hold = true;
		eq(batch.executeBatch(), true);
		await held();
		eq(p.select(key(p, 'Accepted', 2)), true);
		eq(p.execute(), true);
		unhold();
		await idle(batch);
		await idle(p);
		eq(batch.snapshot().batch.outcome.succeeded, true,
		   'Late Batch keeps the checked operation result');
		eq(batch.snapshot().batch.outcome.valueStatus, 'Stale');
		eq(batch.snapshot().batch.items.map(item => item.outcome.succeeded), [ true, true ]);
		eq(batch.rows([], {source : 'batchResponse'}).available, false,
		   'Late read-only Batch cannot publish responses after a peer mutation');
		eq(p.select(key(p, 'Config')), true);
		eq(batch.executeBatch(), true);
		await idle(batch);
		const previousResponse = batch.rows([], {source : 'batchResponse'});
		eq(batch.setWritesEnabled(true), true);
		eq(batch.addBatch('command', key(batch, 'Accepted', 2)), true);
		eq(q.select(key(q, 'Config')), true);
		await read(q);
		eq(q.setWritesEnabled(true), true);
		eq(q.beginEdit(), true);
		edit(q, 'gain', '7.');
		hold = true;
		const fillers =
		    Array.from({length : 3}, () => session.read(binding(sdk.control.fields.Config)));
		await held();
		const refusedBefore = packets;
		eq(batch.executeBatch(), false);
		await flush();
		eq(batch.snapshot().batch.outcome.name, 'QueueFull');
		eq(q.snapshot().dataStale, false, 'Rejected Batch does not revoke a peer draft');
		eq(text(q, 'draft', 'gain'), '7.');
		eq(packets, refusedBefore);
		eq(batch.snapshot().batch.items[0].outcome.succeeded, true,
		   'Refusal keeps checked item history');
		unhold();
		await Promise.all(fillers);
		await idle(batch);
		eq(batch.snapshot().batch.items.length, 3, 'Refusal keeps the Batch recipe');
		eq(batch.selectBatch(signedBatch), true);
		eq(batch.rows([], {source : 'batchResponse'}).rows[0].text, previousResponse.rows[0].text,
		   'QueueFull preserves the previous owned Batch response');
		eq(batch.clearBatch(), true);
		eq(batch.addBatch('command', key(batch, 'Accepted', 2)), true);
		eq(batch.addBatch('service', key(batch, 'Inspect', 3)), true);
		const serviceItem = batch.snapshot().batch.selectedKey;
		eq(batch.addBatch('read', key(batch, 'Unavailable')), true);
		const mixedBefore = packets;
		eq(batch.executeBatch(), true);
		eq(q.snapshot().dataStale, true, 'Accepted mixed Batch immediately invalidates peers');
		eq(text(q, 'draft', 'gain'), '7.');
		await idle(batch);
		eq(packets, mixedBefore + 1, 'Mixed request never fans out or reads back implicitly');
		const facts = batch.snapshot().batch.items.map(item => item.outcome);
		eq(facts[0].accepted, true);
		eq(facts[0].executed, false);
		eq(facts[1].succeeded, true);
		eq(facts[2].succeeded, false);
		eq(batch.selectBatch(serviceItem), true);
		eq(batch.rows([], {source : 'batchResponse'}).rows.find(row => row.name === 'large').text,
		   '9007199254740993');
		await flush();
		eq(packets, mixedBefore + 1, 'Publishing per-item facts does not replay mutations');

		scenarios++;
		// Removing either discovery record must not imply the other capability.
		const oldOptionalSession = session.session;
		await session.disconnect();
		eq(batch.snapshot().batch.outcome.historical, true);
		eq(batch.rows([], {source : 'batchResponse'}).available, false);
		discovery.batch = false;
		await session.reconnect();
		await flush();
		yes(session.session > oldOptionalSession);
		eq(watch.snapshot().watch.rows.length, 0, 'New admission does not restore a poll recipe');
		eq(batch.snapshot().batch.items.length, 0);
		eq(watch.addWatch(key(watch, 'Wide')), true);
		let beforeCapability = packets;
		eq(watch.refreshWatch(), true);
		await idle(watch);
		eq(packets, beforeCapability + 1);
		eq(batch.addBatch('read', key(batch, 'Wide')), true);
		beforeCapability = packets;
		eq(batch.executeBatch(), false, 'TRM1 cannot authorize Batch');
		eq(packets, beforeCapability);
		yes(batch.snapshot().batch.status.includes('TBM1'));
		await session.disconnect();
		discovery.readMany = false;
		discovery.batch = true;
		await session.reconnect();
		await flush();
		eq(watch.addWatch(key(watch, 'Wide')), true);
		beforeCapability = packets;
		eq(watch.refreshWatch(), false, 'TBM1 cannot authorize ReadMany');
		eq(packets, beforeCapability, 'Missing TRM1 causes no implicit FieldRead');
		yes(watch.snapshot().watch.status.includes('TRM1'));
		eq(watch.setWatchIndividual(true), true);
		eq(watch.refreshWatch(), true);
		await idle(watch);
		eq(packets, beforeCapability + 1);
		eq(batch.addBatch('read', key(batch, 'Wide')), true);
		eq(batch.executeBatch(), true);
		await idle(batch);
		eq(packets, beforeCapability + 2);

		scenarios++;
		// Earlier duck-typed Session facades keep ordinary/Individual I/O usable.
		const ordinary = createSessionProvider(
		    {session : {...session, readMany : undefined, batch : undefined}});
		all.push(ordinary);
		eq(ordinary.addWatch(key(ordinary, 'Wide')), true);
		eq(ordinary.setReadManyCapability(session.capabilities.batch), true);
		const ordinaryBefore = packets;
		eq(ordinary.refreshWatch(), false);
		eq(packets, ordinaryBefore);
		yes(/Unsupported|unavailable/i.test(ordinary.snapshot().watch.status));
		eq(ordinary.setWatchIndividual(true), true);
		eq(ordinary.refreshWatch(), true);
		await idle(ordinary);
		eq(packets, ordinaryBefore + 1);
		eq(ordinary.addBatch('read', key(ordinary, 'Wide')), true);
		eq(ordinary.executeBatch(), false);
		eq(packets, ordinaryBefore + 1);
		yes(/Unsupported|unavailable/i.test(ordinary.snapshot().batch.status));
		ordinary.dispose();

		scenarios++;
		// A read may finish physically after revocation, but cannot republish data.
		hold = true;
		eq(watch.refreshWatch(), true);
		await held();
		const beforeLateRead = packets;
		const retiringRead = session.disconnect();
		eq(watch.snapshot().current, false);
		eq(watch.snapshot().watch.rows.every(row => row.text === ''), true);
		unhold();
		await retiringRead;
		await idle(watch);
		eq(watch.snapshot().watch.rows.every(row => row.text === ''), true);
		discovery.readMany = discovery.batch = true;
		await session.reconnect();
		await flush();
		eq(packets, beforeLateRead, 'Reconnect does not restart a previous Watchlist');
		eq(watch.snapshot().watch.running, false);

		scenarios++;
		// An unmounted Batch editor leaves accepted native execution and its facts
		// alive, and reconnect never resubmits an uncertain mutation.
		eq(batch.setWritesEnabled(true), true);
		eq(batch.addBatch('command', key(batch, 'Accepted', 2)), true);
		hold = true;
		eq(batch.executeBatch(), true);
		await held();
		const beforeUncertain = packets, beforeBatchClose = closes;
		batch.dispose();
		eq(session.ready, true);
		eq(closes, beforeBatchClose);
		const retiringBatch = session.disconnect();
		unhold();
		await retiringBatch;
		await idle(batch);
		eq(batch.snapshot().batch.outcome.name, 'OutcomeUnknown');
		eq(batch.snapshot().batch.outcome.historical, true);
		eq(batch.rows([], {source : 'batchResponse'}).available, false);
		await session.reconnect();
		await flush();
		eq(packets, beforeUncertain, 'Unknown Batch does not replay after new admission');
		eq(batch.executeBatch(), false);
		eq(packets, beforeUncertain);
		eq(peak, 1, 'All optional operations use the original physical FIFO');

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
			eq(q.snapshot().endpoints.find(row => row.name === 'Config').presentation, {}, 'Old overlay stays dormant for changed schema');
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
	} catch (error) {
		primaryFailure = error;
		throw error;
	} finally {
		await finishDriver({
			primaryFailure,
			stopHolding : unhold,
			dispose : () => all.forEach(provider => provider.dispose()),
			close : () => session.close(),
			closeInput : () => child.stdin.end(),
			exit,
			abort : () => child.kill(),
			label : mode + ' Session provider driver',
			diagnostics : () => diagnostics()
		});
		if (!primaryFailure)
			eq(child.exitCode, 0);
	}
}
console.log(JSON.stringify({passed : true, hardware : false, checks, scenarios, results}));
