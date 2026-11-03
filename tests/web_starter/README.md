# Thin generated web starter

```sh
python tests/web_starter/run.py --sdk-root ../telemetry --build-dir /tmp/web-starter --node node
```

The output directory must be fresh and outside both repositories. The test loads
real immutable public SDK IR from the checked-in mixed/empty Descriptor fixtures,
checks the three output paths and deterministic schema receipt, and executes the
generated entry page with instrumented public package exports. It proves exact
client/metadata/SDK/provider ownership wiring and reuse of the view's unmount
function. It also verifies that importing the optional hook does not import QML.

This focused test does not simulate a DOM, transport, or form. The installed
web-configurator browser gate imports the generated page and qualifies the real
view against the actual SDK client/native peer.
