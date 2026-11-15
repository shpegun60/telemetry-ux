# Controlled Batch presentation checks

This installed `TelemetryUX::dynamic_configurator` consumer uses a controlled
submission/completion source. Its 239 checks cover ordered duplicate IDs, owned draft
bytes, exact count/byte budgets, one pending submission, mutation permission,
QueueFull preservation, checked outcomes versus unavailable values, explicit
post-callback publication, previous-session history and reentrant detachment.
It does not claim that the controlled source executes native operations.

The Batch's steps (UR-1) are held here to `tests/helpers/UndoKinds.hpp`: add,
add several, remove, a move made twice within two seconds (one step) and
Clear - done, undone and redone the same, across a disconnect and a source of
another session, rows by uid in their places. Rows put back after a reconnect
come back historical and keep Execute off; Execute is never a step, and a
step waits, its words kept, while a Batch is in flight.

The maintained `tests/qt_connection/run.py` gate builds and runs this target in
addition to the real Session/TCP/Configurator workflow. Both consumers use
installed SDK/UX packages; no SDK source fallback is provided.
