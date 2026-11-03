/* Provider-neutral browser view. SPDX-License-Identifier: MIT. */
import type {PresentationProvider} from './Provider.js';

export interface ConfiguratorView {
	/** Unsubscribe and remove this view only; the host retains provider/client ownership. */
	unmount(): void;
}

export function mountConfigurator(
    element: HTMLElement, options: {readonly provider: PresentationProvider;}): ConfiguratorView;
