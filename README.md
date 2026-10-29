# QATENtion

A C++17 / Qt 6 Widgets port of [ATENtion](https://github.com/librarian/ATENtion).
Windows x64, Linux x64, and macOS Intel/Apple Silicon cross-builds run on Linux.

## Desktop features

The original application's menu structure is retained: Power, Keys, Mouse, View,
Connection, Logging, Storage and Help. Controls use the platform's default Qt
Widgets style and palette; no custom theme is forced.

- Multiple reorderable console tabs, saved tab restoration, background streaming control.
- Saved profiles, automatic fresh-token reconnect, debug log panel and file.
- Power on/off/reset/ACPI shutdown; keyboard/mouse hotplug.
- Key combinations, custom USB HID combinations, paced clipboard typing.
- Absolute mouse, physical layout-dependent key positions on Windows/Linux,
  Windows lock-key markers, input release on focus loss and tab changes.
- Fullscreen, actual size, fit, smooth scaling, PNG screenshots, quality/chroma,
  full-frame cadence and FPS limits.
- User list, host-power status, session information.
- Read-only ISO mounting, drag-and-drop, slot occupancy and unmounting.
  Media has an independent connection and survives a video reconnect.
- Hermon raw tiles, palette and RLE bit-plane video; ASPEED AJPG.
- Separate screenshot/ISO diagnostic CLI.

See [docs/PARITY.md](docs/PARITY.md) for the reference baseline and remaining
differences. Automated checks use synthetic BMC transcripts; real hardware booting
from a mounted ISO is not established by those checks.

## Build

Install Qt 6.8.3 (Core, Gui, Network, Widgets and Test),
CMake 3.24+, Ninja, and a C/C++ compiler. CI pins Qt to 6.8.3.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/gcc_64
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$PWD/dist"
```

Use the Qt `macos` directory on macOS, or `msvc2022_64` on Windows. On Windows run
from an x64 Visual Studio developer shell with LLVM available, and configure with
`-DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl`. The vendor C decoder
uses GNU-style variadic macros accepted by Clang; it is compiled as C, not C++.

Linux build prerequisites on Ubuntu/Debian:

```sh
sudo apt install build-essential cmake ninja-build libgl1-mesa-dev libegl1-mesa-dev libxkbcommon-dev libfontconfig1-dev
```

Run `build/qatention` (`build/qatention.app/Contents/MacOS/qatention` on macOS),
or the equivalent installed path. `--smoke-test` opens the UI briefly and checks
that TLS and the bundled client certificate can be loaded; it makes no network
connections. CI runs it both before and after packaging.

CI uploads plain ZIP files with the Qt runtime, with no nested archive. Linux
packages target the CI distribution's ABI, not every Linux release. Windows packages are unsigned.
No signing credentials are required for these development builds.
Linux still needs system graphics/input libraries (including libxkbcommon, X11/XCB,
OpenGL/EGL, and fontconfig); the archive bundles Qt, not an entire Linux userspace.

### GitHub build matrix

[Desktop builds](https://github.com/librarian/qatention/actions/workflows/build.yml)
runs on pushes and pull requests. It builds and tests Linux x64 and Windows x64
using Linux runners for both targets: Linux builds natively; Windows cross-compiles
with MinGW and uses Wine for the protocol tests and installed UI/TLS smoke test.
Wine validation does not replace interactive Windows hardware testing.
The Windows package bundles OpenSSL built with MinGW, requiring no MSVC runtime.
macOS cross-builds run in a separate Linux-hosted workflow.
The workflow is also runnable manually from GitHub Actions.

## Connect and saved credentials

Use **Connection > Connect / Change server**. Connecting saves the named profile
and credentials. Saved profiles appear in the dialog; manual tokens are saved too
but may expire. Windows protects passwords with current-user DPAPI, like ATENtion.
Existing plaintext Windows profiles migrate when saved. Linux stores credentials
in an atomically written, owner-readable JSON file.

Profiles are in Qt's application configuration directory:
%LOCALAPPDATA%/qatention/QATENtion/profiles.json on Windows, or
~/.config/qatention/QATENtion/profiles.json on Linux (respecting XDG_CONFIG_HOME).
Windows-protected passwords must be re-entered when moving profiles to another
account or platform.

New connections default to **Allow unverified BMC certificates**, matching
ATENtion. Clear it to require validation for HTTPS, console and virtual media.
Existing profiles retain their explicit choice. These endpoints may serve
different certificates: a valid web certificate does not imply the KVM certificate
is valid. The bundled vendor client identity authenticates the client, not the
server. Put an unencrypted client.pfx beside the executable to override it.

Web login acquires fresh console tokens. Java-interface switching is opt-in.
Automatic reconnect allows 30 attempts: five seconds between the first five,
then thirty seconds. Receiving a console message resets the budget. Authentication
rejection, invalid protocol and TLS validation failures stop retries. Manual-token
connections do not automatically retry. Explicit disconnect unmounts media.

**Logging** enables diagnostics, opens the log panel/file, and clears the log.
Credentials and typed keys are excluded. Files rotate at 10 MiB; the panel holds
64K characters. An unsupported video packet may be captured beside the log and
contains console pixels. Logs live in the application's local data directory.

## Diagnostic CLI

Both archives include bin/qatention-capture (with .exe on Windows).
Passwords are prompted without terminal echo.

    qatention-capture --host bmc.example --user ADMIN --output console.bmp
    qatention-capture --host bmc.example --user ADMIN --mount-iso installer.iso --timeout 600
    qatention-capture --check-native-decoder
    qatention-capture --help

Options include existing --jnlp, --raw-output, --web-port, --http,
--client-pfx, media endpoint overrides, and --verify-cert. ISO capture mode
detaches on timeout or Ctrl+C. Existing screenshot/raw files are not overwritten.

## Cross-compile Windows on Linux

Install the native Linux build dependencies above plus
`g++-mingw-w64-x86-64-posix wine64 wine`. Install matching Qt 6.8.3 Linux host
and Windows MinGW SDKs (for example with `aqtinstall`). Then:

```sh
bash scripts/build_windows_openssl.sh "$PWD/openssl-windows" /tmp/openssl-build
cmake -S . -B build-windows -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw64.cmake \
  -DCMAKE_PREFIX_PATH=/path/to/windows/6.8.3/mingw_64 \
  -DQT_HOST_PATH=/path/to/linux/6.8.3/gcc_64
cmake --build build-windows --parallel 3
cmake --install build-windows --prefix "$PWD/dist"
python scripts/package_windows.py --qt /path/to/windows/6.8.3/mingw_64 \
  --openssl openssl-windows --build build-windows --dest dist
python scripts/archive.py dist qatention-windows-x64.zip
```

The packaging script uses Qt's `windeployqt` under Wine and runs both core tests
and the installed UI/TLS smoke test. CI caches the Windows SDK and OpenSSL build.

## Architecture

- src/core: asynchronous Qt Network sessions, video decoding, profiles and media.
- src/ui: native Qt Widgets menus, tabs, dialogs and console rendering/input.
- src/capture.cpp: command-line capture and media client.
- third_party/aspeed: MPL-2.0 C decoder, linked statically.
- tests: protocol vectors, mock BMC, SCSI/media, decoder, profile and UI checks.

The ASPEED decoder remains C with a C++ ownership wrapper. No .NET runtime or
separate decoder DLL is required. See [THIRD_PARTY.md](THIRD_PARTY.md).

## macOS cross-build previews

The macOS cross-build workflow uses pinned osxcross, LLVM, a checksum-verified
macOS 14.5 SDK snapshot, Qt 6.8.3 and rcodesign. Packages target macOS 12 or newer.
It builds separate arm64 (Apple Silicon) and x86_64 (Intel) ZIPs on Ubuntu.

Extract and open qatention.app. The bundles have ad-hoc code signatures, without
Developer ID signing or notarization. macOS may require Privacy & Security >
Open Anyway after the first launch attempt. The ZIP includes MACOS-TESTING.txt.
The CLI is inside qatention.app/Contents/MacOS/qatention-capture.

CI checks architectures, bundled library dependencies, signature code-page hashes and symlinks;
it cannot run the macOS application. Login, TLS, video, input and media need testing
on a Mac. macOS currently stores profile credentials in owner-readable JSON.

### Investigating incremental video

Choose **Logging → Record video trace for 60 seconds…** and select a folder.
Reconnect both tabs after recording starts, reproduce the stale display, then
switch away and back. Keep the app open until the log reports that recording
has stopped. ZIP the new `qatention-trace-*` directory for analysis.

Each tab has a separate JSONL recording capped at 64 MiB, containing compressed
screen pixels, decoded-image hashes, request types, timestamps, and input counts.
These files contain screen content; use a non-sensitive console screen. They do
not record web authentication or key values. The duplicate-endpoint full-frame
workaround is disabled during recording and restored afterward; the selected
full-frame cadence is retained (use Every 5 seconds to investigate incrementals).

Replay a recording without connecting to a BMC:

```sh
qatention-capture --replay-video tab-1.jsonl --output last-frame.png
```

The report compares live and replayed image hashes. Matching hashes establish
repeatability, not decoder correctness: the captured stream must also be checked
against the reference decoder and full frames. Reconnect during recording to
provide a known decoder reset; a trace started mid-session may lack earlier state.
