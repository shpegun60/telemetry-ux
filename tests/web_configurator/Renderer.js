/* Pure DOM contracts, separate from the real native SDK journeys. MIT. */
import {mountConfigurator} from 'telemetry-ux/web';

export async function runRendererChecks()
{
	let checks = 0;
	const check = (condition, message) => {
		++checks;
		if (!condition)
			throw new Error('Renderer check ' + checks + ': ' + message);
	};
	const hostile = '<img src=x onerror="window.__tuxRendererInjected__=true">';
	const endpoints = Object.freeze([
		{key : '1:0', id : 0, category : 1, catalog : '<svg onload=alert(1)>', name : hostile},
		{key : '1:1', id : 1, category : 1, catalog : 'Drafts', name : 'Draft A'},
		{key : '1:2', id : 2, category : 1, catalog : 'Drafts', name : 'Draft B'}
	].map(Object.freeze));
	const actions = Object.freeze({
		refresh : false,
		beginEdit : false,
		cancelEdit : false,
		apply : false,
		execute : false,
		call : false
	});
	let state = Object.freeze({
		mode : 'static',
		current : true,
		session : 91,
		endpoints,
		selected : endpoints[0],
		busy : false,
		writesEnabled : false,
		revision : 1,
		hasValue : true,
		hasDraft : false,
		hasResponse : false,
		dirty : false,
		error : '',
		outcomes : Object.freeze({}),
		watch : Object.freeze({active : false, individual : false, intervalMs : 1000,
		    canAdd : false, canRefresh : false, rows : Object.freeze([]), status : 'Empty watchlist'}),
		batch : Object.freeze({active : false, canAdd : false, canExecute : false,
		    items : Object.freeze([]), addOperations : Object.freeze([]), status : 'Empty batch'}),
		actions
	});
	const listeners = new Set();
	const windows = [];
	let subscriptions = 0, unsubscriptions = 0, lifecycleCalls = 0, selections = 0, textCalls = 0;
	let draftText = '';
	let nested = false, snapshotCalls = 0, semantic = false;
	const publish = changes => {
		state = Object.freeze({...state, ...changes, revision : state.revision + 1});
		for (const listener of [...listeners])
			listener(state);
	};
	const noHostCall = () => {
		++lifecycleCalls;
		return false;
	};
	const provider = {
		snapshot : () => {
		    ++snapshotCalls;
		    return state;
		},
		subscribe(listener) {
		    ++subscriptions;
		    listeners.add(listener);
		    return () => {
			    ++unsubscriptions;
			    listeners.delete(listener);
		    };
		},
		rows(path = [], {source = 'value', offset = 0, limit = 128} = {}) {
		    windows.push({source, path : [...path ], offset, limit});
		    const available = source === 'value'   ? state.hasValue
		                      : source === 'draft' ? state.hasDraft
		                                           : false;
		    const aggregate = source === 'value' && nested && path.length === 0;
		    const total = available ? source === 'value' && !aggregate ? 300 : 1 : 0;
		    const rows = [];
		    for (let index = offset; index < Math.min(total, offset + limit); ++index) {
			    const rowPath =
			        source === 'value'
			            ? nested ? aggregate ? [ 'samples' ] : [ 'samples', index ] : [ index ]
			            : [ 'counter' ];
			    rows.push(Object.freeze({
				    key : JSON.stringify(rowPath),
				    path : Object.freeze(rowPath),
				    name : source === 'value' ? 'Row ' + index + ' <script>' : 'Counter',
				    presentation : semantic ? {label: 'Exact total', unit: 'ticks', help: hostile,
				        minimum: '-9223372036854775808', maximum: '18446744073709551615', step: '1'} : undefined,
				    kind : aggregate ? 'array' : 'scalar',
				    type : aggregate ? 'array' : 'U64',
				    children : aggregate ? 300 : 0,
				    text : source === 'draft' ? draftText
				           : index === 0      ? hostile
				                              : String(index),
				    error : '',
				    editable : source === 'draft',
				    enumEntries : Object.freeze([])
			    }));
		    }
		    return Object.freeze({
			    path : Object.freeze([...path ]),
			    total,
			    offset,
			    available,
			    kind : source === 'value'   ? aggregate ? 'struct' : 'array'
			           : source === 'draft' ? 'scalar'
			                                : null,
			    rows : Object.freeze(rows)
		    });
		},
		select(key) {
		    ++selections;
		    publish({selected : endpoints.find(endpoint => endpoint.key === key)});
		    return true;
		},
		setText(path, text, revision) {
		    if (revision !== state.revision || JSON.stringify(path) !== '["counter"]')
			    return false;
		    ++textCalls;
		    draftText = text;
		    publish({dirty : true});
		    return true;
		},
		refresh : noHostCall,
		beginEdit : noHostCall,
		cancelEdit : noHostCall,
		apply : noHostCall,
		execute : noHostCall,
		call : noHostCall,
		setWritesEnabled : noHostCall,
		invalidate : noHostCall,
		sync : noHostCall,
		close : noHostCall
	};
	const host = document.createElement('section');
	host.setAttribute('aria-label', 'Independent renderer test host');
	const sibling = document.createElement('p');
	sibling.textContent = 'This belongs to the host';
	host.append(sibling);
	document.body.append(host);
	let view;
	try {
		view = mountConfigurator(host, {provider});
		const workspace = host.querySelector('.tux-workspace');
		const editor = workspace.querySelector('.tux-content');
		check(workspace.firstElementChild === editor,
		      'Selected endpoint editor comes before optional tools in document and keyboard order');
		const firstTool = [...workspace.children].find(item => item !== editor && !item.hidden);
		check(firstTool && editor.getBoundingClientRect().top < firstTool.getBoundingClientRect().top,
		      'Selected endpoint editor is visually above optional tools');
		const count = () => host.querySelectorAll('.tux-row[data-source="value"]').length;
		const more = () =>
		    host.querySelector('[data-action="load-more"][data-source="value"][data-path="[]"]');
		check(subscriptions === 1 && listeners.size === 1, 'One presentation subscription');
		check(count() === 128, 'Initial array window contains 128 rows');
		check(windows.filter(row => row.source === 'value').every(row => row.offset === 0),
		      'No eager second array window');
		check(more() !== null, 'Additional rows have an explicit action');
		const retiredMore = more();
		more().click();
		check(count() === 256, 'One explicit action adds the next 128 rows');
		check(more() !== null, 'The last partial window is still explicit');
		more().click();
		check(count() === 300 && more() === null, 'The final partial window ends at 300');
		check(windows.every(row => row.limit === 128),
		      'Every provider call has a bounded 128-row window');
		check(windows.some(row => row.source === 'value' && row.offset === 128),
		      'Second window starts at 128');
		check(windows.some(row => row.source === 'value' && row.offset === 256),
		      'Third window starts at 256');
		publish({selected : endpoints[1]});
		check(count() === 128 && !retiredMore.isConnected,
		      'A different selection starts with its own first window');
		const windowsBeforeRetired = windows.length;
		retiredMore.click();
		check(count() === 128 && windows.length === windowsBeforeRetired,
		      'A retired load control cannot paginate a different selection');
		check(host.querySelector('button[data-endpoint="1:0"]').textContent === hostile,
		      'Endpoint names remain literal text');
		check(host.querySelector('.tux-row[data-source="value"] .tux-value').textContent ===
		          hostile,
		      'Value text remains literal text');
		check(!host.querySelector('img, svg, script'), 'Metadata creates no HTML elements');
		check(window.__tuxRendererInjected__ !== true, 'Metadata executes no script');
		const button = host.querySelector('button[data-endpoint="1:0"]');
		button.focus();
		check(document.activeElement === button, 'Native endpoint button accepts keyboard focus');
		button.click();
		check(document.activeElement === button,
		      'Synchronous selection preserves the same focused node');
		check(selections === 1, 'One activation selects once');
		const search = host.querySelector('[data-role="endpoint-search"]');
		search.value = 'no endpoint matches this text';
		search.dispatchEvent(new Event('input', {bubbles : true}));
		check(!host.querySelector('button[data-endpoint]'), 'Search removes unmatched endpoints');
		search.value = '';
		search.dispatchEvent(new Event('input', {bubbles : true}));
		button.click();
		check(selections === 1, 'A retired endpoint button cannot affect the restored list');
		draftText = '4';
		publish({selected : endpoints[1], hasValue : false, hasDraft : true});
		const oldInput = host.querySelector('input[data-source="draft"]');
		check(oldInput !== null, 'Draft creates a scalar text input');
		oldInput.focus();
		oldInput.value = '4.';
		oldInput.dispatchEvent(new Event('input', {bubbles : true}));
		check(draftText === '4.' && oldInput.value === '4.',
		      'Intermediate text is forwarded without numeric conversion');
		check(document.activeElement === oldInput,
		      'A synchronous provider update preserves input focus');
		publish({error : 'An unrelated presentation update'});
		check(document.activeElement === oldInput && oldInput.value === '4.',
		      'Other notifications retain the focused raw text');
		draftText = '6.5';
		publish({selected : endpoints[2], error : ''});
		const newInput = host.querySelector('input[data-source="draft"]');
		check(newInput !== oldInput && !oldInput.isConnected,
		      'A different selection retires the old input');
		const beforeTextCalls = textCalls;
		oldInput.value = '9.5';
		oldInput.dispatchEvent(new Event('input', {bubbles : true}));
		check(textCalls === beforeTextCalls && draftText === '6.5' && newInput.value === '6.5',
		      'Detached stale input cannot edit a new draft at the same path');
		check(lifecycleCalls === 0,
		      'Mount, pagination and presentation changes never call host I/O or lifecycle');
		nested = true;
		publish({selected : endpoints[0], hasValue : true, hasDraft : false, error : ''});
		const aggregateButton = () => host.querySelector('[data-expand=\'value:["samples"]\']');
		const retiredExpand = aggregateButton();
		check(count() === 1, 'Nested array starts collapsed');
		retiredExpand.click();
		check(count() === 129, 'Explicit expansion loads one child window');
		publish({selected : endpoints[1]});
		check(count() === 1 && !retiredExpand.isConnected,
		      'New selection retires expansion control');
		const windowsBeforeExpand = windows.length;
		retiredExpand.click();
		check(count() === 1 && windows.length === windowsBeforeExpand,
		      'A retired expansion control cannot expand a different selection');
		const liveExpand = aggregateButton();
		liveExpand.click();
		check(count() === 129, 'Current expansion control remains functional');
		const liveMore = host.querySelector('[data-action="load-more"]');
		semantic = true;
		draftText = '18446744073709551615';
		publish({hasValue: false, hasDraft: true});
		const hinted = host.querySelector('.tux-row[data-source="draft"]');
		check(hinted.querySelector('label').textContent === 'Exact total', 'Overlay label decorates the visible row');
		check(hinted.querySelector('.tux-row-detail').textContent.includes('Counter / U64'), 'Original member name remains visible');
		check(hinted.querySelector('.tux-unit').textContent === 'ticks', 'Unit is separate from editable numeric text');
		check(hinted.querySelector('input').value === '18446744073709551615', 'U64 editor remains exact and unscaled');
		check(hinted.querySelector('.tux-row-detail').textContent.includes('Advisory only: min -9223372036854775808 / max 18446744073709551615'), 'Range is exact, visible and explicitly advisory');
		check(!host.querySelector('img,svg,script') && window.__tuxRendererInjected__ !== true, 'Help remains literal text');
		const snapshotsBeforeUnmount = snapshotCalls, windowsBeforeUnmount = windows.length;
		view.unmount();
		liveExpand.click();
		liveMore.click();
		check(snapshotCalls === snapshotsBeforeUnmount && windows.length === windowsBeforeUnmount,
		      'Retired controls after unmount do not query or render the provider');
		check(unsubscriptions === 1 && listeners.size === 0, 'Unmount unsubscribes exactly once');
		check(sibling.isConnected && sibling.parentNode === host,
		      'Unmount preserves host-owned siblings');
		check(!host.querySelector('[data-role="configurator"]'), 'Unmount removes its own DOM');
		view.unmount();
		check(unsubscriptions === 1 && lifecycleCalls === 0,
		      'Repeated unmount has no host side effects');
		return {
			passed : true,
			checks,
			rows : 300,
			window : 128,
			unsubscriptions,
			lifecycleCalls,
			safeText : true,
			keyboardFocus : true,
			staleInput : true
		};
	} finally {
		view?.unmount();
		host.remove();
	}
}
