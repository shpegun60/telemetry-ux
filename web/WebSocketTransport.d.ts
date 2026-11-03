/* Borrowed browser WebSocket carrier; no additional SDK request queue. MIT. */
import type {EncodedBackend, EncodedOptions} from 'telemetry-sdk/client';

export interface WebSocketDisconnect {
	readonly kind: 'close'|'error'|'invalid-frame'|'detached';
	readonly code: number;
	readonly reason: string;
	readonly wasClean: boolean;
}

export interface WebSocketTransportOptions {
	/** Logical termination only. The host invalidates its provider and closes its client/socket. */
	readonly onDisconnect: (event: WebSocketDisconnect) => void;
	/** Complete Encoded frame limit, 32..65536 bytes; default 65536. */
	readonly maxFrameBytes?: number;
	/** Browser send-buffer budget, at least maxFrameBytes; default twice that limit. */
	readonly maxBufferedBytes?: number;
	/** Local opt-in only; the host must independently admit matching remote capabilities. */
	readonly encodedOptions?: EncodedOptions;
}

export interface WebSocketTransport {
	readonly backend: EncodedBackend;
	/** Ends this binding and notifies once; never closes the borrowed socket or asserts quiescence. */
	dispose(): void;
}

/** One binding per open socket lifetime. Bootstrap, admission, recovery and physical close stay with the host. */
export function bindWebSocket(socket: WebSocket,
                              options: WebSocketTransportOptions): WebSocketTransport;
