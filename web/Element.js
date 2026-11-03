/* Declarative lifecycle for the existing renderer. Ruslan Kovtun, codexAi. MIT. */
import {mountConfigurator} from './Configurator.js';
import {assertPresentationProvider} from './ProviderContract.js';

// DOM globals are deliberately consulted only by explicit registration.
const definitions = new WeakMap();

export function defineConfiguratorElement(tagName = 'telemetry-configurator')
{
	const registry = globalThis.customElements;
	const BaseElement = globalThis.HTMLElement;
	if (!registry || typeof registry.define !== 'function' || typeof BaseElement !== 'function')
		throw new TypeError('defineConfiguratorElement requires a browser DOM');
	if (typeof tagName !== 'string')
		throw new TypeError('The custom element tag name must be a string');
	let known = definitions.get(registry);
	if (!known) {
		known = new Map();
		definitions.set(registry, known);
	}
	const existing = registry.get(tagName);
	if (existing) {
		if (known.get(tagName) === existing)
			return existing;
		throw new TypeError('The custom element name is already registered: ' + tagName);
	}

	class ConfiguratorElement extends BaseElement {
		#provider = null;
		#view = null;
		#connected = false;
		#epoch = 0;
		#reconciling = false;
		#scheduled = false;

		constructor()
		{
			super();
			// Upgrade an own property assigned before registration. Never mount
			// from the constructor, including an already connected upgrade.
			if (Object.prototype.hasOwnProperty.call(this, 'provider')) {
				const provider = this.provider;
				if (provider !== null)
					assertPresentationProvider(provider);
				delete this.provider;
				this.#provider = provider;
			}
		}

		get provider()
		{
			return this.#provider;
		}

		set provider(provider)
		{
			if (provider !== null)
				assertPresentationProvider(provider);
			if (provider === this.#provider)
				return;
			this.#provider = provider;
			++this.#epoch;
			this.#reconcile();
		}

		connectedCallback()
		{
			this.#connected = true;
			++this.#epoch;
			this.#reconcile();
		}

		disconnectedCallback()
		{
			this.#connected = false;
			++this.#epoch;
			this.#reconcile();
		}

		#schedule()
		{
			if (this.#scheduled)
				return;
			this.#scheduled = true;
			queueMicrotask(() => {
				this.#scheduled = false;
				this.#reconcile();
			});
		}

		#reconcile()
		{
			if (this.#reconciling) {
				this.#schedule();
				return;
			}
			const connected = this.#connected && this.isConnected;
			if (this.#view && connected && this.#view.provider === this.#provider)
				return;
			this.#reconciling = true;
			const epoch = this.#epoch;
			const provider = this.#provider;
			try {
				const previous = this.#view;
				this.#view = null;
				previous?.view.unmount();
				if (epoch !== this.#epoch || !this.#connected || !this.isConnected || !provider)
					return;
				const candidate = mountConfigurator(this, {provider});
				if (epoch === this.#epoch && this.#connected && this.isConnected)
					this.#view = {provider, view : candidate};
				else
					candidate.unmount();
			} finally {
				this.#reconciling = false;
				if (epoch !== this.#epoch)
					this.#schedule();
			}
		}
	}

	known.set(tagName, ConfiguratorElement);
	try {
		registry.define(tagName, ConfiguratorElement);
	} catch (failure) {
		known.delete(tagName);
		throw failure;
	}
	return ConfiguratorElement;
}
