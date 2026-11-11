# Device files

The Device files models and panel against an installed SDK and an installed UX,
over localhost TCP to the SDK's native server. No board.

- **Models** (`main.cpp`, 71 checks):
  - `DeviceFilesModel` builds the tree from the device's own paths, folders with
    their file count and size, sizes and access in words, three-state marks and
    the selection a download takes.
  - `DeviceFileTransfers` writes files byte for byte through `QSaveFile`, shows
    progress while a 300 KB file is read, and leaves nothing under the real name
    when cancelled.
  - A transfers model destroyed in the middle of a read hands its reader to the
    session, which stays usable.
  - A device without a descriptor still lists and downloads its files.
  - `localPathFor` keeps hostile device paths inside the chosen folder: `..`,
    backslashes, a drive, a UNC prefix, reserved Windows names, forbidden and
    control characters, trailing dots and spaces, an empty path.
  - A device listing 4100 files is cut at 4096, in its order, and says so. This
    takes about 70 s here (one request costs about 17 ms; see the SDK session's
    timer), so the check allows two minutes.
- **Panel** (`panel.cpp` with `Panel.qml`, off screen, 47 checks):
  - the not-connected words, the tree once connected, the column headings;
  - the Download button and the marked line following the marks;
  - `downloadRequested` carrying the selection;
  - a transfer row ending with `Saved`, and its Show in folder giving the host
    the local file;
  - a download that cannot be written: `! Could not write …` with Retry, which
    saves it in the same row once the obstacle is gone; Clear finished;
  - the current row highlighted, and Enter folding and unfolding a folder;
  - not one QML warning (only the offscreen platform's font notice is set aside).

```sh
python3 -B tests/device_files/run.py --build-dir /external/device-files \
  --sdk-prefix /path/to/sdk --ux-prefix /path/to/ux --qt-prefix /path/to/qt
```

The SDK must provide the files API: telemetry-sdk F-1 (Resource requests on the
client queue, the files client and the files-only connection).
