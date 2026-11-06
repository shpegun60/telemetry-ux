/* Installed public Session -> UX programmer journey. SPDX-License-Identifier: MIT. */
import {connectStatic, connectDynamic} from 'telemetry-sdk';
import type {ConnectionProfile} from 'telemetry-sdk/session';
import {
	createSessionProvider,
	mountConfigurator,
	mountFieldEditor,
	mountCommandPanel,
	mountServicePanel,
	mountWatchlist,
	mountBatchEditor
} from 'telemetry-ux/web';
import type {SessionPresentationState, PresentationProvider} from 'telemetry-ux/web';
import * as sdk from './generated/js/device.js';
import {structuralMetadata} from './generated/js/device.metadata.js';

export async function windows(transport: ConnectionProfile, host: HTMLElement)
{
	const stat = await connectStatic({sdk, metadata : structuralMetadata, transport});
	const generated: typeof sdk = stat.sdk as typeof sdk;
	void generated;
	const dyn = await connectDynamic({transport});
	const full = mountConfigurator(host, {session : stat});
	const field = mountFieldEditor(
	    host,
	    {session : stat, endpoint : {category : 'field', catalog : 'control', name : 'Config'}});
	const command = mountCommandPanel(host, {session : dyn, endpoint : {category : 2, id : 1}});
	const service = mountServicePanel(
	    host,
	    {session : dyn, endpoint : {category : 'service', catalog : 'control', name : 'Inspect'}});
	const provider = createSessionProvider({session : dyn, endpoint : {category : 1, id : 0}});
	const ordinary: PresentationProvider = provider;
	const state: SessionPresentationState = provider.snapshot();
	const ready: boolean = state.connection.ready;
	const disposed: boolean = state.disposed;
	const stale: boolean = state.dataStale;
	const reason: string = state.dataInvalidationReason;
	const stop = provider.subscribe(next => {
		const s: SessionPresentationState = next;
		void s;
	});
	provider.invalidateData('Headless mutation completed');
	provider.dispose();
	full.provider.dispose();
	field.provider.invalidateData();
	full.unmount();
	field.unmount();
	command.unmount();
	service.unmount();
	// @ts-expect-error exactly one source is required
	mountConfigurator(host, {});
	// @ts-expect-error provider and Session ownership choices are exclusive
	mountConfigurator(host, {provider, session : dyn});
	// @ts-expect-error Session provider has no Watchlist admission surface
	provider.refreshWatch();
	// @ts-expect-error Session provider has no Batch admission surface
	provider.executeBatch();
	// @ts-expect-error Session mounts do not provide Watchlist support
	mountWatchlist(host, {session : dyn});
	// @ts-expect-error Session mounts do not provide Batch support
	mountBatchEditor(host, {session : dyn});
	// @ts-expect-error selectors are endpoint categories, not arbitrary strings
	createSessionProvider({session : stat, endpoint : {category : 'anything', id : 0}});
	// @ts-expect-error scalar text remains exact text
	provider.setText([ 'large' ], 9007199254740993, state.revision);
	// @ts-expect-error immutable state
	state.disposed = true;
	void[ordinary, ready, disposed, stale, reason, stop];
}
