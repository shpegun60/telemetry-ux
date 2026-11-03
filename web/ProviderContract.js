// Shared renderer/element admission only; no client or value logic. MIT.
export function assertPresentationProvider(provider)
{
	if (!provider || typeof provider.snapshot !== 'function' ||
	    typeof provider.subscribe !== 'function' || typeof provider.rows !== 'function')
		throw new TypeError('Telemetry Configurator requires an explicit provider');
	return provider;
}
