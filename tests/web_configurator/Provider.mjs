// Actual installed providers and SDK clients against a persistent native Model. MIT.
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
import {readFile} from 'node:fs/promises';
import {pathToFileURL} from 'node:url';
import {resolve} from 'node:path';
import {createInterface} from 'node:readline';
import {DynamicClient} from 'telemetry-sdk/dynamic';
import {parseDescriptor} from 'telemetry-sdk/runtime';
import {EncodedBackend, DeviceClient, Status} from 'telemetry-sdk/client';
import {createDynamicProvider, createStaticProvider} from 'telemetry-ux/web';
const options =
    Object.fromEntries(Array.from({length : (process.argv.length - 2) / 2},
                                  (_, i) => [process.argv[2 + i * 2], process.argv[3 + i * 2]]));
assert.deepEqual(Object.keys(options).sort(), [ '--descriptor', '--device', '--generated' ]);
const sdk = await import(pathToFileURL(resolve(options['--generated'], 'device.js')));
const {structuralMetadata} =
    await import(pathToFileURL(resolve(options['--generated'], 'device.metadata.js')));
const descriptor = await readFile(options['--descriptor']);
const schema = parseDescriptor(descriptor);
let checks = 0;

function eq(a, b)
{
	checks++;
	assert.deepEqual(a, b);
}

for (const mode of ['dynamic', 'static']) {
	const peer = spawn(options['--device'], [], {stdio : [ 'pipe', 'pipe', 'pipe' ]});
	let waiter, errors = '', frames = 0, hold, release;
	peer.stderr.on('data', x => errors += x);
	createInterface({input : peer.stdout}).on('line', line => {
		assert(waiter);
		const w = waiter;
		waiter = undefined;
		w(line);
	});
	const exit = new Promise(resolve => peer.on('exit', resolve));
	const backend = EncodedBackend.fromExchange(async packet => {
		frames++;
		const data = await new Promise(resolve => {
			waiter = resolve;
			peer.stdin.write(Buffer.from(packet).toString('hex') + '\n');
		});
		if (hold)
			await new Promise(resolve => release = resolve);
		return new Uint8Array(Buffer.from(data, 'hex'));
	});
	const client =
	    mode === 'dynamic'
	        ? new DynamicClient({backend, maxPending : 1})
	        : new DeviceClient({backend, matchesHeader : sdk.matchesHeader, maxPending : 1});
	eq(mode === 'dynamic' ? client.admit(schema, descriptor.subarray(0, 64), 17)
	                      : client.openSession(17, descriptor.subarray(0, 64)),
	   mode === 'dynamic' ? Status.Ok : true);
	const p = mode === 'dynamic'
	              ? createDynamicProvider({client})
	              : createStaticProvider({client, metadata : structuralMetadata, sdk});
	let events = 0;
	const unsubscribe = p.subscribe(() => events++);

	function endpoint(name, category = 1)
	{
		return p.snapshot().endpoints.find(row => row.name === name && row.category === category);
	}

	async function done()
	{
		for (let i = 0; p.snapshot().busy && i < 1000; i++)
			await new Promise(resolve => setTimeout(resolve, 1));
		eq(p.snapshot().busy, false);
	}

	eq(p.snapshot().current, true);
	eq(p.snapshot().writesEnabled, false);
	eq(p.snapshot().endpoints.length, 10);
	eq(p.select(endpoint('Config').key), true);
	eq(p.refresh(), true);
	await done();
	let rows = p.rows().rows;
	eq(rows.find(row => row.name === 'large').text, '18446744073709551615');
	eq(rows.find(row => row.name === 'negative').text, '-9223372036854775808');
	eq(rows.find(row => row.name === 'zero').text, '-0');
	eq(rows.find(row => row.name === 'mode').text, '-123');

	// Presentation state and windows contain immutable primitives, never a DTO.
	const state = p.snapshot();
	eq(Object.isFrozen(state), true);
	eq(Object.isFrozen(state.endpoints), true);
	eq('value' in state, false);
	eq('draft' in state, false);
	eq('response' in state, false);
	eq(Object.isFrozen(p.rows().rows[0].path), true);
	eq(p.rows([], {offset : 0, limit : 1}).rows.length, 1);
	eq(p.rows([], {offset : 999, limit : 128}).rows.length, 0);
	assert.throws(() => p.rows([], {limit : 129}), TypeError);
	checks++;
	eq(p.select(endpoint('ReadOnly').key), true);
	eq(p.refresh(), true);
	await done();
	eq(p.beginEdit(), false);
	eq(p.select(endpoint('Counters').key), true);
	eq(p.refresh(), true);
	await done();
	eq(p.snapshot().selected.id, 65536);
	eq(p.select(endpoint('Config').key), true);
	eq(p.refresh(), true);
	await done();
	eq(p.beginEdit(), false);
	eq(p.setWritesEnabled(true), true);
	eq(p.beginEdit(), true);

	const oldRevision = p.snapshot().revision;
	eq(p.setText([ 'gain' ], '7', oldRevision - 1), false);
	eq(p.setText([ 'missing' ], '7', oldRevision), false);
	eq(p.snapshot().revision, oldRevision);
	eq(p.setText([ 'gain' ], '4.25', oldRevision), true);
	// Real core capacity refusal before an event-loop activation preserves both
	// existing Field snapshot and draft. No fake result or backend is involved.
	const bound = mode === 'dynamic' ? client.bind('FieldRead', 0).endpoint : undefined;
	const externalRead =
	    mode === 'dynamic' ? client.readField(bound) : client.read(sdk.control.fields.Config);
	assert(externalRead.requestId > 0);
	checks++;
	eq(p.refresh(), false);
	eq(p.snapshot().hasValue, true);
	eq(p.snapshot().hasDraft, true);
	await externalRead;
	await Promise.resolve();
	eq(p.snapshot().outcomes.read.status, Status.QueueFull);
	let reentry = 0;
	let reentryResults;
	const stopReentry = p.subscribe(() => {
		reentry++;
		reentryResults = [
			p.select(endpoint('Config').key), p.refresh(), p.beginEdit(), p.setWritesEnabled(false)
		];
	});
	eq(p.setWritesEnabled(true), true);
	eq(reentry, 1);
	for (const result of reentryResults)
		eq(result, false);
	stopReentry();
	eq(p.setText([ 'large' ], '18446744073709551616', p.snapshot().revision), false);
	eq(p.rows([], {source : 'draft'}).rows.find(row => row.name === 'large').text,
	   '18446744073709551616');
	eq(p.snapshot().actions.apply, false);
	eq(p.setText([ 'large' ], '18446744073709551615', p.snapshot().revision), true);
	eq(p.setText([ 'gain' ], '15', p.snapshot().revision), true);
	eq(p.apply(), true);
	await done();
	eq(p.rows().rows.find(row => row.name === 'gain').text, '10');
	eq(p.snapshot().outcomes.write.succeeded, true);
	eq(p.snapshot().outcomes.write.name, 'Ok');
	eq(p.snapshot().outcomes.write.display, 'Applied');
	eq(p.snapshot().outcomes.readback.succeeded, true);
	eq(p.snapshot().hasDraft, false);
	eq(p.select(endpoint('Accepted', 2).key), true);
	eq(p.snapshot().hasDraft, true);
	eq(p.rows([], {source : 'draft'}).kind, 'void');
	eq(p.execute(), true);
	await done();
	eq(p.snapshot().outcomes.command.accepted, true);
	eq(p.snapshot().outcomes.command.name, 'Ok');
	eq(p.snapshot().outcomes.command.display, 'Accepted');
	eq(p.snapshot().outcomes.command.executed, false);
	eq(p.select(endpoint('Echo', 3).key), true);
	eq(p.setText([ 'gain' ], '-1', p.snapshot().revision), true);
	eq(p.call(), true);
	await done();
	eq(p.snapshot().outcomes.service.status, 0);
	eq(p.snapshot().outcomes.service.name, 'Ok');
	eq(p.snapshot().outcomes.service.display, 'InvalidArgument');
	eq(p.snapshot().outcomes.service.endpointStatus, 1);
	eq(p.snapshot().outcomes.service.succeeded, false);
	assert.match(p.snapshot().error, /InvalidArgument/);
	checks++;
	eq(p.select(endpoint('Empty', 3).key), true);
	eq(p.call(), true);
	await done();
	eq(p.rows([], {source : 'response'}).kind, 'struct');
	eq(p.rows([], {source : 'response'}).available, true);
	eq(p.rows([], {source : 'response'}).total, 0);
	eq(p.select(endpoint('Ping', 3).key), true);
	eq(p.call(), true);
	await done();
	eq(p.rows([], {source : 'response'}).kind, 'void');
	eq(p.rows([], {source : 'response'}).available, false);
	eq(p.select(endpoint('Config').key), true);
	eq(p.refresh(), true);
	await done();
	eq(p.beginEdit(), true);
	eq(p.setText([ 'nested', 'matrix', 1, 1 ], '-15', p.snapshot().revision), true);
	eq(p.rows([ 'nested', 'matrix', 1 ], {source : 'draft'}).rows[1].text, '-15');
	hold = true;
	eq(p.apply(), true);
	for (let i = 0; !release && i < 1000; i++)
		await new Promise(resolve => setTimeout(resolve, 1));
	assert(release);
	p.invalidate();
	eq(p.snapshot().current, false);
	eq(p.sync(), false);
	eq(p.snapshot().hasDraft, false);
	eq(p.snapshot().hasValue, false);
	hold = false;
	release();
	await done();
	eq(p.snapshot().outcomes.write.succeeded, true);
	eq(p.snapshot().outcomes.write.valueStatus, 'Stale');
	eq(p.snapshot().outcomes.write.historical, true);
	eq(p.rows().rows.length, 0);
	unsubscribe();
	client.close();
	peer.stdin.end();
	eq(await exit, 0);
	eq(errors, '');
	assert(events > 0);
	checks++;
	console.log(JSON.stringify({mode, checks, frames}));
}
console.log(JSON.stringify({passed : true, checks}));
