/* Explicit custom-element registration. SPDX-License-Identifier: MIT. */
import type {PresentationProvider} from './Provider.js';

export interface TelemetryConfiguratorElement extends HTMLElement {
	/** Host owned; null removes this view without changing the provider/client. */
	provider: PresentationProvider|null;
}

export interface TelemetryConfiguratorElementConstructor {
	new(): TelemetryConfiguratorElement;
	readonly prototype: TelemetryConfiguratorElement;
}

/** Explicit registration; importing the module does not access DOM globals. */
export function defineConfiguratorElement(
    tagName?: string, options?: {readonly shadow?: boolean;}): TelemetryConfiguratorElementConstructor;

declare global
{
	interface HTMLElementTagNameMap {
		'telemetry-configurator': TelemetryConfiguratorElement;
	}
}
