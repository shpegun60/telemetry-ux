/* A user supplies one already-owned SDK client and one host element. MIT. */
import type {DeviceClient} from 'telemetry-sdk/client';
import type {DynamicClient} from 'telemetry-sdk/dynamic';
import {createStaticProvider, createDynamicProvider, mountConfigurator} from 'telemetry-ux/web';
import * as sdk from './generated/js/device.js';
import {structuralMetadata} from './generated/js/device.metadata.js';
import {mountDevice} from './generated/ui/DevicePage.js';
import type {PresentationProvider, PresentationState} from 'telemetry-ux/web';

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
    return page;
}

export function staticWindow(client: DeviceClient, host: HTMLElement) {
    const provider = createStaticProvider({client, metadata: structuralMetadata, sdk});
    const view = mountConfigurator(host, {provider});
    const state = provider.snapshot();
    const watchCount: number = state.watch.rows.length;
    provider.addWatch();
    provider.setWatchInterval(1000);
    provider.tickWatch(1000);
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
