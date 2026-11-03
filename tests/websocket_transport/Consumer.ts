// Public installed transport export: positive inference and meaningful strict negatives. MIT.
import {EncodedBackend} from 'telemetry-sdk/client';
import {
	bindWebSocket,
	type WebSocketDisconnect,
	type WebSocketTransport,
	type WebSocketTransportOptions
} from 'telemetry-ux/transports/websocket';

declare const socket: WebSocket;
const onDisconnect = (event: WebSocketDisconnect): void => {
	const reason: string = event.reason;
	const clean: boolean = event.wasClean;
	const code: number = event.code;
	const kind: 'close'|'error'|'invalid-frame'|'detached' = event.kind;
	void[reason, clean, code, kind];
	// @ts-expect-error disconnect facts are immutable
	event.kind = 'close';
};
const options: WebSocketTransportOptions = {
	onDisconnect,
	maxFrameBytes : 65536,
	maxBufferedBytes : 131072,
	encodedOptions : {readMany : true, batch : false}
};
const carrier: WebSocketTransport = bindWebSocket(socket, options);
const backend: EncodedBackend = carrier.backend;
backend.supports('FieldRead');
carrier.dispose();
bindWebSocket(socket, {onDisconnect});

// @ts-expect-error the host must provide a disconnect callback
bindWebSocket(socket, {});
bindWebSocket(socket, {
	// @ts-expect-error callback receives typed disconnect facts
	onDisconnect : (event: number) => {
	    void event;
	}
});
// @ts-expect-error limits are numeric
bindWebSocket(socket, {onDisconnect, maxFrameBytes : '65536'});
// @ts-expect-error this carrier has no hidden retry option
bindWebSocket(socket, {onDisconnect, retry : true});
// @ts-expect-error local capabilities require actual booleans
bindWebSocket(socket, {onDisconnect, encodedOptions : {batch : 'yes'}});
// @ts-expect-error an arbitrary EventTarget is not a WebSocket
bindWebSocket(new EventTarget(), {onDisconnect});
// @ts-expect-error transport handle cannot replace its SDK backend
carrier.backend = backend;
