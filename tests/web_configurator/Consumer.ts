/* A user supplies one already-owned SDK client and one host element. MIT. */
import type {DeviceClient} from 'telemetry-sdk/client';
import type {DynamicClient} from 'telemetry-sdk/dynamic';
import {createStaticProvider, createDynamicProvider, mountConfigurator} from 'telemetry-ux/web';
import * as sdk from './generated/js/device.js';
import {structuralMetadata} from './generated/js/device.metadata.js';
import {mountDevice} from './generated/ui/DevicePage.js';
import type {PresentationProvider, PresentationState} from 'telemetry-ux/web';
import type {PresentationOverlay} from 'telemetry-ux/web';
const presentation: PresentationOverlay = {
    format: 'telemetry-presentation', version: 1, fingerprint: structuralMetadata.fingerprint,
    entries: [{category: 1, id: 0, role: 'value', path: ['large'], maximum: '18446744073709551615'}]
};
// @ts-expect-error presentation numeric hints are exact text, not JavaScript Number
const badHint: PresentationOverlay = {...presentation, entries: [{category: 1, id: 0, role: 'value', path: [], maximum: 18446744073709551615}]};
// @ts-expect-error Commands have request hints, never Field value hints
const badRole: PresentationOverlay = {...presentation, entries: [{category: 2, id: 0, role: 'value', path: [], label: 'Wrong'}]};
void badHint; void badRole;

// A pre-Watchlist host provider remains a valid renderer contract.
type LegacyProvider = Pick<PresentationProvider,
    'snapshot' | 'subscribe' | 'rows' | 'invalidate' | 'sync' | 'select' |
    'setWritesEnabled' | 'setText' | 'refresh' | 'beginEdit' | 'cancelEdit' |
    'apply' | 'execute' | 'call'>;
declare const legacy: LegacyProvider;
const compatibleProvider: PresentationProvider = legacy;
const compatibleState: PresentationState = {...legacy.snapshot(), watch: undefined};
void compatibleProvider;
void compatibleState;

export function generatedWindow(client: DeviceClient, host: HTMLElement) {
    const page = mountDevice(host, {client});
    page.provider.snapshot();
	page.provider.addBatch('read');
	page.provider.addWatch();
    return page;
}

export function staticWindow(client: DeviceClient, host: HTMLElement) {
    const provider = createStaticProvider({client, metadata: structuralMetadata, sdk, presentation});
    const view = mountConfigurator(host, {provider});
    const state = provider.snapshot();
    const watchCount: number = state.watch.rows.length;
    provider.addWatch();
    provider.setWatchInterval(1000);
    provider.tickWatch(1000);
	provider.addBatch('read');
	provider.setBatchCapability(new Uint8Array(24));
	provider.executeBatch();
	provider.rows([], {source: 'batchResponse'});
	const batchCount: number = provider.snapshot().batch.items.length;
	// @ts-expect-error operations use the checked public operation names
	provider.addBatch('arbitrary');
	void batchCount;
    // @ts-expect-error owner clock is an explicit finite millisecond number
    provider.startWatch('1000');
    void watchCount;
    const page = provider.rows([], {source: 'value', offset: 0, limit: 128});
    const text: string | undefined = page.rows[0]?.text;
    const unsubscribe = provider.subscribe(next => {
        const current: boolean = next.current;
        const accepted: boolean | undefined = next.outcomes.command?.accepted;
        void current;
        void accepted;
    });
    // @ts-expect-error callers cannot mutate a published presentation snapshot
    state.revision = 3;
    // @ts-expect-error scalar input is exact raw text, not a numeric conversion
    provider.setText(['large'], 9007199254740993, state.revision);
    // @ts-expect-error an explicit provider is required
    mountConfigurator(host, {});
    return {provider, view, unsubscribe, text};
}

export function dynamicWindow(client: DynamicClient, host: HTMLElement) {
    const provider = createDynamicProvider({client});
    const view = mountConfigurator(host, {provider});
    const state = provider.snapshot();
    provider.setText(['large'], '18446744073709551615', state.revision);
    // @ts-expect-error rows expose immutable presentation records
    provider.rows().rows.push({});
    return {provider, view};
}
