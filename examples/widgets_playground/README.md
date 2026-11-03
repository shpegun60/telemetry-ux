# Qt Widgets playground

Author: Ruslan Kovtun (shpegun60), codexAi. SPDX-License-Identifier: MIT.

This local, simulated Model playground displays native C++ endpoint types and
values. It owns its window, demonstration catalog and Resource facade. It does
not connect to a physical device. For Descriptor-driven device connections, use
the [Universal Inspector](../../apps/inspector/README.md).

Install Telemetry SDK **0.1.0**, including `core`, `resource_adapter` and
`resource_protocol`, then configure with that prefix and a Qt 6.4+ Widgets kit:

```sh
cmake -S examples/widgets_playground -B /tmp/telemetry-widgets \
  -DCMAKE_PREFIX_PATH="/path/to/installed-sdk;/path/to/qt"
cmake --build /tmp/telemetry-widgets
/tmp/telemetry-widgets/telemetry_playground
```

On Windows, use your Qt kit's compiler and generator and run the `.exe` from the
chosen build directory. Open this directory's `CMakeLists.txt` in Qt Creator.
No SDK source checkout, COBS, SerialPort, QML or network package is required.

The internal `widgets_demo_resources` static target owns the application's demo
Model and its Descriptor/Values providers. Its public facade exposes generic
Resource headers; the Model and provider implementation remain private. This
target is an example implementation, not an installed SDK component.

`telemetry_playground --smoke-test` checks displayed exact U64/S64, nested
structures, native/encoded writes and commands, validation, and Resource
Descriptor/Values prefixes before closing the window. The maintained
[installed test](../../tests/widgets_playground/README.md) runs this offscreen
and also checks the generic facade without Qt in its consumer.
