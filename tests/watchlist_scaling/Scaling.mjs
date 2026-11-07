// Installed Session Watchlist scheduling over the maintained native Model/dispatcher. MIT.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {readFile} from 'node:fs/promises';
import {connectStatic, connectDynamic, EncodedBackend} from 'telemetry-sdk';
import {createSessionProvider} from 'telemetry-ux/web';
import * as sdk from './generated/js/device.js';
import {structuralMetadata} from './generated/js/device.metadata.js';
import {publication, waitForPublication, finishDriver} from './Publication.mjs';

const [executable] = process.argv.slice(2);
const descriptor = new Uint8Array(await readFile(new URL('./descriptor.bin', import.meta.url)));
let checks = 0, scenarios = 0;
const eq = (actual, expected, why = '') => {
	++checks;
	assert.deepEqual(actual, expected, why);
};
const yes = (value, why = '') => {
	++checks;
	assert(value, why);
};
const flush = async () => {
	for (let i = 0; i < 30; ++i)
		await Promise.resolve();
};
const results = [];

for (const mode of ['static', 'dynamic']) {
	const child = spawn(executable, [], {stdio : [ 'pipe', 'pipe', 'pipe' ]});
	let waiter, errors = '', active = 0, peak = 0, sessionId = 16;
	let manyCount = 0, holdMany = 0, failMany = 0, release, capabilityLimits;
	const trace = [], draining = new Set();
	const peerEvents = publication(), responseEvents = publication();
	let peerFailure, primaryFailure;
	const failPeer = error => {
		peerFailure ??= error;
		waiter?.reject(peerFailure);
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
			failPeer(new Error('Native peer exited ' + code + ' signal=' + signal + ': ' + errors));
			resolve(code);
		});
	});
	void exit.catch(() => {
	                });
	createInterface({input : child.stdout}).on('line', line => {
		assert(waiter, 'Native traffic bypassed the one request owner');
		const {resolve} = waiter;
		waiter = undefined;
		resolve(line);
	});
	const native = command => new Promise((resolve, reject) => {
		if (peerFailure) {
			reject(peerFailure);
			return;
		}
		assert(!waiter, 'Parallel physical exchange');
		waiter = {resolve, reject};
		child.stdin.write(command + '\n');
	});
	const backend = EncodedBackend.fromExchange(async bytes => {
		++active;
		peak = Math.max(peak, active);
		const frame = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
		const operation = bytes[6];
		// Test-only observations of the public TEQ1/TRM1 bytes. The SDK still
		// builds/validates frames and the unchanged Peer owns dispatch/parsing.
		const ids = operation === 5 ? Array.from({length : frame.getUint16(32, true)},
		                                         (_, i) => frame.getUint32(34 + i * 4, true))
		                            : [ frame.getUint32(16, true) ];
		trace.push({operation, ids});
		const ordinal = operation === 5 ? ++manyCount : 0;
		try {
			const reply = new Uint8Array(
			    Buffer.from(await native(Buffer.from(bytes).toString('hex')), 'hex'));
			if (ordinal && ordinal === holdMany)
				await new Promise(resolve => {
					release = resolve;
					responseEvents.publish();
				});
			if (ordinal && ordinal === failMany) {
				failMany = 0;
				throw new Error('Injected middle ReadMany transport failure');
			}
			return reply;
		} finally {
			--active;
			if (!active) {
				for (const done of draining)
					done();
				draining.clear();
			}
		}
	}, {readMany : true});
	const profile = {
		async open(context) {
		    await native('session ' + (++sessionId));
		    const readMany = new Uint8Array(Buffer.from(await native('readmany'), 'hex'));
		    // Discovery may advertise a stricter profile than the executor's
		    // maximum. Only reduce the three documented public TRM1 limits;
		    // identity, session and fingerprint remain the real peer's record.
		    if (capabilityLimits) {
			    const capability =
			        new DataView(readMany.buffer, readMany.byteOffset, readMany.byteLength);
			    if (capabilityLimits.items !== undefined)
				    readMany[5] = capabilityLimits.items;
			    if (capabilityLimits.request !== undefined)
				    capability.setUint16(8, capabilityLimits.request, true);
			    if (capabilityLimits.reply !== undefined)
				    capability.setUint16(10, capabilityLimits.reply, true);
		    }
		    return {
			    peer : 'watchlist-scaling-native',
			    session : sessionId,
			    backend,
			    header : descriptor.slice(0, 64),
			    descriptor,
			    readMany,
			    supersedes : context.previous,
			    async close() {
			        if (active)
				        await new Promise(resolve => draining.add(resolve));
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
	const watch = createSessionProvider({session});
	const peer = createSessionProvider(
	    {session, endpoint : {category : 'command', catalog : 'control', name : 'Accepted'}});
	const fields = () =>
	    watch.snapshot().endpoints.filter(row => row.category === 1 && row.catalog === 'scaling');
	const ids = () => fields().map(row => row.id);
	const currentRows = () => watch.snapshot().watch.rows.map(
	    row => ({id : row.id, status : row.status, text : row.text}));
	const unhold = () => {
		holdMany = 0;
		const done = release;
		release = undefined;
		done?.();
	};

	async function until(predicate, why)
	{
		await waitForPublication({
			label : mode + ' ' + why,
			ready : predicate,
			subscribe(changed) {
			    const stops = [
				    watch.subscribe(changed), peer.subscribe(changed),
				    responseEvents.subscribe(changed), peerEvents.subscribe(changed)
			    ];
			    return () => stops.forEach(stop => stop());
			},
			failure : () => peerFailure,
			diagnostics
		});
		yes(predicate(), why);
		await flush();
	}

	const held = () => until(() => typeof release === 'function',
	                         'Controlled native chunk never reached its held response');
	const idle = () => until(() => !watch.snapshot().watch.active && !watch.snapshot().busy,
	                         'Watch cycle did not drain');
	const cycle = () => until(() => !watch.snapshot().watch.pending && !watch.snapshot().busy &&
	                                watch.snapshot().watch.status.includes('complete'),
	                          'Running Watch cycle did not complete');
	const diagnostics = () => {
		const state = watch.snapshot();
		return {
			mode,
			active,
			exchanges : trace.length,
			manyCount,
			holdMany,
			failMany,
			nativePending : !!waiter,
			held : !!release,
			peerExit : child.exitCode,
			peerError : peerFailure?.message,
			connection : session.state,
			busy : state.busy,
			watchActive : state.watch.active,
			watchPending : state.watch.pending,
			watchStatus : state.watch.status,
			peerBusy : peer.snapshot().busy
		};
	};

	function select(count)
	{
		eq(watch.clearWatch(), true);
		for (const row of fields().slice(0, count))
			eq(watch.addWatch(row.key), true);
		eq(watch.snapshot().watch.rows.map(row => row.id), ids().slice(0, count));
	}

	function values(count)
	{
		const rows = watch.snapshot().watch.rows;
		eq(rows.length, count);
		rows.forEach((row, index) => {
			eq(row.id, ids()[index]);
			eq([ row.status, row.text ],
			   index === 32 ? [ 'Unavailable', '' ] : [ 'Ok', '18446744073709551615' ]);
		});
	}

	try {
		scenarios++;
		eq(fields().length, 65, 'The actual Descriptor contains 65 separate scalar Fields');
		eq(fields().map(row => row.name),
		   Array.from({length : 65}, (_, i) => 'Field' + String(i).padStart(2, '0')));
		eq(trace.length, 0, 'Mount and discovery send no endpoint request');
		eq(watch.refreshWatch(), false);
		eq(watch.startWatch(0), false);
		for (const value of [0, -1, 0.5, 1.5, 60001, Infinity, NaN])
			eq(watch.setWatchInterval(value), false);
		eq(watch.setWatchInterval(1), true);
		eq(watch.snapshot().watch.intervalMs, 1);
		eq(watch.setWatchInterval(60000), true);
		eq(watch.setWatchInterval(1), true);

		for (const count of [1, 32, 33, 65]) {
			scenarios++;
			select(count);
			eq(watch.setWatchIndividual(false), true);
			const before = trace.length;
			eq(watch.refreshWatch(), true);
			await idle();
			values(count);
			const expected = [];
			for (let offset = 0; offset < count; offset += 32)
				expected.push({
					operation : 5,
					ids : ids().slice(offset, offset + 32 < count ? offset + 32 : count)
				});
			eq(trace.slice(before), expected,
			   'The host list is ordered bounded ReadMany chunks, never one oversized frame');
			eq(Object.isFrozen(watch.snapshot().watch.rows[count - 1]), true);
		}

		scenarios++;
		select(65);
		eq(watch.setWatchIndividual(true), true);
		let before = trace.length;
		eq(watch.refreshWatch(), true);
		await idle();
		values(65);
		eq(trace.slice(before), ids().map(id => ({operation : 4, ids : [ id ]})),
		   'Individual explicitly performs 65 sequential FieldReads');
		eq(watch.setWatchIndividual(false), true);

		scenarios++;
		select(65);
		holdMany = manyCount + 1;
		before = trace.length;
		eq(watch.startWatch(0), true);
		await held();
		for (const now of [1, 2, 3, 8, 16, 32, 64, 100])
			eq(watch.tickWatch(now), false);
		eq(trace.length, before + 1, 'One delayed chunk never grows a queue of polling ticks');
		unhold();
		await cycle();
		eq(trace.length, before + 3);
		values(65);
		eq(watch.tickWatch(100), false);
		eq(watch.tickWatch(101), true);
		await cycle();
		eq(trace.length, before + 6, 'Only one new cycle follows the skipped ticks');
		eq(watch.stopWatch(), true);
		eq(watch.tickWatch(1000), false);

		for (const [limits, count] of [[ {items : 7}, 7 ], [ {request : 14}, 3 ],
		                               [ {reply : 34}, 2 ]]) {
			scenarios++;
			capabilityLimits = limits;
			await session.disconnect();
			await session.reconnect();
			select(65);
			before = trace.length;
			eq(watch.refreshWatch(), true);
			await idle();
			values(65);
			const expected = [];
			for (let offset = 0; offset < 65; offset += count)
				expected.push({operation : 5, ids : ids().slice(offset, offset + count)});
			eq(trace.slice(before), expected,
			   'Each chunk respects independently advertised item/request/worst-case reply limits');
		}
		scenarios++;
		capabilityLimits = {reply : 10};
		await session.disconnect();
		await session.reconnect();
		select(65);
		before = trace.length;
		eq(watch.refreshWatch(), false,
		   'One scalar that cannot fit the advertised reply is refused before I/O');
		await idle();
		eq(trace.length, before);
		eq(currentRows().every(row => row.text === ''), true);
		capabilityLimits = undefined;
		await session.disconnect();
		await session.reconnect();

		scenarios++;
		select(65);
		holdMany = manyCount + 2;
		before = trace.length;
		eq(watch.refreshWatch(), true);
		await held();
		eq(trace.length, before + 2);
		const completedPrefix = currentRows();
		eq(completedPrefix.slice(0, 32).every(row => row.status === 'Ok'), true);
		eq(watch.stopWatch(), true);
		eq(watch.snapshot().watch.pending, true);
		eq(watch.clearWatch(), false, 'Stop retains the pending chunk lifetime fence');
		unhold();
		await idle();
		eq(trace.length, before + 2, 'Stop before the final chunk prevents its handoff');
		eq(currentRows(), completedPrefix, 'The late second chunk cannot publish after Stop');

		scenarios++;
		select(65);
		holdMany = manyCount + 1;
		before = trace.length;
		eq(watch.refreshWatch(), true);
		await held();
		eq(peer.setWritesEnabled(true), true);
		eq(peer.execute(), true);
		eq(currentRows().every(row => row.status === 'Stale' && row.text === ''), true);
		unhold();
		await idle();
		await until(() => !peer.snapshot().busy, 'Peer Command did not drain');
		eq(trace.slice(before).map(row => row.operation), [ 5, 2 ],
		   'Mutation invalidation prevents the next ReadMany chunk');
		eq(currentRows().every(row => row.status === 'Stale' && row.text === ''), true,
		   'A delayed old chunk cannot restore values after an accepted peer mutation');

		scenarios++;
		select(65);
		failMany = manyCount + 2;
		holdMany = failMany;
		before = trace.length;
		eq(watch.refreshWatch(), true);
		await held();
		eq(currentRows().slice(0, 32).every(row => row.status === 'Ok'), true);
		unhold();
		await idle();
		eq(trace.length, before + 2, 'A middle transport failure does not start the final chunk');
		eq(currentRows().every(row => row.status === 'Stale' && row.text === ''), true,
		   'A failed cycle does not retain a successful earlier prefix as current');
		yes(!watch.snapshot().watch.status.includes('OutcomeUnknown'),
		    'ReadMany remains read-only');
		await session.disconnect();
		await session.reconnect();
		await flush();
		eq(watch.snapshot().watch.rows.length, 0);
		eq(watch.snapshot().watch.running, false);
		eq(trace.length, before + 2, 'Recovery does not restore a previous polling recipe');

		scenarios++;
		select(65);
		holdMany = manyCount + 2;
		before = trace.length;
		eq(watch.refreshWatch(), true);
		await held();
		const closing = session.disconnect();
		eq(watch.snapshot().current, false);
		eq(currentRows().every(row => row.text === ''), true);
		unhold();
		await closing;
		await idle();
		eq(trace.length, before + 2);
		eq(currentRows().every(row => row.text === ''), true);
		await session.reconnect();
		await flush();
		eq(trace.length, before + 2, 'A late old lease response cannot restart the old cycle');
		eq(watch.snapshot().watch.rows.length, 0);
		eq(peak, 1);
		results.push({mode, fields : 65, exchanges : trace.length, readMany : manyCount, peak});
	} catch (error) {
		primaryFailure = error;
		throw error;
	} finally {
		await finishDriver({
			primaryFailure,
			stopHolding : unhold,
			dispose : () => {
			    watch.dispose();
			    peer.dispose();
			},
			close : () => session.close(),
			closeInput : () => child.stdin.end(),
			exit,
			abort : () => child.kill(),
			label : mode + ' Watchlist scaling driver',
			diagnostics
		});
		if (!primaryFailure)
			eq(child.exitCode, 0);
	}
}
console.log(JSON.stringify({passed : true, hardware : false, checks, scenarios, results}));
