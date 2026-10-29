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

With verification enabled, HTTPS login can recover a missing intermediate chain
using the certificate's AIA `caIssuers` URLs (HTTP or HTTPS). Downloads use a separate
client without BMC cookies or credentials, reject redirects, and are bounded to
64 KiB per response, six downloads, and 15 seconds total. DER certificates and PEM
chains are supported. The completed chain must validate against existing trusted
roots and the hostname; only missing-issuer handshake errors can be recovered.
Validated intermediates are cached in memory for up to 32 leaf certificates and
revalidated on reuse. They are never installed as trusted roots or imported into
Keychain. Debug logging reports retrieval and validation. This does not accept
self-signed console/media certificates or change the unverified setting.

When strict verification encounters an untrusted console certificate, the GUI
shows its endpoint and SHA-256 fingerprint. **Trust and save** stores that exact
fingerprint and endpoint in the selected profile and reconnects. Cancel leaves
the connection blocked. Saved pins are checked even when ordinary TLS validation
succeeds or the broad compatibility checkbox is enabled; changed certificates,
changed endpoints, and plaintext downgrades are rejected. A pin permits the
expected self-signed/hostname trust errors, not expiry or invalid signatures.
Use **Forget trusted console certificate** in the profile editor and Connect to
explicitly clear an old pin before reviewing a replacement. HTTPS and virtual
media certificate policies are unaffected. The command-line client does not
interactively grant trust.

Web login acquires fresh console tokens. Java-interface switching is opt-in.
Automatic reconnect allows 30 attempts: five seconds between the first five,
then thirty seconds. Receiving a console message resets the budget. Authentication
rejection, invalid protocol and TLS validation failures stop retries. Manual-token
connections do not automatically retry. Explicit disconnect unmounts media.

**Logging** enables diagnostics, opens the log panel/file, and clears the log.
Debug logging starts disabled on every launch on desktop and Android. Enabling it
applies only to the current app session.
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

Mac packages use Qt's OpenSSL TLS backend with bundled OpenSSL 3.5.9. The vendor
client identity stays in process memory during authentication; SecureTransport is
excluded so it cannot import the private key into the login Keychain or prompt
for access. Existing Keychain entries from older builds are not modified.

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

### Cached incremental video on ATEN/ASPEED firmware

On the tested ATEN/ASPEED BMC, incremental video requests can repeatedly return
an identical cached packet. Fresh screen content then arrives only after a
full-frame request. A five-second full-refresh interval can therefore produce
five-second visual updates even while input, packet reception, decoding, and
painting continue normally. Switching tabs triggers a full refresh and can make
the screen appear to catch up.

Packet recordings confirmed repeated identical responses between full requests;
all 835 recorded frames reproduced the live image hashes in both independent
and interleaved decoder replay. This matches the behavior documented by ATENtion
and is not specific to macOS rendering. It should not be assumed to affect every
BMC firmware version.

For affected servers choose **View → Full-frame requests → Continuous**, then use
**View → Frame rate limit** to control bandwidth and BMC load. QATENtion also
forces full requests while multiple tabs connect to the same endpoint, restoring
the configured cadence when only one connection remains. This compatibility
behavior requests fresh pictures; it does not repair the firmware's incremental
implementation.

### Opening a connection

Startup opens the profile/connection dialog. Select a saved profile or enter a
new server. The native **+ New tab** button creates another tab and opens the same
dialog. If you cancel, double-click the empty **New session** tab to choose a
profile later. Configured tabs retain their normal selection behavior.

Startup restores saved tabs disconnected and opens the profile selection dialog.
Connections start only after an explicit Connect or Reconnect action.

## Android preview

The Android ARM64 preview shares the desktop C++ protocol/decoder core and uses a
separate Qt Quick Material interface. It supports one connection, live video,
tap-to-click input and a persistent US ANSI keyboard whose main section fits the
screen width. **Fn** opens the navigation keys; **Num** opens the numpad. Tap the
same layer button again to return to the main keyboard. Ctrl/Alt/Shift/Win latch until tapped again; each
key sends immediately. Modifiers clear on disconnect or leaving the foreground.

Video initially fits both the width and height of its viewport, preserving its
aspect ratio. Pinch to zoom (up to 8x) and drag to pan/scroll, or use the +/− buttons.
**Fit** resets zoom and pan. Taps click the remote console. Enable **Drag** when you need to drag the remote
mouse instead of panning the view. Android system-bar and cutout insets are reserved around the interface.
Logs can be viewed and copied from the Logs dialog.

Startup shows the connection form without connecting automatically. The preview
offers multiple saved profiles, selected by hostname and username, with **New**
and **Delete** controls. Connecting creates or updates the selected host/user
profile without affecting other profiles. It also remembers the last host and username. **Save password on this device** defaults on:
passwords are encrypted with AES-GCM using an app-specific Android Keystore key,
scoped to hostname and username. No authentication prompt is required; unchecking
the option and connecting removes that saved password. App backup is disabled.
It supports strict HTTPS verification with missing-intermediate recovery and
explicit console certificate trust, stored per host. It does not yet offer
media mounting or background-session guarantees.
Desktop profiles and UI are unaffected.

The **Android preview** GitHub Actions workflow builds on Ubuntu and publishes
`qatention-android-arm64-preview.apk`, an installable APK for ARM64
Android 9/API 28 or newer. Download it on your phone and allow installation from
that download source. Main-branch builds use one persistent signing identity stored
in repository secrets (`ANDROID_SIGNING_KEY`, PKCS#8 DER base64, and
`ANDROID_SIGNING_CERT`, X.509 PEM base64). CI verifies that identity after signing;
version codes increase with the workflow run number. Keep a private backup of the
signing key: losing it prevents updates to existing installations. Pull-request
builds use disposable debug signing and are not update-compatible with main.

**One final uninstall is required when moving from the early debug-signed previews
to the persistent signature.** This removes old settings and console trust.
Subsequent main builds can update in place, retaining passwords and settings.

For the first device test: connect, confirm video changes, tap/drag on the video,
send text/Enter, and switch away from and back to the app. Use Logs → Copy when
reporting a problem. Actual device behavior is not established by a successful
cross-build; the workflow also runs the mobile UI/TLS smoke test on Linux.

Build locally with Qt 6.8.3 host + Android ARM64 packages, JDK 17, Android SDK 35,
and NDK 26.1.10909125, following `.github/workflows/android.yml`. OpenSSL 3.5.9 is
built from checksum-verified source with `scripts/build_android_openssl.sh` and
bundled under Android-specific library names. To test the interface on desktop:

```sh
cmake -S . -B build-mobile -DQATENTION_MOBILE=ON
cmake --build build-mobile
build-mobile/qatention
```

## Code style and analysis

First-party C++ (`src/` and `tests/`) follows the
[NBS clang-format style](https://github.com/ydb-platform/nbs/blob/main/.clang-format),
with Qt macros and local include groups. Use **clang-format 18.1.8**; vendored
`third_party/` sources and generated files are excluded. Existing Qt-style names
are retained. QML, JavaScript and Java are outside the C++ formatting check.

```sh
python -m pip install -r tools/requirements.txt
python scripts/check_cpp.py --fix  # apply formatting
python scripts/check_cpp.py        # check without modifying files
cmake -S . -B build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
python scripts/check_cpp.py --build build
```

Repeat configure/build/tidy with `-DQATENTION_MOBILE=ON` in a separate build
directory to cover the mobile interface. The Code quality workflow does both.
The NBS-inspired clang-tidy policy checks analyzer findings, bug-prone code,
performance, portability and selected modernization rules; findings fail CI.
CodeQL runs security-extended analysis on C++, Python and Actions on pushes,
pull requests and weekly. Native Windows/macOS/Android-only branches still need
their platform builds and runtime tests; Linux analysis is not full platform
coverage. Dependabot proposes weekly Actions and Python-tool updates. Qt,
OpenSSL and SDK versions remain explicit manual updates in the build workflows
and scripts.

## Releases

**Prepare draft release** is manual-only and is not triggered by pushes or tags.
Once Desktop builds, macOS cross-builds, Android preview and Code quality have
all succeeded for the same `main` push commit, select **Run workflow** on `main`,
provide its full commit SHA and a tag such as `v0.2.0-preview.3`.

The workflow verifies the source runs, downloads the five existing artifacts,
checks their original SHA256 digests and creates a **draft** release with
`SHA256SUMS` and `release-source.json` recording provenance. It never rebuilds or
re-signs the APK, so Android version codes and the persistent signing identity
are preserved. Missing/expired artifacts or failed checks stop the release.
Review and publish the draft explicitly in GitHub Releases after device testing.

This is artifact promotion: release tags do not change embedded application
versions. Update CMake/application version metadata before building when needed;
Android remains named `preview` until it is ready for a stable release. macOS
bundles remain ad-hoc signed, not notarized. Release setup does not itself create
a tag or release.

### Android session controls

Open **Menu** for:

- **View settings:** fit/actual size, smooth scaling, keyboard visibility, frame
  rate limit, full-frame refresh policy, quality and 4:4:4/4:2:2 display preference.
  These preferences are saved on the device.
- **Virtual media:** select an ISO with the system file picker, monitor its status
  and unmount it. Use a local, seekable file; download cloud-hosted images first.
  ISO data is read on demand, not copied into memory. Keep the app open while
  serving media; Android background execution is not guaranteed. Disconnecting
  asks for confirmation while media is attached and unmounts it.
- **Power:** power on/off, reset or ACPI soft off, with confirmation before sending
  the command. Current power state is shown when reported by the BMC.
- **Ctrl+Alt+Delete**, **USB hotplug** and **Debug log**.

Pinch and drag continue to zoom and scroll the console. **Fit** returns from
actual-size mode to fitting the available console area.
