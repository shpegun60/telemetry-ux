/* Import the installed package without any browser globals. MIT. */
import assert from 'node:assert/strict';

assert.equal(typeof globalThis.HTMLElement, 'undefined');
assert.equal(typeof globalThis.customElements, 'undefined');
assert.equal(typeof globalThis.document, 'undefined');
const api = await import('telemetry-ux/web');
assert.equal(typeof api.defineConfiguratorElement, 'function');
assert.equal(typeof api.mountConfigurator, 'function');
assert.equal(typeof api.createStaticProvider, 'function');
assert.equal(typeof api.createDynamicProvider, 'function');
assert.throws(() => api.defineConfiguratorElement(), TypeError);
assert.equal(typeof globalThis.HTMLElement, 'undefined');
assert.equal(typeof globalThis.customElements, 'undefined');
assert.equal(typeof globalThis.document, 'undefined');
console.log(JSON.stringify({passed : true, checks : 11}));
