// Exercise generated wiring without reimplementing the shared view. MIT.
import assert from 'node:assert/strict';
import {mountDevice} from './ui/DevicePage.js';
import * as sdk from './js/device.js';
import {structuralMetadata} from './js/device.metadata.js';
import {calls, expectedProvider, unmount} from 'telemetry-ux/web';

const host = Object.freeze({host : true});
const client = Object.freeze({client : true});
assert.equal(calls.length, 0);
const page = mountDevice(host, {client});
assert.equal(calls.length, 2);
assert.deepEqual(calls[0],
                 {kind : 'provider', options : {client, metadata : structuralMetadata, sdk}});
assert.deepEqual(calls[1], {kind : 'mount', host, options : {provider : expectedProvider}});
assert.ok(Object.isFrozen(page));
assert.deepEqual(Object.keys(page), [ 'provider', 'unmount' ]);
assert.equal(page.provider, expectedProvider);
assert.equal(page.unmount, unmount);
page.unmount();
assert.deepEqual(calls[2], {kind : 'unmount'});
assert.equal(calls.length, 3);
console.log('Generated web starter wiring: 10 checks passed');
