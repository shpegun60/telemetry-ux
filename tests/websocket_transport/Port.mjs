// Installed carrier boundary controls: real EncodedBackend, fake browser socket. MIT.
import assert from 'node:assert/strict';
import {EncodedBackend, Operations, Status} from 'telemetry-sdk/client';
import {bindWebSocket} from 'telemetry-ux/transports/websocket';

let checks = 0;

function check(value, message)
{
	++checks;
	assert.ok(value, message);
}

function equal(actual, expected, message)
{
	++checks;
	assert.deepEqual(actual, expected, message);
}

function refuses(action, expression, message)
{
	++checks;
	assert.throws(action, expression, message);
}

class Socket {
	readyState = 1;
	bufferedAmount = 0;
	binaryType = 'blob';
	listeners = new Map();
	sent = [];
	closes = 0;
	addHook;
	sendHook;

	addEventListener(name, listener)
	{
		if (!this.listeners.has(name))
			this.listeners.set(name, new Set());
		this.listeners.get(name).add(listener);
		this.addHook?.(name, listener);
	}

	removeEventListener(name, listener)
	{
		this.listeners.get(name)?.delete(listener);
	}

	send(packet)
	{
		this.sent.push(new Uint8Array(packet));
		this.sendHook?.(packet);
	}

	close()
	{
		++this.closes;
		this.readyState = 3;
	}

	emit(name, event = {})
	{
		for (const listener of [...(this.listeners.get(name) || [])])
			listener(event);
	}

	get listenerCount()
	{
		return [...this.listeners.values() ].reduce((sum, set) => sum + set.size, 0);
	}
}

function binding(options = {})
{
	const socket = new Socket(), events = [];
	const port = bindWebSocket(socket, {
		onDisconnect(event) {
		    check(socket.listenerCount === 0, 'listeners detach before host notification');
		    check(Object.isFrozen(event), 'disconnect event is immutable');
		    events.push(event);
		},
		...options
	});
	return {socket, port, events};
}

function start(backend, options = {})
{
	const controller = new AbortController();
	let handoffs = 0;
	const request = {
		operation : 'FieldRead',
		session : 17,
		requestId : 1,
		endpointId : 7,
		payload : new Uint8Array(),
		responseBytes : 4,
		...options.request
	};
	const context = {
		session : request.session,
		requestId : request.requestId,
		signal : controller.signal,
		clock : {now : () => 0, setTimeout : () => 0, clearTimeout() {}},
		handoff(mayExecute) {
		    ++handoffs;
		    check(mayExecute === true, 'Encoded backend conservatively authorizes a handoff');
		    return options.authorize !== false;
		}
	};
	if (options.aborted)
		controller.abort();
	// Observe rejection immediately. This fixture owns no DeviceClient or FIFO.
	const completion =
	    backend.execute(request, context).then(result => ({result}), error => ({error}));
	return {
		controller,
		request,
		context,
		completion,
		get handoffs() {
		    return handoffs;
		}
	};
}

function reply(request, payload = new Uint8Array([ 1, 2, 3, 4 ]))
{
	const frame = new Uint8Array(32 + payload.length), view = new DataView(frame.buffer);
	view.setUint32(0, 0x31514554, true);
	frame[4] = frame[5] = 1;
	frame[6] = Operations[request.operation];
	view.setUint32(8, request.session, true);
	view.setUint32(12, request.requestId, true);
	view.setUint32(16, request.endpointId, true);
	view.setUint32(20, payload.length, true);
	view.setUint32(24, request.responseBytes, true);
	frame.set(payload, 32);
	return frame;
}

// Public export and capability opt-in, copied options and one borrowed lifetime.
{
	const options = {encodedOptions : {readMany : false, batch : false}};
	const {socket, port, events} = binding(options);
	check(port.backend instanceof EncodedBackend, 'existing SDK backend is reused');
	check(Object.isFrozen(port), 'binding handle is immutable');
	equal(socket.binaryType, 'arraybuffer');
	equal(socket.listenerCount, 3);
	for (const operation of ['FieldRead', 'FieldWrite', 'Command', 'Service'])
		check(port.backend.supports(operation), 'ordinary operation stays enabled');
	check(!port.backend.supports('ReadMany') && !port.backend.supports('Batch'),
	      'caps default off');
	options.encodedOptions.readMany = options.encodedOptions.batch = true;
	check(!port.backend.supports('ReadMany') && !port.backend.supports('Batch'),
	      'options snapshotted');
	refuses(() => bindWebSocket(socket, {onDisconnect() {}}), /already had/);
	port.dispose();
	port.dispose();
	equal(events.map(event => event.kind), [ 'detached' ]);
	equal(socket.closes, 0, 'dispose never closes host socket');
	refuses(() => bindWebSocket(socket, {onDisconnect() {}}), /already had/,
	        'successful binding cannot be transferred later in that socket lifetime');
	const enabled = binding({encodedOptions : {readMany : true, batch : true}});
	check(enabled.port.backend.supports('ReadMany') && enabled.port.backend.supports('Batch'));
	enabled.port.dispose();
}

// Full binary frames, correlation and owned SDK reply payload across socket reuse.
for (const form of ['arraybuffer', 'view']) {
	const {socket, port, events} = binding();
	const active = start(port.backend);
	equal(socket.sent.length, 1);
	equal(active.handoffs, 1);
	const wire = socket.sent[0];
	equal(wire.length, 32);
	equal(new DataView(wire.buffer).getUint32(0, true), 0x31514554);
	equal(wire[6], Operations.FieldRead);
	equal(new DataView(wire.buffer).getUint32(12, true), active.request.requestId);
	let settled = false;
	active.completion.then(() => {
		settled = true;
	});
	socket.emit('message', {data : reply({...active.request, requestId : 999}).buffer});
	await Promise.resolve();
	check(!settled && events.length === 0, 'unrelated complete reply cannot finish live request');
	const frame = reply(active.request);
	socket.emit('message',
	            {data : form === 'arraybuffer' ? frame.buffer : new DataView(frame.buffer)});
	frame.fill(99);
	const {result, error} = await active.completion;
	check(!error && result.dispatch === 0 && result.endpointStatus === 0);
	equal([...result.payload ], [ 1, 2, 3, 4 ], 'received data is owned before host reuse');
	await result.released;
	const second = start(port.backend, {request : {requestId : 2}});
	socket.emit('message', {data : reply(second.request).buffer});
	check(!!(await second.completion).result, 'next explicit operation reuses existing backend');
	equal(socket.sent.length, 2, 'no automatic send or replay');
	port.dispose();
}

// A mutating request is copied into the complete frame; this is not an execution guarantee.
{
	const {socket, port} = binding(), payload = new Uint8Array([ 7, 8 ]);
	const active =
	    start(port.backend, {request : {operation : 'Command', payload, responseBytes : 0}});
	payload.fill(99);
	equal([...socket.sent[0].subarray(32) ], [ 7, 8 ], 'send owns a complete copied request');
	equal(socket.sent[0][6], Operations.Command);
	socket.emit('message', {data : reply(active.request, new Uint8Array()).buffer});
	const {result} = await active.completion;
	check(result.dispatch === 0 && result.endpointStatus === 0);
	equal(result.payload.length, 0);
	equal(socket.sent.length, 1);
	port.dispose();
}

// Invalid carrier data ends once, detaches before callback, and lets host settle its client.
for (const data of ['text', new Blob([ 'binary-looking text' ]), new ArrayBuffer(0),
                    new ArrayBuffer(31), new ArrayBuffer(65537)]) {
	const {socket, port, events} = binding();
	const active = start(port.backend);
	socket.emit('message', {data});
	equal(events.map(event => event.kind), [ 'invalid-frame' ]);
	equal(socket.listenerCount, 0);
	socket.emit('message', {data : reply(active.request).buffer});
	socket.emit('close', {code : 1000, reason : 'later', wasClean : true});
	port.dispose();
	equal(events.length, 1, 'invalid-frame completion notified once');
	active.controller.abort();
	check((await active.completion).error?.status === Status.Cancelled,
	      'host explicitly cancels backend; carrier does not own its client');
	equal(socket.sent.length, 1);
	equal(socket.closes, 0);
}

// A structurally malformed Encoded reply belongs to SDK validation, not a second parser.
{
	const {socket, port, events} = binding();
	const active = start(port.backend), frame = reply(active.request);
	frame[0] = 0;
	socket.emit('message', {data : frame.buffer});
	check((await active.completion).error?.status === Status.MalformedReply);
	equal(events.length, 0, 'carrier does not replace SDK wire validation');
	port.dispose();
}

// Close/error/dispose callback reentry and throws retain deterministic ownership.
for (const kind of ['close', 'error', 'detached']) {
	const socket = new Socket();
	let port, calls = 0;
	port = bindWebSocket(socket, {
		onDisconnect(event) {
		    ++calls;
		    equal(socket.listenerCount, 0);
		    equal(event.kind, kind);
		    if (kind === 'close') {
			    equal(event.code, 1001);
			    equal(event.reason, 'x'.repeat(160), 'host reason is bounded');
			    check(event.wasClean);
		    }
		    port.dispose();
		    throw new Error('host-notification');
		}
	});
	refuses(() => kind === 'detached'
	                  ? port.dispose()
	                  : socket.emit(kind, {code : 1001, reason : 'x'.repeat(200), wasClean : true}),
	        /host-notification/);
	port.dispose();
	socket.emit('error');
	equal(calls, 1);
	equal(socket.listenerCount, 0);
	equal(socket.closes, 0);
	const rejected = start(port.backend);
	check(!!(await rejected.completion).error);
	equal(rejected.handoffs, 1,
	      'failure remains after SDK handoff; no unsent guarantee is invented');
	equal(socket.sent.length, 0);
}

// Send bounds/errors are conservative after handoff, with no transport retries.
for (const variant of ['pressure', 'nan', 'negative', 'frame', 'closed', 'send-error', 'reentry']) {
	const {socket, port, events} = binding({maxFrameBytes : 32, maxBufferedBytes : 64});
	let request = {};
	if (variant === 'pressure')
		socket.bufferedAmount = 33;
	if (variant === 'nan')
		socket.bufferedAmount = NaN;
	if (variant === 'negative')
		socket.bufferedAmount = -1;
	if (variant === 'frame')
		request = {payload : new Uint8Array([ 1 ])};
	if (variant === 'closed')
		socket.readyState = 3;
	if (variant === 'send-error')
		socket.sendHook = () => {
			throw Error('send failed');
		};
	if (variant === 'reentry')
		Object.defineProperty(socket, 'bufferedAmount', {
			get() {
			    port.dispose();
			    return 0;
			}
		});
	const active = start(port.backend, {request});
	check(!!(await active.completion).error);
	equal(active.handoffs, 1);
	equal(socket.sent.length, variant === 'send-error' ? 1 : 0);
	await Promise.resolve();
	equal(socket.sent.length, variant === 'send-error' ? 1 : 0, 'never retries failed send');
	port.dispose();
	equal(events.length, 1);
}

// Only the SDK's explicit denied/aborted handoff establishes no send in these controls.
for (const option of [{authorize : false}, {aborted : true}]) {
	const {socket, port, events} = binding();
	const active = start(port.backend, option);
	check((await active.completion).error?.status === Status.Cancelled);
	equal(active.handoffs, option.aborted ? 0 : 1);
	equal(socket.sent.length, 0);
	equal(events.length, 0);
	port.dispose();
}

// Failed setup is rolled back even when a wrapped add attaches and THEN throws.
for (const failure of ['binary-type', 'add-then-throw', 'close-during-add']) {
	const socket = new Socket();
	let calls = 0, binary = 'blob', armed = true;
	if (failure === 'binary-type')
		Object.defineProperty(socket, 'binaryType', {
			get() {
			    return binary;
			},
			set(value) {
			    if (armed) {
				    armed = false;
				    throw Error('binary failed');
			    }
			    binary = value;
			}
		});
	if (failure === 'add-then-throw')
		socket.addHook = name => {
			if (armed && name === 'close') {
				armed = false;
				throw Error('add failed');
			}
		};
	if (failure === 'close-during-add')
		socket.addHook = name => {
			if (armed && name === 'close') {
				armed = false;
				socket.emit('close', {code : 1001, reason : 'during setup', wasClean : true});
			}
		};
	refuses(() => bindWebSocket(socket, {
		        onDisconnect() {
		            ++calls;
		        }
	        }),
	        /failed|closed during/);
	equal(socket.listenerCount, 0, 'failed setup removes every listener it attempted to attach');
	equal(socket.binaryType, 'blob', 'failed setup restores host setting');
	const retry = bindWebSocket(socket, {
		onDisconnect() {
		    ++calls;
		}
	});
	equal(socket.listenerCount, 3, 'failed constructor did not permanently claim socket');
	retry.dispose();
	equal(calls, failure === 'close-during-add' ? 2 : 1);
}

for (const options
         of [{onDisconnect : null}, {onDisconnect() {}, maxFrameBytes : 31},
             {onDisconnect() {}, maxFrameBytes : 65537}, {onDisconnect() {}, maxBufferedBytes : 1},
             {onDisconnect() {}, encodedOptions : {batch : 'yes'}}]) {
	const socket = new Socket();
	refuses(() => bindWebSocket(socket, options),
	        error => error instanceof TypeError || error instanceof RangeError);
	equal(socket.listenerCount, 0);
	equal(socket.binaryType, 'blob');
}
{
	const socket = new Socket();
	socket.readyState = 0;
	refuses(() => bindWebSocket(socket, {onDisconnect() {}}), /already-open/);
	equal(socket.listenerCount, 0);
}

console.log(JSON.stringify({
	passed : true,
	checks,
	scope :
	    'Installed public carrier + real SDK EncodedBackend; fake socket, no DeviceClient/FIFO or physical delivery guarantee',
	hardware : false
}));
