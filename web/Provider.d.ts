/* Explicit-provider presentation over Telemetry SDK. SPDX-License-Identifier: MIT. */
import type {ByteInput, DeviceClient} from 'telemetry-sdk/client';
import type {DynamicClient} from 'telemetry-sdk/dynamic';
import type {
	StaticConnectionSession, DynamicConnectionSession, SessionSnapshot} from
	'telemetry-sdk/session';

export type ValuePath = readonly(string|number)[];
export type ValueSource = 'value'|'draft'|'response';
export type BatchValueSource = 'batchDraft'|'batchResponse';
export type BatchOperation = 'read'|'write'|'command'|'service';
export interface PresentationLimits {
	readonly nodes?: number;
	readonly depth?: number;
	readonly wireBytes?: number;
}
export interface PresentationEndpoint {
	readonly key: string;
	readonly category: 1|2|3;
	readonly id: number;
	readonly catalog: string;
	readonly name: string;
}
export type OutcomeKind = 'read'|'write'|'readback'|'command'|'service';
export interface PresentationOutcome {
	readonly operation: OutcomeKind;
	readonly endpointKey: string;
	readonly endpointName: string;
	readonly status: number|null;
	readonly name: string;
	readonly display: string;
	readonly session: number;
	readonly requestId: number;
	readonly dispatch: number|null;
	readonly endpointStatus: number|null;
	readonly succeeded: boolean;
	readonly accepted: boolean;
	readonly executed: boolean;
	readonly valueStatus: string;
	readonly historical: boolean;
}
export interface PresentationState {
	readonly mode: 'static'|'dynamic';
	readonly current: boolean;
	readonly session: number;
	readonly endpoints: readonly PresentationEndpoint[];
	readonly selected: PresentationEndpoint|null;
	readonly busy: boolean;
	readonly writesEnabled: boolean;
	readonly revision: number;
	readonly hasValue: boolean;
	readonly hasDraft: boolean;
	readonly hasResponse: boolean;
	readonly dirty: boolean;
	readonly error: string;
	readonly outcomes: Readonly<Partial<Record<OutcomeKind, PresentationOutcome>>>;
	readonly watch?: PresentationWatch;
	readonly batch?: PresentationBatch;
	readonly actions:
	    Readonly<Record<'refresh'|'beginEdit'|'cancelEdit'|'apply'|'execute'|'call', boolean>>;
}
export interface PresentationWatchRow {
	readonly key: string;
	readonly id: number;
	readonly name: string;
	readonly catalog: string;
	readonly status: string;
	readonly text: string;
}
export interface PresentationWatch {
	readonly rows: readonly PresentationWatchRow[];
	readonly active: boolean;
	readonly pending: boolean;
	readonly running: boolean;
	readonly individual: boolean;
	readonly intervalMs: number;
	readonly status: string;
	readonly canAdd: boolean;
	readonly canRefresh: boolean;
}
export interface PresentationRow {
	readonly key: string;
	readonly path: ValuePath;
	readonly name: string;
	readonly kind: 'void'|'scalar'|'enum'|'struct'|'array';
	readonly type: string;
	readonly children: number;
	readonly text: string;
	readonly error: string;
	readonly editable: boolean;
	readonly enumEntries: readonly Readonly<{code : string; name : string}>[];
}
export interface PresentationRows {
	readonly path: ValuePath;
	readonly total: number;
	readonly offset: number;
	readonly kind: PresentationRow['kind']|null;
	readonly available: boolean;
	readonly rows: readonly PresentationRow[];
}
export interface PresentationProvider {
	snapshot(): PresentationState;
	subscribe(listener: (state: PresentationState) => void): () => void;
	rows(path?: ValuePath, options
	     ?: Readonly<{source?: ValueSource; offset?: number; limit?: number}>): PresentationRows;
	/** Notify before the host revokes, resets or closes the supplied client. */
	invalidate(): void;
	/** Notify after admission; the same explicitly revoked session stays hidden. */
	sync(): boolean;
	select(key: string): boolean;
	setWritesEnabled(enabled: boolean): boolean;
	setText(path: ValuePath, text: string, revision: number): boolean;
	refresh(): boolean;
	beginEdit(): boolean;
	cancelEdit(): boolean;
	apply(): boolean;
	execute(): boolean;
	call(): boolean;
}
// Existing host-written providers need not implement the optional Watchlist.
export interface WatchPresentationState extends PresentationState {
	readonly watch: PresentationWatch;
}
export interface WatchPresentationProvider extends PresentationProvider {
	snapshot(): WatchPresentationState;
	/** Add a readable scalar/enum Field. One cycle reads ordered chunks of at most 32 IDs. */
	addWatch(key?: string): boolean;
	removeWatch(key: string): boolean;
	clearWatch(): boolean;
	/** Explicit fallback; false means one ReadMany request, never implicit fan-out. */
	setWatchIndividual(individual: boolean): boolean;
	/** Nominal whole milliseconds, 1..60000. Actual cadence depends on transport/owner scheduling. */
	setWatchInterval(milliseconds: number): boolean;
	/** Caller discovers TRM1 and resupplies it after each admission. SDK validates it. */
	setReadManyCapability(input: ByteInput): boolean;
	refreshWatch(): boolean;
	/** Owner supplies a monotonic millisecond clock, also used for tickWatch(). */
	startWatch(nowMs: number): boolean;
	/** Stops polling; an accepted request remains pending until completion/drain. */
	stopWatch(): boolean;
	/** Call from the host loop. Missed ticks are skipped; no timer is created. */
	tickWatch(nowMs: number): boolean;
}
export interface PresentationBatchOutcome {
	readonly status: number|null;
	readonly name: string;
	readonly display: string;
	readonly session: number;
	readonly requestId: number;
	readonly succeeded: boolean;
	readonly valueStatus: string;
	readonly historical: boolean;
}
export interface PresentationBatchItem {
	readonly key: string;
	readonly endpointKey: string;
	readonly category: 1|2|3;
	readonly name: string;
	readonly catalog: string;
	readonly operation: BatchOperation;
	readonly status: string;
	readonly outcome?: PresentationOutcome;
}
export interface PresentationBatch {
	readonly items: readonly PresentationBatchItem[];
	readonly selectedKey: string;
	readonly active: boolean;
	readonly pending: boolean;
	readonly status: string;
	readonly canAdd: boolean;
	readonly addOperations: readonly BatchOperation[];
	readonly canExecute: boolean;
	readonly hasDraft: boolean;
	readonly hasResponse: boolean;
	readonly outcome?: PresentationBatchOutcome;
}
export interface BatchPresentationState extends WatchPresentationState {
	readonly batch: PresentationBatch;
}
export interface BatchPresentationProvider extends WatchPresentationProvider {
	snapshot(): BatchPresentationState;
	rows(path?: ValuePath, options
	     ?: Readonly<{source?: ValueSource | BatchValueSource; offset?: number; limit?: number}>):
	    PresentationRows;
	/** Initializes new full replacement DTOs with canonical zeros, not device defaults. */
	addBatch(operation: BatchOperation, endpointKey?: string): boolean;
	removeBatch(key: string): boolean;
	moveBatch(key: string, delta: -1|1): boolean;
	selectBatch(key: string): boolean;
	clearBatch(): boolean;
	setBatchText(path: ValuePath, text: string, revision: number): boolean;
	/** Caller discovers TBM1 independently of TRM1 and resupplies after admission. */
	setBatchCapability(input: ByteInput): boolean;
	/** One SDK Batch request. No transaction, replay, automatic readback or exactly-once promise. */
	executeBatch(): boolean;
}

// Compatible with the optional generated StructuralMetadata v1 module. The UX
// receives its graph, never parses a Descriptor or owns an SDK RuntimeSchema.
export interface StructuralTypeInput {
	readonly id: number;
	readonly kind: number;
	readonly wireBytes: number;
}
export interface StructuralEndpointInput {
	readonly category: 1|2|3;
	readonly id: number;
	readonly catalogName: string;
	readonly groupSymbol: string;
	readonly symbol: string;
	readonly name: string;
	readonly valueType: number;
	readonly requestType: number;
	readonly responseType: number;
	readonly capabilities: number;
}
export interface StructuralInput {
	readonly format: 'telemetry-structural-metadata';
	readonly version: 1;
	readonly fingerprint: string;
	readonly types: readonly StructuralTypeInput[];
	readonly endpoints: readonly StructuralEndpointInput[];
}
export function createStaticProvider(options: {
	readonly client: DeviceClient; readonly metadata : StructuralInput; readonly sdk : {
		readonly fingerprint : bigint; readonly codecs : object
	};
	readonly limits?: PresentationLimits;
}): BatchPresentationProvider;
export function createDynamicProvider(
    options: {readonly client: DynamicClient; readonly limits?: PresentationLimits;}):
    BatchPresentationProvider;

export type SessionEndpointSelector =
    Readonly < {category : 'field' | 'command' | 'service'; catalog : string; name : string}|
{
	category: 1|2|3;
	id: number
}
> ;
export interface SessionProviderOptions {
	readonly session: StaticConnectionSession|DynamicConnectionSession;
	readonly limits?: PresentationLimits;
	/** Resolve one endpoint without I/O. Reconnect restores selection only for an unchanged fingerprint. */
	readonly endpoint?: SessionEndpointSelector;
}
export interface SessionPresentationState extends BatchPresentationState {
	readonly connection: SessionSnapshot;
	readonly disposed: boolean;
	/** Peer or explicit invalidation hides values but preserves stale draft text until cancel/restart. */
	readonly dataStale: boolean;
	readonly dataInvalidationReason: string;
}
export interface SessionPresentationProvider extends BatchPresentationProvider {
	snapshot(): SessionPresentationState;
	subscribe(listener: (state: SessionPresentationState) => void): () => void;
	/** Retire this presentation. Accepted requests retain their outcomes; the borrowed Session stays owned by the host. */
	dispose(): void;
	/** Invalidate data in every live provider borrowing this Session after direct/headless operations. */
	invalidateData(reason?: string): void;
}
/** One local presentation, including optional Watchlist/Batch, over a borrowed Session and its existing FIFO. */
export function createSessionProvider(options: SessionProviderOptions): SessionPresentationProvider;
