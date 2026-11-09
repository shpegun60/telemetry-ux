# Windows portable Inspector qualification

This optional distribution workflow is separate from the normal
[Qt Creator source project](../../examples/inspector/README.md). Building from source
does not require a ZIP or deployment step.

The release-pair workflow builds the recorded SDK commit and exact UX candidate
with Qt 6.10.1 / MinGW 13.1, including Qt SerialPort. `build.py` requires clean
tracked inputs and a fresh external output directory. It builds and installs the
selected SDK components, compiles the actual Inspector, runs `windeployqt`,
retains notices, and creates a ZIP, source identity and SHA-256 file manifest.
It never flashes a device or opens a serial port.

A second Windows runner downloads that ZIP. This consumer job installs no Qt,
Python package or compiler. `VerifyPortable.ps1` validates the archive/payload
hashes and exact source pair, extracts into a path containing spaces, then runs
with a Windows/System32-only `PATH` and no external Qt/QML import paths:

- CLI help;
- an actual QML FieldRead;
- actual QML editing, one Write and a distinct normalized readback;
- an actual packaged loopback TCP server and QML client.

Every process has a bounded lifetime. Any nonzero exit or stderr diagnostic fails
qualification. Logs, screenshots and `qualification.json` are retained. The
consumer restores the caller's environment and stops only its own loopback
server. The job proves a relocated Windows distribution in the runner profile;
it does not qualify a physical UART, Ethernet firmware or every Windows version.

For a local producer, provide Python 3.12+, CMake/Ninja and one matching MinGW Qt
kit with its SerialPort module installed. Run `python build.py --help` for the
explicit SDK/UX revisions, source paths and tools. `--no-serial` builds a
Demo/TCP-only package; the CI distribution deliberately exercises serial-enabled
packaging. Existing output directories and source modifications are refused.

To independently check a produced package in PowerShell:

```powershell
./packaging/windows/VerifyPortable.ps1 `
  -Archive C:/external/Telemetry-Inspector-Windows-x64.zip `
  -OutputDirectory C:/external/new-qualification `
  -SdkRevision <full-sdk-commit> -UxRevision <full-ux-commit>
```

Keep the sibling `.zip.sha256` file beside the archive. Publish artifacts only
after both the producer and **Windows portable Inspector (clean runner)** pass.
