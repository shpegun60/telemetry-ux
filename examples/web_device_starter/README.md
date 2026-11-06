# Your own Model, one Configurator

This is a small runnable application you can change for your own project. Start
with [native/Device.hpp](native/Device.hpp): it defines Config, a read-only exact
U64 serial number, Reset and Scale. The same browser discovers those endpoints
from Descriptor v3; no handwritten form or Qt class is needed for each endpoint.

The application calls only:

```js
import {connectDynamic, demoWebSocket} from 'telemetry-sdk';
import {mountConfigurator} from 'telemetry-ux/web';

const device = await connectDynamic({transport: demoWebSocket(location.origin)});
const view = mountConfigurator(document.querySelector('#device'), {session: device});
// Explicit owner actions; disconnect/reconnect never replay a mutation.
await device.disconnect();
await device.reconnect();
view.unmount();
await device.close();
```

`demoWebSocket` is used because this starter's server implements that exact
reference contract. It is not a generic profile for an arbitrary HTTP server.

## Prepare and run

You need C++20, CMake 3.21+, Ninja, Node/npm and Python 3.10+. Put their executables
on PATH, or use the explicit `--cxx`, `--cmake`, `--ninja`, `--node` and
`--npm-cli /path/to/npm/bin/npm-cli.js` options. Use matching SDK/UX package files
from the same accepted source pair; the development `0.1.0` label alone does not
identify that pair.

If you are building from the two checkouts, create the package files first:

```sh
mkdir -p /packages
npm pack /path/to/telemetry-sdk --ignore-scripts --pack-destination /packages
npm pack /path/to/telemetry-ux --ignore-scripts --pack-destination /packages
```

First install an SDK prefix, once, from the matching SDK checkout:

```sh
cmake -S /path/to/telemetry-sdk -B /external/sdk-build -G Ninja \
  -DCMAKE_INSTALL_PREFIX=/external/sdk-prefix \
  -DTELEMETRY_COMPONENTS="core;encoded_server;batch_server"
cmake --build /external/sdk-build --parallel 2
cmake --install /external/sdk-build
```

From the UX checkout, prepare the application in a fresh external directory:

```sh
python -B examples/web_device_starter/prepare.py --output /external/my-device \
  --sdk-prefix /external/sdk-prefix \
  --sdk-package /packages/telemetry-sdk-0.1.0.tgz \
  --ux-package /packages/telemetry-ux-0.1.0.tgz
python -m venv /external/host-env
/external/host-env/bin/python -m pip install websockets==15.0.1
/external/host-env/bin/python -B examples/web_device_starter/serve.py \
  --prepared /external/my-device --port 8080
```

On Windows use normal absolute Windows paths and `host-env\Scripts\python.exe`.
Create `/packages` with `New-Item -ItemType Directory` instead of `mkdir -p`.
Preparation resolves platform executable suffixes itself and reports an actionable
error for a missing tool. It builds only against the installed CMake package,
installs both npm tarballs offline with scripts disabled, copies this browser
page, and records exact package/executable hashes in `prepared.json`. A nonempty
output directory is refused; no previous files are deleted.

Open `http://127.0.0.1:8080`, press **Connect**, select Config and press **Read**.
Enable changes, choose **Edit**, set `periodMs` to 1 and press **Apply + readback**.
The device clamps it to 10, which appears only after the separate readback. Reset
and Scale use the same Session/FIFO. Changes require user opt-in.

Dynamic is the default and needs no generator. Static is disabled until its files
have been prepared. To enable the Static option, add
`--codegen-python /path/to/python-with-telemetry-codegen-core` to the preparation
command. The matching installed `telemetry-codegen-core` generates exact DTO/Codec
and structural metadata with preserving-write support. Static validates the
observed header against that generated SDK and never falls back to Dynamic.
Keep your application code outside the generator-owned output directory.

## What to change for your product

1. Replace DTOs and endpoint tables in `native/Device.hpp`; rebuild into a new output.
2. Change title/styles and host connection controls in `index.html` / `DeviceApp.js`.
3. Keep `mountConfigurator({session})`, or mount independent Field/Command/Service
   panels over that same Session. Each panel owns selection/draft, not a client.
4. For firmware, adapt the documented server contract to your HTTP/WS stack or
   supply your own SDK `ConnectionProfile`. Port the native dispatch call, not the
   Python process pipe. No STM32 network implementation is supplied here.

## Exact server and lifecycle contract

The thin [serve.py](serve.py) launcher reuses the maintained
[reference HTTP/WS server](../web_demo/serve_ws.py). There is one native process,
one exclusive lease and no added execution queue or Telemetry wire format.

- `GET /demo/transport` returns the separate WS URL and `telemetry.encoded.v1`
  subprotocol. Browser Origin must match the HTTP origin.
- The socket greets with `{ "session": nonzero_u32 }` only after the server has
  exclusively admitted that monotonically increasing session at its native peer.
- `GET /descriptor.bin` and `/schema-header.bin` provide Descriptor v3 and its
  first 64 bytes. These are application bootstrap routes, not a new Resource API.
- `/capabilities/read-many.bin` and `/capabilities/batch.bin` return the native
  session/fingerprint-bound records. Discovery is not admission; optional backend
  features require separate explicit SDK opt-in. The ordinary starter UI uses
  only FieldRead/FieldWrite/Command/Service.
- Every subsequent socket message is one complete bounded binary TEQ1 frame
  (32..65536 bytes). The native installed dispatcher validates it before getters
  or mutations. TCP fragmentation is handled by the WS library, not Telemetry.
- A new lease from the same still-running peer proves supersession only after
  previous native work has actually returned. Socket closure alone is not proof.
  A restarted server whose session counter goes backwards is refused; start a
  fresh Session/profile only after the application's recovery requirements hold.

Disconnect invalidates values while retaining operation outcomes. Reconnect is
explicit and performs no endpoint replay or implicit read. An uncertain mutation
remains `OutcomeUnknown`; check actual device state before deciding whether a new
operation is appropriate. No rollback, exactly-once execution or automatic
authentication/authorization is promised.

The reference server listens only on localhost. This starter is a host application
integration, not a network-deployment or physical STM32 qualification. A production
HTTP server must establish its own authenticated owner/access policy and preserve
the same trusted admission and physical retirement rules.

[Real browser acceptance test](../../tests/web_device_starter/README.md)
