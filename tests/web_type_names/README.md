# The web Configurator over descriptors v3.1

A check in headless Chromium that the browser Configurator reads what the SDK
calls descriptor v3.1 the way the Qt models do (the named-types plan, slice 4
of section 7b):

- a descriptor with advisory attributes (`attrs.hex`) gives the same endpoints,
  group headings and value rows, on the page and from the provider, as the same
  model without them (`named.hex`);
- named types show by their names, `fixture::Mode` for an enum Field and
  `metrology::Window` for a member;
- a group tree (`tree.hex`) shows its groups as their paths, `adc/channel1`.

It uses the SDK's own `parseDescriptor` and this repository's real dynamic
provider and renderer; the client is a stand-in that answers a Field read with
the type's zero value, so no device or SDK package is needed. The descriptors
are the SDK's goldens copied in [../qml_layout/fixtures](../qml_layout/fixtures).

```sh
python -B tests/web_type_names/run.py --build-dir /external/web-type-names \
  --browser-python /path/to/playwright-env/bin/python \
  --sdk-source /path/to/telemetry-sdk
```

`--browser-python` is a Python with Playwright 1.63.0 and its Chromium;
`--sdk-source` the SDK checkout the UX builds against (its `web/` and
`lib/client/web/` are served as they are). The page's screenshot and
`summary.json` are written to the build directory.
