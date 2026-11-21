/* The web Configurator over descriptors v3.1 in a real browser (the plan's
 * slice 4, 7b): the SDK's own parseDescriptor and the UX's real dynamic
 * provider and renderer, fed the SDK's golden descriptors. A descriptor with
 * advisory attributes must give the same endpoints and the same value rows as
 * the same model without them; a group tree's groups read as their paths;
 * named types show by their names. The client is a stand-in that answers a
 * Field read with the type's zero value, so no device is needed. MIT. */
import {decodeValue, parseDescriptor} from 'telemetry-sdk/runtime';
import {createDynamicProvider, mountConfigurator} from 'telemetry-ux/web';

const fixture = async name => {
	const text = await (await fetch('/fixtures/' + name)).text();
	return new Uint8Array(text.trim().split(/\s+/).map(pair => parseInt(pair, 16)));
};

// A DynamicClient stand-in: ready with the model, binding any endpoint, and
// answering a Field read at once with the type's canonical zero bytes.
function standIn(model)
{
	const fields = new Map(model.fields.map(field => [field.id, field]));
	return {
		ready : true,
		session : 7,
		schema : model,
		bind : (method, id) => ({status : 0, endpoint : {method, id}}),
		readField(endpoint) {
			const field = fields.get(endpoint.id);
			const bytes = new Uint8Array(model.types[field.typeId].wireBytes);
			const reply = {
				operation : {succeeded : true, payload : bytes, dispatch : 0, endpointStatus : 0},
				valueStatus : 'Ok',
				value : decodeValue(model, field.typeId, bytes)
			};
			const request = Promise.resolve(reply);
			request.requestId = 1;
			return request;
		}
	};
}

const settle = () => new Promise(resolve => setTimeout(resolve, 50));

async function look(name)
{
	const model = parseDescriptor(await fixture(name));
	const provider = createDynamicProvider({client : standIn(model)});
	const host = document.createElement('section');
	host.dataset.fixture = name;
	document.body.appendChild(host);
	mountConfigurator(host, {provider});
	await settle();
	const state = provider.snapshot();
	const endpoints = state.endpoints.map(row => [row.category, row.catalog, row.name].join(' / '));
	const headings = [...host.querySelectorAll('.tux-endpoint-group h3')].map(node => node.textContent);
	// A struct Field with a named member type (else the first struct), read:
	// its value rows' kinds and types.
	const typeOf = row => model.types[model.fields.find(field => field.id === row.id).typeId];
	const structs = state.endpoints.filter(row => row.category === 1 && typeOf(row).kind === 3);
	const struct = structs.find(row => typeOf(row).members.some(member => model.types[member.typeId].name)) || structs[0];
	let rows = [];
	if (struct) {
		provider.select(struct.key);
		await settle();
		provider.refresh();
		await settle();
		rows = provider.rows([], {source : 'value'}).rows.map(row => [row.name, row.kind, row.type, row.typeName].join(' | '));
	}
	const shown = [...host.querySelectorAll('[data-row]')].map(node => node.textContent.replace(/\s+/g, ' ').trim());
	// An enum Field, read: its one row by the enum's name.
	const enumeration = state.endpoints.find(row => row.category === 1 && typeOf(row).kind === 2);
	let enumRows = [];
	if (enumeration) {
		provider.select(enumeration.key);
		await settle();
		provider.refresh();
		await settle();
		enumRows = provider.rows([], {source : 'value'}).rows.map(row => [row.name, row.kind, row.type, row.typeName].join(' | '));
	}
	return {name, minor : model.minor, groupTree : model.groupTree, fingerprint : String(model.fingerprint),
	        endpoints, headings, struct : struct ? struct.name : '', rows, shown, enumRows};
}

try {
	window.__result = {ok : true, looks : [await look('named.hex'), await look('attrs.hex'), await look('tree.hex')]};
} catch (failure) {
	window.__result = {ok : false, error : String(failure && failure.stack || failure)};
}
