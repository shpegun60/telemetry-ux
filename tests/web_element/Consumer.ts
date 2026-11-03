/* A host supplies its provider; the element owns presentation only. MIT. */
import {
	defineConfiguratorElement,
	createStaticProvider,
	createDynamicProvider,
	mountFieldEditor, mountCommandPanel, mountServicePanel, mountWatchlist,
	type TelemetryConfiguratorElement,
	type PresentationProvider
} from 'telemetry-ux/web';

declare const staticProvider: ReturnType<typeof createStaticProvider>;
declare const dynamicProvider: ReturnType<typeof createDynamicProvider>;
const Constructor = defineConfiguratorElement();
const element = document.createElement('telemetry-configurator');
element.provider = staticProvider;
element.provider = dynamicProvider;
element.provider = null;
const typed: TelemetryConfiguratorElement = new Constructor();
const provider: PresentationProvider|null = typed.provider;
const custom = new (defineConfiguratorElement('application-settings'))();
custom.provider = staticProvider;
const shadow = new (defineConfiguratorElement('shadow-settings', {shadow : true}))();
shadow.provider = dynamicProvider;
for (const mount of [mountFieldEditor, mountCommandPanel, mountServicePanel])
	mount(document.createElement('section'), {provider : staticProvider}).unmount();
mountWatchlist(document.createElement('section'), {provider : dynamicProvider, now : () => 1000}).unmount();
document.body.append(custom);
const selected = document.querySelector('telemetry-configurator');
if (selected)
	selected.provider = dynamicProvider;

// @ts-expect-error providers are explicit objects, never a URL/transport
element.provider = '/device';
// @ts-expect-error missing presentation provider methods
element.provider = {};
// @ts-expect-error only null explicitly clears the provider
element.provider = undefined;
// @ts-expect-error registration takes a string tag name
defineConfiguratorElement(17);
// @ts-expect-error shadow mode is a boolean option
defineConfiguratorElement('bad-shadow', {shadow : 'open'});
// @ts-expect-error this presentation element has no client close operation
element.close();
void provider;
