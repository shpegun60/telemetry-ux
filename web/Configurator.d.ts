/* Provider-neutral browser view. SPDX-License-Identifier: MIT. */
import type {PresentationProvider} from './Provider.js';

export interface ConfiguratorView {
	/** Unsubscribe and remove this view only; the host retains provider/client ownership. */
	unmount(): void;
}

export interface ConfiguratorOptions {
	readonly provider: PresentationProvider;
	/** Same monotonic millisecond clock used by the host's tickWatch owner loop. */
	readonly now?: () => number;
}

export function mountConfigurator(
    element: HTMLElement, options: ConfiguratorOptions): ConfiguratorView;

/** Standalone views follow the host's selection without selecting or sending on mount. */
export function mountFieldEditor(
    element: HTMLElement, options: ConfiguratorOptions): ConfiguratorView;
export function mountCommandPanel(
    element: HTMLElement, options: ConfiguratorOptions): ConfiguratorView;
export function mountServicePanel(
    element: HTMLElement, options: ConfiguratorOptions): ConfiguratorView;
/** Optional provider Watchlist contract; owner-driven ticks remain with the host. */
export function mountWatchlist(
    element: HTMLElement, options: ConfiguratorOptions): ConfiguratorView;
