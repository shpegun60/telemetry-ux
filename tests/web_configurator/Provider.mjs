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
	let waiter, errors = '', frames = 0, hold, release, nextReadTransform, lastWrittenPayload;
	let writes = 0;
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
		if (packet[6] === 1) {
			++writes;
			lastWrittenPayload = new Uint8Array(packet.subarray(32));
		}
		const data = await new Promise(resolve => {
			waiter = resolve;
			peer.stdin.write(Buffer.from(packet).toString('hex') + '\n');
		});
		if (hold)
			await new Promise(resolve => release = resolve);
		const reply = new Uint8Array(Buffer.from(data, 'hex'));
		if (packet[6] === 4 && nextReadTransform) {
			const transform = nextReadTransform;
			nextReadTransform = undefined;
			// The real peer has already produced a correlated, successful
			// response. Replace only selected valid float representations.
			eq(reply[7], 0);
			eq(reply[28], 0);
			transform(reply.subarray(32));
		}
		return reply;
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

	async function done(provider = p)
	{
		for (let i = 0; provider.snapshot().busy && i < 1000; i++)
			await new Promise(resolve => setTimeout(resolve, 1));
		eq(provider.snapshot().busy, false);
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

	// Float wire representations are valid input, including signaling NaNs.
	// Edit another leaf through the real provider and inspect the actual
	// FieldWrite packet before the native Model consumes it. The provider
	// must not quiet an untouched NaN or lose a signed zero.
	const config =
	    structuralMetadata.endpoints.find(row => row.category === 1 && row.name === 'Config');
	const configType = structuralMetadata.types[config.valueType];
	const offset = name => configType.members.find(row => row.name === name).wireOffset;
	const f32Offset = offset('f32'), f64Offset = offset('zero'), gainOffset = offset('gain');
	const profiles = [
		{f32 : 0x7fc12345, f64 : 0x7ff8123456789abcn, text : 'NaN'},
		{f32 : 0xffc12345, f64 : 0xfff8123456789abcn, text : 'NaN'},
		{f32 : 0x7fa12345, f64 : 0x7ff0123456789abcn, text : 'NaN'},
		{f32 : 0xffa12345, f64 : 0xfff0123456789abcn, text : 'NaN'},
		{f32 : 0, f64 : 0n, text : '0'}, {f32 : 0x80000000, f64 : 0x8000000000000000n, text : '-0'}
	];

	async function floatDraft(profile)
	{
		let original;
		nextReadTransform = payload => {
			const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
			view.setUint32(f32Offset, profile.f32, true);
			view.setBigUint64(f64Offset, profile.f64, true);
			original = new Uint8Array(payload);
		};
		eq(p.refresh(), true);
		await done();
		eq(nextReadTransform, undefined);
		eq(p.snapshot().hasValue, true);
		eq(p.beginEdit(), true);
		return original;
	}

	async function applied(expected)
	{
		lastWrittenPayload = undefined;
		const before = writes;
		eq(p.apply(), true);
		await done();
		eq(writes, before + 1);
		eq(lastWrittenPayload, expected);
		eq(p.snapshot().outcomes.write.succeeded, true);
		eq(p.snapshot().outcomes.readback.succeeded, true);
		eq(p.snapshot().hasDraft, false);
	}

	for (const [index, profile] of profiles.entries()) {
		const original = await floatDraft(profile);
		const draftRows = p.rows([], {source : 'draft'}).rows;
		eq(draftRows.find(row => row.name === 'f32').text, profile.text);
		eq(draftRows.find(row => row.name === 'zero').text, profile.text);
		eq(p.setText([ 'gain' ], '6.25', p.snapshot().revision), true);
		const expected = new Uint8Array(original);
		new DataView(expected.buffer).setFloat64(gainOffset, 6.25, true);
		if (index === 0) {
			// QueueFull must retain the draft's original wire seed as well
			// as its visible text. The unrelated typed read uses the same FIFO.
			const before = writes;
			const read = mode === 'dynamic' ? client.readField(bound)
			                                : client.read(sdk.control.fields.Config);
			eq(read.requestId > 0, true);
			eq(p.apply(), false);
			eq(p.snapshot().hasDraft, true);
			eq(writes, before);
			await read;
			await Promise.resolve();
			eq(p.snapshot().outcomes.write.status, Status.QueueFull);
		} else if (index === 1) {
			// A subsequent Refresh owns a new snapshot; it cannot silently
			// replace the already edited draft's original base bytes.
			nextReadTransform = payload => {
				new DataView(payload.buffer, payload.byteOffset, payload.byteLength)
				    .setUint32(f32Offset, 0x7fa54321, true);
			};
			eq(p.refresh(), true);
			await done();
			eq(p.snapshot().hasDraft, true);
			eq(p.rows([], {source : 'draft'}).rows.find(row => row.name === 'gain').text, '6.25');
		}
		await applied(expected);
		const written = new DataView(lastWrittenPayload.buffer);
		eq(written.getUint32(f32Offset, true), profile.f32);
		eq(written.getBigUint64(f64Offset, true), profile.f64);
	}
	// Changed leaves must use their new canonical values, while the other
	// float still keeps its original signaling-NaN representation.
	for (const changed of ['f32', 'zero']) {
		const original = await floatDraft(profiles[3]);
		eq(p.setText([ changed ], changed === 'f32' ? '1.5' : '2.5', p.snapshot().revision), true);
		const expected = new Uint8Array(original), view = new DataView(expected.buffer);
		if (changed === 'f32')
			view.setFloat32(f32Offset, 1.5, true);
		else
			view.setFloat64(f64Offset, 2.5, true);
		await applied(expected);
	}
	const positiveZero = await floatDraft(profiles[4]);
	eq(p.setText([ 'f32' ], '-0', p.snapshot().revision), true);
	eq(p.setText([ 'zero' ], '-0', p.snapshot().revision), true);
	const negativeZero = new Uint8Array(positiveZero), zeroView = new DataView(negativeZero.buffer);
	zeroView.setUint32(f32Offset, 0x80000000, true);
	zeroView.setBigUint64(f64Offset, 0x8000000000000000n, true);
	await applied(negativeZero);
	if (mode === 'static') {
		// An older generated SDK is still readable. Missing preserving
		// support must explicitly refuse Apply, without falling back to
		// the ordinary encoder that loses signaling-NaN representations.
		const {encodePreserving, ...legacyWrite} = sdk.control.fields.Config.write;
		eq(typeof encodePreserving, 'function');
		const legacySdk = {
			...sdk,
			control : {
				...sdk.control,
				fields : {
					...sdk.control.fields,
					Config : {...sdk.control.fields.Config, write : legacyWrite}
				}
			}
		};
		const legacy =
		    createStaticProvider({client, metadata : structuralMetadata, sdk : legacySdk});
		eq(legacy.select(endpoint('Config').key), true);
		eq(legacy.refresh(), true);
		await done(legacy);
		eq(legacy.snapshot().hasValue, true);
		eq(legacy.setWritesEnabled(true), true);
		eq(legacy.beginEdit(), true);
		eq(legacy.setText([ 'gain' ], '7.5', legacy.snapshot().revision), true);
		const before = frames;
		eq(legacy.apply(), false);
		eq(legacy.snapshot().hasDraft, true);
		eq(legacy.snapshot().busy, false);
		assert.match(legacy.snapshot().error, /Unsupported.*--wire-editing/);
		checks++;
		eq(frames, before);
		legacy.invalidate();
	}

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
	eq(p.snapshot().writesEnabled, false);
	eq(p.setWritesEnabled(true), false);
	// Reuse the same provider and real SDK client. No new presentation owner can
	// accidentally hide a permission carried across admission.
	async function readmit(session) {
		const previous = client.session;
		if (mode === 'dynamic')
			client.invalidate();
		else
			client.resetSession();
		const ack = await new Promise(resolve => {
			waiter = resolve;
			peer.stdin.write('session ' + session + '\n');
		});
		eq(JSON.parse(ack).session, session);
		if (mode === 'dynamic') {
			eq(client.confirmQuiescence({session : previous, txReleased : true,
			                            oldFramesExcluded : true, remoteSessionClosed : true}), true);
			eq(client.admit(schema, descriptor.subarray(0, 64), session), Status.Ok);
		} else {
			eq(client.confirmSessionClosed(previous), true);
			eq(client.openSession(session, descriptor.subarray(0, 64)), true);
		}
	}
	await readmit(18);
	eq(p.sync(), true);
	eq(p.snapshot().writesEnabled, false);
	const beforePermission = frames;
	for (const category of [1, 2, 3]) {
		eq(p.select(p.snapshot().endpoints.find(row => row.category === category).key), true);
		eq(p.apply(), false);
		eq(p.execute(), false);
		eq(p.call(), false);
	}
	eq(frames, beforePermission);
	eq(p.setWritesEnabled(true), true);
	eq(p.sync(), true);
	eq(p.snapshot().writesEnabled, true); // Same-session sync preserves the user's opt-in.
	await readmit(19); // Direct replacement, with no provider.invalidate() notification.
	eq(p.setWritesEnabled(true), false);
	eq(p.sync(), true);
	eq(p.snapshot().writesEnabled, false);
	eq(frames, beforePermission);
	eq(p.setWritesEnabled(true), true); // Explicit permission in the new admission works.
	eq(p.snapshot().outcomes.write.historical, true);
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
