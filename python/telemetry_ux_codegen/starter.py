"""Emit a thin static-provider browser page over the shared UX view (MIT).

The host retains its DeviceClient, admission, transport, and connection lifecycle.
No schema-specific form or protocol implementation is generated here.
"""
from tools.telemetry_codegen.emitter_api import GeneratedTarget


PAGE = '''// Generated Telemetry UX starter. SPDX-License-Identifier: MIT.
import * as sdk from '../js/device.js';
import {structuralMetadata} from '../js/device.metadata.js';
import {createStaticProvider, mountConfigurator} from 'telemetry-ux/web';

export function mountDevice(host, {client}) {
  const provider = createStaticProvider({client, metadata: structuralMetadata, sdk});
  const view = mountConfigurator(host, {provider});
  return Object.freeze({provider, unmount: view.unmount});
}
'''

DECLARATION = '''// Generated Telemetry UX starter. SPDX-License-Identifier: MIT.
import type {DeviceClient} from 'telemetry-sdk/client';
import type {PresentationProvider} from 'telemetry-ux/web';

export declare function mountDevice(host: HTMLElement, options: {
  readonly client: DeviceClient;
}): Readonly<{
  provider: PresentationProvider;
  unmount(): void;
}>;
'''

README = '''# Generated device page

This page uses the generated JavaScript SDK and structural metadata through the
shared `telemetry-ux/web` presentation. Install the matching `telemetry-sdk` and
`telemetry-ux` packages (both development version 0.1.0).

```js
import {mountDevice} from './ui/DevicePage.js';
// The host creates and admits its DeviceClient with its chosen transport.
const page = mountDevice(document.querySelector('#device'), {client});
// Revoke displayed values before the host resets or closes that client:
page.provider.invalidate();
// After the host admits a new session:
page.provider.sync();
// When removing the page:
page.unmount();
```

The host must provide `client`, resolve the package imports (for example with its
bundler or import map), and may load `telemetry-ux/configurator.css`. This module
creates no client or connection and performs no automatic reads. Use the shared
view to select an endpoint, read values, enable writes, edit requests, and inspect
confirmed or uncertain results. `page.provider` exposes that same presentation
API for host customization; edit host code rather than generated files.
Call `page.provider.invalidate()` before resetting or closing the client, then
`page.provider.sync()` after a new admission. The client does not emit UI state
events automatically; the host forwards these lifecycle changes to the provider.

`unmount()` unsubscribes and removes this view. It does not disconnect the client,
cancel an accepted operation, or destroy the provider. The host owns those
lifetimes. The static provider uses the generated schema; it does not discover or
parse a Descriptor from an unknown device.
'''


def generate_web_starter(schema):
    """Return only the entry page; shared SDK/UX implementations stay imported."""
    return GeneratedTarget(
        {"ui/DevicePage.js": PAGE, "ui/DevicePage.d.ts": DECLARATION,
         "ui/README.md": README},
        {"namespace": schema.namespace, "mount": "mountDevice",
         "provider": "static", "lifecycle": "host-owned"})
