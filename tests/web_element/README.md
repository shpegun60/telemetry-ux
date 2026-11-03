# Installed Configurator custom-element qualification

The suite packs and installs Telemetry UX alongside an explicit matching SDK npm
archive outside the checkout. It checks strict TypeScript consumers, imports the
installed package in Node without DOM globals, and drives the actual installed
element and renderer in Chromium. No SDK source checkout, native helper or board
is used by this presentation-lifecycle fixture.

```sh
python3 -B tests/web_element/run.py --build-dir /tmp/ux-element \
  --sdk-package /path/to/telemetry-sdk-0.1.0.tgz \
  --browser-python /path/to/playwright-venv/bin/python \
  --node /path/to/node --tsc /path/to/typescript/lib/tsc.js
```

Use Playwright 1.63.0 and its matching Chromium. `--npm-js` selects the matching npm
CLI if Node's installation uses a different layout. Outputs must be fresh and
outside the checkout; nothing is deleted.

The browser fixture implements the public presentation contract only. It proves
explicit/idempotent/foreign registration, predefinition property upgrade,
provider-before-connect and connect-before-provider, identity/null/invalid
assignment, detach/reconnect, application-owned light-DOM children, late retired
buttons, and synchronous subscribe/unsubscribe that remove or replace the
element/provider. Every fixture subscription must be removed once; adapter
lifecycle must invoke no host close/invalidate/cancel or operation. The one real
button click is counted separately. Reentrant presentation is checked after its
coalesced microtask, including an ABA reconnect with the same provider.

These checks do not claim new native SDK operation coverage. The real Static and
Dynamic SDK/native browser journeys remain in [web_configurator](../web_configurator/README.md).
Receipts preserve actual browser/version/counts, source and installed-package
hashes, tool identities and before/after equality. Unexpected browser warnings
and page errors fail the suite.

The same installed-package fixture also mounts the independent Field, Command
and Service panels with one shared provider, checks category mismatches and
retired controls, and verifies shadow-mode named slot assignment, package-local
CSS loading and real `::part` styling in Chromium. Shadow replacement and
reconnection must preserve host-owned slot content without extra operations.
An optional Watchlist fixture checks the standalone view's exact U64 text,
explicit owner clock, pending-state controls, Stop and retired row buttons.
This fixture tests presentation only; actual Static and Dynamic ReadMany
execution belongs to the native provider suite.

The Batch presentation fixture covers ordered mixed items, explicit permission,
single-submit controls, local reordering, exact U64 argument/response text,
retired inputs including blur, `Accepted`/`NotExecuted`/`OutcomeUnknown`, and
removal or session changes. It verifies no DTO creation or device calls on mount;
actual SDK Batch encoding, execution and lifetime remain native-provider tests.
