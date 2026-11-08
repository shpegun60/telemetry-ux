/* Optional, schema-bound display hints. No value conversion or I/O. MIT. */
const empty = Object.freeze({});
const fields = ['label', 'unit', 'help', 'minimum', 'maximum', 'step'];
const decimal = /^-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?![\s\S])/;
const fail = () => { throw new TypeError('Invalid Telemetry presentation overlay'); };
const key = (category, id, role, path) => JSON.stringify([category, id, role, path]);
const record = value => value !== null && typeof value === 'object' && !Array.isArray(value);
function keys(value, allowed) {
	if (!record(value) || Object.keys(value).some(name => !allowed.includes(name))) fail();
}

// Parse once and copy every retained primitive. Caller mutation cannot change
// a mounted view. Unknown schema identities remain dormant, including reconnect.
export function compilePresentation(input) {
	if (input === undefined) return () => empty;
	keys(input, ['format', 'version', 'fingerprint', 'entries']);
	if (input.format !== 'telemetry-presentation' || input.version !== 1 ||
	    typeof input.fingerprint !== 'string' || !/^0x[0-9a-f]{16}(?![\s\S])/.test(input.fingerprint) ||
	    !Array.isArray(input.entries) || input.entries.length > 4096) fail();
	const fingerprint = input.fingerprint;
	const entries = new Map();
	for (const entry of input.entries) {
		keys(entry, ['category', 'id', 'role', 'path', ...fields]);
		if (![1, 2, 3].includes(entry.category) || !Number.isInteger(entry.id) ||
		    entry.id < 0 || entry.id > 0xffffffff || !Array.isArray(entry.path) ||
		    entry.path.length > 64 || Array.from(entry.path).some(part =>
		        typeof part !== 'string' && !(Number.isInteger(part) && part >= 0 && part <= 0xffffffff)) ||
		    !(entry.category === 1 ? entry.role === 'value' : entry.category === 2
		        ? entry.role === 'request' : ['request', 'response'].includes(entry.role))) fail();
		const hints = {};
		for (const field of fields) if (Object.hasOwn(entry, field)) {
			const value = entry[field];
			const limit = field === 'help' ? 4096 : field === 'label' ? 256 : field === 'unit' ? 64 : 128;
			if (typeof value !== 'string' || value.length > limit) fail();
			if (['minimum', 'maximum', 'step'].includes(field) && !decimal.test(value)) fail();
			if (field === 'step' && (value.startsWith('-') || !/[1-9]/.test(value))) fail();
			hints[field] = value;
		}
		const selector = key(entry.category, entry.id, entry.role, entry.path);
		if (!Object.keys(hints).length || entries.has(selector)) fail();
		entries.set(selector, Object.freeze(hints));
	}
	return (identity, category, id, role, path) => {
		const actual = typeof identity === 'bigint' && identity >= 0n && identity <= 0xffffffffffffffffn
		    ? '0x' + identity.toString(16).padStart(16, '0') : identity;
		return actual === fingerprint ? entries.get(key(category, id, role, path)) ?? empty : empty;
	};
}

export function presentationDetail(hints, original = '') {
	if (!hints) return original;
	const lines = original ? [original] : [];
	if (hints.help) lines.push(hints.help);
	const range = [];
	if (hints.minimum !== undefined) range.push('min ' + hints.minimum);
	if (hints.maximum !== undefined) range.push('max ' + hints.maximum);
	if (hints.step !== undefined) range.push('step ' + hints.step);
	if (range.length) lines.push('Advisory only: ' + range.join(' / ') + '. Device validation still applies.');
	return lines.join('\n');
}
