/* Provider-neutral browser view. Authors: Ruslan Kovtun, codexAi. MIT. */
import {assertPresentationProvider} from './ProviderContract.js';
import {createSessionProvider} from './Provider.js';
import {presentationDetail} from './Presentation.js';

let nextViewId = 0;
const pageSize = 128;
const categoryNames = {
	1 : 'Fields',
	2 : 'Commands',
	3 : 'Services'
};
const valueLabels = {
	Ok : 'Value available',
	NoValue : 'No response value',
	Stale : 'Value is stale; refresh before using it',
	InvalidPayload : 'Invalid value payload'
};

/** Borrow a provider, or own presentation over a borrowed Session. */
export function mountConfigurator(element, options)
{
	return mount(element, options, 0);
}

/** Standalone panels use the host's current selection and the same renderer. */
export function mountFieldEditor(element, options)
{
	return mount(element, options, 1);
}

export function mountCommandPanel(element, options)
{
	return mount(element, options, 2);
}

export function mountServicePanel(element, options)
{
	return mount(element, options, 3);
}

export function mountWatchlist(element, options)
{
	return mount(element, options, 4);
}

export function mountBatchEditor(element, options)
{
	return mount(element, options, 5);
}

function mount(element, options = {}, categoryFilter)
{
	if (!element?.ownerDocument || typeof element.appendChild !== 'function')
		throw new TypeError('mountConfigurator requires a host element');
	const {
		provider,
		session,
		now = () => performance.now(),
		limits,
		endpoint,
		presentation,
		autoPoll = session !== undefined
	} = options;
	if ((provider === undefined) === (session === undefined))
		throw new TypeError('Supply exactly one provider or Session');
	if (provider !== undefined && presentation !== undefined)
		throw new TypeError('Supply presentation when creating the provider, or mount a Session');
	if (typeof now !== 'function')
		throw new TypeError('The Watchlist clock must be a function');
	if (typeof autoPoll !== 'boolean')
		throw new TypeError('autoPoll must be a boolean');
	const owned =
	    session === undefined ? undefined : createSessionProvider({session, limits, endpoint, presentation});
	const selected = owned ?? provider;
	let view;
	try {
		view = mountPresentation(element, selected, categoryFilter, now);
	} catch (error) {
		owned?.dispose();
		throw error;
	}
	let alive = true, timer, stopPolling;
	const timerHost = element.ownerDocument.defaultView ?? globalThis;

	function cancelTimer()
	{
		if (timer !== undefined) {
			timerHost.clearTimeout(timer);
			timer = undefined;
		}
	}

	function schedule(state)
	{
		if (!alive || !state.current || !state.watch?.running || state.watch.pending ||
		    state.busy) {
			cancelTimer();
			return;
		}
		if (timer !== undefined)
			return;
		timer = timerHost.setTimeout(() => {
			timer = undefined;
			if (!alive)
				return;
			try {
				const time = now();
				if (!Number.isFinite(time) || time < 0)
					selected.stopWatch();
				else
					selected.tickWatch(time);
			} catch {
				selected.stopWatch();
			}
			schedule(selected.snapshot());
		}, Math.max(1, state.watch.intervalMs));
	}

	if (autoPoll) {
		try {
			stopPolling = selected.subscribe(schedule);
			schedule(selected.snapshot());
		} catch (failure) {
			alive = false;
			cancelTimer();
			stopPolling?.();
			view.unmount();
			owned?.dispose();
			throw failure;
		}
	}
	return Object.freeze({
		provider : selected,
		unmount() {
		    if (!alive)
			    return;
		    alive = false;
		    cancelTimer();
		    stopPolling?.();
		    try {
			    view.unmount();
		    } finally {
			    owned?.dispose();
		    }
		}
	});
}

function mountPresentation(element, provider, categoryFilter, now)
{
	if (!element?.ownerDocument || typeof element.appendChild !== 'function')
		throw new TypeError('mountConfigurator requires a host element');
	assertPresentationProvider(provider);
	if (typeof now !== 'function')
		throw new TypeError('The Watchlist clock must be a function');

	const document = element.ownerDocument;
	const viewId = 'telemetry-configurator-' + (++nextViewId);
	let alive = true;
	let state;
	let context;
	let localError = '';
	let rendering = false;
	let queued;
	const expanded = new Set();
	const pages = new Map();
	const endpointButtons = new Map();
	const endpointGroups = new Map();
	const watchRows = new Map();
	let watchContext;
	const batchRows = new Map();
	let batchContext;

	function node(tag, className, text)
	{
		const value = document.createElement(tag);
		if (className)
			value.className = className;
		if (text !== undefined)
			value.textContent = text;
		return value;
	}

	const root = node('section', 'telemetry-configurator');
	root.dataset.role = 'configurator';
	root.dataset.panel =
	    [ 'configurator', 'field', 'command', 'service', 'watchlist', 'batch' ][categoryFilter];
	root.setAttribute('part', 'root');
	root.setAttribute('aria-label', 'Telemetry Configurator');
	const header = node('header', 'tux-header');
	header.setAttribute('part', 'header');
	const title = node('h2', '', [
		'Device Configurator', 'Field editor', 'Command', 'Service', 'Watchlist', 'Mixed Batch'
	][categoryFilter]);
	const mode = node('span', 'tux-mode');
	mode.dataset.role = 'mode';
	header.append(title, mode);
	const permission = node('label', 'tux-permission');
	permission.setAttribute('part', 'permission');
	const permissionInput = node('input');
	permissionInput.type = 'checkbox';
	permissionInput.dataset.action = 'writes';
	permission.append(permissionInput,
	                  document.createTextNode('Enable writes, Commands and Services'));
	permission.hidden = categoryFilter === 4;
	const watch = node('section', 'tux-section tux-watchlist');
	watch.setAttribute('part', 'watchlist');
	watch.setAttribute('aria-label', 'Watchlist');
	watch.append(node('h3', '', 'Watchlist'));
	const watchActions = node('div', 'tux-actions');
	const watchButtons = new Map();
	for (const [action, text] of [[ 'addWatch', 'Add selected Field' ],
	                              [ 'refreshWatch', 'Refresh' ], [ 'startWatch', 'Start' ],
	                              [ 'stopWatch', 'Stop' ], [ 'clearWatch', 'Clear' ]]) {
		const button = node('button', '', text);
		button.type = 'button';
		button.dataset.action = action;
		button.disabled = true;
		button.addEventListener('click', () => {
			if (!watch.hidden && !button.disabled)
				invoke(() => action === 'startWatch' ? provider.startWatch(now())
				                                     : provider[action]());
		});
		watchButtons.set(action, button);
		watchActions.append(button);
	}
	const individualLabel = node('label', 'tux-permission');
	const individualInput = node('input');
	individualInput.type = 'checkbox';
	individualInput.dataset.action = 'watch-individual';
	individualLabel.append(individualInput,
	                       document.createTextNode('Individual FieldRead fallback'));
	individualInput.addEventListener('change', () => {
		if (!individualInput.disabled)
			invoke(() => provider.setWatchIndividual(individualInput.checked));
	});
	const intervalLabel = node('label', '', 'Poll interval (ms)');
	const intervalInput = node('input');
	intervalInput.type = 'number';
	intervalInput.min = '1';
	intervalInput.max = '60000';
	intervalInput.step = '1';
	intervalInput.id = viewId + '-watch-interval';
	intervalInput.dataset.action = 'watch-interval';
	intervalLabel.htmlFor = intervalInput.id;
	intervalInput.addEventListener('change', () => {
		if (!intervalInput.disabled)
			invoke(() => provider.setWatchInterval(Number(intervalInput.value)));
	});
	const watchStatus = node('p', 'tux-message');
	watchStatus.dataset.role = 'watch-status';
	const watchTable = node('table', 'tux-watch-table');
	const watchHead = node('thead');
	const watchHeading = node('tr');
	for (const text of ['Field', 'Value', 'Status', ''])
		watchHeading.append(node('th', '', text));
	watchHead.append(watchHeading);
	const watchBody = node('tbody');
	watchTable.append(watchHead, watchBody);
	const watchValues = node('div', 'tux-watch-values');
	watchValues.tabIndex = 0;
	watchValues.setAttribute('role', 'region');
	watchValues.setAttribute('aria-label', 'Watched Field values');
	watchValues.setAttribute('part', 'watch-values');
	watchValues.append(watchTable);
	watch.append(watchActions, individualLabel, intervalLabel, intervalInput, watchStatus,
	             watchValues);
	const noWatch = node('p', 'tux-message', 'Watchlist is unavailable in this provider.');
	const batch = node('section', 'tux-section tux-batch');
	batch.hidden = true;
	batch.setAttribute('part', 'batch');
	batch.setAttribute('aria-label', 'Mixed Batch');
	batch.append(node('h3', '', 'Mixed Batch'));
	const batchActions = node('div', 'tux-actions');
	const batchOperation = node('select');
	batchOperation.setAttribute('aria-label', 'Batch operation for the selected endpoint');
	batchOperation.dataset.action = 'batch-operation';
	for (const [value, label] of [[ 'read', 'FieldRead' ], [ 'write', 'FieldWrite' ],
	                              [ 'command', 'Command' ], [ 'service', 'Service' ]]) {
		const option = node('option', '', label);
		option.value = value;
		batchOperation.append(option);
	}
	batchActions.append(batchOperation);
	const batchButtons = new Map();
	for (const [action, label] of [[ 'addBatch', 'Add selected endpoint' ],
	                               [ 'executeBatch', 'Execute Batch' ],
	                               [ 'clearBatch', 'Clear Batch' ]]) {
		const button = node('button', '', label);
		button.type = 'button';
		button.dataset.action = action;
		button.disabled = true;
		button.addEventListener('click', () => {
			if (!batch.hidden && !button.disabled)
				invoke(() => action === 'addBatch' ? provider.addBatch(batchOperation.value)
				                                   : provider[action]());
		});
		batchButtons.set(action, button);
		batchActions.append(button);
	}
	const batchStatus = node('p', 'tux-message');
	batchStatus.dataset.role = 'batch-status';
	const batchError = node('p', 'tux-error');
	batchError.dataset.role = 'batch-error';
	batchError.setAttribute('role', 'alert');
	const batchTable = node('table', 'tux-batch-table');
	const batchHead = node('thead'), batchHeading = node('tr');
	for (const label of ['Order', 'Operation', 'Endpoint', 'Result', 'Actions'])
		batchHeading.append(node('th', '', label));
	batchHead.append(batchHeading);
	const batchBody = node('tbody');
	batchTable.append(batchHead, batchBody);
	batch.append(
	    batchActions,
	    node(
	        'p', 'tux-message',
	        'Sequential best effort. Partial results are retained. No automatic retry from this editor.'),
	    node(
	        'p', 'tux-message',
	        'New request arguments start at canonical zero values, not device defaults. FieldWrite replaces the entire Field value; review every member.'),
	    batchStatus, batchError, batchTable);
	const batchNoArguments = node('p', 'tux-message', 'FieldRead has no request arguments.');
	batch.append(batchNoArguments);
	const noBatch = node('p', 'tux-message', 'Mixed Batch is unavailable in this provider.');
	noBatch.hidden = true;
	const layout = node('div', 'tux-layout');
	layout.hidden = categoryFilter === 4 || categoryFilter === 5;
	const navigation = node('nav', 'tux-navigation');
	navigation.setAttribute('part', 'endpoints');
	navigation.hidden = categoryFilter !== 0;
	navigation.setAttribute('aria-label', 'Device endpoints');
	const searchLabel = node('label', '', 'Find an endpoint');
	const search = node('input');
	search.type = 'search';
	search.dataset.role = 'endpoint-search';
	search.id = viewId + '-search';
	searchLabel.htmlFor = search.id;
	const endpointList = node('div');
	const noMatches = node('p', 'tux-message', 'No matching endpoints.');
	navigation.append(searchLabel, search, endpointList);
	const content = node('div', 'tux-content');
	const selection = node('h3', 'tux-selection', 'Select an endpoint');
	selection.dataset.role = 'selection';
	const actions = node('div', 'tux-actions');
	actions.setAttribute('part', 'actions');
	const buttons = new Map();
	for (const [action, text] of [[ 'refresh', 'Read' ], [ 'beginEdit', 'Edit' ],
	                              [ 'cancelEdit', 'Cancel' ], [ 'apply', 'Apply + readback' ],
	                              [ 'execute', 'Execute' ], [ 'call', 'Call' ]]) {
		const button = node('button', '', text);
		button.type = 'button';
		button.dataset.action = action;
		button.addEventListener('click', () => {
			if (matchesSelection() && state.actions[action])
				invoke(() => provider[action]());
		});
		actions.append(button);
		buttons.set(action, button);
	}
	const error = node('p', 'tux-error');
	error.setAttribute('part', 'error');
	error.dataset.role = 'error';
	error.setAttribute('role', 'alert');
	content.append(selection, actions, error);
	const sections = new Map();
	for (const [source, label] of [[ 'value', 'Source value' ],
	                               [ 'draft', 'Request / local draft' ], [ 'response', 'Response' ],
	                               [ 'batchDraft', 'Selected Batch item arguments' ],
	                               [ 'batchResponse', 'Selected Batch item response' ]]) {
		const panel = node('section', 'tux-section');
		panel.setAttribute('part', {
			draft : 'request',
			batchDraft : 'batch-request',
			batchResponse : 'batch-response'
		}[source] || source);
		panel.dataset.source = source;
		const heading = node('h3', '', label);
		const headingId = viewId + '-' + source;
		heading.id = headingId;
		panel.setAttribute('aria-labelledby', headingId);
		const rowList = node('div');
		const empty = node('p', 'tux-message');
		panel.append(heading, rowList, empty);
		if (source.startsWith('batch')) {
			panel.hidden = true;
			batch.append(panel);
		} else
			content.append(panel);
		sections.set(source, {panel, heading, rowList, empty, cache : new Map()});
	}
	const outcomes = node('section', 'tux-section');
	outcomes.setAttribute('part', 'outcomes');
	outcomes.setAttribute('aria-label', 'Operation results');
	outcomes.append(node('h3', '', 'Operation results'));
	const outcomeList = node('div');
	outcomes.append(outcomeList);
	content.append(outcomes);
	if (categoryFilter === 0) {
		const workspace = node('div', 'tux-workspace');
		workspace.append(content, watch, noWatch, batch, noBatch);
		layout.append(navigation, workspace);
	} else
		layout.append(navigation, content);
	const status = node('p', 'tux-status');
	status.setAttribute('part', 'status');
	status.dataset.role = 'status';
	status.setAttribute('role', 'status');
	if (categoryFilter === 0)
		root.append(header, permission, layout, status);
	else
		root.append(header, permission, watch, noWatch, batch, noBatch, layout, status);
	element.append(root);

	function matchesSelection()
	{
		return categoryFilter === 0 || state.selected?.category === categoryFilter;
	}

	function invoke(action)
	{
		if (!alive)
			return;
		localError = '';
		try {
			action();
		} catch (failure) {
			localError = failure instanceof Error ? failure.message : String(failure);
		}
		update(provider.snapshot());
	}

	function reconcile(parent, children)
	{
		let cursor = parent.firstChild;
		for (const child of children) {
			if (cursor === child)
				cursor = cursor.nextSibling;
			else
				parent.insertBefore(child, cursor);
		}
		while (cursor) {
			const next = cursor.nextSibling;
			cursor.remove();
			cursor = next;
		}
	}

	function renderEndpoints()
	{
		const filter = search.value.toLocaleLowerCase();
		const groups = new Map();
		for (const entry of endpointButtons.values())
			entry.live = false;
		for (const endpoint of state.endpoints) {
			const text =
			    [ categoryNames[endpoint.category], endpoint.catalog, endpoint.name, endpoint.presentation?.label ].join(' ');
			if (!text.toLocaleLowerCase().includes(filter))
				continue;
			const key = endpoint.category + ':' + endpoint.catalog;
			if (!groups.has(key)) {
				let group = endpointGroups.get(key);
				if (!group) {
					group = {element : node('section', 'tux-endpoint-group'), heading : node('h3')};
					endpointGroups.set(key, group);
				}
				group.heading.textContent =
				    categoryNames[endpoint.category] + ' / ' + endpoint.catalog;
				groups.set(key, {group, children : [ group.heading ]});
			}
			let entry = endpointButtons.get(endpoint.key);
			if (!entry) {
				const button = node('button', 'tux-endpoint');
				button.type = 'button';
				entry = {button, key : endpoint.key, live : false};
				button.addEventListener('click', () => {
					if (entry.live)
						invoke(() => provider.select(entry.key));
				});
				endpointButtons.set(endpoint.key, entry);
			}
			entry.live = true;
			const button = entry.button;
			button.textContent = endpoint.presentation?.label || endpoint.name;
			button.title = presentationDetail(endpoint.presentation, endpoint.catalog + ' / ' + endpoint.name);
			button.dataset.endpoint = endpoint.key;
			button.setAttribute('aria-current', String(state.selected?.key === endpoint.key));
			button.disabled = state.busy;
			groups.get(key).children.push(button);
		}
		for (const {group, children} of groups.values())
			reconcile(group.element, children);
		reconcile(endpointList, groups.size
		                            ? [...groups.values() ].map(entry => entry.group.element)
		                            : [ noMatches ]);
		for (const [key, entry] of endpointButtons)
			if (!entry.live)
				endpointButtons.delete(key);
		for (const [key] of endpointGroups)
			if (!groups.has(key))
				endpointGroups.delete(key);
	}

	function rowEntry(section, source, row)
	{
		const token = source + ':' + JSON.stringify(row.path);
		let entry = section.cache.get(token);
		if (!entry) {
			const wrapper = node('div', 'tux-row');
			wrapper.dataset.source = source;
			const labelCell = node('div', 'tux-row-name');
			const label = node('label');
			const detail = node('small', 'tux-row-detail');
			detail.id = viewId + '-detail-' + (++nextViewId);
			labelCell.append(label, detail);
			const cell = node('div');
			const value = node('span', 'tux-value');
			const input = node('input', 'tux-editor');
			input.type = 'text';
			input.autocomplete = 'off';
			input.spellcheck = false;
			input.id = viewId + '-input-' + (++nextViewId);
			input.dataset.source = source;
			const choices = node('datalist');
			choices.id = input.id + '-choices';
			input.setAttribute('list', choices.id);
			const rowError = node('span', 'tux-error');
			rowError.id = input.id + '-error';
			input.setAttribute('aria-describedby', detail.id + ' ' + rowError.id);
			const unit = node('span', 'tux-unit');
			const children = node('button');
			children.type = 'button';
			cell.append(value, input, unit, choices, rowError, children);
			wrapper.append(labelCell, cell);
			entry = {
				wrapper,
				label,
				detail,
				value,
				input,
				choices,
				unit,
				rowError,
				children,
				path : row.path
			};
			input.addEventListener('input', () => {
				if (!entry.live || entry.context !== context || !entry.editable)
					return;
				invoke(() => source === 'batchDraft'
				                 ? provider.setBatchText(entry.path, input.value, entry.revision)
				                 : provider.setText(entry.path, input.value, entry.revision));
			});
			input.addEventListener('blur', () => {
				if (alive && entry.live && entry.context === context)
					update(provider.snapshot());
			});
			children.addEventListener('click', () => {
				if (!alive || !entry.live || entry.context !== context)
					return;
				if (expanded.has(token))
					expanded.delete(token);
				else
					expanded.add(token);
				update(provider.snapshot());
			});
			section.cache.set(token, entry);
		}
		entry.path = row.path;
		entry.context = context;
		entry.revision = state.revision;
		entry.live = true;
		entry.editable = row.editable;
		entry.wrapper.dataset.row = row.key;
		entry.wrapper.dataset.path = JSON.stringify(row.path);
		entry.wrapper.style.setProperty('--tux-depth', String(Math.min(row.path.length, 16)));
		entry.label.textContent = row.presentation?.label || row.name;
		entry.detail.textContent = presentationDetail(row.presentation,
		    (row.presentation?.label ? row.name + ' / ' : '') + (row.type || row.kind));
		entry.unit.textContent = row.presentation?.unit || '';
		entry.unit.hidden = !entry.unit.textContent;
		entry.value.textContent = row.text;
		entry.value.hidden = row.editable;
		entry.input.hidden = !row.editable;
		entry.input.disabled = state.busy || (source === 'batchDraft' && state.batch?.active);
		entry.input.dataset.path = JSON.stringify(row.path);
		entry.input.setAttribute('aria-invalid', String(Boolean(row.error)));
		if (row.editable)
			entry.label.htmlFor = entry.input.id;
		else
			entry.label.removeAttribute('for');
		// Never replace a focused input or rewrite intermediate text during a
		// provider notification. Invalid raw text is retained by the provider.
		const activeElement = root.getRootNode()?.activeElement || document.activeElement;
		if (entry.input !== activeElement && entry.input.value !== row.text)
			entry.input.value = row.text;
		entry.rowError.textContent = row.error;
		entry.rowError.hidden = !row.error;
		const choiceNodes = [];
		for (const choice of (row.enumEntries || []).slice(0, pageSize)) {
			const option = node('option');
			option.value = choice.code;
			option.label = choice.name;
			choiceNodes.push(option);
		}
		entry.choices.replaceChildren(...choiceNodes);
		if (row.enumEntries?.length > pageSize)
			entry.detail.textContent += ' / first 128 enum labels; exact codes accepted';
		const aggregate = row.kind === 'array' || row.kind === 'struct';
		entry.children.hidden = !aggregate;
		entry.children.textContent =
		    (expanded.has(token) ? 'Collapse' : 'Expand') + ' / ' + row.children + ' children';
		entry.children.setAttribute('aria-label',
		                            (expanded.has(token) ? 'Collapse ' : 'Expand ') + row.name);
		entry.children.setAttribute('aria-expanded', String(expanded.has(token)));
		entry.children.dataset.expand = token;
		return entry.wrapper;
	}

	function renderRows(source)
	{
		const section = sections.get(source);
		const nodes = [];
		const live = new Set();
		for (const entry of section.cache.values())
			entry.live = false;
		const rootWindow = provider.rows([], {source, offset : 0, limit : pageSize});

		function append(path, firstWindow)
		{
			const pageKey = source + ':' + JSON.stringify(path);
			const count = pages.get(pageKey) || 1;
			let loaded = 0;
			let total = 0;
			for (let page = 0; page < count; ++page) {
				const result =
				    page === 0 && firstWindow
				        ? firstWindow
				        : provider.rows(path, {source, offset : page * pageSize, limit : pageSize});
				total = result.total;
				for (const row of result.rows) {
					const token = source + ':' + JSON.stringify(row.path);
					live.add(token);
					nodes.push(rowEntry(section, source, row));
					++loaded;
					if ((row.kind === 'array' || row.kind === 'struct') && expanded.has(token))
						append(row.path);
				}
				if ((page + 1) * pageSize >= total)
					break;
			}
			if (loaded < total) {
				const more = node('button', '', 'Load next 128 / ' + loaded + ' of ' + total);
				more.type = 'button';
				more.dataset.action = 'load-more';
				more.dataset.source = source;
				more.dataset.path = JSON.stringify(path);
				const ownerContext = context;
				more.addEventListener('click', () => {
					if (!alive || ownerContext !== context || more.parentNode !== section.rowList)
						return;
					pages.set(pageKey, count + 1);
					update(provider.snapshot());
				});
				nodes.push(more);
			}
		}

		if (rootWindow.available)
			append([], rootWindow);
		reconcile(section.rowList, nodes);
		for (const [token] of section.cache) {
			if (!live.has(token))
				section.cache.delete(token);
		}
		section.empty.hidden = nodes.length !== 0;
		section.empty.textContent =
		    rootWindow.kind === 'void' ? source === 'response' || source === 'batchResponse'
		                                     ? 'Void response type / No response value expected.'
		                                     : 'Void / No argument value.'
		    : rootWindow.available && rootWindow.kind === 'struct' ? 'Empty structure.'
		    : rootWindow.available && rootWindow.kind === 'array'  ? 'Empty array.'
		    : source === 'value' ? 'No current source value. Select a Field and press Read.'
		    : source === 'draft' || source === 'batchDraft' ? 'No local request draft.'
		                                                    : 'No current response value.';
	}

	function renderOutcomes()
	{
		const nodes = [];
		for (const [operation, result] of Object.entries(state.outcomes)) {
			if (!result)
				continue;
			// Standalone legacy panels follow selection. A revoked Session may
			// retain its own historical facts after that selection is cleared.
			if (categoryFilter && !matchesSelection() &&
			    !(state.connection && !state.current && result.historical === true))
				continue;
			if (categoryFilter &&
			    (categoryFilter === 1   ? !['read', 'write', 'readback'].includes(operation)
			     : categoryFilter === 2 ? operation !== 'command'
			     : categoryFilter === 3 ? operation !== 'service'
			                            : true))
				continue;
			const line = node('p', 'tux-outcome');
			line.dataset.role = 'outcome';
			line.dataset.operation = operation;
			line.dataset.status = String(result.status);
			line.dataset.dispatch = String(result.dispatch);
			line.dataset.endpointStatus = String(result.endpointStatus);
			line.dataset.session = String(result.session);
			line.dataset.requestId = String(result.requestId);
			line.dataset.valueStatus = result.valueStatus;
			line.dataset.historical = String(Boolean(result.historical));
			const display = result.display || result.name;
			const facts = [ operation + ' / ' + result.endpointName + ': ' + display ];
			if (result.historical)
				facts.push('Historical result');
			if (result.accepted && display !== 'Accepted')
				facts.push('Accepted');
			if (result.executed && display !== 'Executed')
				facts.push('Executed');
			facts.push(valueLabels[result.valueStatus] || result.valueStatus);
			if (result.name === 'OutcomeUnknown')
				facts.push('Verify the device before a manual retry');
			line.textContent = facts.join(' / ');
			nodes.push(line);
			const details = node('details', 'tux-message');
			details.append(node('summary', '', 'Technical details'),
			               node('p', '',
			                    'status=' + result.status + ' / dispatch=' + result.dispatch +
			                        ' / endpoint=' + result.endpointStatus + ' / session=' +
			                        result.session + ' / request=' + result.requestId));
			nodes.push(details);
		}
		outcomeList.replaceChildren(...nodes);
		outcomes.hidden = nodes.length === 0;
	}

	function renderWatchlist()
	{
		const value = state.watch;
		noWatch.hidden = categoryFilter !== 4 || Boolean(value);
		watch.hidden = !value || (categoryFilter !== 0 && categoryFilter !== 4);
		if (watch.hidden) {
			for (const button of watchButtons.values())
				button.disabled = true;
			individualInput.disabled = true;
			intervalInput.disabled = true;
			for (const entry of watchRows.values())
				entry.live = false;
			watchRows.clear();
			watchBody.replaceChildren();
			return;
		}
		const identity = state.mode + ':' + state.session;
		if (watchContext !== identity) {
			for (const entry of watchRows.values())
				entry.live = false;
			watchRows.clear();
			watchContext = identity;
		}
		watchButtons.get('addWatch').disabled = !value.canAdd;
		watchButtons.get('refreshWatch').disabled = !value.canRefresh || value.active;
		watchButtons.get('startWatch').disabled = !value.canRefresh || value.active;
		watchButtons.get('stopWatch').disabled = !value.active;
		watchButtons.get('clearWatch').disabled =
		    !state.current || value.active || !value.rows.length;
		individualInput.checked = value.individual;
		individualInput.disabled = !state.current || value.active;
		intervalInput.disabled = !state.current || value.active;
		if (intervalInput !== (root.getRootNode()?.activeElement || document.activeElement))
			intervalInput.value = String(value.intervalMs);
		watchStatus.textContent = value.status;
		const nodes = [];
		for (const entry of watchRows.values())
			entry.live = false;
		for (const row of value.rows) {
			let entry = watchRows.get(row.key);
			if (!entry) {
				const element = node('tr');
				element.dataset.watch = row.key;
				const name = node('td'), text = node('td'), status = node('td'),
				      action = node('td');
				const remove = node('button', '', 'Remove');
				remove.type = 'button';
				remove.dataset.action = 'removeWatch';
				action.append(remove);
				element.append(name, text, status, action);
				entry = {element, name, text, status, remove, live : false};
				const owner = entry;
				remove.addEventListener('click', () => {
					if (alive && owner.live && watchRows.get(row.key) === owner && !remove.disabled)
						invoke(() => provider.removeWatch(row.key));
				});
				watchRows.set(row.key, entry);
			}
			entry.live = true;
			entry.name.textContent = row.catalog + ' / ' + (row.presentation?.label || row.name);
			entry.name.title = presentationDetail(row.presentation, row.name);
			entry.text.textContent = row.text + (row.text && row.presentation?.unit ? ' ' + row.presentation.unit : '');
			entry.text.setAttribute('part', 'watch-value');
			entry.status.textContent = row.status;
			entry.remove.disabled = !state.current || value.active;
			nodes.push(entry.element);
		}
		reconcile(watchBody, nodes);
		for (const [key, entry] of watchRows)
			if (!entry.live)
				watchRows.delete(key);
	}

	function retireSection(source)
	{
		const section = sections.get(source);
		for (const entry of section.cache.values())
			entry.live = false;
		section.cache.clear();
		section.rowList.replaceChildren();
		section.panel.hidden = true;
	}

	function renderBatch()
	{
		const value = state.batch;
		noBatch.hidden = categoryFilter !== 5 || Boolean(value);
		batch.hidden = !value || (categoryFilter !== 0 && categoryFilter !== 5);
		if (batch.hidden) {
			for (const button of batchButtons.values())
				button.disabled = true;
			batchOperation.disabled = true;
			for (const entry of batchRows.values())
				entry.live = false;
			batchRows.clear();
			batchBody.replaceChildren();
			retireSection('batchDraft');
			retireSection('batchResponse');
			return;
		}
		const identity = state.mode + ':' + state.session;
		if (batchContext !== identity) {
			for (const entry of batchRows.values())
				entry.live = false;
			batchRows.clear();
			batchContext = identity;
		}
		const categoryOperations = {1 : [ 'read', 'write' ], 2 : [ 'command' ], 3 : [ 'service' ]};
		const allowed = value.addOperations || categoryOperations[state.selected?.category] || [];
		for (const option of batchOperation.options) {
			option.disabled = option.hidden = !allowed.includes(option.value);
		}
		if (!allowed.includes(batchOperation.value))
			batchOperation.value = allowed[0] || '';
		const locked = !state.current || value.active;
		batchOperation.disabled = locked || !allowed.length;
		batchButtons.get('addBatch').disabled = locked || !value.canAdd || !allowed.length;
		batchButtons.get('executeBatch').disabled =
		    locked || !value.canExecute ||
		    (!state.writesEnabled && value.items.some(row => row.operation !== 'read'));
		batchButtons.get('clearBatch').disabled = locked || !value.items.length;
		batchStatus.textContent = value.status;
		batchError.textContent = localError || value.error || '';
		batchError.hidden = !batchError.textContent;
		for (const entry of batchRows.values())
			entry.live = false;
		const nodes = [];
		for (const [index, row] of value.items.entries()) {
			let entry = batchRows.get(row.key);
			if (!entry) {
				const element = node('tr');
				element.dataset.batch = String(row.key);
				const order = node('td'), operation = node('td'), name = node('td'),
				      status = node('td');
				const controls = node('td', 'tux-batch-controls');
				entry = {element, order, operation, name, status, controls, live : false};
				const owner = entry;
				for (const [action, label, method,
				            delta] of [[ 'selectBatch', 'View / edit', 'selectBatch' ],
				                       [ 'batch-up', 'Up', 'moveBatch', -1 ],
				                       [ 'batch-down', 'Down', 'moveBatch', 1 ],
				                       [ 'removeBatch', 'Remove', 'removeBatch' ]]) {
					const button = node('button', '', label);
					button.type = 'button';
					button.dataset.action = action;
					button.addEventListener('click', () => {
						if (alive && owner.live && !batch.hidden &&
						    batchRows.get(row.key) === owner && !button.disabled)
							invoke(() => delta === undefined ? provider[method](row.key)
							                                 : provider[method](row.key, delta));
					});
					entry[action] = button;
					controls.append(button);
				}
				element.append(order, operation, name, status, controls);
				batchRows.set(row.key, entry);
			}
			entry.live = true;
			entry.order.textContent = String(index + 1);
			entry.operation.textContent = {
				read : 'FieldRead',
				write : 'FieldWrite',
				command : 'Command',
				service : 'Service'
			}[row.operation] || row.operation;
			entry.name.textContent = row.catalog + ' / ' + row.name;
			entry.status.textContent = row.status || 'NotExecuted';
			entry.status.setAttribute('part', 'batch-result');
			entry.element.setAttribute('aria-current', String(row.key === value.selectedKey));
			for (const button of entry.controls.children)
				button.disabled = locked;
			entry['batch-up'].disabled ||= index === 0;
			entry['batch-down'].disabled ||= index + 1 === value.items.length;
			nodes.push(entry.element);
		}
		reconcile(batchBody, nodes);
		for (const [key, entry] of batchRows)
			if (!entry.live)
				batchRows.delete(key);
		const selectedItem = value.items.find(row => row.key === value.selectedKey);
		batchNoArguments.hidden = !selectedItem || selectedItem.operation !== 'read';
		for (const [source, available] of [[ 'batchDraft', value.hasDraft ],
		                                   [ 'batchResponse', value.hasResponse ]]) {
			if (selectedItem && available) {
				sections.get(source).panel.hidden = false;
				renderRows(source);
			} else
				retireSection(source);
		}
	}

	function render()
	{
		const nextContext = state.mode + ':' + state.session + ':' + (state.selected?.key || '') +
		                    ':' + (state.batch?.selectedKey || '');
		if (context !== nextContext) {
			expanded.clear();
			pages.clear();
			for (const section of sections.values()) {
				for (const entry of section.cache.values())
					entry.live = false;
				section.cache.clear();
			}
			context = nextContext;
		}
		root.dataset.mode = state.mode;
		root.dataset.selected = state.selected?.key || '';
		root.setAttribute('aria-busy', String(state.busy));
		mode.textContent = state.mode === 'static' ? 'Static SDK' : 'Dynamic Descriptor';
		renderWatchlist();
		permissionInput.checked = state.writesEnabled;
		permissionInput.disabled = state.busy || !state.current;
		selection.textContent = state.selected
		                            ? state.selected.catalog + ' / ' + (state.selected.presentation?.label || state.selected.name)
		                            : 'Select an endpoint';
		const matches = matchesSelection();
		if (!matches)
			selection.textContent =
			    'Select a ' +
			    [ '', 'Field', 'Command', 'Service', 'Field', 'Field' ][categoryFilter];
		for (const [action, button] of buttons) {
			const category = state.selected?.category;
			button.hidden = action === 'execute'      ? category !== 2
			                : action === 'call'       ? category !== 3
			                : action === 'beginEdit'  ? !category || state.hasDraft
			                : action === 'cancelEdit' ? !state.hasDraft
			                : action === 'apply'      ? category !== 1 || !state.hasDraft
			                                          : category !== 1;
			if (action === 'beginEdit')
				button.textContent = category === 1 ? 'Edit' : 'Prepare arguments';
			button.hidden ||= !matches;
			button.disabled = !matches || !state.actions[action];
		}
		const connectionError = !state.current && state.connection?.lastError;
		error.textContent =
		    localError || state.error || (state.dataStale && state.dataInvalidationReason) ||
		    (connectionError && `${connectionError.code}: ${connectionError.message}`) || '';
		error.hidden = !error.textContent;
		if (!categoryFilter)
			renderEndpoints();
		if (matches) {
			if (categoryFilter === 0 || categoryFilter === 1)
				renderRows('value');
			renderRows('draft');
			if (categoryFilter === 0 || categoryFilter === 3)
				renderRows('response');
		} else {
			for (const [source, section] of sections) {
				if (source.startsWith('batch'))
					continue;
				for (const entry of section.cache.values())
					entry.live = false;
				section.cache.clear();
				section.rowList.replaceChildren();
			}
		}
		sections.get('value').panel.hidden =
		    Boolean(state.selected && state.selected.category !== 1);
		sections.get('draft').heading.textContent =
		    state.selected?.category === 1 ? 'Local Field draft' : 'Request arguments';
		sections.get('draft').panel.hidden = !state.hasDraft;
		sections.get('response').panel.hidden =
		    (categoryFilter !== 0 && categoryFilter !== 3) ||
		    (!state.hasResponse && state.selected?.category !== 3);
		if (!matches)
			for (const [source, section] of sections)
				if (!source.startsWith('batch'))
					section.panel.hidden = true;
		renderOutcomes();
		renderBatch();
		status.dataset.session = String(state.session);
		const connectionLabels = {
			connecting : 'Connecting',
			bootstrapping : 'Loading schema',
			disconnecting : 'Disconnecting',
			disconnected : 'Disconnected',
			'recovery-required' : 'Recovery required',
			closed : 'Closed'
		};
		status.textContent =
		    (state.current ? 'Connected'
		                   : connectionLabels[state.connection?.state] || 'Disconnected') +
		    (state.busy ? ' / Working' : ' / Idle') + (state.dirty ? ' / Unsaved draft' : '');
	}

	function update(snapshot)
	{
		if (!alive)
			return;
		queued = snapshot;
		if (rendering)
			return;
		rendering = true;
		try {
			while (queued) {
				state = queued;
				queued = undefined;
				render();
			}
		} finally {
			rendering = false;
		}
	}

	search.addEventListener('input', () => renderEndpoints());
	permissionInput.addEventListener(
	    'change', () => invoke(() => provider.setWritesEnabled(permissionInput.checked)));
	let unsubscribe;
	try {
		update(provider.snapshot());
		unsubscribe = provider.subscribe(update);
	} catch (failure) {
		alive = false;
		root.remove();
		throw failure;
	}
	return Object.freeze({
		unmount() {
		    if (!alive)
			    return;
		    alive = false;
		    queued = undefined;
		    try {
			    unsubscribe();
		    } finally {
			    root.remove();
		    }
		}
	});
}
