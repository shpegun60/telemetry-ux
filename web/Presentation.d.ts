/* Advisory presentation metadata; all numbers remain exact text. MIT. */
import type {ValuePath} from './Provider.js';
export interface PresentationHints {
	readonly label?: string;
	readonly unit?: string;
	readonly help?: string;
	/** Display hints only. No range enforcement, conversion, defaults or permissions. */
	readonly minimum?: string;
	readonly maximum?: string;
	readonly step?: string;
}
export type PresentationEntry = PresentationHints & Readonly<{
	category: 1; id: number; role: 'value'; path: ValuePath
}|{category: 2; id: number; role: 'request'; path: ValuePath
}|{category: 3; id: number; role: 'request'|'response'; path: ValuePath}>;
export interface PresentationOverlay {
	readonly format: 'telemetry-presentation';
	readonly version: 1;
	/** Exact generated metadata fingerprint, 0x followed by 16 lowercase hex digits. */
	readonly fingerprint: string;
	readonly entries: readonly PresentationEntry[];
}
/** Internal immutable lookup, also useful for hosts implementing custom renderers. */
export function compilePresentation(input?: PresentationOverlay):
    (identity: bigint|string, category: number, id: number, role: string, path: ValuePath) => PresentationHints;
export function presentationDetail(hints?: PresentationHints, original?: string): string;
