// Deterministic negative controls for test-driver fences, not production scheduling. MIT.
import assert from 'node:assert/strict';
import {publication, waitForPublication, watchdog, finishDriver} from './Publication.mjs';

let checks = 0, scenarios = 0;
const eq = (actual, expected) => {
	++checks;
	assert.deepEqual(actual, expected);
};
const yes = value => {
	++checks;
	assert(value);
};
const flush = async () => {
	for (let i = 0; i < 8; ++i)
		await Promise.resolve();
};

function fixture()
{
	const events = publication(), callbacks = new Map();
	let serial = 0, listeners = 0, removals = 0;
	const state = {ready : false, failure : undefined};
	const timers = {
		setTimeout(callback, milliseconds) {
		    eq(milliseconds, 30000);
		    callbacks.set(++serial, callback);
		    return serial;
		},
		clearTimeout(id) {
		    callbacks.delete(id);
		}
	};
	const options = {
		label : 'controlled test phase',
		ready : () => state.ready,
		failure : () => state.failure,
		diagnostics : () => ({phase : 'controlled', ready : state.ready}),
		timers,
		subscribe(changed) {
		    ++listeners;
		    const stop = events.subscribe(changed);
		    return () => {
			    --listeners;
			    ++removals;
			    stop();
		    };
		}
	};
	return {
		state,
		options,
		publish : events.publish,
		get listeners() {
		    return listeners;
		},
		get removals() {
		    return removals;
		},
		get timers() {
		    return callbacks.size;
		},
		expire() {
		    eq(callbacks.size, 1);
		    const [id, callback] = callbacks.entries().next().value;
		    callbacks.delete(id);
		    callback();
		}
	};
}

// Delayed real event turns are not a transport deadline. The event is authoritative.
{
	++scenarios;
	const f = fixture();
	let completed = false;
	const waiting = waitForPublication(f.options).then(() => completed = true);
	for (let i = 0; i < 1300; ++i) {
		await new Promise(resolve => setImmediate(resolve));
		if (i === 1100)
			eq(completed, false);
	}
	eq(f.listeners, 1);
	f.state.ready = true;
	f.publish();
	await waiting;
	eq(completed, true);
	eq(f.listeners, 0);
	eq(f.timers, 0);
	eq(f.removals, 1);
	f.publish();
	await flush();
	eq(f.removals, 1);
}

// Synchronous subscription delivery cannot use an uninitialized disposer.
{
	++scenarios;
	const f = fixture(), subscribe = f.options.subscribe;
	f.state.ready = true;
	await waitForPublication({
		...f.options,
		subscribe(changed) {
		    const stop = subscribe(changed);
		    changed();
		    return stop;
		}
	});
	eq(f.listeners, 0);
	eq(f.timers, 0);
	eq(f.removals, 1);
}

// A transient idle publication followed by another request in the same stack is not idle.
{
	++scenarios;
	const f = fixture();
	let completed = false;
	const waiting = waitForPublication(f.options).then(() => completed = true);
	f.state.ready = true;
	f.publish();
	f.state.ready = false;
	await flush();
	eq(completed, false);
	eq(f.listeners, 1);
	f.state.ready = true;
	f.publish();
	await waiting;
	eq(f.listeners, 0);
	eq(f.timers, 0);
}

for (const message of ['Native peer exited 17', 'Native peer spawn error']) {
	++scenarios;
	const f = fixture(), error = new Error(message);
	const waiting = waitForPublication(f.options);
	f.state.failure = error;
	f.publish();
	++checks;
	await assert.rejects(waiting, failure => {
		eq(failure.cause, error);
		yes(failure.message.includes('controlled test phase'));
		yes(failure.message.includes(message));
		return true;
	});
	eq(f.listeners, 0);
	eq(f.timers, 0);
	eq(f.removals, 1);
}

// A peer already dead cannot become success through an initially idle snapshot.
{
	++scenarios;
	const f = fixture();
	f.state.ready = true;
	f.state.failure = new Error('Already exited');
	++checks;
	await assert.rejects(waitForPublication(f.options), /Already exited/);
	eq(f.listeners, 0);
	eq(f.timers, 0);
}

// Watchdog expiry is independent of SDK time and always releases the observer.
{
	++scenarios;
	const f = fixture(), waiting = waitForPublication(f.options);
	f.expire();
	++checks;
	await assert.rejects(waiting, /controlled test phase: real watchdog expired.*controlled/);
	eq(f.listeners, 0);
	eq(f.timers, 0);
	eq(f.removals, 1);
	f.state.ready = true;
	f.publish();
	await flush();
	eq(f.removals, 1);
}

// Failed predicate/subscription and a synchronous publisher failure clean up too.
{
	++scenarios;
	const f = fixture();
	++checks;
	await assert.rejects(waitForPublication({
		                     ...f.options,
		                     ready() {
		                         throw new Error('bad snapshot');
		                     }
	                     }),
	                     /bad snapshot/);
	eq(f.listeners, 0);
	eq(f.timers, 0);
	++checks;
	await assert.rejects(waitForPublication({
		                     ...f.options,
		                     subscribe() {
		                         throw new Error('subscribe error');
		                     }
	                     }),
	                     /subscription failed/);
	eq(f.timers, 0);
}

for (const rejectWork of [false, true]) {
	++scenarios;
	const f = fixture(), error = new Error('work failed');
	const work = rejectWork ? Promise.reject(error) : Promise.resolve(23);
	const waiting = watchdog(work, 'owned work', f.options);
	if (rejectWork) {
		++checks;
		await assert.rejects(waiting, failure => failure.cause === error);
	} else
		eq(await waiting, 23);
	eq(f.timers, 0);
}

// If the transport itself stalls, shutdown remains bounded and preserves the first failure.
{
	++scenarios;
	const f = fixture(), primary = new Error('original held boundary failure');
	let aborted = 0, disposed = 0;
	const cleanup = finishDriver({
		primaryFailure : primary,
		stopHolding() {},
		dispose() {
		    ++disposed;
		},
		close : () => new Promise(() => {
		                          }),
		closeInput() {},
		exit : Promise.resolve(1),
		abort() {
		    ++aborted;
		},
		label : 'owned driver',
		timers : f.options.timers
	});
	await flush();
	f.expire();
	const secondary = await cleanup;
	yes(secondary.message.includes('session drain: real watchdog expired'));
	eq(primary.message, 'original held boundary failure');
	eq(aborted, 1);
	eq(disposed, 1);
	eq(f.timers, 0);
}

// Finally cleanup may report a secondary native exit, but must not mask the assertion.
{
	++scenarios;
	const f = fixture(), primary = new Error('primary assertion');
	let aborted = 0;
	const operation = async () => {
		try {
			throw primary;
		} finally {
			await finishDriver({
				primaryFailure : primary,
				stopHolding() {},
				dispose() {},
				close() {},
				closeInput() {},
				exit : Promise.resolve(17),
				abort() {
				    ++aborted;
				},
				label : 'owned driver',
				timers : f.options.timers
			});
		}
	};
	++checks;
	await assert.rejects(operation(), failure => failure === primary);
	eq(aborted, 1);
	eq(f.timers, 0);
}

// A cleanup failure without a primary assertion is itself a test failure.
{
	++scenarios;
	const f = fixture();
	let aborted = 0;
	++checks;
	await assert.rejects(finishDriver({
		                     stopHolding() {},
		                     dispose() {},
		                     close() {},
		                     closeInput() {},
		                     exit : Promise.resolve(17),
		                     abort() {
		                         ++aborted;
		                     },
		                     label : 'owned driver',
		                     timers : f.options.timers
	                     }),
	                     /native exit 17/);
	eq(aborted, 1);
	eq(f.timers, 0);
}

console.log(JSON.stringify({
	passed : true,
	hardware : false,
	checks,
	scenarios,
	delayed_event_turns : 1300,
	watchdog_ms : 30000
}));
