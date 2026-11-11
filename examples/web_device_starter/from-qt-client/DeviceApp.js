// Application-owned connection; the reusable Configurator remains in UX. MIT.
import {connectDynamic, demoWebSocket} from 'telemetry-sdk';
import {mountConfigurator} from 'telemetry-ux/web';

const element = id => document.getElementById(id);
let session, view, unsubscribe, attempt, pendingTask, retirement;
let working = false, disposed = false, epoch = 0;

function render()
{
    const exists = session && session.state !== 'closed';
    element('connect').disabled = working || exists;
    element('disconnect').disabled = disposed || Boolean(retirement) || !(attempt || exists);
    element('reconnect').disabled = working || !exists || session.ready;
    element('close').disabled = working || !exists;
    element('status').textContent = working ? 'Working…' : session?.state ?? 'Not connected';
}

function run(action)
{
    if (working || disposed) return;
    const revision = ++epoch;
    const controller = new AbortController();
    attempt = controller;
    working = true;
    element('error').textContent = '';
    render();
    const task = (async () => {
        try {
            await action(controller.signal, revision);
        } catch (error) {
            if (!disposed && epoch === revision) {
                const hint = error.code === 'BootstrapFailed'
                    ? 'Could not connect. Check that the emulator is running and no other client is connected. '
                    : '';
                element('error').textContent = hint + `${error.code ?? error.name}: ${error.message}`;
            }
        } finally {
            if (attempt === controller) attempt = undefined;
            if (epoch === revision) working = false;
            if (!disposed) render();
        }
    })();
    pendingTask = task;
    return task;
}

element('connect').onclick = () => run(async (signal, revision) => {
    // A new explicit connection gets its own bootstrap owner. reconnect() keeps
    // the previous peer/session evidence through the existing Session instead.
    const transport = demoWebSocket(location.origin, {
        enableReadMany: true, enableBatch: true,
        maxFrameBytes: 8192, maxBufferedBytes: 8192
    });
    view?.unmount();
    unsubscribe?.();
    const candidate = await connectDynamic({transport, signal});
    if (disposed || epoch !== revision || signal.aborted) {
        await candidate.close();
        return;
    }
    session = candidate;
    unsubscribe = session.subscribe(render);
    try {
        view = mountConfigurator(element('device'), {session});
        element('empty').hidden = true;
    } catch (error) {
        await session.close();
        throw error;
    }
});
function retire(close)
{
    if (retirement || disposed) return retirement;
    ++epoch;
    attempt?.abort();
    const opening = pendingTask;
    working = true;
    // Defer one microtask so render() can already see the retirement owner.
    retirement = Promise.resolve().then(async () => {
        await opening;
        if (close) await session?.close();
        else await session?.disconnect();
    }).catch(error => {
        element('error').textContent = `${error.code ?? error.name}: ${error.message}`;
    }).finally(() => {
        retirement = undefined;
        working = false;
        if (!disposed) render();
    });
    render();
    return retirement;
}
element('disconnect').onclick = () => retire(false);
element('reconnect').onclick = () => run(signal => session.reconnect({signal}));
element('close').onclick = () => retire(true);

export async function dispose()
{
    disposed = true;
    ++epoch;
    attempt?.abort();
    view?.unmount();
    unsubscribe?.();
    await pendingTask;
    await retirement;
    await session?.close();
}

render();
