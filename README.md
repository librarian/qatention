# QATENtion

An early C++17 / Qt 6 Quick port of [ATENtion](https://github.com/librarian/ATENtion),
a Supermicro ATEN/Pilot and ASPEED KVM console client. Current build targets:
Windows x64 and Linux x64. macOS builds are deferred; Android is outside this first milestone.

## Preview scope

Implemented: HTTPS web login and console token acquisition, manual token login,
direct mutual TLS, Hermon type-0 full frames and incremental tiles, ASPEED AJPG,
absolute mouse input, basic US keyboard mapping, Ctrl+Alt+Del, fit-to-window display,
fullscreen, PNG screenshots, saved connection profiles, automatic reconnect,
connection timeouts, and input release on focus loss.
The Java-console interface switch is opt-in in the connection dialog.

Not yet ported: virtual media, multiple tabs, OS credential stores, power controls, clipboard typing, non-US/physical keyboard
layouts and lock-key synchronization, and the less common palette/bit-plane ATEN
codecs. Unknown protocol or unsupported video formats close the connection with
an error. This is a first functional port, not feature parity with ATENtion.

No live BMC or interactive Windows/macOS validation is implied by CI. Tests use
synthetic wire transcripts and known decoder blocks. Confirm authentication, video,
keyboard/mouse and focus-loss cleanup against your own BMC before relying on it.

## Build

Install Qt 6.8.3 (Core, Gui, Network, Qml, Quick, Quick Controls 2 and Test),
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

CI uploads plain ZIP files with the Qt runtime and QML imports, with no nested archive. Linux
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
macOS jobs are disabled.
The workflow is also runnable manually from GitHub Actions.

## Connect

Enter the BMC host and web credentials; HTTPS is the default. Debug logs exclude
passwords, tokens, cookies and typed keys.

Use **Save profile** in the connection dialog to store a named host and connection
settings, including the username and password. Saving the same name updates it.
Select a profile to load it, or use Delete to remove it. Manual-mode tokens are
saved too, but can expire and need replacing. The local profile file contains
credentials in plaintext; it is written atomically with owner read/write permissions
(where supported). Profiles live in `profiles.json` under
Qt's application configuration directory (`%LOCALAPPDATA%/qatention/QATENtion`
on Windows, `$XDG_CONFIG_HOME/qatention/QATENtion` or
`~/.config/qatention/QATENtion` on Linux).

Automatic reconnect applies only after a web-login console has connected.
Dropped connections trigger up to six attempts, after 2, 4, 8, 16, 30 and 30
seconds. Each attempt logs in again for fresh console tokens. Receiving a console
message resets the retry budget. Authentication rejection, TLS errors and invalid
protocol data stop retries; Disconnect cancels pending attempts and forgets the
in-memory password; explicitly saved profiles remain on disk. Manual-token
connections require fresh tokens and do not retry.

If the BMC uses a self-signed certificate, the connection dialog has an explicit
option to allow an unverified certificate for that connection. The bundled vendor
client certificate authenticates the client; it does **not** authenticate the BMC.
The HTTPS and console endpoints can present different server certificates.
Validation failures identify the endpoint and the exact TLS errors; debug logging
also records the certificate subject, issuer, and SHA-256 fingerprint.

If the BMC is configured for HTML5-only console access, opt into switching its
console setting to Java. This changes a BMC setting. Otherwise leave it unchecked.
Manual mode takes the separate username/password tokens from a fresh ATEN JNLP
(arguments 1 and 2), not the web password. It supports TLS or plaintext transport.
Redfish-only web authentication is not implemented.

Click the console to focus input. Move focus out of it to release held keys and
buttons. OS-reserved shortcuts may be intercepted by the local desktop; use the
Ctrl+Alt+Del button for that remote sequence. Keyboard mapping currently assumes
a US layout. The default requests incremental updates with a full refresh every
five seconds. The faster option requests full frames continuously, with a 30 Hz
request cap; actual performance depends on the BMC.

## Debug logging and appearance

The **Debug log** button opens a live log panel with an opt-in toggle, Clear button,
and selectable file path. The toggle persists between launches. Logs record connection
states, reconnect attempts, TLS validation failures, and video rectangle metadata.
The file rotates at 10 MiB and keeps one previous file; the panel keeps 64K characters.
The first unsupported video packet is saved beside the log for decoder investigation
and may contain console pixels. Logs are under Qt's application-local data directory,
in `logs/qatention.log`.

Controls use Qt's default platform style and palette: Windows on Windows, Fusion
on Linux, and macOS when that build is enabled. No custom palette is forced.
Qt Quick's Linux Fusion style is a desktop style, not a GTK widget implementation.

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

- `src/core`: bounded, asynchronous Qt Network protocol state machine and decoding.
- `src/ui`, `qml`: Qt Quick desktop frontend and console input/rendering.
- `third_party/aspeed`: the existing MPL-2.0 C decoder, linked into the app.
- `tests`: wire vectors, fragmented mock-BMC handshake, image decoding, bounds,
  cancellation, and input release/control behavior.

The C decoder is already native machine code. A C++ RAII wrapper owns its session
context and serializes its global scratch state. A C++ rewrite would be a separate
codec project requiring golden images, incremental sequences and malformed-input
tests; it is not necessary to eliminate .NET or a separately shipped decoder DLL.

See [THIRD_PARTY.md](THIRD_PARTY.md) for provenance and licenses.
