// Shared JSON contract and immutable, typed identity lookup in the installed npm package. MIT.
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
const {compilePresentation, presentationDetail} = await import(
    new URL('Presentation.js', import.meta.resolve('telemetry-ux/web')));
const fixture = JSON.parse(await readFile(new URL('./cases.json', import.meta.url), 'utf8'));
assert.equal(fixture.format, 'telemetry-presentation-parser-cases');
let checks = 0;
for (const item of fixture.cases) {
	if (item.valid) assert.doesNotThrow(() => compilePresentation(item.input), item.name);
	else assert.throws(() => compilePresentation(item.input), TypeError, item.name);
	++checks;
}
const input = {
	format: 'telemetry-presentation', version: 1, fingerprint: '0x0123456789abcdef', entries: [
		{category: 1, id: 42, role: 'value', path: ['x.y', 0, '__proto__'], label: 'Voltage',
		 unit: 'V', help: '<img src=x>', minimum: '-9223372036854775808', maximum: '18446744073709551615', step: '0.1'},
		{category: 3, id: 42, role: 'response', path: ['0'], label: 'Text member'},
		{category: 3, id: 42, role: 'response', path: [0], label: 'Array index'}
	]
};
const lookup = compilePresentation(input), identity = 0x0123456789abcdefn;
const hints = lookup(identity, 1, 42, 'value', ['x.y', 0, '__proto__']);
assert.equal(hints.maximum, '18446744073709551615'); ++checks;
assert.equal(hints.minimum, '-9223372036854775808'); ++checks;
assert(Object.isFrozen(hints)); ++checks;
input.entries[0].label = 'Changed'; input.entries[0].path[0] = 'Wrong'; input.entries.length = 0;
assert.equal(lookup(identity, 1, 42, 'value', ['x.y', 0, '__proto__']).label, 'Voltage'); ++checks;
for (const args of [
	[identity + 1n, 1, 42, 'value', ['x.y', 0, '__proto__']],
	[undefined, 1, 42, 'value', ['x.y', 0, '__proto__']],
	[Number(identity), 1, 42, 'value', ['x.y', 0, '__proto__']],
	[identity, 2, 42, 'request', ['x.y', 0, '__proto__']],
	[identity, 3, 42, 'request', ['0']],
	[identity, 1, 42, 'value', ['x', 'y', 0, '__proto__']]
]) { assert.deepEqual(lookup(...args), {}); ++checks; }
assert.equal(lookup(identity, 3, 42, 'response', ['0']).label, 'Text member'); ++checks;
assert.equal(lookup(identity, 3, 42, 'response', [0]).label, 'Array index'); ++checks;
assert(presentationDetail(hints, 'Original / U64').includes('Advisory only:')); ++checks;
assert(presentationDetail(hints).includes('max 18446744073709551615')); ++checks;
assert.equal(presentationDetail(), ''); ++checks;
assert.deepEqual(compilePresentation()(identity, 1, 42, 'value', []), {}); ++checks;
console.log(JSON.stringify({passed: true, checks, cases: fixture.cases.length}));
