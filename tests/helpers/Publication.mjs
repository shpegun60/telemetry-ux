// Test-only IPC/publication fences. These watchdogs never advance the SDK clock. MIT.
export function publication()
{
	const listeners = new Set();
	return {
		subscribe(listener) {
		    listeners.add(listener);
		    return () => listeners.delete(listener);
		},
		publish() {
		    for (const listener of [...listeners])
			    if (listeners.has(listener))
				    listener();
		}
	};
}

function diagnosticError(label, reason, diagnostics, cause)
{
	let state;
	try {
		state = JSON.stringify(diagnostics());
	} catch (error) {
		state = 'diagnostics unavailable: ' + error;
	}
	return new Error(label + ': ' + reason + ' ' + state, cause ? {cause} : undefined);
}

export function waitForPublication({
	label,
	ready,
	subscribe,
	failure = () => undefined,
	diagnostics = () => ({}),
	timeoutMs = 30000,
	timers = globalThis
})
{
	return new Promise((resolve, reject) => {
		let settled = false, queued = false, unsubscribe;
		const finish = error => {
			if (settled)
				return;
			settled = true;
			timers.clearTimeout(timer);
			unsubscribe?.();
			error ? reject(error) : resolve();
		};
		const timer = timers.setTimeout(
		    () => finish(diagnosticError(label, 'real watchdog expired', diagnostics)), timeoutMs);
		const changed = () => {
			if (settled || queued)
				return;
			queued = true;
			// A provider may publish between completed and next sequential requests.
			// Observe the state only after that publication's entire stack returns.
			queueMicrotask(() => {
				queued = false;
				if (settled)
					return;
				try {
					const error = failure();
					if (error)
						throw error;
					if (ready())
						finish();
				} catch (cause) {
					finish(diagnosticError(label, String(cause), diagnostics, cause));
				}
			});
		};
		try {
			unsubscribe = subscribe(changed);
			if (typeof unsubscribe !== 'function')
				throw new TypeError('Publication subscription must return a disposer');
			if (settled)
				unsubscribe();
			else
				changed(); // Also observe an already published boundary.
		} catch (cause) {
			finish(diagnosticError(label, 'subscription failed', diagnostics, cause));
		}
	});
}

export function watchdog(work, label,
                         {timeoutMs = 30000, diagnostics = () => ({}), timers = globalThis} = {})
{
	return new Promise((resolve, reject) => {
		const timer = timers.setTimeout(
		    () => reject(diagnosticError(label, 'real watchdog expired', diagnostics)), timeoutMs);
		Promise.resolve(work).then(
		    value => {
			    timers.clearTimeout(timer);
			    resolve(value);
		    },
		    error => {
			    timers.clearTimeout(timer);
			    reject(diagnosticError(label, String(error), diagnostics, error));
		    });
	});
}

// Keep a primary assertion/watchdog failure when forced shutdown also fails.
// Abort only the caller's own native subprocess; never touch the real device.
export async function finishDriver({
	primaryFailure,
	stopHolding,
	dispose,
	close,
	closeInput,
	exit,
	abort,
	label,
	diagnostics = () => ({}),
	timeoutMs = 30000,
	timers = globalThis
})
{
	let aborted = false;
	const abortOnce = () => {
		if (!aborted) {
			aborted = true;
			abort();
		}
	};
	try {
		stopHolding();
		dispose();
		if (primaryFailure)
			abortOnce();
		await watchdog(Promise.resolve().then(close), label + ' session drain',
		               {timeoutMs, diagnostics, timers});
		closeInput();
		const code = await watchdog(exit, label + ' native exit', {timeoutMs, diagnostics, timers});
		if (code !== 0)
			throw diagnosticError(label, 'native exit ' + code, diagnostics);
	} catch (error) {
		abortOnce();
		if (!primaryFailure)
			throw error;
		return error;
	}
}
