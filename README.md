# QATENtion

An early C++17 / Qt 6 Quick port of [ATENtion](https://github.com/librarian/ATENtion),
a Supermicro ATEN/Pilot and ASPEED KVM console client. Desktop targets: Windows x64,
Linux x64, and macOS Intel/Apple Silicon. Android is outside this first milestone.

## Preview scope

Implemented: HTTPS web login and console token acquisition, manual token login,
direct mutual TLS, Hermon type-0 full frames and incremental tiles, ASPEED AJPG,
absolute mouse input, basic US keyboard mapping, Ctrl+Alt+Del, fit-to-window display,
fullscreen, PNG screenshots, connection timeouts, and input release on focus loss.
The Java-console interface switch is opt-in in the connection dialog.

Not yet ported: virtual media, multiple tabs, saved profiles/OS credential stores,
automatic reconnect, power controls, clipboard typing, non-US/physical keyboard
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

CI uploads install-tree archives with the Qt runtime and QML imports. Linux
packages target the CI distribution's ABI, not every Linux release. macOS archives
are architecture-specific and unsigned/unnotarized. Windows packages are unsigned.
No signing credentials are required for these development builds.
Linux still needs system graphics/input libraries (including libxkbcommon, X11/XCB,
OpenGL/EGL, and fontconfig); the archive bundles Qt, not an entire Linux userspace.

### GitHub build matrix

[Desktop builds](https://github.com/librarian/qatention/actions/workflows/build.yml)
runs on pushes and pull requests. It builds and tests Linux x64, Windows x64,
macOS Intel, and macOS Apple Silicon, then uploads their install-tree archives.
The workflow is also runnable manually from GitHub Actions.

## Connect

Enter the BMC host and web credentials; HTTPS is the default. Credentials and
single-use tokens remain in memory and are not saved in settings or logs.
If the BMC uses a self-signed certificate, the connection dialog has an explicit
option to allow an unverified certificate for that connection. The bundled vendor
client certificate authenticates the client; it does **not** authenticate the BMC.

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
