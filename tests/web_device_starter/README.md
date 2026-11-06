# Installed own-device starter journey

Run the exact shipped preparation command from the [starter README](../../examples/web_device_starter/README.md),
then use a Python environment with `websockets==15.0.1`, Playwright and its Chromium:

```sh
python -B tests/web_device_starter/run.py --prepared /external/my-device \
  --output /external/my-device-browser-check
```

The output must be new. This test starts the existing reference HTTP/WebSocket
server with the starter's own compiled Model and uses the shipped browser page.
It checks discovery, exact U64, real Read → Edit → Write → normalized readback,
Command, Service, lost acknowledgement, uncertainty, explicit reconnect, no replay,
stale-value suppression and terminal close. Static is exercised as well when the
prepared application contains generated files. It records browser diagnostics,
source/executable hashes and screenshots in `summary.json` and sibling files.

This is host HTTP/WebSocket qualification. It does not prove STM32 HTTP, Ethernet,
UART, DMA, access control or firmware Resource discovery behavior.
