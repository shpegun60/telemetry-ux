/*
 * Build a structural request editor and inspect exact binary values in the browser.
 *
 * The validated descriptor supplies shapes; UI defaults and form budgets remain
 * local policy. The optional HTTP fake adapter owns cancellation and single-call
 * state while the imported codec remains independent of a transport.
 *
 * Authors: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.
 */
import {
	TypeKind,
	defaultLimits,
	DescriptorCache,
	decodeValue,
	decodeValues,
	encodeServiceRequest,
	decodeServiceResponse
} from 'telemetry-sdk/runtime';

const $ = id => document.getElementById(id);
const cache = new DescriptorCache();
const scalarNames =
    [ '', 'Bool', 'U8', 'S8', 'U16', 'S16', 'U32', 'S32', 'U64', 'S64', 'F32', 'F64' ];
const serviceStatuses = [ 'Ok', 'InvalidArgument', 'Unavailable', 'Busy', 'Failed' ];
const dispatchStatuses = {
	0 : 'Ok',
	4 : 'NotFound',
	5 : 'InvalidPayload',
	6 : 'BufferTooSmall',
	7 : 'WorkspaceTooSmall',
	8 : 'InternalError',
	9 : 'Unavailable'
};
let model, readEditor, pending;
let isFake = false;
let displayRevision = 0;

function message(text, error = false)
{
	$('message').textContent = text;
	$('message').className = error ? 'error' : '';
}

const pretty = value => JSON.stringify(value, (_, item) => {
	if (typeof item === 'bigint')
		return item.toString();
	if (typeof item === 'number' && !Number.isFinite(item))
		return String(item);
	if (Object.is(item, -0))
		return '-0';
	return item;
}, 2) ?? '(Void: no payload)';

// Form nodes borrow the current model and DOM inputs. The recursive budget
// bounds presentation work independently of the codec's structural ceilings.
function editor(typeId, label, value, budget)
{
	if (++budget.nodes > 4096)
		throw new Error('Value is too large for this form; use the payload byte tools.');
	const type = model.types[typeId];
	if (type.kind === TypeKind.Void) {
		const node = document.createElement('p');
		node.textContent = 'No request payload.';
		return {node, read : () => undefined};
	}
	if (type.kind === TypeKind.Struct || type.kind === TypeKind.Array) {
		const node = document.createElement('fieldset');
		const legend = document.createElement('legend');
		legend.textContent = label;
		node.append(legend);
		const children = [];
		const entries =
		    type.kind === TypeKind.Struct
		        ? type.members
		        : Array.from({length : type.elementCount},
		                     (_, i) => ({name : String(i), typeId : type.elementTypeId}));
		for (const entry of entries) {
			const child = editor(entry.typeId, entry.name, value[entry.name], budget);
			children.push([ entry.name, child.read ]);
			node.append(child.node);
		}
		return {
			node,
			read : () => {
			    const result = type.kind === TypeKind.Struct ? Object.create(null) : [];
			    for (const [name, read] of children)
				    result[name] = read();
			    return result;
			}
		};
	}
	const code = type.kind === TypeKind.Enum ? model.types[type.underlyingTypeId].scalarCode
	                                         : type.scalarCode;
	const node = document.createElement('div');
	node.className = 'leaf';
	const title = document.createElement('label');
	const input = document.createElement('input');
	input.id = 'value-' + budget.nodes;
	input.dataset.member = label;
	title.htmlFor = input.id;
	title.textContent = label + ' ';
	const tag = document.createElement('span');
	tag.className = 'type';
	tag.textContent = (type.kind === TypeKind.Enum ? 'Enum / ' : '') + scalarNames[code];
	title.append(tag);
	input.type = code === 1 ? 'checkbox' : 'text';
	if (code === 1)
		input.checked = value;
	else
		input.value = Object.is(value, -0) ? '-0' : String(value);
	node.append(title, input);
	if (type.kind === TypeKind.Enum) {
		const list = document.createElement('datalist');
		list.id = input.id + '-codes';
		for (const entry of type.entries) {
			const option = document.createElement('option');
			option.value = String(entry.code);
			option.label = entry.name;
			list.append(option);
		}
		input.setAttribute('list', list.id);
		const hint = document.createElement('p');
		hint.className = 'enum-hint';
		hint.textContent = type.entries.map(entry => `${entry.name} = ${entry.code}`).join(' · ') +
				           ' · Unknown representable codes are allowed.';
		node.append(list, hint);
	}
	return {
		node,
		read : () => {
		    if (code === 1)
			    return input.checked;
		    const text = input.value.trim();
		    if (text === '')
			    throw new Error(label + ': enter a value.');
		    if (code === 8 || code === 9)
			    return BigInt(text);
		    const number = Number(text);
		    if (Number.isNaN(number) && text !== 'NaN')
			    throw new Error(label + ': invalid number.');
		    return number;
		}
	};
}

function renderService()
{
	++displayRevision;
	pending?.abort();
	$('response').textContent = 'No response yet.';
	$('editor').replaceChildren();
	readEditor = undefined;
	$('download').disabled = $('call').disabled = true;
	const service = model?.services[Number($('service').value)];
	if (!service)
		return;
	try {
		const type = model.types[service.requestTypeId];
		if (type.nodes > 4096)
			throw new Error('Value is too large for this form; use the payload byte tools.');
		// UI zero initialization is local. No descriptor default is inferred.
		const initial = decodeValue(model, type.id, new Uint8Array(type.wireBytes));
		const form = editor(type.id, 'Request', initial, {nodes : 0});
		readEditor = form.read;
		$('editor').append(form.node);
		$('download').disabled = false;
		$('call').disabled = !isFake;
	} catch (error) {
		message(error.message, true);
	}
}

// Publish a replacement model only after descriptor validation succeeds;
// cancel the previous form operation before replacing its service/field views.
function loadModel(bytes, fake)
{
	const candidate = cache.load(bytes);
	pending?.abort();
	model = candidate;
	isFake = fake;
	$('service').replaceChildren();
	for (const catalog of model.serviceCatalogs) {
		for (let i = 0; i < catalog.count; ++i) {
			const index = catalog.first + i;
			const option = document.createElement('option');
			option.value = String(index);
			option.textContent = catalog.name + ' / ' + model.services[index].name;
			$('service').append(option);
		}
	}
	$('service').disabled = model.services.length === 0;
	$('model-info').textContent =
	    `${model.types.length} types · ${model.fields.length} fields · ` +
		`${model.commands.length} commands · ${model.services.length} services · ` +
		`fingerprint ${model.fingerprint.toString(16).padStart(16, '0')}`;
	$('response').textContent = 'No response yet.';
	$('values').textContent = 'No values loaded.';
	renderService();
	message('Descriptor validated.');
}

const selectedService = () => model.services[Number($('service').value)];

// This function is the example HTTP adapter. The imported codec has no fetch,
// packet header, requestId, Bind or connection state. At most one call is live.
async function callFake()
{
	if (pending || !readEditor)
		return;
	const current = model;
	const revision = displayRevision;
	const service = selectedService();
	const controller = new AbortController();
	pending = controller;
	$('call').disabled = true;
	$('response').textContent = 'Waiting for response…';
	const timer = setTimeout(() => controller.abort(), 5000);
	try {
		const payload = encodeServiceRequest(current, service.id, readEditor());
		message('Calling fake Device…');
		const response = await fetch('/service/' + service.id, {
			method : 'POST',
			body : payload,
			signal : controller.signal,
			headers : {'Content-Type' : 'application/octet-stream'},
		});
		if (!response.ok)
			throw new Error(await response.text());
		const bytes = new Uint8Array(await response.arrayBuffer());
		if (displayRevision !== revision)
			return;
		const dispatchText = response.headers.get('X-Dispatch-Status');
		const endpointText = response.headers.get('X-Endpoint-Status');
		if (dispatchText === null || endpointText === null)
			throw new Error('Missing example status headers.');
		const dispatch = Number(dispatchText), endpoint = Number(endpointText);
		if (!/^(0|[4-9])$/.test(dispatchText) || !/^[0-4]$/.test(endpointText))
			throw new Error('Unknown result status.');
		if (dispatch !== 0 || endpoint !== 0) {
			if (bytes.length !== 0)
				throw new Error('Failure must not contain a response payload.');
			throw new Error(dispatch !== 0 ? dispatchStatuses[dispatch]
			                               : serviceStatuses[endpoint]);
		}
		$('response').textContent = pretty(decodeServiceResponse(current, service.id, bytes));
		message('Ok — one application call.');
	} catch (error) {
		if (displayRevision === revision) {
			$('response').textContent = 'No response confirmed.';
			message(error.name === 'AbortError'
			            ? 'Timeout or cancelled: outcome unknown. Request was not repeated.'
			            : error.message,
			        true);
		}
	} finally {
		clearTimeout(timer);
		if (pending === controller)
			pending = undefined;
		$('call').disabled = !readEditor || !isFake;
	}
}

$('service').addEventListener('change', renderService);
$('call').addEventListener('click', callFake);
$('load-device').addEventListener('click', async () => {
	let revision = displayRevision;
	try {
		const response = await fetch('/descriptor.bin');
		if (!response.ok)
			throw new Error('Cannot load fake Device descriptor.');
		const descriptor = await response.arrayBuffer();
		if (displayRevision !== revision)
			return;
		loadModel(descriptor, true);
		revision = displayRevision;
		const values = await fetch('/values.bin');
		if (!values.ok)
			throw new Error('Cannot load fake Device values.');
		const bytes = await values.arrayBuffer();
		if (displayRevision !== revision)
			return;
		$('values').textContent = pretty(decodeValues(model, bytes));
	} catch (error) {
		if (displayRevision === revision)
			message(error.message, true);
	}
});

for (const [id, action] of [[ 'descriptor-file', bytes => loadModel(bytes, false) ],
                            [
	                            'values-file',
	                            bytes => {
	                                $('values').textContent = pretty(decodeValues(model, bytes));
	                                message('Values decoded.');
	                            }
                            ],
                            [
	                            'response-file',
	                            bytes => {
	                                $('response').textContent = pretty(
	                                    decodeServiceResponse(model, selectedService().id, bytes));
	                                message('Response decoded.');
	                            }
                            ],
]) {
	$(id).addEventListener('change', async event => {
		const revision = displayRevision;
		try {
			const file = event.target.files[0];
			if (!file)
				return;
			const maximum =
			    id === 'descriptor-file' ? defaultLimits.descriptorBytes
			    : id === 'response-file'
			        ? model?.types[selectedService().responseTypeId].wireBytes
			        : model &&
			              24 + model.fields.reduce(
			                       (sum, row) => sum + 1 + model.types[row.typeId].wireBytes, 0);
			// The demo displays a whole file. Larger live streams belong to
			// an incremental application adapter, not a giant DOM/ArrayBuffer.
			if (maximum === undefined || file.size > Math.min(maximum, 16 * 1024 * 1024))
				throw new Error(
				    'File exceeds the selected structural size or no descriptor is loaded.');
			const bytes = await file.arrayBuffer();
			if (displayRevision !== revision)
				return;
			action(bytes);
		} catch (error) {
			if (displayRevision === revision)
				message(error.message, true);
		}
	});
}

$('download').addEventListener('click', () => {
	try {
		const service = selectedService();
		const bytes = encodeServiceRequest(model, service.id, readEditor());
		const url = URL.createObjectURL(new Blob([ bytes ], {type : 'application/octet-stream'}));
		const link = document.createElement('a');
		link.href = url;
		link.download = 'request-' + service.id + '.bin';
		link.click();
		setTimeout(() => URL.revokeObjectURL(url), 1000);
		message(`Saved ${bytes.length} payload bytes.`);
	} catch (error) {
		message(error.message, true);
	}
});
