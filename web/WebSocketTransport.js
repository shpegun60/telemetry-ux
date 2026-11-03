// A borrowed, complete-frame WebSocket carrier over the existing SDK backend. MIT.
import {EncodedBackend} from 'telemetry-sdk/client';

const ownedSockets = new WeakSet();
const OPEN = 1;

function integer(value, minimum, maximum, name)
{
	if (!Number.isSafeInteger(value) || value < minimum || value > maximum)
		throw new RangeError(name);
	return value;
}

/** Bind one already-open, exclusively borrowed socket. The host owns its lifecycle. */
export function bindWebSocket(socket, options)
{
	if (!socket || typeof socket !== 'object' || !options || typeof options !== 'object')
		throw new TypeError('An open WebSocket and transport options are required');
	// Snapshot caller options before claiming or modifying the connection.
	const notify = options.onDisconnect;
	const frameLimit = integer(options.maxFrameBytes ?? 65536, 32, 65536, 'maxFrameBytes');
	const bufferLimit = integer(options.maxBufferedBytes ?? frameLimit * 2, frameLimit, 0x7fffffff,
	                            'maxBufferedBytes');
	const supplied = options.encodedOptions ?? {};
	if (!supplied || typeof supplied !== 'object' || typeof notify !== 'function')
		throw new TypeError('onDisconnect must be a function; encodedOptions must be an object');
	const readMany = supplied.readMany, batch = supplied.batch;
	if ((readMany !== undefined && typeof readMany !== 'boolean') ||
	    (batch !== undefined && typeof batch !== 'boolean'))
		throw new TypeError('Encoded capability options must be boolean');
	const add = socket.addEventListener, remove = socket.removeEventListener, send = socket.send;
	const previousBinaryType = socket.binaryType;
	if (typeof add !== 'function' || typeof remove !== 'function' || typeof send !== 'function')
		throw new TypeError('A WebSocket EventTarget is required');
	// Getters above may have changed or claimed the socket. Never transfer ownership.
	if (socket.readyState !== OPEN)
		throw new Error('Bind only an already-open WebSocket');
	if (ownedSockets.has(socket))
		throw new Error(
		    'This WebSocket already had a Telemetry carrier; reconnect with a new socket');

	let ended = false;
	const listeners = [];

	function detach()
	{
		for (const [name, listener] of listeners)
			remove.call(socket, name, listener);
		listeners.length = 0;
	}

	function end(kind, code = 0, reason = '', wasClean = false)
	{
		if (ended)
			return;
		ended = true;
		detach();
		// This describes logical binding termination, never remote quiescence.
		notify(Object.freeze({kind, code, reason : String(reason).slice(0, 160), wasClean}));
	}

	const backend = new EncodedBackend((packet, context) => {
		const state = socket.readyState, queued = socket.bufferedAmount;
		if (ended || context.signal.aborted || state !== OPEN)
			throw new Error('WebSocket carrier is not open');
		if (!Number.isSafeInteger(queued) || queued < 0 || packet.byteLength > frameLimit ||
		    queued > bufferLimit - packet.byteLength)
			throw new Error('WebSocket transport buffer limit');
		// WebSocket.send copies the supplied bytes. Its return is not an execution ACK.
		send.call(socket, packet);
	}, {readMany : readMany === true, batch : batch === true});
	const message = event => {
		if (ended)
			return;
		const data = event.data;
		let length = 0;
		try {
			if (data instanceof ArrayBuffer || ArrayBuffer.isView(data))
				length = data.byteLength;
		} catch {
			// Detached or otherwise unusable browser data is not a complete frame.
		}
		if (length < 32 || length > frameLimit) {
			end('invalid-frame', 0, 'Expected a bounded complete binary Encoded frame');
			return;
		}
		backend.receive(data);
	};
	const close = event => end('close', event.code, event.reason, event.wasClean === true);
	const error = () => end('error', 0, 'WebSocket transport error');
	ownedSockets.add(socket);
	try {
		socket.binaryType = 'arraybuffer';
		for (const entry of [[ 'message', message ], [ 'close', close ], [ 'error', error ]]) {
			listeners.push(entry);
			add.call(socket, entry[0], entry[1]);
		}
		const state = socket.readyState;
		if (ended || state !== OPEN)
			throw new Error('The WebSocket closed during binding');
	} catch (failure) {
		ended = true;
		detach();
		ownedSockets.delete(socket);
		try {
			socket.binaryType = previousBinaryType;
		} catch {
			// Keep the original binding error; the socket still belongs to its host.
		}
		throw failure;
	}
	return Object.freeze({backend, dispose : () => end('detached')});
}
