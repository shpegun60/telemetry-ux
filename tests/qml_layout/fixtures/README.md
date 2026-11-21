# Descriptor v3.1 fixtures

Copies of the SDK's independent golden descriptors, from `telemetry-sdk`
`tests/structured/descriptor/` at `3cd9ce8` (written there by `golden.py`, not
by the producer), so the UX checks read the same bytes every decoder of the SDK
is held to:

| File | Bytes | What it is |
|---|---:|---|
| `named.hex` | 1027 | the SDK's mixed model with scoped type names: `fixture::Mode`, `fixture::State`, `fixture::MotorConfig` |
| `attrs.hex` | 1106 | the same model with advisory and application attributes on every record kind |
| `tree.hex` | 1130 | a group tree: the `adc` (two channel subgroups, `Config`, `Status`, `Reset`, `Calibrate`) and `metrology` branches |

`DeviceTreeNamed.cpp` checks that `attrs.hex` gives the Qt models the same tree
as `named.hex` (attributes are skipped, never shown), and builds its other
group trees from C++ with the SDK's own producer. Refresh a copy only when
the SDK's golden changes.
