/* Provider-neutral browser view. Authors: Ruslan Kovtun, codexAi. MIT. */

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
	Stale : 'Values are stale; refresh after reconnect',
	InvalidPayload : 'Invalid value payload'
};

/** Mount presentation only. The host continues to own its provider and client. */
export function mountConfigurator(element, {provider} = {})
{
	if (!element?.ownerDocument || typeof element.appendChild !== 'function')
		throw new TypeError('mountConfigurator requires a host element');
	if (!provider || typeof provider.snapshot !== 'function' ||
	    typeof provider.subscribe !== 'function' || typeof provider.rows !== 'function')
		throw new TypeError('mountConfigurator requires an explicit provider');

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
	root.setAttribute('aria-label', 'Telemetry Configurator');
	const header = node('header', 'tux-header');
	const title = node('h2', '', 'Device Configurator');
	const mode = node('span', 'tux-mode');
	mode.dataset.role = 'mode';
	header.append(title, mode);
	const permission = node('label', 'tux-permission');
	const permissionInput = node('input');
	permissionInput.type = 'checkbox';
	permissionInput.dataset.action = 'writes';
	permission.append(permissionInput,
	                  document.createTextNode('Enable writes, Commands and Services'));
	const layout = node('div', 'tux-layout');
	const navigation = node('nav', 'tux-navigation');
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
	const buttons = new Map();
	for (const [action, text] of [[ 'refresh', 'Read' ], [ 'beginEdit', 'Edit' ],
	                              [ 'cancelEdit', 'Cancel' ], [ 'apply', 'Apply + readback' ],
	                              [ 'execute', 'Execute' ], [ 'call', 'Call' ]]) {
		const button = node('button', '', text);
		button.type = 'button';
		button.dataset.action = action;
		button.addEventListener('click', () => invoke(() => provider[action]()));
		actions.append(button);
		buttons.set(action, button);
	}
	const error = node('p', 'tux-error');
	error.dataset.role = 'error';
	error.setAttribute('role', 'alert');
	content.append(selection, actions, error);
	const sections = new Map();
	for (const [source, label] of [[ 'value', 'Source value' ],
	                               [ 'draft', 'Request / local draft' ],
	                               [ 'response', 'Response' ]]) {
		const panel = node('section', 'tux-section');
		panel.dataset.source = source;
		const heading = node('h3', '', label);
		const headingId = viewId + '-' + source;
		heading.id = headingId;
		panel.setAttribute('aria-labelledby', headingId);
		const rowList = node('div');
		const empty = node('p', 'tux-message');
		panel.append(heading, rowList, empty);
		content.append(panel);
		sections.set(source, {panel, heading, rowList, empty, cache : new Map()});
	}
	const outcomes = node('section', 'tux-section');
	outcomes.setAttribute('aria-label', 'Operation results');
	outcomes.append(node('h3', '', 'Operation results'));
	const outcomeList = node('div');
	outcomes.append(outcomeList);
	content.append(outcomes);
	layout.append(navigation, content);
	const status = node('p', 'tux-status');
	status.dataset.role = 'status';
	status.setAttribute('role', 'status');
	root.append(header, permission, layout, status);
	element.append(root);

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
			    [ categoryNames[endpoint.category], endpoint.catalog, endpoint.name ].join(' ');
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
			button.textContent = endpoint.name;
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
			input.setAttribute('aria-describedby', rowError.id);
			const children = node('button');
			children.type = 'button';
			cell.append(value, input, choices, rowError, children);
			wrapper.append(labelCell, cell);
			entry = {
				wrapper,
				label,
				detail,
				value,
				input,
				choices,
				rowError,
				children,
				path : row.path
			};
			input.addEventListener('input', () => {
				if (!entry.live || entry.context !== context || !entry.editable)
					return;
				invoke(() => provider.setText(entry.path, input.value, entry.revision));
			});
			input.addEventListener('blur', () => update(provider.snapshot()));
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
		entry.label.textContent = row.name;
		entry.detail.textContent = row.type || row.kind;
		entry.value.textContent = row.text;
		entry.value.hidden = row.editable;
		entry.input.hidden = !row.editable;
		entry.input.disabled = state.busy;
		entry.input.dataset.path = JSON.stringify(row.path);
		entry.input.setAttribute('aria-invalid', String(Boolean(row.error)));
		if (row.editable)
			entry.label.htmlFor = entry.input.id;
		else
			entry.label.removeAttribute('for');
		// Never replace a focused input or rewrite intermediate text during a
		// provider notification. Invalid raw text is retained by the provider.
		if (entry.input !== document.activeElement && entry.input.value !== row.text)
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
		    rootWindow.kind === 'void' ? source === 'response'
		                                     ? 'Void response type / No response value expected.'
		                                     : 'Void / No argument value.'
		    : rootWindow.available && rootWindow.kind === 'struct' ? 'Empty structure.'
		    : rootWindow.available && rootWindow.kind === 'array'  ? 'Empty array.'
		    : source === 'value' ? 'No current source value. Select a Field and press Read.'
		    : source === 'draft' ? 'No local request draft.'
		                         : 'No current response value.';
	}

	function renderOutcomes()
	{
		const nodes = [];
		for (const [operation, result] of Object.entries(state.outcomes)) {
			if (!result)
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

	function render()
	{
		const nextContext = state.mode + ':' + state.session + ':' + (state.selected?.key || '');
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
		permissionInput.checked = state.writesEnabled;
		permissionInput.disabled = state.busy || !state.current;
		selection.textContent = state.selected
		                            ? state.selected.catalog + ' / ' + state.selected.name
		                            : 'Select an endpoint';
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
			button.disabled = !state.actions[action];
		}
		error.textContent = localError || state.error;
		error.hidden = !error.textContent;
		renderEndpoints();
		renderRows('value');
		renderRows('draft');
		renderRows('response');
		sections.get('value').panel.hidden =
		    Boolean(state.selected && state.selected.category !== 1);
		sections.get('draft').heading.textContent =
		    state.selected?.category === 1 ? 'Local Field draft' : 'Request arguments';
		sections.get('draft').panel.hidden = !state.hasDraft;
		sections.get('response').panel.hidden =
		    !state.hasResponse && state.selected?.category !== 3;
		renderOutcomes();
		status.dataset.session = String(state.session);
		status.textContent = (state.current ? 'Connected' : 'Disconnected') +
		                     (state.busy ? ' / Working' : ' / Idle') +
		                     (state.dirty ? ' / Unsaved draft' : '');
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
