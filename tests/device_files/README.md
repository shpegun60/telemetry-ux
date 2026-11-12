# Device files

The Device files models and panel against an installed SDK and an installed UX,
over localhost TCP to the SDK's native server. No board.

- **Models** (`main.cpp`, 674 checks):
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
  - A device listing 4100 files is cut at 4096, in its order, and says so. With
    the SDK session's event pump this takes under a second (0.6 s here); a
    session that only pumps on its 5 ms timer takes about 40 s.
  - Open in the viewer (`DeviceFileTransfers::openIn` into `OpenFiles`): the tab
    is there before the first byte, reading, and then shows the file; a copy
    that cannot be written leaves the reason in the tab; a cancelled read ends
    its reading.
  - `FileContent` and `OpenFiles`: a CSV with quoted fields as a table with
    numeric columns, text, binary as hex rows, an open quote that is only text,
    a CSV named by the device only, a missing file; the same copy opened twice is
    one row.
  - The Descriptor and Values trees are lazy: a struct's members and an array's
    elements are made when their row is first opened. A long array opens as
    Eclipse shows one: ranges of 256, `[0 … 255]`, `[256 … 511]`, …, ranges of
    ranges past 65 536, so any element is a few levels down and only the rows on
    the way are made. Find makes the rows that lead to its matches, wherever
    they are; Copy writes what is not opened from the parsed
    descriptor and the shown snapshot, and under Find only what is shown. Both
    stop at a bound and say so: Find as "N+ matches", Copy with a last line;
    Find's rows stay within 100 000; through ranges a match deep in a long
    array costs a few hundred.
    Live marks a Field whose bytes changed, folded or not, and counts it on its
    group's row.
  - Measured (Release, this machine): a descriptor whose 20 endpoints hold
    54 612 member rows each opens in under 1 ms, finds in about 20 ms and copies
    the first 500 000 rows in about 200 ms. A values file of 10 MB (160 Fields of
    16 000 floats, each token under a READ's 65 535 bytes) opens in about 7 ms,
    takes a newer snapshot in about 8 ms, finds in about 100 ms (making about
    2 000 rows through the ranges) and copies in about 230 ms. Find with a filter already on once took 59 s on the descriptor:
    a filtering proxy looks through every made row below a parent for each row
    added there, so the contents clear the filter before they make rows.
  - JSON as a tree: the file's own key order (read from the text; QJsonObject
    would sort it), members made when opened and 256 at a time, Find by name,
    path or value into rows not made, a value copied exactly as the file
    writes it, a broken file's line and column, a byte order mark, a name
    without an extension guessed from its first bytes (a guess that fails, such
    as a log starting `[INFO]`, stays text and says nothing), escapes in keys,
    surrogate pairs and `\/`. A JSON file of 10.5 MB (160 000 small objects)
    opens in about 135 ms and finds a value at its very end in about 290 ms -
    three ranges of 65 536, then one of 256, on the way. An object of many
    keys opens as ranges too, named by their first and last key; a range copied
    as JSON is its members in brackets, as the file writes them.
  - The read-many and batch capability records (TRM1, TBM1), written by the
    SDK's own `writeCapability`: every field in words, the descriptor they
    bind to (the connected device's, or another in the warning colour), their
    regions, and a version this program does not read left as hex.
- The checks wait on the event loop as a program does, never with a sleep: on
  Windows a 1 ms sleep costs a whole timer quantum (about 15.6 ms) per pass, and
  made the list above measure the loop instead of the session.
- **Panel** (`panel.cpp` with `Panel.qml`, off screen, 172 checks). It calls
  `telemetry::ux::registerQmlTypes()` first, as a host does. It checks the hex grid
  (`HexView`) on its own:
  - **Layout:** 32, 16 or 8 bytes a row by width, the text column dropped first.
  - **Keys:** arrows, Shift to extend, Home, Ctrl+End, Ctrl+A, Esc; Tab from hex to
    text and then out.
  - **Status and copy:** the status and accessible text; Copy as in every form, with
    the 1 MiB limit and its notice; the clipboard.
  - **Go to** in every form (`0x20`, `40h`, `100`, `+0x10`, `-16`, `end-16`,
    `end`) and its refusals; **Select block** by length and by end.
  - **Find** as hex, text, integer and float, in both byte orders: the match
    selected, next and previous going round once and saying so, the count and the
    match's place, the refusals.
  - **The inspector** at `E8 03`: little-endian and big-endian, signed and
    unsigned, bits, `time_t`, `—` past the end.
  - **Regions:** a codec's region named in the status.
  - **The context menu** requested by the Menu key and Shift+F10.
  - **An empty file.**

  The panel itself:
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
