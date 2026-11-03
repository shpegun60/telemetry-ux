/* Actual DOM lifecycle checks, with an explicit presentation-only fixture. MIT. */
import {defineConfiguratorElement} from 'telemetry-ux/web';

export async function runElementChecks()
{
	let checks = 0;
	const providers = [];
	const hosts = [];
	const check = (condition, message) => {
		++checks;
		if (!condition)
			throw new Error('Element check ' + checks + ': ' + message);
	};
	const rejects = (action, message) => {
		let rejected = false;
		try {
			action();
		} catch (error) {
			rejected = error instanceof TypeError || error instanceof DOMException;
		}
		check(rejected, message);
	};
	const settle = async () => {
		await Promise.resolve();
		await Promise.resolve();
	};

	function fixture(name, mode = 'static', hooks = {})
	{
		const counters = {subscriptions : 0, unsubscriptions : 0, io : 0, lifecycle : 0};
		const listeners = new Set();
		const endpoint = Object.freeze({key : '1:0', category : 1, id : 0, catalog : 'Host', name});
		const state = Object.freeze({
			mode,
			current : true,
			session : 17,
			endpoints : Object.freeze([ endpoint ]),
			selected : endpoint,
			busy : false,
			writesEnabled : false,
			revision : 1,
			hasValue : false,
			hasDraft : false,
			hasResponse : false,
			dirty : false,
			error : '',
			outcomes : Object.freeze({}),
			actions : Object.freeze({
				refresh : true,
				beginEdit : false,
				cancelEdit : false,
				apply : false,
				execute : false,
				call : false
			})
		});
		const action = () => {
			++counters.io;
			return true;
		};
		const noHostLifecycle = () => {
			++counters.lifecycle;
		};
		const provider = {
			snapshot : () => state,
			subscribe(listener) {
			    ++counters.subscriptions;
			    listeners.add(listener);
			    hooks.subscribe?.(listener, state);
			    return () => {
				    ++counters.unsubscriptions;
				    listeners.delete(listener);
				    hooks.unsubscribe?.();
			    };
			},
			rows : (path = [], {offset = 0} = {}) =>
			    ({path, offset, total : 0, rows : [], available : false, kind : null}),
			select : action,
			setWritesEnabled : action,
			setText : action,
			refresh : action,
			beginEdit : action,
			cancelEdit : action,
			apply : action,
			execute : action,
			call : action,
			close : noHostLifecycle,
			invalidate : noHostLifecycle,
			cancel : noHostLifecycle,
			sync : noHostLifecycle
		};
		const result = {provider, counters, listeners};
		providers.push(result);
		return result;
	}

	const views = element => element.querySelectorAll('[data-role="configurator"]');
	const selected = element => element.querySelector('[data-role="selection"]')?.textContent;

	function host(tag = 'telemetry-configurator')
	{
		const element = document.createElement(tag);
		hosts.push(element);
		return element;
	}

	try {
		check(customElements.get('telemetry-configurator') === undefined,
		      'import does not register');
		const early = host();
		let reentrantConstructor;
		const earlyProvider = fixture('Before registration', 'static', {
			subscribe() {
			    reentrantConstructor = defineConfiguratorElement();
			}
		});
		early.provider = earlyProvider.provider;
		const panel = document.createElement('aside');
		panel.textContent = 'Application-owned panel';
		early.append(panel);
		document.body.append(early);
		check(views(early).length === 0, 'unknown element has no hidden view');
		const Constructor = defineConfiguratorElement();
		check(early instanceof Constructor, 'declarative connected element upgrades');
		check(early.provider === earlyProvider.provider, 'predefinition provider retained');
		check(!Object.hasOwn(early, 'provider'),
		      'predefinition property no longer shadows accessor');
		check(views(early).length === 1 && earlyProvider.counters.subscriptions === 1,
		      'upgraded element mounts once');
		check(reentrantConstructor === Constructor,
		      'registration during synchronous upgrade is idempotent');
		check(early.firstChild === panel && early.shadowRoot === null,
		      'light DOM host panels preserved');
		check(defineConfiguratorElement() === Constructor, 'known default registration idempotent');
		const Other = defineConfiguratorElement('host-device-settings');
		check(defineConfiguratorElement('host-device-settings') === Other && Other !== Constructor,
		      'custom tag registration is independent and idempotent');

		class Foreign extends HTMLElement {}

		customElements.define('foreign-device-settings', Foreign);
		rejects(() => defineConfiguratorElement('foreign-device-settings'),
		        'foreign registration refused');
		check(customElements.get('foreign-device-settings') === Foreign,
		      'foreign constructor retained');
		rejects(() => defineConfiguratorElement('div'), 'invalid custom element name refused');
		rejects(() => defineConfiguratorElement(42), 'non-string tag name refused');

		const element = host();
		check(element.provider === null, 'new element starts without provider');
		document.body.append(element);
		check(views(element).length === 0, 'connect before provider does not mount');
		const first = fixture('First static');
		element.provider = first.provider;
		check(views(element).length === 1 && first.counters.subscriptions === 1,
		      'provider assignment to connected element mounts synchronously');
		const firstRoot = views(element)[0];
		element.provider = first.provider;
		check(views(element)[0] === firstRoot && first.counters.subscriptions === 1 &&
		          first.counters.unsubscriptions === 0,
		      'same provider assignment does no work');
		for (const invalid of [undefined, false, {}, {snapshot : 0, subscribe() {}, rows() {}},
		                       {snapshot() {}, subscribe : 0, rows() {}},
		                       {snapshot() {}, subscribe() {}, rows : 0}]) {
			rejects(() => element.provider = invalid, 'invalid provider refused');
			check(element.provider === first.provider && views(element)[0] === firstRoot &&
			          first.counters.unsubscriptions === 0,
			      'invalid assignment preserves old view');
		}
		const firstRead = element.querySelector('[data-action="refresh"]');
		firstRead.click();
		check(first.counters.io === 1, 'real rendered action reaches current fixture provider');
		const second = fixture('Second dynamic', 'dynamic');
		element.provider = second.provider;
		check(first.counters.unsubscriptions === 1 && second.counters.subscriptions === 1 &&
		          views(element).length === 1,
		      'replacement retires one old view and mounts one new');
		check(selected(element).includes('Second dynamic') &&
		          views(element)[0].dataset.mode === 'dynamic',
		      'same element supports the dynamic provider contract');
		firstRead.click();
		check(first.counters.io === 1 && second.counters.io === 0,
		      'retired old button cannot submit');
		const secondRead = element.querySelector('[data-action="refresh"]');
		element.remove();
		check(element.provider === second.provider && views(element).length === 0 &&
		          second.counters.unsubscriptions === 1,
		      'detach retains provider and removes subscription');
		secondRead.click();
		check(second.counters.io === 0, 'detached old button cannot submit');
		document.body.append(element);
		check(views(element).length === 1 && second.counters.subscriptions === 2,
		      'reconnect reuses provider with one fresh view');
		element.provider = null;
		check(views(element).length === 0 && second.counters.unsubscriptions === 2,
		      'null removes only the view');
		element.provider = null;
		check(second.counters.unsubscriptions === 2, 'repeated null is idempotent');

		const beforeConnect = host('host-device-settings');
		const beforeProvider = fixture('Provider before connection');
		beforeConnect.provider = beforeProvider.provider;
		check(beforeProvider.counters.subscriptions === 0 && views(beforeConnect).length === 0,
		      'provider before connection does not mount');
		document.body.append(beforeConnect);
		check(beforeProvider.counters.subscriptions === 1 && views(beforeConnect).length === 1,
		      'connection mounts the previously assigned provider');

		const removeHost = host();
		document.body.append(removeHost);
		let lateRead;
		let removeOnce = true;
		const removing = fixture('Remove inside subscribe', 'static', {
			subscribe() {
			    if (removeOnce) {
				    removeOnce = false;
				    lateRead = removeHost.querySelector('[data-action="refresh"]');
				    removeHost.remove();
			    }
			}
		});
		removeHost.provider = removing.provider;
		await settle();
		check(!removeHost.isConnected && views(removeHost).length === 0 &&
		          removing.counters.subscriptions === 1 && removing.counters.unsubscriptions === 1,
		      'late mount candidate after reentrant removal is retired once');
		lateRead.click();
		check(removing.counters.io === 0, 'late candidate button is inactive');
		document.body.append(removeHost);
		check(removing.counters.subscriptions === 2 && views(removeHost).length === 1,
		      'provider survives a reentrant detach and later reconnect');

		const replacingHost = host();
		document.body.append(replacingHost);
		const replacement = fixture('Replacement current', 'dynamic');
		const replacing = fixture('Retired during subscribe', 'static', {
			subscribe(listener, state) {
			    listener(state);
			    replacingHost.provider = replacement.provider;
			}
		});
		replacingHost.provider = replacing.provider;
		await settle();
		check(replacing.counters.subscriptions === 1 && replacing.counters.unsubscriptions === 1 &&
		          replacement.counters.subscriptions === 1 && views(replacingHost).length === 1,
		      'subscribe replacement owns one current view and one late cleanup');
		check(replacingHost.provider === replacement.provider &&
		          selected(replacingHost).includes('Replacement current'),
		      'late candidate cannot overwrite new provider');
		await settle();
		check(replacement.counters.subscriptions === 1, 'queued stale pass does not resubscribe');

		const unsubscribeHost = host();
		document.body.append(unsubscribeHost);
		const final = fixture('Unsubscribe selected final', 'dynamic');
		const skipped = fixture('Superseded before mount');
		const previous = fixture('Unsubscribe previous', 'static', {
			unsubscribe() {
			    unsubscribeHost.provider = final.provider;
			}
		});
		unsubscribeHost.provider = previous.provider;
		unsubscribeHost.provider = skipped.provider;
		await settle();
		check(previous.counters.unsubscriptions === 1 && skipped.counters.subscriptions === 0 &&
		          final.counters.subscriptions === 1,
		      'unsubscribe replacement skips superseded mount');
		check(unsubscribeHost.provider === final.provider &&
		          selected(unsubscribeHost).includes('selected final'),
		      'unsubscribe replacement remains current');

		const unsubscribeRemoveHost = host();
		document.body.append(unsubscribeRemoveHost);
		const afterRemoval = fixture('Retained after unsubscribe removal', 'dynamic');
		const removingPrevious = fixture('Previous removes during unsubscribe', 'static', {
			unsubscribe() {
			    unsubscribeRemoveHost.remove();
			}
		});
		unsubscribeRemoveHost.provider = removingPrevious.provider;
		unsubscribeRemoveHost.provider = afterRemoval.provider;
		await settle();
		check(!unsubscribeRemoveHost.isConnected && views(unsubscribeRemoveHost).length === 0 &&
		          removingPrevious.counters.unsubscriptions === 1 &&
		          afterRemoval.counters.subscriptions === 0,
		      'unsubscribe removal prevents mounting into the detached element');
		document.body.append(unsubscribeRemoveHost);
		check(unsubscribeRemoveHost.provider === afterRemoval.provider &&
		          afterRemoval.counters.subscriptions === 1 &&
		          views(unsubscribeRemoveHost).length === 1,
		      'unsubscribe removal retains the new provider for later reconnection');

		const abaHost = host();
		document.body.append(abaHost);
		let bounce = true;
		const aba = fixture('Remove and reconnect in subscribe', 'static', {
			subscribe() {
			    if (bounce) {
				    bounce = false;
				    abaHost.remove();
				    document.body.append(abaHost);
			    }
			}
		});
		abaHost.provider = aba.provider;
		await settle();
		check(abaHost.isConnected && views(abaHost).length === 1 &&
		          aba.counters.subscriptions === 2 && aba.counters.unsubscriptions === 1,
		      'same-provider reconnect epoch retires the late candidate');
		check(early.firstChild === panel && panel.textContent === 'Application-owned panel',
		      'all lifecycle operations preserve the application panel');
	} finally {
		for (const element of hosts)
			element.remove();
		await settle();
	}
	for (const {counters, listeners} of providers) {
		check(counters.subscriptions === counters.unsubscriptions && listeners.size === 0,
		      'each accepted subscription is removed exactly once');
		check(counters.lifecycle === 0, 'view lifecycle never invokes host lifecycle methods');
	}
	check(providers.reduce((sum, entry) => sum + entry.counters.io, 0) === 1,
	      'only the one explicit live button click invokes an action');
	return {
		passed : true,
		checks,
		providers : providers.length,
		fixture : 'presentation-only',
		actualSdkJourneys : false
	};
}
