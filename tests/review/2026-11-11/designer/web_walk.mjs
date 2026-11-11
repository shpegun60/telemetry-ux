// Designer review: drives the emulator's browser page over the Chrome DevTools
// Protocol (Node 22+ global WebSocket/fetch) and saves screenshots.
// Usage: node web_walk.mjs <debug-port> <page-url> <out-dir>
import {writeFileSync} from 'node:fs';
import {join} from 'node:path';

const [debugPort, pageUrl, out] = process.argv.slice(2);
const targets = await (await fetch(`http://127.0.0.1:${debugPort}/json/list`)).json();
const page = targets.find(t => t.type === 'page');
const socket = new WebSocket(page.webSocketDebuggerUrl);
await new Promise(resolve => socket.addEventListener('open', resolve, {once: true}));
let next = 0;
const pending = new Map();
socket.addEventListener('message', event => {
    const message = JSON.parse(event.data);
    if (message.id && pending.has(message.id)) {
        pending.get(message.id)(message);
        pending.delete(message.id);
    }
});
const send = (method, params = {}) => new Promise(resolve => {
    const id = ++next;
    pending.set(id, resolve);
    socket.send(JSON.stringify({id, method, params}));
});
const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
const evaluate = async expression => (await send('Runtime.evaluate', {expression, awaitPromise: true, returnByValue: true})).result?.result?.value;
const shot = async (name, full = false) => {
    const metrics = full ? await send('Page.getLayoutMetrics') : null;
    const clip = full ? {x: 0, y: 0, width: 1600, height: Math.ceil(metrics.result.cssContentSize.height), scale: 1} : undefined;
    const result = await send('Page.captureScreenshot', {format: 'png', captureBeyondViewport: full, clip});
    writeFileSync(join(out, name + '.png'), Buffer.from(result.result.data, 'base64'));
    console.log('SHOT', name, full ? `full height ${clip.height}` : 'viewport');
};
const click = async (selector, text) => evaluate(`(() => {
    const all = [...document.querySelectorAll(${JSON.stringify(selector)})];
    const hit = all.find(e => ${text ? `e.textContent.trim() === ${JSON.stringify(text)}` : 'true'});
    if (!hit) return 'missing';
    hit.click(); return 'clicked ' + (hit.disabled ? '(disabled) ' : '') + hit.textContent.trim().slice(0, 40);
})()`);
const type = async (selector, value) => evaluate(`(() => {
    const e = document.querySelector(${JSON.stringify(selector)}); if (!e) return 'missing';
    e.focus(); e.value = ${JSON.stringify(value)}; e.dispatchEvent(new Event('input', {bubbles: true})); return 'typed';
})()`);
const text = async selector => evaluate(`[...document.querySelectorAll(${JSON.stringify(selector)})].map(e => e.textContent.trim().replace(/\\s+/g, ' ')).join(' | ')`);

await send('Page.enable');
await send('Emulation.setDeviceMetricsOverride', {width: 1600, height: 900, deviceScaleFactor: 1, mobile: false});
await send('Page.navigate', {url: pageUrl});
await wait(2500);
await shot('w01-web-start-1600x900');
console.log('CONNECT', await click('#connect'));
await wait(3000);
console.log('STATUS', await text('#status'), '| MODE', await text('[data-role=mode]'), '| ERR', await text('#error'));
await shot('w02-web-connected-1600x900');
await shot('w02b-web-connected-full', true);
console.log('SEARCH', await type('[data-role=endpoint-search]', 'Volt'));
await wait(600);
console.log('RESULTS', await text('.tux-endpoint'));
await shot('w03-web-search');
console.log('SELECT', await click('.tux-endpoint', 'Voltage'));
await wait(400);
console.log('READ', await click('button[data-action=refresh]'));
await wait(800);
console.log('ADD', await click('button[data-action=addWatch]'));
await wait(300);
await type('[data-role=endpoint-search]', 'Current');
await wait(500);
await click('.tux-endpoint', 'Current');
await wait(300);
await click('button[data-action=addWatch]');
console.log('START', await click('button[data-action=startWatch]'));
await wait(2500);
console.log('WATCH', await text('.tux-watchlist tbody tr'));
await shot('w04-web-watch-polling');
await shot('w04b-web-watch-polling-full', true);
await click('button[data-action=stopWatch]');
await send('Emulation.setDeviceMetricsOverride', {width: 390, height: 844, deviceScaleFactor: 1, mobile: true});
await wait(800);
await shot('w05-web-phone-390');
socket.close();
