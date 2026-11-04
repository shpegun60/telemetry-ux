/* Actual DOM lifecycle checks, with an explicit presentation-only fixture. MIT. */
import {defineConfiguratorElement, mountConfigurator, mountFieldEditor, mountCommandPanel, mountServicePanel,
        mountWatchlist, mountBatchEditor} from 'telemetry-ux/web';

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
		const counters = {subscriptions : 0, unsubscriptions : 0, io : 0, lifecycle : 0, snapshots : 0};
		const listeners = new Set();
		const category = hooks.category || 1;
		const endpoint = Object.freeze({key : category + ':0', category, id : 0, catalog : 'Host', name});
		let state = Object.freeze({
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
				call : false,
				...hooks.actions
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
			snapshot : () => { ++counters.snapshots; return state; },
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
		const result = {provider, counters, listeners, publish(changes) {
			state = Object.freeze({...state, ...changes, revision : state.revision + 1});
			for (const listener of [...listeners])
				listener(state);
		}};
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

		// Three independently mounted panels share one host-owned provider and selection.
		const shared = fixture('Shared selection', 'static', {actions : {
			refresh : true, beginEdit : true, cancelEdit : true,
			apply : true, execute : true, call : true
		}});
		const knownFact = Object.freeze({status : 0, dispatch : 0, endpointStatus : 0,
			session : 17, requestId : 1, valueStatus : 'Ok', endpointName : 'Previous',
			name : 'Ok', display : 'Known result'});
		shared.publish({hasResponse : true, outcomes : {read : knownFact, write : knownFact,
			readback : knownFact, command : knownFact, service : knownFact}});
		const panelHosts = [host('section'), host('section'), host('section')];
		panelHosts.forEach(element => document.body.append(element));
		const panelViews = [mountFieldEditor, mountCommandPanel, mountServicePanel]
			.map((mount, index) => mount(panelHosts[index], {provider : shared.provider}));
		try {
			check(shared.counters.io === 0 && shared.counters.subscriptions === 3,
			      'standalone panels subscribe without selecting or sending');
			check(panelHosts.every(element => element.querySelector('nav').hidden),
			      'standalone panels do not create a second endpoint selector');
			check(panelHosts.every(element => !element.querySelector('.tux-workspace')),
			      'standalone category panels retain their independent DOM layout');
			check(selected(panelHosts[0]).includes('Shared selection') &&
			          selected(panelHosts[1]) === 'Select a Command' &&
			          selected(panelHosts[2]) === 'Select a Service',
			      'each category panel follows only a matching host selection');
			check(panelHosts[0].querySelectorAll('[data-role="outcome"]').length === 3 &&
			          panelHosts[0].querySelector('[data-source="response"]').hidden &&
			          panelHosts[1].querySelectorAll('[data-role="outcome"]').length === 0 &&
			          panelHosts[2].querySelectorAll('[data-role="outcome"]').length === 0,
			      'Field editor shows Field facts only and hides earlier Service response');
			const oldRead = panelHosts[0].querySelector('[data-action="refresh"]');
			shared.publish({selected : {key : '2:0', category : 2, catalog : 'Host', name : 'Run'}});
			oldRead.dispatchEvent(new Event('click'));
			check(shared.counters.io === 0 && oldRead.hidden && oldRead.disabled,
			      'mismatched panel blocks even a synthetic click on a retained control');
			check(panelHosts[1].querySelectorAll('[data-role="outcome"]').length === 1 &&
			          panelHosts[1].querySelector('[data-role="outcome"]').dataset.operation === 'command' &&
			          panelHosts[1].querySelector('[data-source="response"]').hidden,
			      'Command panel isolates Command history and excludes Service response');
			panelHosts[1].querySelector('[data-action="execute"]').click();
			check(shared.counters.io === 1, 'Command panel delegates one explicit Execute');
			shared.publish({selected : {key : '3:0', category : 3, catalog : 'Host', name : 'Query'}});
			panelHosts[2].querySelector('[data-action="call"]').click();
			check(shared.counters.io === 2, 'Service panel delegates one explicit Call');
			check(panelHosts[2].querySelectorAll('[data-role="outcome"]').length === 1 &&
			          panelHosts[2].querySelector('[data-role="outcome"]').dataset.operation === 'service' &&
			          !panelHosts[2].querySelector('[data-source="response"]').hidden,
			      'Service panel shows only Service history and its response');
			shared.publish({current : false, selected : null,
			                actions : {refresh : false, beginEdit : false, cancelEdit : false,
			                           apply : false, execute : false, call : false}});
			check(panelHosts.every(element => [...element.querySelectorAll('[data-action]')]
			          .filter(button => button.tagName === 'BUTTON').every(button => button.disabled)),
			      'invalidated selection disables all standalone actions');
		} finally {
			panelViews.forEach(view => view.unmount());
		}
		check(shared.listeners.size === 0 && shared.counters.unsubscriptions === 3,
		      'standalone panels remove only their own subscriptions');

		const watching = fixture('Watch source');
		const watchCalls = [];
		for (const method of ['addWatch', 'removeWatch', 'clearWatch', 'setWatchIndividual',
		                      'setWatchInterval', 'refreshWatch', 'startWatch', 'stopWatch'])
			watching.provider[method] = (...args) => { watchCalls.push({method, args}); return true; };
		const watchState = Object.freeze({
			rows : Object.freeze([{key : '1:0', id : 0, name : '<img src=x>', catalog : 'Counter',
			                       status : 'Ok', text : '18446744073709551615'}]),
			active : false, pending : false, running : false, individual : false,
			intervalMs : 1000, status : 'Ready', canAdd : true, canRefresh : true
		});
		watching.publish({watch : watchState});
		const watchHost = host('section');
		document.body.append(watchHost);
		const watchView = mountWatchlist(watchHost, {provider : watching.provider, now : () => 123.25});
		try {
			check(watchCalls.length === 0 && watching.counters.io === 0,
			      'Watchlist mount starts no polling, selection or request');
			check(watchHost.querySelector('[part="watch-value"]').textContent === '18446744073709551615' &&
			          !watchHost.querySelector('img'),
			      'Watchlist preserves exact U64 text and literal endpoint names');
			check(watchHost.querySelector('.tux-layout').hidden &&
			          watchHost.querySelector('[part="permission"]').hidden,
			      'standalone Watchlist omits unrelated editor and write controls');
			check(!watchHost.querySelector('.tux-workspace'),
			      'standalone Watchlist is not moved into a hidden workspace');
			watchHost.querySelector('[data-action="refreshWatch"]').click();
			watchHost.querySelector('[data-action="startWatch"]').click();
			check(watchCalls.length === 2 && watchCalls[0].method === 'refreshWatch' &&
			          watchCalls[1].method === 'startWatch' && watchCalls[1].args[0] === 123.25,
			      'explicit controls delegate once using the injected owner clock');
			watching.publish({watch : {...watchState, active : true, pending : true}});
			check(watchHost.querySelector('[data-action="watch-individual"]').disabled &&
			          watchHost.querySelector('[data-action="watch-interval"]').disabled &&
			          watchHost.querySelector('[data-action="removeWatch"]').disabled &&
			          !watchHost.querySelector('[data-action="stopWatch"]').disabled,
			      'accepted draining batch locks edits while Stop remains available');
			watchHost.querySelector('[data-action="stopWatch"]').click();
			check(watchCalls[2].method === 'stopWatch', 'Stop delegates without dropping pending state');
			watching.publish({watch : watchState});
			watching.publish({current : false});
			check(watchHost.querySelector('[data-action="clearWatch"]').disabled &&
			          watchHost.querySelector('[data-action="removeWatch"]').disabled &&
			          watchHost.querySelector('[data-action="watch-individual"]').disabled &&
			          watchHost.querySelector('[data-action="watch-interval"]').disabled,
			      'disconnected Watchlist controls match rejected provider edits');
			watching.publish({current : true});
			const retiredRemove = watchHost.querySelector('[data-action="removeWatch"]');
			watching.publish({session : 18, watch : watchState});
			retiredRemove.dispatchEvent(new Event('click'));
			check(watchCalls.length === 3, 'retired Watchlist control cannot mutate a new session');
			watchHost.querySelector('[data-action="removeWatch"]').click();
			check(watchCalls[3].method === 'removeWatch' && watchCalls[3].args[0] === '1:0',
			      'current Remove delegates an exact endpoint key');
			const lateRemove = watchHost.querySelector('[data-action="removeWatch"]');
			watchView.unmount();
			lateRemove.click();
			check(watchCalls.length === 4 && watching.listeners.size === 0,
			      'unmounted Watchlist controls do not send or retain subscriptions');
		} finally { watchView.unmount(); }

		const batching = fixture('Batch Field');
		const batchCalls = [];
		let argumentText = '0', responseText = '18446744073709551615';
		let batchState = {
			items : [
				{key : 'a', endpointKey : '1:0', category : 1, operation : 'write', name : '<img src=x>', catalog : 'Device', status : 'NotExecuted'},
				{key : 'b', endpointKey : '2:0', category : 2, operation : 'command', name : 'Run', catalog : 'Device', status : 'NotExecuted'},
				{key : 'c', endpointKey : '3:0', category : 3, operation : 'service', name : 'Query', catalog : 'Device', status : 'NotExecuted'},
				{key : 'd', endpointKey : '1:0', category : 1, operation : 'read', name : 'Counter', catalog : 'Device', status : 'NotExecuted'}
			], selectedKey : 'a', active : false, pending : false, canAdd : true,
			canExecute : true, hasDraft : true, hasResponse : false, status : 'Prepared',
			addOperations : ['read', 'write']
		};
		const publishBatch = changes => {
			batchState = {...batchState, ...changes};
			batching.publish({batch : batchState});
		};
		batching.provider.rows = (path = [], {source, offset = 0} = {}) => {
			const available = source === 'batchDraft' ? batchState.hasDraft
			                  : source === 'batchResponse' && batchState.hasResponse;
			return {path, offset, total : available ? 1 : 0, available, kind : available ? 'scalar' : null,
				rows : available && !offset ? [{key : '[]', path : [], name : 'Counter', kind : 'scalar',
					type : 'U64', children : 0, text : source === 'batchDraft' ? argumentText : responseText,
					error : '', editable : source === 'batchDraft' && !batchState.active, enumEntries : []}] : []};
		};
		batching.provider.setBatchText = (path, input, revision) => {
			if (revision !== batching.provider.snapshot().revision)
				return false;
			batchCalls.push({method : 'setBatchText', path, input});
			argumentText = input;
			publishBatch({});
			return true;
		};
		batching.provider.addBatch = operation => {
			batchCalls.push({method : 'addBatch', operation}); return true;
		};
		batching.provider.selectBatch = key => {
			batchCalls.push({method : 'selectBatch', key});
			argumentText = '0';
			publishBatch({selectedKey : key, hasDraft : batchState.items.find(row => row.key === key)?.operation !== 'read', hasResponse : false});
			return true;
		};
		batching.provider.moveBatch = (key, delta) => {
			batchCalls.push({method : 'moveBatch', key, delta});
			const items = [...batchState.items], from = items.findIndex(row => row.key === key);
			const [entry] = items.splice(from, 1); items.splice(from + delta, 0, entry);
			publishBatch({items}); return true;
		};
		for (const method of ['removeBatch', 'clearBatch'])
			batching.provider[method] = key => { batchCalls.push({method, key}); return true; };
		batching.provider.executeBatch = () => {
			batchCalls.push({method : 'executeBatch'});
			publishBatch({active : true, pending : true, canExecute : false, status : 'Pending'});
			return true;
		};
		publishBatch({});
		const batchHost = host('section');
		document.body.append(batchHost);
		const batchView = mountBatchEditor(batchHost, {provider : batching.provider});
		try {
			const action = name => batchHost.querySelector('[data-action="' + name + '"]');
			const rowAction = (key, name) => batchHost.querySelector('[data-batch="' + key + '"] [data-action="' + name + '"]');
			check(batchCalls.length === 0 && batching.counters.io === 0,
			      'Batch mount neither sends nor selects or creates argument DTOs');
			check(batchHost.querySelector('.tux-layout').hidden &&
			          !batchHost.querySelector('.tux-workspace'),
			      'standalone Batch keeps its visible editor outside the hidden endpoint layout');
			check(batchHost.querySelectorAll('tbody tr[data-batch]').length === 4 &&
			          !batchHost.querySelector('img') &&
			          batchHost.textContent.includes('canonical zero values') &&
			          batchHost.textContent.includes('replaces the entire Field value'),
			      'ordered mixed items are literal text with explicit full-replacement initialization');
			check(action('executeBatch').disabled, 'mixed Batch mutations need explicit session permission');
			batching.publish({writesEnabled : true});
			check(!action('executeBatch').disabled, 'explicit permission enables an admitted ready Batch');
			const operation = action('batch-operation');
			operation.value = 'write'; action('addBatch').click();
			check(batchCalls.at(-1).method === 'addBatch' && batchCalls.at(-1).operation === 'write',
			      'Add delegates the explicit operation without sending');
			const oldInput = batchHost.querySelector('input[data-source="batchDraft"]');
			oldInput.focus(); oldInput.value = '18446744073709551615';
			oldInput.dispatchEvent(new Event('input', {bubbles : true}));
			check(batchCalls.at(-1).method === 'setBatchText' &&
			          batchCalls.at(-1).input === '18446744073709551615' &&
			          document.activeElement === oldInput && oldInput.isConnected,
			      'Batch uses the shared exact-text editor without remounting focused input');
			rowAction('b', 'selectBatch').click();
			const beforeRetiredInput = batchCalls.length;
			const snapshotsBeforeRetiredInput = batching.counters.snapshots;
			oldInput.value = '9'; oldInput.dispatchEvent(new Event('input', {bubbles : true}));
			oldInput.dispatchEvent(new Event('blur'));
			check(batchCalls.length === beforeRetiredInput && !oldInput.isConnected &&
			          batching.counters.snapshots === snapshotsBeforeRetiredInput,
			      'retired Batch input and blur cannot query or edit a different selected item');
			rowAction('b', 'batch-up').click();
			check([...batchHost.querySelectorAll('tbody tr[data-batch]')].map(row => row.dataset.batch).join(',') === 'b,a,c,d',
			      'reordering is explicit and preserves item identity');
			action('executeBatch').click(); action('executeBatch').dispatchEvent(new Event('click'));
			check(batchCalls.filter(row => row.method === 'executeBatch').length === 1 &&
			          action('addBatch').disabled && action('clearBatch').disabled &&
			          [...batchHost.querySelectorAll('[data-batch] button')].every(button => button.disabled),
			      'one Execute delegates once and locks all item mutations until completion');
			publishBatch({active : false, pending : false, canExecute : false,
				status : 'OutcomeUnknown - verify device before retry', items : batchState.items.map((row, index) =>
					({...row, status : index === 0 ? 'Accepted' : 'NotExecuted'}))});
			check(batchHost.textContent.includes('Accepted') && batchHost.textContent.includes('NotExecuted') &&
			          batchHost.textContent.includes('OutcomeUnknown') && action('executeBatch').disabled &&
			          batchCalls.filter(row => row.method === 'executeBatch').length === 1,
			      'partial ordered results preserve Accepted and NotExecuted without automatic replay');
			rowAction('c', 'selectBatch').click();
			publishBatch({hasResponse : true});
			check(batchHost.querySelector('[data-source="batchResponse"] .tux-value').textContent === '18446744073709551615',
			      'selected Batch response uses the same exact-text DTO renderer');
			rowAction('d', 'selectBatch').click();
			check(batchHost.querySelector('[data-source="batchDraft"]').hidden &&
			          batchHost.textContent.includes('FieldRead has no request arguments.'),
			      'FieldRead is not initialized as a request DTO');
			const retiredRemove = rowAction('a', 'removeBatch');
			batching.publish({session : 18});
			const beforeRemove = batchCalls.length;
			retiredRemove.dispatchEvent(new Event('click'));
			check(batchCalls.length === beforeRemove, 'retired Batch row control cannot target a new session');
			const lateClear = action('clearBatch');
			batchView.unmount(); lateClear.dispatchEvent(new Event('click'));
			check(batchCalls.length === beforeRemove && batching.listeners.size === 0,
			      'unmount removes only the Batch subscription and makes late controls inert');
		} finally { batchView.unmount(); }

		const Shadow = defineConfiguratorElement('shadow-device-settings', {shadow : true});
		check(defineConfiguratorElement('shadow-device-settings', {shadow : true}) === Shadow,
		      'same shadow registration is idempotent');
		rejects(() => defineConfiguratorElement('shadow-device-settings'),
		        'shadow mode cannot silently change for a registered name');
		rejects(() => defineConfiguratorElement('invalid-shadow-settings', {shadow : 'open'}),
		        'shadow mode requires an explicit boolean');
		const shadowHost = host('shadow-device-settings');
		const slotted = [];
		for (const name of ['header', 'before-content', 'after-content', 'footer']) {
			const content = document.createElement('aside');
			content.slot = name;
			content.textContent = 'Host-owned ' + name;
			shadowHost.append(content);
			slotted.push(content);
		}
		const shadowFirst = fixture('Shadow first');
		shadowHost.provider = shadowFirst.provider;
		check(shadowHost.shadowRoot === null && shadowFirst.counters.subscriptions === 0,
		      'shadow construction does not mount while detached');
		document.body.append(shadowHost);
		const shadowRoot = shadowHost.shadowRoot;
		check(shadowRoot && views(shadowRoot).length === 1 && views(shadowHost).length === 0,
		      'shadow mode mounts the same renderer inside one open shadow root');
		check(slotted.every(content => content.assignedSlot?.name === content.slot),
		      'all four extension slots retain host-owned elements');
		const stylesheet = shadowRoot.querySelector('link[rel="stylesheet"]');
		await new Promise((resolve, reject) => {
			if (stylesheet.sheet) resolve();
			else { stylesheet.addEventListener('load', resolve, {once : true});
			       stylesheet.addEventListener('error', reject, {once : true}); }
		});
		const styling = document.createElement('style');
		styling.textContent = 'shadow-device-settings::part(header) { border-top: 7px solid rgb(12, 34, 56); }';
		document.head.append(styling);
		try {
			check(getComputedStyle(shadowRoot.querySelector('[part="header"]')).borderTopWidth === '7px',
			      'host CSS can customize a documented shadow part');
			check(getComputedStyle(shadowRoot.querySelector('[part="root"]')).backgroundColor === 'rgb(17, 25, 35)',
			      'shadow mode loads the package-local default stylesheet');
		} finally { styling.remove(); }
		const shadowNext = fixture('Shadow replaced', 'dynamic');
		const retiredShadowRead = shadowRoot.querySelector('[data-action="refresh"]');
		shadowHost.provider = shadowNext.provider;
		retiredShadowRead.click();
		check(selected(shadowRoot).includes('Shadow replaced') &&
		          shadowFirst.counters.unsubscriptions === 1 && shadowFirst.counters.io === 0,
		      'shadow provider replacement retires controls without sending');
		shadowHost.remove();
		check(views(shadowRoot).length === 0 && shadowNext.counters.unsubscriptions === 1,
		      'shadow detach unsubscribes and removes the rendered view');
		document.body.append(shadowHost);
		check(shadowHost.shadowRoot === shadowRoot && views(shadowRoot).length === 1 &&
		          slotted.every(content => content.parentNode === shadowHost),
		      'shadow reconnect reuses slots and retains host-owned content');
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
	check(providers.reduce((sum, entry) => sum + entry.counters.io, 0) === 3,
	      'only three explicit live button clicks invoke actions');
	return {
		passed : true,
		checks,
		providers : providers.length,
		fixture : 'presentation-only',
		actualSdkJourneys : false
	};
}

/** Real renderer geometry fixture; no client, transport or simulated wire codec. */
export function mountWorkspaceFixture()
{
	const counters = {io : 0, lifecycle : 0, subscriptions : 0, unsubscriptions : 0, adds : 0};
	const listeners = new Set();
	const endpoints = Array.from({length : 40}, (_, index) => ({
		key : '1:' + index, category : 1, id : index, catalog : 'Measurements',
		name : 'Counter ' + String(index + 1).padStart(2, '0')
	}));
	const batchItems = count => Array.from({length : count}, (_, index) => ({
		key : 'item-' + index, endpointKey : endpoints[index % endpoints.length].key,
		category : 1, operation : 'read', catalog : 'Measurements',
		name : endpoints[index % endpoints.length].name, status : 'Read completed'
	}));
	let state = {
		mode : 'dynamic', current : true, session : 17, endpoints, selected : endpoints[0],
		busy : false, writesEnabled : false, revision : 1, hasValue : true, hasDraft : false,
		hasResponse : false, dirty : false, error : '', outcomes : {},
		actions : {refresh : true, beginEdit : false, cancelEdit : false, apply : false,
		           execute : false, call : false},
		watch : {
			rows : endpoints.slice(0, 4).map(endpoint => ({...endpoint,
				text : '18446744073709551615', status : 'Ok'})),
			active : false, pending : false, running : false, individual : false,
			intervalMs : 1000, status : 'ReadMany complete', canAdd : true, canRefresh : true
		},
		batch : {items : batchItems(4), selectedKey : 'item-0', active : false, pending : false,
		         canAdd : true, canExecute : true, hasDraft : false, hasResponse : false,
		         status : 'Batch reply received - inspect each item outcome', addOperations : ['read']}
	};
	function publish(changes)
	{
		state = {...state, ...changes, revision : state.revision + 1};
		for (const listener of [...listeners])
			listener(state);
	}
	const provider = {
		snapshot : () => state,
		subscribe(listener) {
		    ++counters.subscriptions;
		    listeners.add(listener);
		    return () => { ++counters.unsubscriptions; listeners.delete(listener); };
		},
		rows(path = [], {source, offset = 0, limit = 128} = {}) {
		    const available = source === 'value';
		    return {path, offset, total : available ? 24 : 0, available,
			    kind : available ? 'struct' : null,
			    rows : available ? Array.from({length : 24}, (_, index) => ({
				    key : 'sample-' + index, path : ['sample-' + index], name : 'Sample ' + (index + 1),
				    kind : 'scalar', type : 'U64', children : 0, text : '18446744073709551615',
				    error : '', editable : false, enumEntries : []
			    })).slice(offset, offset + limit) : []};
		},
		select(key) {
		    const selected = endpoints.find(endpoint => endpoint.key === key);
		    if (!selected) return false;
		    publish({selected});
		    return true;
		},
		addBatch(operation) {
		    if (operation !== 'read' || state.batch.items.length >= 32) return false;
		    ++counters.adds;
		    const index = state.batch.items.length;
		    const item = {key : 'item-' + index, endpointKey : state.selected.key,
			    category : 1, operation, catalog : state.selected.catalog,
			    name : state.selected.name, status : 'Prepared'};
		    publish({batch : {...state.batch, items : [...state.batch.items, item]}});
		    return true;
		}
	};
	for (const name of ['refresh', 'apply', 'execute', 'call', 'executeBatch',
	                    'refreshWatch', 'startWatch'])
		provider[name] = () => { ++counters.io; return true; };
	for (const name of ['close', 'invalidate', 'cancel', 'sync'])
		provider[name] = () => { ++counters.lifecycle; };
	const host = document.createElement('main');
	document.body.append(host);
	const view = mountConfigurator(host, {provider});
	return {
		counters,
		resizeBatch(count) {
		    if (!Number.isInteger(count) || count < 1 || count > 32) throw new RangeError();
		    publish({batch : {...state.batch, items : batchItems(count), canAdd : count < 32}});
		},
		dispose() { view.unmount(); host.remove(); }
	};
}
