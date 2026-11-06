/* Provider-neutral browser view. SPDX-License-Identifier: MIT. */
import type {PresentationProvider} from './Provider.js';
import type {SessionPresentationProvider, SessionProviderOptions} from './Provider.js';

export interface ConfiguratorView {
	/** Unsubscribe and remove this view only; the host retains provider/client ownership. */
	unmount(): void;
	readonly provider: PresentationProvider;
}

export interface ProviderConfiguratorOptions {
	readonly provider: PresentationProvider;
	readonly session?: never;
	/** Same monotonic millisecond clock used by the host's tickWatch owner loop. */
	readonly now?: () => number;
}

export interface SessionConfiguratorOptions extends SessionProviderOptions {
	readonly provider?: never;
	readonly now?: () => number;
}

export type ConfiguratorOptions = ProviderConfiguratorOptions|SessionConfiguratorOptions;
export interface SessionConfiguratorView extends ConfiguratorView {
	/** This view owns presentation only. Its borrowed Session remains with the host. */
	readonly provider: SessionPresentationProvider;
}

export function mountConfigurator(element: HTMLElement,
                                  options: SessionConfiguratorOptions): SessionConfiguratorView;
export function mountConfigurator(element: HTMLElement,
                                  options: ConfiguratorOptions): ConfiguratorView;

/** Provider mounts follow its selection. Session mounts may select options.endpoint without I/O. */
export function mountFieldEditor(element: HTMLElement,
                                 options: SessionConfiguratorOptions): SessionConfiguratorView;
export function mountFieldEditor(element: HTMLElement,
                                 options: ConfiguratorOptions): ConfiguratorView;
export function mountCommandPanel(element: HTMLElement,
                                  options: SessionConfiguratorOptions): SessionConfiguratorView;
export function mountCommandPanel(element: HTMLElement,
                                  options: ConfiguratorOptions): ConfiguratorView;
export function mountServicePanel(element: HTMLElement,
                                  options: SessionConfiguratorOptions): SessionConfiguratorView;
export function mountServicePanel(element: HTMLElement,
                                  options: ConfiguratorOptions): ConfiguratorView;
/** Optional provider Watchlist contract; owner-driven ticks remain with the host. */
export function mountWatchlist(element: HTMLElement,
                               options: ProviderConfiguratorOptions): ConfiguratorView;
/** Ordered mixed Batch with one explicit submit and the provider's shared request editor. */
export function mountBatchEditor(element: HTMLElement,
                                 options: ProviderConfiguratorOptions): ConfiguratorView;
