/* Explicit-provider presentation over existing Telemetry SDK clients.
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
 */
import {Batch, Status} from 'telemetry-sdk/client';
import {encodeValue} from 'telemetry-sdk/runtime';

const names = new Map(Object.entries(Status).map(([ name, code ]) => [code, name]));
const kinds = [ 'void', 'scalar', 'enum', 'struct', 'array' ];
const scalarNames =
    [ '', 'Bool', 'U8', 'S8', 'U16', 'S16', 'U32', 'S32', 'U64', 'S64', 'F32', 'F64' ];
const freeze = Object.freeze;
const empty = freeze([]);
const fail = message => {
	throw new TypeError(message);
};
const text = value => typeof value === 'number' && Object.is(value, -0) ? '-0' : String(value);
const endpointNames = {
	write : [ 'Applied', 'NotFound', 'ReadOnly', 'InvalidValue', 'Busy', 'Unavailable' ],
	command : [
		'Executed', 'Accepted', 'NotFound', 'Unavailable', 'ArgumentCountMismatch', 'InvalidValue',
		'Busy', 'Failed'
	],
	service : [ 'Ok', 'InvalidArgument', 'Unavailable', 'Busy', 'Failed' ]
};
const dispatchNames = {
	4 : 'NotFound',
	5 : 'InvalidPayload',
	6 : 'BufferTooSmall',
	7 : 'WorkspaceTooSmall',
	8 : 'InternalError',
	9 : 'Unavailable'
};
const pathKey = path => JSON.stringify(path);
const define = (object, key, value) => Object.defineProperty(
    object, key, {value, writable : true, enumerable : true, configurable : true});

function limitsFor(input = {})
{
	const result = {...{nodes : 65536, depth : 64, wireBytes : 1048576}, ...input};
	for (const key of ['nodes', 'depth', 'wireBytes'])
		if (!Number.isSafeInteger(result[key]) || result[key] < 1)
			fail('Invalid presentation limit: ' + key);
	return freeze(result);
}

// This traverses the already supplied metadata graph. Descriptor parsing and
// canonical payload validation remain exclusively in the SDK.
function graph(types, limits)
{
	if (!Array.isArray(types) || types.length > limits.nodes)
		fail('Invalid presentation type graph');
	const rows = types.map((type, id) => {
		if (!type || type.id !== id || !Number.isInteger(type.kind) || type.kind < 0 ||
		    type.kind > 4 || !Number.isSafeInteger(type.wireBytes) || type.wireBytes < 0)
			fail('Invalid presentation type');
		const row = {id, kind : type.kind, wireBytes : type.wireBytes};
		if (row.kind === 1) {
			if (!Number.isInteger(type.scalarCode) || type.scalarCode < 1 || type.scalarCode > 11)
				fail('Invalid presentation scalar');
			row.scalarCode = type.scalarCode;
		} else if (row.kind === 2) {
			row.underlyingTypeId = type.underlyingTypeId;
			row.entries = freeze(type.entries.map(
			    entry => freeze({code : String(entry.code), name : String(entry.name)})));
		} else if (row.kind === 3) {
			row.members = freeze(type.members.map(
			    member => freeze({typeId : member.typeId, name : String(member.name)})));
		} else if (row.kind === 4) {
			if (!Number.isSafeInteger(type.elementCount) || type.elementCount < 0)
				fail('Invalid presentation array');
			row.elementTypeId = type.elementTypeId;
			row.elementCount = type.elementCount;
		}
		return freeze(row);
	});
	const costs = new Map();

	function preflight(id, visiting = new Set())
	{
		if (costs.has(id))
			return costs.get(id);
		const row = rows[id];
		if (!row || visiting.has(id) || visiting.size >= limits.depth)
			fail('Invalid or excessive presentation type depth');
		visiting.add(id);
		let nodes = 1, depth = 1;
		const add = (childId, count) => {
			const child = preflight(childId, visiting);
			nodes += child.nodes * count;
			depth = Math.max(depth, child.depth + 1);
			if (!Number.isSafeInteger(nodes) || nodes > limits.nodes)
				fail('Presentation value exceeds node budget');
		};
		if (row.kind === 2)
			add(row.underlyingTypeId, 1);
		if (row.kind === 3)
			for (const member of row.members)
				add(member.typeId, 1);
		if (row.kind === 4)
			add(row.elementTypeId, row.elementCount);
		visiting.delete(id);
		if (row.wireBytes > limits.wireBytes || depth > limits.depth)
			fail('Presentation value exceeds byte or depth budget');
		const result = freeze({nodes, depth});
		costs.set(id, result);
		return result;
	}

	// Check only a selected type before initialization/copying; unrelated large
	// endpoints can remain discoverable without allocating their DTOs.
	return {types : freeze(rows), preflight};
}

function copyValue(types, id, value, initialize = false)
{
	const type = types[id];
	if (type.kind === 0)
		return undefined;
	if (type.kind === 1)
		return initialize ? (type.scalarCode === 1                            ? false
		                     : type.scalarCode === 8 || type.scalarCode === 9 ? 0n
		                                                                      : 0)
		                  : value;
	if (type.kind === 2)
		return copyValue(types, type.underlyingTypeId, value, initialize);
	if (type.kind === 4) {
		const result = new Array(type.elementCount);
		for (let i = 0; i < result.length; ++i)
			result[i] =
			    copyValue(types, type.elementTypeId, initialize ? undefined : value[i], initialize);
		return result;
	}
	const result = Object.create(null);
	for (const member of type.members)
		define(result, member.name,
		       copyValue(types, member.typeId, initialize ? undefined : value[member.name],
		                 initialize));
	return result;
}

function location(types, typeId, value, path)
{
	if (!Array.isArray(path) || path.length > 64)
		fail('Invalid value path');
	let type = types[typeId];
	for (const key of path) {
		if (type.kind === 3 && typeof key === 'string') {
			const member = type.members.find(row => row.name === key);
			if (!member)
				fail('Unknown member path');
			type = types[member.typeId];
		} else if (type.kind === 4 && Number.isSafeInteger(key) && key >= 0 &&
		           key < type.elementCount) {
			type = types[type.elementTypeId];
		} else
			fail('Invalid value path');
		value = value[key];
	}
	return {type, value};
}

function replace(types, id, value, path, replacement, index = 0)
{
	if (index === path.length)
		return replacement;
	const type = types[id], key = path[index];
	if (type.kind === 4) {
		const result = value.slice();
		result[key] = replace(types, type.elementTypeId, value[key], path, replacement, index + 1);
		return result;
	}
	const result = Object.create(null);
	for (const member of type.members)
		define(result, member.name,
		       member.name === key
		           ? replace(types, member.typeId, value[key], path, replacement, index + 1)
		           : value[member.name]);
	return result;
}

function parseScalar(types, type, input)
{
	if (typeof input !== 'string')
		fail('Text must be a string');
	if (type.kind === 2)
		type = types[type.underlyingTypeId];
	if (type.kind !== 1)
		fail('Only scalar leaves can be edited');
	const code = type.scalarCode;
	if (code === 1) {
		if (input === 'true')
			return true;
		if (input === 'false')
			return false;
		fail('Enter true or false');
	}
	if (code <= 9) {
		if (!/^[+-]?\d+$/.test(input))
			fail('Enter a decimal integer');
		const number = BigInt(input);
		if (code === 8 || code === 9)
			return number;
		const result = Number(number);
		if (!Number.isSafeInteger(result))
			fail('Integer is outside the exact numeric range');
		return result;
	}
	if (input === 'NaN')
		return NaN;
	if (input === 'Infinity' || input === '+Infinity')
		return Infinity;
	if (input === '-Infinity')
		return -Infinity;
	if (!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$/.test(input))
		fail('Enter a number');
	return Number(input);
}

function staticAdapter({client, metadata, sdk})
{
	if (!client || !sdk || !metadata || metadata.format !== 'telemetry-structural-metadata' ||
	    metadata.version !== 1 || typeof sdk.fingerprint !== 'bigint' ||
	    BigInt(metadata.fingerprint) !== sdk.fingerprint)
		fail('Matching generated SDK and structural metadata v1 are required');
	const endpoints = metadata.endpoints.map(row => {
		const category = [ '', 'fields', 'commands', 'services' ][row.category];
		const binding = sdk[row.groupSymbol]?.[category]?.[row.symbol];
		if (!binding || binding.id !== row.id ||
		    (row.category === 1 ? binding.read?.fingerprint : binding.fingerprint) !==
		        sdk.fingerprint)
			fail('Generated endpoint does not match structural metadata');
		return {...row, catalog : row.catalogName, binding};
	});
	return {
		mode : 'static',
		client,
		identity : () =>
		    ({ready : client.ready === true, session : client.session, schema : metadata}),
		describe : () => ({types : metadata.types, endpoints}),
		validate(id, value) {
		    const codec = sdk.codecs?.['Type_' + id];
		    if (!codec || typeof codec.encode !== 'function')
			    fail('Generated value codec is missing');
		    codec.encode(value);
		},
		submit(endpoint, operation, value, originalPayload) {
		    if (operation === 'read')
			    return client.read(endpoint.binding);
		    if (operation === 'write') {
			    const write = endpoint.binding.write;
			    if (typeof write?.encodePreserving !== 'function')
				    fail(
				        'Unsupported: regenerate the SDK with --wire-editing for safe Field editing');
			    return client.write(
			        {...write, encode : edited => write.encodePreserving(edited, originalPayload)},
			        value);
		    }
		    if (operation === 'command')
			    return client.command(endpoint.binding, value);
		    return client.service(endpoint.binding, value);
		},
		readMany(capability, endpoints) {
		    if (typeof client.bindReadMany !== 'function' || typeof client.readMany !== 'function')
			    fail('ReadMany is unavailable in this SDK');
		    const bound = client.bindReadMany(capability, endpoints.map(row => row.binding));
		    if (bound.status !== Status.Ok)
			    fail(names.get(bound.status) || 'ReadMany binding refused');
		    return client.readMany(bound.binding);
		},
		manyResult(result) {
		    return {operation : result, items : result.value};
		},
		batch(capability, items) {
		    if (typeof client.batch !== 'function')
			    fail('Batch is unavailable in this SDK');
		    const requests = items.map(item => {
			    const binding = item.endpoint.binding;
			    return item.operation === 'read'      ? Batch.read(binding)
			           : item.operation === 'write'   ? Batch.write(binding, item.request)
			           : item.operation === 'command' ? Batch.command(binding, item.request)
			                                          : Batch.service(binding, item.request);
		    });
		    return client.batch(capability, requests);
		},
		batchResult(result) {
		    return {operation : result, valueStatus : result.valueStatus, items : result.items};
		},
		result(result) {
		    return {
			    operation : result,
			    valueStatus : result.status === Status.CodecError ? 'InvalidPayload'
			                  : result.value !== undefined        ? 'Ok'
			                                                      : 'NoValue',
			    value : result.value
		    };
		}
	};
}

function dynamicDescription(schema)
{
	const endpoints = [];
	for (const [category, key, catalogKey] of [[ 1, 'fields', 'fieldCatalogs' ],
	                                           [ 2, 'commands', 'commandCatalogs' ],
	                                           [ 3, 'services', 'serviceCatalogs' ]])
		for (const catalog of schema[catalogKey])
			for (let index = catalog.first; index < catalog.first + catalog.count; ++index) {
				const row = schema[key][index];
				endpoints.push({
					...row,
					category,
					catalog : catalog.name,
					valueType : category === 1 ? row.typeId : 0,
					requestType : category === 1 ? 0 : row.requestTypeId,
					responseType : category === 3 ? row.responseTypeId : 0
				});
			}
	return {types : schema.types, endpoints};
}

function dynamicAdapter({client})
{
	if (!client || typeof client.bind !== 'function')
		fail('An existing DynamicClient is required');
	return {
		mode : 'dynamic',
		client,
		identity : () =>
		    ({ready : client.ready === true, session : client.session, schema : client.schema}),
		describe : dynamicDescription,
		validate(id, value, schema) {
		    encodeValue(schema, id, value);
		},
		submit(endpoint, operation, value, originalPayload) {
		    const method = {
			    read : 'FieldRead',
			    write : 'FieldWrite',
			    command : 'Command',
			    service : 'Service'
		    }[operation];
		    const bound = client.bind(method, endpoint.id);
		    if (bound.status !== Status.Ok)
			    fail(names.get(bound.status) || 'Binding refused');
		    if (operation === 'read')
			    return client.readField(bound.endpoint);
		    if (operation === 'write') {
			    if (typeof client.writeFieldPreserving !== 'function')
				    fail('Unsupported: this SDK does not support safe Field editing');
			    return client.writeFieldPreserving(bound.endpoint, value, originalPayload);
		    }
		    return client[operation](bound.endpoint, value);
		},
		result : result => result,
		readMany(capability, endpoints) {
		    if (typeof client.bindReadMany !== 'function' || typeof client.readMany !== 'function')
			    fail('ReadMany is unavailable in this SDK');
		    const handles = endpoints.map(row => {
			    const bound = client.bind('FieldRead', row.id);
			    if (bound.status !== Status.Ok)
				    fail(names.get(bound.status) || 'FieldRead binding refused');
			    return bound.endpoint;
		    });
		    const bound = client.bindReadMany(capability, handles);
		    if (bound.status !== Status.Ok)
			    fail(names.get(bound.status) || 'ReadMany binding refused');
		    return client.readMany(bound.binding);
		},
		manyResult : result => result,
		batch(capability, items) {
		    if (typeof client.batch !== 'function')
			    fail('Batch is unavailable in this SDK');
		    const operations = {
			    read : 'FieldRead',
			    write : 'FieldWrite',
			    command : 'Command',
			    service : 'Service'
		    };
		    const requests = items.map(item => {
			    const bound = client.bind(operations[item.operation], item.endpoint.id);
			    if (bound.status !== Status.Ok)
				    fail(names.get(bound.status) || 'Batch item binding refused');
			    return {endpoint : bound.endpoint, value : item.request};
		    });
		    return client.batch(capability, requests);
		},
		batchResult : result => result
	};
}

function presentation(adapter, inputLimits, hooks = {})
{
	const limits = limitsFor(inputLimits), listeners = new Set();
	let identity, description, model, endpoints = empty, selected = null;
	let revision = 0, epoch = 0, blockedSession, current = false, writesEnabled = false;
	let dataEpoch = 0, dataStale = false, draftStale = false, dataInvalidationReason = '';
	let publishing = false, notifying = false, notifyAgain = false, active;
	let value, draft, response, hasValue = false, hasDraft = false, hasResponse = false,
	                            dirty = false, error = '';
	let valuePayload, draftPayload;
	let texts = new Map(), outcomes = Object.create(null);
	const outcomeVersions = Object.create(null);
	let watched = [], watchCapability, watchRunning = false, watchCycle = false, watchPending,
	    watchIndividual = false, watchInterval = 1000, watchDue = 0, watchRevision = 0,
	    watchCursor = 0, watchStatus = 'Select scalar Fields to watch.';
	let batchItems = [], batchSelected, batchCapability, batchPending, batchOutcome,
	    batchRevision = 0, nextBatchKey = 0, batchStatus = 'Add ordered operations to the Batch.';
	const watchItemNames = [ 'Ok', 'NotFound', 'UnsupportedType', 'Unavailable', 'ReadError' ];
	const watchActive = () => watchRunning || watchCycle || !!watchPending;
	const watchEditable = () =>
	    isCurrent() && !active && !batchPending && !publishing && !notifying && !watchActive();
	const batchEditable = watchEditable;

	function isCurrent()
	{
		if (!current || !identity)
			return false;
		const live = adapter.identity();
		return live.ready && live.session === identity.session && live.schema === identity.schema;
	}

	function selectedInternal()
	{
		return selected && description?.endpoints.find(row => row.key === selected.key);
	}

	function clearValues()
	{
		++dataEpoch;
		dataStale = draftStale = false;
		dataInvalidationReason = '';
		value = draft = response = undefined;
		valuePayload = draftPayload = undefined;
		hasValue = hasDraft = hasResponse = dirty = false;
		texts.clear();
		++revision;
	}

	function invalidateData(reason)
	{
		++dataEpoch;
		dataStale = true;
		draftStale = hasDraft;
		dataInvalidationReason = reason;
		value = response = undefined;
		valuePayload = undefined;
		hasValue = hasResponse = false;
		clearWatchValues();
		clearBatchResponses();
		// An older in-flight read must not republish values after another
		// view's mutation. Retain pending storage and checked operation facts.
		watchRunning = watchCycle = false;
		++watchRevision;
		++batchRevision;
		watchStatus = watchPending ? 'Stopping - draining a stale read.'
		                           : 'Stale after mutation - explicitly refresh watched Fields.';
		++revision;
		notify();
	}

	function snapshot()
	{
		const valid = isCurrent(),
		      idle = !active && !batchPending && !publishing && !watchActive() && valid;
		const endpoint = selectedInternal();
		const canMutate = idle && writesEnabled && !!endpoint && !draftStale;
		const facts = Object.create(null);
		for (const [key, fact] of Object.entries(outcomes))
			facts[key] =
			    freeze({...fact, historical : !valid || fact.session !== identity.session});
		const state = {
			mode : adapter.mode,
			current : valid,
			session : identity?.session || 0,
			endpoints : valid ? endpoints : empty,
			selected : valid ? selected : null,
			busy : !!active || !!watchPending || !!batchPending,
			writesEnabled,
			revision,
			hasValue : valid && hasValue,
			hasDraft : valid && hasDraft,
			hasResponse : valid && hasResponse,
			dirty : valid && dirty,
			error,
			outcomes : freeze(facts),
			watch : freeze({
				rows : freeze(watched.map(row => freeze({
					                          key : row.endpoint.key,
					                          id : row.endpoint.id,
					                          name : row.endpoint.name,
					                          catalog : String(row.endpoint.catalog),
					                          status : valid ? row.status : 'Stale',
					                          text : valid ? row.text : ''
				                          }))),
				active : watchActive(),
				pending : !!watchPending,
				running : watchRunning,
				individual : watchIndividual,
				intervalMs : watchInterval,
				status : watchStatus,
				canAdd : idle && watched.length < 32 && !!endpoint && watchScalar(endpoint) &&
				             !watched.some(row => row.endpoint.key === endpoint.key),
				canRefresh : idle && watched.length > 0
			}),
			batch : freeze({
				items : freeze(batchItems.map(
				    item => freeze({
					    key : item.key,
					    endpointKey : item.endpoint.key,
					    category : item.endpoint.category,
					    name : item.endpoint.name,
					    catalog : String(item.endpoint.catalog),
					    operation : item.operation,
					    status : valid ? item.status : 'Historical - ' + item.status,
					    outcome :
					        item.outcome && freeze({
						        ...item.outcome,
						        historical : !valid || item.outcome.session !== identity?.session
					        })
				    }))),
				selectedKey : batchSelected || '',
				active : !!batchPending,
				pending : !!batchPending,
				status : batchStatus,
				canAdd : idle && batchItems.length < 32 && batchAddOperations(endpoint).length > 0,
				addOperations : freeze(valid ? batchAddOperations(endpoint) : []),
				canExecute :
				    idle && batchItems.length > 0 && !batchHasErrors() &&
				        (writesEnabled || batchItems.every(item => item.operation === 'read')),
				hasDraft : valid && !!selectedBatch() && selectedBatch().operation !== 'read',
				hasResponse : valid && selectedBatch()?.hasResponse === true,
				outcome : batchOutcome && freeze({
					          ...batchOutcome,
					          historical : !valid || batchOutcome.session !== identity?.session
				          })
			}),
			actions : freeze({
				refresh : idle && endpoint?.category === 1,
				beginEdit : idle && !!endpoint && !draftStale &&
				                (endpoint.category !== 1 ||
				                 (writesEnabled && endpoint.capabilities === 3 && hasValue)),
				cancelEdit : idle && hasDraft,
				apply : canMutate && endpoint.category === 1 && endpoint.capabilities === 3 &&
				            hasDraft && !hasErrors(),
				execute : canMutate && endpoint.category === 2 && hasDraft && !hasErrors(),
				call : canMutate && endpoint.category === 3 && hasDraft && !hasErrors()
			})
		};
		return hooks.snapshot ? hooks.snapshot(state, {dataStale, dataInvalidationReason})
		                      : freeze(state);
	}

	function notify()
	{
		if (notifying) {
			notifyAgain = true;
			return;
		}
		notifying = true;
		try {
			// One coalesced later notification handles synchronous invalidation
			// from a listener without recursive publication.
			do {
				notifyAgain = false;
				const state = snapshot();
				const token = epoch;
				for (const listener of Array.from(listeners)) {
					try {
						listener(state);
					} catch { /* One host listener cannot suppress others. */
					}
					if (token !== epoch)
						break;
				}
			} while (notifyAgain);
		} finally {
			notifying = false;
		}
	}

	function hasErrors()
	{
		return [...texts.values() ].some(row => row.error);
	}

	function revoke()
	{
		if (!current && blockedSession === identity?.session)
			return;
		++epoch;
		blockedSession = identity?.session;
		current = false;
		writesEnabled = false;
		retireWatch();
		retireBatch();
		clearValues();
		notify();
	}

	function sync()
	{
		if (publishing || notifying)
			return false;
		const live = adapter.identity();
		if (!live.ready || !live.schema || live.session === blockedSession) {
			if (current)
				revoke();
			return false;
		}
		if (current && identity?.session === live.session && identity.schema === live.schema)
			return true;
		++epoch;
		current = false;
		writesEnabled = false;
		retireWatch();
		watched = [];
		retireBatch();
		batchItems = [];
		batchSelected = undefined;
		clearValues();
		identity = live;
		const token = epoch;
		publishing = true;
		try {
			description = adapter.describe(live.schema);
			model = graph(description.types, limits);
			const seen = new Set();
			description.endpoints = description.endpoints.map(row => {
				if (![1, 2, 3].includes(row.category) || !Number.isInteger(row.id) || row.id < 0 ||
				    row.id > 0xffffffff)
					fail('Invalid presentation endpoint');
				const key = row.category + ':' + row.id;
				if (seen.has(key))
					fail('Duplicate presentation endpoint');
				seen.add(key);
				return {...row, key};
			});
			endpoints = freeze(description.endpoints.map(row => freeze({
				                                             key : row.key,
				                                             category : row.category,
				                                             id : row.id,
				                                             catalog : String(row.catalog),
				                                             name : String(row.name)
			                                             })));
			selected = null;
			// A Session supplies independently discovered records once per
			// admission, never by issuing discovery or requests from the view.
			const capabilities = adapter.capabilities?.();
			watchCapability = copyCapability(capabilities?.readMany);
			batchCapability = copyCapability(capabilities?.batch);
			const after = adapter.identity();
			current = token === epoch && after.session === live.session &&
			          after.schema === live.schema && after.ready;
			error = '';
		} catch (failure) {
			description = model = undefined;
			endpoints = empty;
			selected = null;
			error = String(failure.message || failure);
		} finally {
			publishing = false;
		}
		notify();
		return current;
	}

	function typeId(source)
	{
		const endpoint = selectedInternal();
		if (!endpoint)
			fail('No selected endpoint');
		if (source === 'response')
			return endpoint.responseType;
		return endpoint.category === 1 ? endpoint.valueType : endpoint.requestType;
	}

	function validate(id, candidate)
	{
		model.preflight(id);
		adapter.validate(id, candidate, identity.schema);
	}

	function beginEdit()
	{
		if (notifying || !snapshot().actions.beginEdit)
			return false;
		const token = epoch, dataToken = dataEpoch, id = typeId('draft');
		publishing = true;
		try {
			model.preflight(id);
			const candidate = copyValue(model.types, id, value, selected.category !== 1);
			validate(id, candidate);
			if (!isCurrent() || token !== epoch || dataToken !== dataEpoch)
				return false;
			draft = candidate;
			draftPayload = selected.category === 1 ? valuePayload : undefined;
			hasDraft = true;
			dataStale = draftStale = false;
			dataInvalidationReason = '';
			dirty = selected.category !== 1;
			texts.clear();
			error = '';
			++revision;
			return true;
		} catch (failure) {
			error = String(failure.message || failure);
			return false;
		} finally {
			publishing = false;
			notify();
		}
	}

	function select(key)
	{
		if (active || batchPending || publishing || notifying || !isCurrent() ||
		    typeof key !== 'string')
			return false;
		const endpoint = endpoints.find(row => row.key === key);
		if (!endpoint)
			return false;
		selected = endpoint;
		clearValues();
		error = '';
		if (endpoint.category !== 1 && snapshot().actions.beginEdit)
			beginEdit();
		else
			notify();
		return true;
	}

	function rows(path = [], options = {})
	{
		const source = options.source ?? 'value', offset = options.offset ?? 0,
		      limit = options.limit ?? 128;
		const item =
		    source === 'batchDraft' || source === 'batchResponse' ? selectedBatch() : undefined;
		const batchSource = source === 'batchDraft' || source === 'batchResponse';
		const editableSource = source === 'draft' || source === 'batchDraft';
		const available = source === 'batchDraft'      ? !!item && item.operation !== 'read'
		                  : source === 'batchResponse' ? item?.hasResponse === true
		                  : source === 'value'         ? hasValue
		                  : source === 'draft'         ? hasDraft
		                  : source === 'response'      ? hasResponse
		                                               : false;
		const output = result => freeze({
			path : freeze([...path ]),
			total : result.total,
			offset,
			kind : result.kind ?? null,
			available : result.available === true,
			rows : freeze(result.rows)
		});
		if (!Array.isArray(path) || path.length > 64 ||
		    !['value', 'draft', 'response', 'batchDraft', 'batchResponse'].includes(source) ||
		    !Number.isInteger(offset) || offset < 0 || !Number.isInteger(limit) || limit < 1 ||
		    limit > 128)
			fail('Invalid row window');
		if (!isCurrent() || (batchSource ? !item : !selected))
			return output({total : 0, rows : []});
		const id = batchSource ? batchType(item, source === 'batchResponse') : typeId(source);
		if (!available)
			return output({total : 0, kind : kinds[model.types[id].kind], rows : []});
		const root = source === 'batchDraft'      ? item.request
		             : source === 'batchResponse' ? item.response
		             : source === 'value'         ? value
		             : source === 'draft'         ? draft
		                                          : response;
		const place = location(model.types, id, root, path);
		const aggregate = place.type.kind === 3 || place.type.kind === 4;
		const count = place.type.kind === 3   ? place.type.members.length
		              : place.type.kind === 4 ? place.type.elementCount
		                                      : 1;
		const result = [];
		for (let i = offset; i < Math.min(count, offset + limit); ++i) {
			let childPath = path, name = path.length ? String(path[path.length - 1]) : 'Value',
			    type = place.type, childValue = place.value;
			if (aggregate) {
				const member = place.type.kind === 3 ? place.type.members[i] : null;
				const key = member ? member.name : i;
				childPath = [...path, key ];
				name = String(key);
				type = model.types[member ? member.typeId : place.type.elementTypeId];
				childValue = place.value[key];
			}
			const isAggregate = type.kind === 3 || type.kind === 4;
			const override =
			    editableSource ? (item ? item.texts : texts).get(pathKey(childPath)) : undefined;
			const scalarType = type.kind === 2 ? model.types[type.underlyingTypeId] : type;
			result.push(freeze({
				key : pathKey(childPath),
				path : freeze([...childPath ]),
				name,
				kind : kinds[type.kind],
				type : scalarNames[scalarType.scalarCode] || kinds[type.kind],
				children : type.kind === 3   ? type.members.length
				           : type.kind === 4 ? type.elementCount
				                             : 0,
				text : override          ? override.text
				       : isAggregate     ? ''
				       : type.kind === 0 ? 'Void'
				                         : text(childValue),
				error : override?.error || '',
				editable : editableSource && !active && !batchPending && !publishing &&
				               !watchActive() && (item || !draftStale) && !isAggregate &&
				               type.kind !== 0,
				enumEntries : type.kind === 2 ? type.entries : empty
			}));
		}
		return output(
		    {total : count, kind : kinds[place.type.kind], available : true, rows : result});
	}

	function editText(path, input, expectedRevision, item)
	{
		if (active || batchPending || publishing || notifying || watchActive() || !isCurrent() ||
		    (!item && draftStale) || (item ? item.operation === 'read' : !hasDraft) ||
		    expectedRevision !== revision || typeof input !== 'string')
			return false;
		const token = epoch, dataToken = dataEpoch;
		const inputTexts = item ? item.texts : texts;
		let validPath = false;
		publishing = true;
		try {
			const id = item ? batchType(item) : typeId('draft');
			const inputValue = item ? item.request : draft;
			const place = location(model.types, id, inputValue, path);
			if (place.type.kind !== 1 && place.type.kind !== 2)
				return false;
			validPath = true;
			const candidate = replace(model.types, id, inputValue, path,
			                          parseScalar(model.types, place.type, input));
			validate(id, candidate);
			if (!isCurrent() || token !== epoch || dataToken !== dataEpoch)
				return false;
			if (item) {
				item.request = candidate;
				item.status = 'Draft';
			} else {
				draft = candidate;
				dirty = true;
			}
			inputTexts.set(pathKey(path), {text : input, error : ''});
			error = '';
			++revision;
			return true;
		} catch (failure) {
			if (validPath && isCurrent() && token === epoch && dataToken === dataEpoch) {
				const reason = String(failure.message || failure);
				inputTexts.set(pathKey(path), {text : input, error : reason});
				error = reason;
				++revision;
			}
			return false;
		} finally {
			publishing = false;
			notify();
		}
	}

	function setText(path, input, expectedRevision)
	{
		return editText(path, input, expectedRevision);
	}

	function fact(operation, endpoint, result, valueStatus = 'NoValue')
	{
		return freeze({
			operation,
			endpointKey : endpoint.key,
			endpointName : endpoint.name,
			status : result.status,
			name : names.get(result.status) || String(result.status),
			display :
			    result.status !== Status.Ok || result.dispatch !== 0
			        ? failureText(operation, result)
			        : endpointNames[operation]?.[result.endpointStatus] ||
			              (result.succeeded ? 'Read completed' : failureText(operation, result)),
			session : result.session,
			requestId : result.requestId,
			dispatch : result.dispatch,
			endpointStatus : result.endpointStatus,
			succeeded : result.succeeded === true,
			accepted : result.accepted === true,
			executed : result.executed === true,
			valueStatus
		});
	}

	function failureText(kind, operation)
	{
		if (operation.status !== Status.Ok)
			return names.get(operation.status) || String(operation.status);
		if (operation.dispatch !== 0)
			return 'Dispatch: ' + (dispatchNames[operation.dispatch] || operation.dispatch);
		return (kind === 'read' || kind === 'readback' ? 'FieldRead' : kind) + ': ' +
		       (endpointNames[kind]?.[operation.endpointStatus] ||
		        'endpoint status ' + operation.endpointStatus);
	}

	function complete(record, result, kind)
	{
		if (active !== record)
			return;
		const decoded = adapter.result(result), operation = decoded.operation;
		const valid = record.epoch === epoch && isCurrent();
		const valuesCurrent = valid && record.dataEpoch === dataEpoch;
		outcomes[kind] = fact(kind, record.endpoint, operation,
		                      (kind === 'write' || kind === 'command' ? valid : valuesCurrent)
		                          ? decoded.valueStatus
		                          : 'Stale');
		if (valuesCurrent && operation.succeeded &&
		    (kind === 'read' || kind === 'readback' || kind === 'service') &&
		    decoded.valueStatus === 'Ok') {
			publishing = true;
			try {
				const id =
				    kind === 'service' ? record.endpoint.responseType : record.endpoint.valueType;
				model.preflight(id);
				const candidate = copyValue(model.types, id, decoded.value);
				const payload = kind === 'service' ? undefined : new Uint8Array(operation.payload);
				validate(id, candidate);
				if (record.epoch === epoch && record.dataEpoch === dataEpoch && isCurrent()) {
					if (kind === 'service') {
						response = candidate;
						hasResponse = model.types[id].kind !== 0;
					} else {
						value = candidate;
						valuePayload = payload;
						hasValue = model.types[id].kind !== 0;
					}
					dataStale = draftStale;
					if (!dataStale)
						dataInvalidationReason = '';
					error = '';
					++revision;
				}
			} catch (failure) {
				error = String(failure.message || failure);
				outcomes[kind] =
				    fact(kind, record.endpoint, operation,
				         record.epoch === epoch && record.dataEpoch === dataEpoch && isCurrent()
				             ? 'InvalidPayload'
				             : 'Stale');
			} finally {
				publishing = false;
			}
			if (record.epoch !== epoch || record.dataEpoch !== dataEpoch || !isCurrent())
				outcomes[kind] = fact(kind, record.endpoint, operation, 'Stale');
		} else if (valid && !operation.succeeded)
			error = failureText(kind, operation);
		if (kind === 'write' && operation.succeeded && valid) {
			// A confirmed Write is not a readback. Keep both outcomes even when
			// the following independent FieldRead is refused or fails.
			try {
				const ticket = outcomeVersions.readback = (outcomeVersions.readback || 0) + 1;
				record.dataEpoch = dataEpoch;
				const request = adapter.submit(record.endpoint, 'read');
				if (request.requestId !== 0) {
					request.then(reply => complete(record, reply, 'readback'),
					             failure => failed(record, failure));
					notify();
					return;
				}
				request.then(
				    reply => {
					    if (outcomeVersions.readback === ticket) {
						    outcomes.readback =
						        fact('readback', record.endpoint, adapter.result(reply).operation);
						    notify();
					    }
				    },
				    () => {
				    });
				error = 'Readback submission refused';
			} catch (failure) {
				error = String(failure.message || failure);
			}
		}
		active = undefined;
		notify();
	}

	function failed(record, failure)
	{
		if (active !== record)
			return;
		active = undefined;
		error = String(failure.message || failure);
		notify();
	}

	function start(kind)
	{
		const action = kind === 'read'      ? 'refresh'
		               : kind === 'write'   ? 'apply'
		               : kind === 'command' ? 'execute'
		                                    : 'call';
		if (notifying || !snapshot().actions[action])
			return false;
		const record = {epoch, dataEpoch, endpoint : selectedInternal()};
		const ticket = outcomeVersions[kind] = (outcomeVersions[kind] || 0) + 1;
		publishing = true;
		let request;
		try {
			if (kind !== 'read')
				validate(typeId('draft'), draft);
			if (record.epoch !== epoch || record.dataEpoch !== dataEpoch || !isCurrent())
				return false;
			active = record;
			request = adapter.submit(record.endpoint, kind, draft, draftPayload);
			if (request.requestId === 0) {
				active = undefined;
				request.then(
				    reply => {
					    if (outcomeVersions[kind] === ticket) {
						    const operation = adapter.result(reply).operation;
						    outcomes[kind] = fact(kind, record.endpoint, operation);
						    error = failureText(kind, operation);
						    notify();
					    }
				    },
				    () => {
				    });
				return false;
			}
			outcomes[kind] = freeze({
				operation : kind,
				endpointKey : record.endpoint.key,
				endpointName : record.endpoint.name,
				status : null,
				name : 'Pending',
				display : 'Pending',
				session : request.session,
				requestId : request.requestId,
				dispatch : null,
				endpointStatus : null,
				succeeded : false,
				accepted : false,
				executed : false,
				valueStatus : 'NoValue'
			});
			if (kind !== 'read') {
				clearWatchValues();
				clearBatchResponses();
				watchStatus = 'Stale after mutation - explicitly refresh watched Fields.';
			}
			if (kind === 'read' || kind === 'write') {
				value = undefined;
				valuePayload = undefined;
				hasValue = false;
			}
			if (kind === 'service') {
				response = undefined;
				hasResponse = false;
			}
			if (kind === 'write') {
				draft = undefined;
				draftPayload = undefined;
				hasDraft = dirty = false;
				texts.clear();
			}
			error = '';
			++revision;
			request.then(reply => complete(record, reply, kind),
			             failure => failed(record, failure));
			if (kind !== 'read')
				hooks.acceptedMutation?.(kind);
			return true;
		} catch (failure) {
			active = undefined;
			error = String(failure.message || failure);
			return false;
		} finally {
			publishing = false;
			notify();
		}
	}

	// Polling is owner-driven through tickWatch(); this presentation creates no
	// timer, transport, SDK client or additional execution queue.
	function watchScalar(endpoint)
	{
		const type = model?.types[endpoint.valueType];
		return endpoint.category === 1 && (endpoint.capabilities & 1) !== 0 &&
		       (type?.kind === 1 || type?.kind === 2) && type.wireBytes <= 8;
	}

	function clearWatchValues()
	{
		for (const row of watched) {
			row.status = 'Stale';
			row.text = '';
		}
	}

	function retireWatch()
	{
		watchRunning = watchCycle = false;
		watchCapability = undefined;
		++watchRevision;
		clearWatchValues();
		watchStatus = watchPending ? 'Stopping - draining the current read.' : 'Stopped';
	}

	function watchFailure(message, retireValues = true)
	{
		watchRunning = watchCycle = false;
		if (retireValues)
			clearWatchValues();
		watchStatus = message;
	}

	function completeWatch(record, result)
	{
		if (watchPending !== record)
			return;
		watchPending = undefined;
		if (record.epoch !== epoch || record.revision !== watchRevision || !isCurrent()) {
			watchRunning = watchCycle = false;
			watchStatus = 'Stopped - superseded read drained.';
			notify();
			return;
		}
		publishing = true;
		try {
			const decoded = record.individual ? adapter.result(result) : adapter.manyResult(result);
			const fieldRefused = record.individual && decoded.operation.status === Status.Ok &&
			                     decoded.operation.dispatch !== 0;
			if (!decoded.operation.succeeded && !fieldRefused)
				throw new Error(failureText('read', decoded.operation));
			const items = record.individual ? [ {
				id : watched[record.cursor].endpoint.id,
				status : !fieldRefused                      ? 0
					     : decoded.operation.dispatch === 4 ? 1
					     : decoded.operation.dispatch === 9 ? 3
					                                        : 4,
				valueStatus : decoded.valueStatus,
				value : decoded.value
			} ]
			                                : decoded.items;
			const count = record.individual ? 1 : watched.length;
			if (!Array.isArray(items) || items.length !== count)
				fail('Invalid ReadMany result');
			const changed = items.map((item, index) => {
				const row = watched[record.individual ? record.cursor : index];
				if (item.id !== row.endpoint.id || !Number.isInteger(item.status) ||
				    item.status < 0 || item.status >= watchItemNames.length)
					fail('Invalid ReadMany item');
				if (item.status !== 0)
					return {row, status : watchItemNames[item.status], text : ''};
				if (item.valueStatus !== undefined && item.valueStatus !== 'Ok')
					fail('ReadMany value is ' + item.valueStatus);
				validate(row.endpoint.valueType, item.value);
				let display = text(item.value);
				const type = model.types[row.endpoint.valueType];
				if (type.kind === 2) {
					const known = type.entries.find(entry => entry.code === display);
					if (known)
						display = known.name + ' (' + display + ')';
				}
				return {row, status : 'Ok', text : display};
			});
			if (record.epoch !== epoch || record.revision !== watchRevision || !isCurrent())
				return;
			for (const change of changed) {
				change.row.status = change.status;
				change.row.text = change.text;
			}
			if (!record.individual || ++watchCursor >= watched.length) {
				watchCycle = false;
				watchStatus = (record.individual ? 'Individual FieldRead' : 'ReadMany') +
				              ' complete - ' + watched.length + ' Fields';
			}
		} catch (failure) {
			watchFailure(String(failure.message || failure), record.accepted);
		} finally {
			publishing = false;
			notify();
		}
		if (watchCycle && record.revision === watchRevision && isCurrent())
			launchWatch();
	}

	function launchWatch()
	{
		if (active || watchPending || publishing || notifying || !isCurrent() || !watchCycle)
			return false;
		const record =
		    {epoch, revision : watchRevision, cursor : watchCursor, individual : watchIndividual};
		publishing = true;
		try {
			if (!watchIndividual && !watchCapability)
				fail(
				    'ReadMany unavailable. Supply TRM1 for this session or explicitly select Individual FieldRead.');
			watchPending = record;
			const request = watchIndividual ? adapter.submit(watched[watchCursor].endpoint, 'read')
			                                : adapter.readMany(watchCapability,
			                                                   watched.map(row => row.endpoint));
			record.accepted = request.requestId !== 0;
			request.then(reply => completeWatch(record, reply), failure => {
				if (watchPending !== record)
					return;
				watchPending = undefined;
				watchFailure(String(failure.message || failure));
				notify();
			});
			if (request.requestId === 0) {
				// The core refused admission. Existing values remain until the
				// refused completion supplies its precise operation status.
				watchStatus = 'Watch submission refused';
				return false;
			}
			watchStatus = watchIndividual ? 'Individual FieldRead pending' : 'ReadMany pending';
			return true;
		} catch (failure) {
			watchPending = undefined;
			watchFailure(String(failure.message || failure), false);
			return false;
		} finally {
			publishing = false;
			notify();
		}
	}

	function refreshWatch()
	{
		if (!watchEditable() || !watched.length)
			return false;
		watchCycle = true;
		watchCursor = 0;
		return launchWatch();
	}

	function selectedBatch()
	{
		return batchItems.find(item => item.key === batchSelected);
	}

	function batchType(item, output = false)
	{
		return output ? item.operation === 'read'      ? item.endpoint.valueType
		                : item.operation === 'service' ? item.endpoint.responseType
		                                               : 0
		       : item.operation === 'write' ? item.endpoint.valueType
		       : item.operation === 'read'  ? 0
		                                    : item.endpoint.requestType;
	}

	function batchAddOperations(endpoint)
	{
		if (!endpoint)
			return [];
		if (endpoint.category === 1)
			return [
				...((endpoint.capabilities & 1) ? [ 'read' ] : []),
				...((endpoint.capabilities & 2) ? [ 'write' ] : [])
			];
		return [ endpoint.category === 2 ? 'command' : 'service' ];
	}

	function batchHasErrors()
	{
		return batchItems.some(item => [...item.texts.values()].some(row => row.error));
	}

	function clearBatchResponses()
	{
		for (const item of batchItems) {
			item.response = undefined;
			item.hasResponse = false;
		}
	}

	function retireBatch()
	{
		batchCapability = undefined;
		++batchRevision;
		clearBatchResponses();
		batchStatus = batchPending ? 'Invalidated - draining the accepted Batch.' : 'Stale';
	}

	function batchFact(operation, valueStatus)
	{
		return freeze({
			status : operation.status,
			name : names.get(operation.status) || String(operation.status),
			display : operation.status !== Status.Ok
			              ? names.get(operation.status) || String(operation.status)
			          : operation.dispatch !== 0
			              ? 'Dispatch: ' + (dispatchNames[operation.dispatch] || operation.dispatch)
			              : 'Batch reply received - inspect each item outcome',
			session : operation.session,
			requestId : operation.requestId,
			succeeded : operation.succeeded === true,
			valueStatus
		});
	}

	function completeBatch(record, result)
	{
		if (batchPending !== record)
			return;
		publishing = true;
		try {
			const decoded = adapter.batchResult(result), operation = decoded.operation;
			const valid =
			    record.epoch === epoch && record.revision === batchRevision && isCurrent();
			batchOutcome = batchFact(operation, valid ? decoded.valueStatus : 'Stale');
			batchStatus = batchOutcome.display;
			if (record.accepted) {
				// Preserve known per-item operation facts independently from the
				// right to expose values using the current metadata/session.
				const items = decoded.items;
				if (Array.isArray(items) && items.length === record.items.length) {
					const facts = items.map((item, index) => {
						const target = record.items[index];
						const expected = {
							read : 'FieldRead',
							write : 'FieldWrite',
							command : 'Command',
							service : 'Service'
						}[target.operation];
						if (item.id !== target.endpoint.id || item.operation !== expected)
							fail('Invalid Batch item identity');
						return fact(target.operation, target.endpoint, item,
						            valid ? item.valueStatus : 'Stale');
					});
					for (let index = 0; index < facts.length; ++index) {
						record.items[index].outcome = facts[index];
						record.items[index].status = facts[index].display;
					}
					if (valid && decoded.valueStatus === 'Ok') {
						const values = items.map((item, index) => {
							const target = record.items[index], id = batchType(target, true);
							if (!item.succeeded || item.valueStatus !== 'Ok' ||
							    model.types[id].kind === 0)
								return undefined;
							const copy = copyValue(model.types, id, item.value);
							validate(id, copy);
							return copy;
						});
						if (record.epoch === epoch && record.revision === batchRevision &&
						    isCurrent())
							for (let index = 0; index < values.length; ++index) {
								const target = record.items[index];
								target.response = values[index];
								target.hasResponse = values[index] !== undefined;
							}
					}
				} else {
					// No reply does not identify any executed prefix. Never infer
					// per-item success from a global OutcomeUnknown or timeout.
					for (const item of record.items)
						item.status = operation.status === Status.Ok ? 'No checked item outcome'
						                                             : batchOutcome.name;
				}
			}
		} catch (failure) {
			clearBatchResponses();
			batchStatus = 'Invalid Batch response: ' + String(failure.message || failure);
			if (batchOutcome)
				batchOutcome = freeze({...batchOutcome, valueStatus : 'InvalidPayload'});
		} finally {
			batchPending = undefined;
			publishing = false;
			++revision;
			notify();
		}
	}

	function executeBatch()
	{
		if (notifying || !snapshot().batch.canExecute)
			return false;
		const record =
		    {epoch, revision : batchRevision, items : batchItems.slice(), accepted : false};
		publishing = true;
		try {
			if (!batchCapability)
				fail('Batch unavailable. Supply TBM1 for this admitted session.');
			for (const item of record.items)
				if (item.operation !== 'read')
					validate(batchType(item), item.request);
			if (record.epoch !== epoch || !isCurrent())
				return false;
			batchPending = record;
			const request = adapter.batch(batchCapability, record.items);
			record.accepted = request.requestId !== 0;
			request.then(reply => completeBatch(record, reply), failure => {
				if (batchPending !== record)
					return;
				batchPending = undefined;
				clearBatchResponses();
				batchStatus = 'Batch transport failure: ' + String(failure.message || failure);
				notify();
			});
			if (!record.accepted)
				return false; // Keep arguments and previous owned results on refusal.
			clearBatchResponses();
			for (const item of record.items) {
				item.status = 'Pending';
				item.outcome = undefined;
			}
			if (record.items.some(item => item.operation !== 'read')) {
				clearValues();
				clearWatchValues();
				watchStatus = 'Stale after mutation - explicitly refresh watched Fields.';
			}
			batchOutcome = freeze({
				status : null,
				name : 'Pending',
				display : 'Pending',
				session : request.session,
				requestId : request.requestId,
				succeeded : false,
				valueStatus : 'NoValue'
			});
			batchStatus = 'Pending - one ordered Batch request';
			if (record.items.some(item => item.operation !== 'read'))
				hooks.acceptedMutation?.('batch');
			return true;
		} catch (failure) {
			batchPending = undefined;
			batchStatus = String(failure.message || failure);
			return false;
		} finally {
			publishing = false;
			notify();
		}
	}

	function copyCapability(input)
	{
		const bytes = input instanceof ArrayBuffer ? new Uint8Array(input)
		              : ArrayBuffer.isView(input)
		                  ? new Uint8Array(input.buffer, input.byteOffset, input.byteLength)
		                  : undefined;
		return bytes?.byteLength === 24 ? new Uint8Array(bytes) : undefined;
	}

	const provider = freeze({
		snapshot,
		rows,
		sync,
		invalidate : revoke,
		select,
		setText,
		beginEdit,
		addBatch(operation, key = selected?.key) {
		    if (!batchEditable() || batchItems.length >= 32 || typeof key !== 'string')
			    return false;
		    const endpoint = description.endpoints.find(row => row.key === key);
		    if (!batchAddOperations(endpoint).includes(operation))
			    return false;
		    const token = epoch;
		    publishing = true;
		    try {
			    const item =
			        {endpoint, operation, texts : new Map(), status : 'Draft', hasResponse : false};
			    const id = batchType(item), cost = model.preflight(id);
			    let nodes = cost.nodes, bytes = model.types[id].wireBytes;
			    for (const previous of batchItems) {
				    const previousId = batchType(previous);
				    nodes += model.preflight(previousId).nodes;
				    bytes += model.types[previousId].wireBytes;
			    }
			    if (nodes > limits.nodes || bytes > limits.wireBytes)
				    fail('Batch arguments exceed presentation limits');
			    // This is a new complete DTO. FieldWrite is full replacement,
			    // never a preserving edit of an earlier FieldRead payload.
			    item.request =
			        operation === 'read' ? undefined : copyValue(model.types, id, undefined, true);
			    if (operation !== 'read')
				    validate(id, item.request);
			    if (token !== epoch || !isCurrent())
				    return false;
			    item.key = epoch + ':' + (++nextBatchKey);
			    batchItems.push(item);
			    batchSelected = item.key;
			    ++batchRevision;
			    ++revision;
			    batchStatus = 'Draft - explicit Execute submits one ordered Batch';
			    return true;
		    } catch (failure) {
			    batchStatus = String(failure.message || failure);
			    return false;
		    } finally {
			    publishing = false;
			    notify();
		    }
		},
		removeBatch(key) {
		    if (!batchEditable() || !batchItems.some(item => item.key === key))
			    return false;
		    batchItems = batchItems.filter(item => item.key !== key);
		    if (batchSelected === key)
			    batchSelected = undefined;
		    ++batchRevision;
		    ++revision;
		    notify();
		    return true;
		},
		moveBatch(key, delta) {
		    if (!batchEditable() || (delta !== -1 && delta !== 1))
			    return false;
		    const index = batchItems.findIndex(item => item.key === key), next = index + delta;
		    if (index < 0 || next < 0 || next >= batchItems.length)
			    return false;
		    [batchItems[index], batchItems[next]] = [ batchItems[next], batchItems[index] ];
		    ++batchRevision;
		    ++revision;
		    notify();
		    return true;
		},
		selectBatch(key) {
		    if (!batchEditable() || !batchItems.some(item => item.key === key))
			    return false;
		    batchSelected = key;
		    ++revision;
		    notify();
		    return true;
		},
		clearBatch() {
		    if (!batchEditable())
			    return false;
		    batchItems = [];
		    batchSelected = undefined;
		    ++batchRevision;
		    ++revision;
		    notify();
		    return true;
		},
		setBatchText(path, input, expectedRevision) {
		    const item = selectedBatch();
		    return !!item && editText(path, input, expectedRevision, item);
		},
		setBatchCapability(input) {
		    if (!batchEditable())
			    return false;
		    const token = epoch;
		    publishing = true;
		    try {
			    const copy = copyCapability(input);
			    if (!copy || token !== epoch || !isCurrent())
				    return false;
			    batchCapability = copy;
			    return true;
		    } catch {
			    return false;
		    } finally {
			    publishing = false;
		    }
		},
		executeBatch,
		addWatch(key = selected?.key) {
		    if (!watchEditable() || watched.length >= 32 || typeof key !== 'string')
			    return false;
		    const endpoint = description.endpoints.find(row => row.key === key);
		    if (!endpoint || !watchScalar(endpoint) ||
		        watched.some(row => row.endpoint.key === key))
			    return false;
		    watched.push({endpoint, status : 'NotRead', text : ''});
		    ++watchRevision;
		    notify();
		    return true;
		},
		removeWatch(key) {
		    if (!watchEditable() || !watched.some(row => row.endpoint.key === key))
			    return false;
		    watched = watched.filter(row => row.endpoint.key !== key);
		    ++watchRevision;
		    notify();
		    return true;
		},
		clearWatch() {
		    if (!watchEditable())
			    return false;
		    watched = [];
		    ++watchRevision;
		    notify();
		    return true;
		},
		setWatchIndividual(individual) {
		    if (!watchEditable() || typeof individual !== 'boolean')
			    return false;
		    watchIndividual = individual;
		    ++watchRevision;
		    notify();
		    return true;
		},
		setWatchInterval(milliseconds) {
		    if (!watchEditable() || !Number.isFinite(milliseconds) || milliseconds < 100 ||
		        milliseconds > 10000)
			    return false;
		    watchInterval = milliseconds;
		    notify();
		    return true;
		},
		setReadManyCapability(input) {
		    if (!watchEditable())
			    return false;
		    const token = epoch;
		    publishing = true;
		    try {
			    const copy = copyCapability(input);
			    if (!copy || token !== epoch || !isCurrent())
				    return false;
			    watchCapability = copy;
			    return true;
		    } catch {
			    return false;
		    } finally {
			    publishing = false;
		    }
		},
		refreshWatch,
		startWatch(nowMs) {
		    if (!Number.isFinite(nowMs) || nowMs < 0 || !watchEditable() || !watched.length)
			    return false;
		    watchDue = nowMs + watchInterval;
		    watchRunning = true;
		    watchCycle = true;
		    watchCursor = 0;
		    if (launchWatch())
			    return true;
		    watchRunning = false;
		    notify();
		    return false;
		},
		stopWatch() {
		    if (publishing || notifying)
			    return false;
		    watchRunning = watchCycle = false;
		    ++watchRevision;
		    watchStatus = watchPending ? 'Stopping - draining the current read.' : 'Stopped';
		    notify();
		    return true;
		},
		tickWatch(nowMs) {
		    if (!Number.isFinite(nowMs) || nowMs < 0 || publishing || notifying || !watchRunning)
			    return false;
		    if (!isCurrent()) {
			    retireWatch();
			    notify();
			    return false;
		    }
		    if (nowMs < watchDue)
			    return false;
		    watchDue = nowMs + watchInterval;
		    if (active || watchPending || watchCycle)
			    return false; // Skip missed ticks; never accumulate requests.
		    watchCycle = true;
		    watchCursor = 0;
		    return launchWatch();
		},
		subscribe(listener) {
		    if (typeof listener !== 'function')
			    fail('A state listener is required');
		    listeners.add(listener);
		    return () => listeners.delete(listener);
		},
		setWritesEnabled(enabled) {
		    if (typeof enabled !== 'boolean' || publishing || notifying ||
		        (enabled && !isCurrent()))
			    return false;
		    writesEnabled = enabled;
		    notify();
		    return true;
		},
		cancelEdit() {
		    if (notifying || !snapshot().actions.cancelEdit)
			    return false;
		    draft = undefined;
		    draftPayload = undefined;
		    hasDraft = dirty = false;
		    dataStale = draftStale = false;
		    dataInvalidationReason = '';
		    texts.clear();
		    error = '';
		    ++revision;
		    notify();
		    return true;
		},
		refresh : () => start('read'),
		apply : () => start('write'),
		execute : () => start('command'),
		call : () => start('service')
	});
	hooks.attach?.({
		invalidateData,
		connectionChanged() {
		    const token = epoch;
		    revoke();
		    if (token === epoch)
			    notify(); // A later nonready connection phase still needs presentation delivery.
		},
		reportError(message) {
		    error = message;
		    notify();
		}
	});
	sync();
	return provider;
}

export function createStaticProvider(options)
{
	return presentation(staticAdapter(options), options.limits);
}

export function createDynamicProvider(options)
{
	return presentation(dynamicAdapter(options), options.limits);
}

// Presentation freshness only. The borrowed Session remains the sole execution owner.
const sessionProviders = new WeakMap();
const ordinaryMethods = [
	'rows', 'subscribe', 'invalidate', 'select', 'setWritesEnabled', 'setText', 'refresh',
	'beginEdit', 'cancelEdit', 'apply', 'execute', 'call'
];
const optionalMethods = [
	'addWatch', 'removeWatch', 'clearWatch', 'setWatchIndividual', 'setWatchInterval',
	'setReadManyCapability', 'refreshWatch', 'startWatch', 'stopWatch', 'tickWatch', 'addBatch',
	'removeBatch', 'moveBatch', 'selectBatch', 'clearBatch', 'setBatchText', 'setBatchCapability',
	'executeBatch'
];

function endpointSelector(input)
{
	if (input === undefined)
		return undefined;
	if (!input || typeof input !== 'object')
		fail('An endpoint selector is required');
	const category = input.category;
	if ([ 1, 2, 3 ].includes(category)) {
		const id = input.id;
		if (!Number.isInteger(id) || id < 0 || id > 0xffffffff ||
		    Object.keys(input).some(key => !['category', 'id'].includes(key)))
			fail('Invalid numeric endpoint selector');
		return freeze({category, id});
	}
	const number = category === 'field'     ? 1
	               : category === 'command' ? 2
	               : category === 'service' ? 3
	                                        : 0;
	const catalog = input.catalog, name = input.name;
	if (!number || typeof catalog !== 'string' || typeof name !== 'string' ||
	    Object.keys(input).some(key => !['category', 'catalog', 'name'].includes(key)))
		fail('Invalid named endpoint selector');
	return freeze({category : number, catalog, name});
}

function selectedEndpoint(selector, endpoints)
{
	const matches = endpoints.filter(
	    row => row.category === selector.category &&
	           ('id' in selector ? row.id === selector.id
	                             : row.catalog === selector.catalog && row.name === selector.name));
	if (matches.length !== 1)
		fail(matches.length ? 'Endpoint selector is ambiguous' : 'Endpoint selector was not found');
	return matches[0].key;
}

function connectionSnapshot(session)
{
	return freeze({
		mode : session.mode,
		state : session.state,
		ready : session.ready,
		session : session.session,
		fingerprint : session.fingerprint,
		recoveryRequired : session.recoveryRequired,
		recoveryGeneration : session.recoveryGeneration,
		lastError : session.lastError
	});
}

/** Borrow a Session; this provider owns presentation state and no device lifecycle. */
export function createSessionProvider({session, limits, endpoint} = {})
{
	if (!session || !['static', 'dynamic'].includes(session.mode) ||
	    [ 'subscribe', 'read', 'writePreserving', 'command', 'service' ].some(
	        name => typeof session[name] !== 'function'))
		fail('An existing ConnectionSession with preserving Field editing is required');
	const selector = endpointSelector(endpoint);
	let disposed = false, queued = false, controls, unsubscribe;
	let savedKey, savedFingerprint, selectorPending = selector !== undefined;
	const base =
	    session.mode === 'static'
	        ? staticAdapter({client : session, metadata : session.metadata, sdk : session.sdk})
	        : {
		          mode : 'dynamic',
		          describe : dynamicDescription,
		          validate(id, value, schema) {
		              encodeValue(schema, id, value);
		          }
	          };
	const adapter = {
		mode : session.mode,
		identity : () => ({
			ready : !disposed && session.ready === true,
			session : session.session,
			schema : session.mode === 'static' ? session.metadata : session.schema
		}),
		describe : base.describe,
		validate : base.validate,
		capabilities : () => session.capabilities,
		submit(row, operation, value, originalPayload) {
		    const target = session.mode === 'static' ? row.binding : row.id;
		    return operation === 'read'    ? session.read(target)
		           : operation === 'write' ? session.writePreserving(target, value, originalPayload)
		                                   : session[operation](target, value);
		},
		result : result => (
		    {operation : result.rawResult, valueStatus : result.valueStatus, value : result.value}),
		readMany(capability, endpoints) {
		    if (typeof session.readMany !== 'function')
			    fail('Unsupported: ReadMany is unavailable in this Session');
		    return session.readMany(
		        capability, endpoints.map(row => session.mode === 'static' ? row.binding : row.id));
		},
		manyResult : result => ({operation : result.rawResult, items : result.items}),
		batch(capability, items) {
		    if (typeof session.batch !== 'function')
			    fail('Unsupported: Batch is unavailable in this Session');
		    const requests = items.map(item => {
			    if (session.mode === 'dynamic')
				    return {
					    operation : {
						    read : 'FieldRead',
						    write : 'FieldWrite',
						    command : 'Command',
						    service : 'Service'
					    }[item.operation],
					    id : item.endpoint.id,
					    value : item.request
				    };
			    const binding = item.endpoint.binding;
			    return item.operation === 'read'      ? Batch.read(binding)
			           : item.operation === 'write'   ? Batch.write(binding, item.request)
			           : item.operation === 'command' ? Batch.command(binding, item.request)
			                                          : Batch.service(binding, item.request);
		    });
		    return session.batch(capability, requests);
		},
		batchResult : result =>
		    ({operation : result.rawResult, valueStatus : result.valueStatus, items : result.items})
	};
	let members = sessionProviders.get(session);
	if (!members) {
		members = new Set();
		sessionProviders.set(session, members);
	}
	const member = reason => {
		if (!disposed)
			controls.invalidateData(reason);
	};

	function invalidatePeers(reason, includeSelf)
	{
		for (const peer of [...members])
			if ((includeSelf || peer !== member) && members.has(peer)) {
				try {
					peer(reason);
				} catch { /* Presentation observers cannot change an accepted operation's ownership. */
				}
			}
	}

	const provider = presentation(adapter, limits, {
		attach(value) {
		    controls = value;
		},
		snapshot(state, data) {
		    return freeze({...state, ...data, connection : connectionSnapshot(session), disposed});
		},
		acceptedMutation() {
		    invalidatePeers('Data changed after a mutation in another view.', false);
		}
	});

	function rememberSelection()
	{
		const state = provider.snapshot();
		if (state.current && state.selected) {
			savedKey = state.selected.key;
			savedFingerprint = session.fingerprint;
		}
	}

	function restoreSelection(initial = false)
	{
		const state = provider.snapshot();
		if (!state.current || state.selected)
			return;
		if (selectorPending) {
			try {
				const key = selectedEndpoint(selector, state.endpoints);
				if (provider.select(key))
					selectorPending = false;
			} catch (cause) {
				selectorPending = false;
				if (initial)
					throw cause;
				controls.reportError(String(cause.message || cause));
			}
		} else if (savedKey !== undefined && savedFingerprint === session.fingerprint) {
			provider.select(savedKey);
		}
		rememberSelection();
	}

	function reconcile()
	{
		if (disposed)
			return false;
		if (!session.ready) {
			provider.invalidate();
			return false;
		}
		const current = provider.sync();
		if (current)
			restoreSelection();
		return current;
	}

	function lifecycle()
	{
		if (disposed)
			return;
		if (!session.ready) {
			controls.connectionChanged();
			return;
		}
		if (queued)
			return;
		queued = true;
		queueMicrotask(() => {
			queued = false;
			if (!disposed)
				reconcile();
		});
	}

	// Resolve a ready selector before registering any long-lived observers.
	restoreSelection(true);
	members.add(member);
	const stopRemembering = provider.subscribe(rememberSelection);
	try {
		unsubscribe = session.subscribe(lifecycle);
	} catch (cause) {
		disposed = true;
		members.delete(member);
		stopRemembering();
		provider.invalidate();
		throw cause;
	}
	const publicProvider = Object.fromEntries(
	    [...ordinaryMethods, ...optionalMethods ].map(name => [name, provider[name]]));
	return freeze({
		...publicProvider,
		snapshot : provider.snapshot,
		sync : reconcile,
		dispose() {
		    if (disposed)
			    return;
		    disposed = true;
		    members.delete(member);
		    stopRemembering();
		    try {
			    unsubscribe();
		    } finally {
			    controls.connectionChanged();
		    }
		},
		invalidateData(
		    reason =
		        'Data changed outside this presentation. Cancel a stale draft or restart it.') {
		    if (disposed)
			    return;
		    if (typeof reason !== 'string')
			    fail('A data invalidation reason must be a string');
		    invalidatePeers(reason, true);
		}
	});
}
