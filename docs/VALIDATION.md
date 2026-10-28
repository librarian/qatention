# Initial preview verification

Local environment: Debian 13 x86_64, GCC 14.2, Qt 6.8.3, CMake/Ninja.
Qt and missing development libraries were installed in an isolated `/tmp` prefix;
no machine-wide SDK or library changes were required.

Verified locally:

- Application and native decoder compile together without .NET.
- Wire-vector and JNLP parsing tests, including XML entity decoding and invalid input.
- Hermon full-frame RGB555 colors, incremental tiles and edge clipping.
- Actual ASPEED 4:4:4 VQ white/black blocks, frame-end handling and image snapshot isolation.
- Fragmented TCP handshake and frame delivery against an in-process mock BMC.
- Keyboard release, view-only input suppression, hostile payload length rejection,
  and cancellation during connection establishment.
- Profile persistence, updates/deletion, restoration of passwords and manual tokens,
  and refusal to overwrite unreadable/corrupt storage.
- Local HTTPS mock BMC: fresh tokens after a dropped console, increasing retry delay,
  cancellation during backoff, and stopping after web authentication rejection.
- Debug logging opt-in, file rotation and clear; HTTPS certificate rejection includes
  endpoint-specific validation reasons.
- Qt Quick UI loads with the software renderer; the bundled mutual-TLS client
  identity imports successfully with the local TLS backend.
- Installed application passes the same headless smoke test without the Qt SDK
  in its runtime library path (OS libraries are still required).

Windows user validation: the user reports that the Windows build works, and the
provided screenshot shows a live host console with the status "Connected —
controlling". This confirms connection and video rendering on that tested setup.
The screenshot alone does not establish the transport/authentication mode or
complete keyboard/mouse behavior.

Linux and native Windows CI builds, protocol/decoder tests and installed-package
smoke tests passed at commit `6037212`. The package checks remove the Qt SDK's runtime
paths before starting the installed application.

The subsequent MinGW cross-build was verified locally with GCC 14 and Wine 10:
all 11 core test cases passed, as did installed UI/TLS startup with the bundled
MinGW OpenSSL 3.5.9 runtime. The Linux ZIP was checked for executable permissions
and absence of nested archives. The new CI runs both targets on Ubuntu 24.04;
Windows protocol and packaged-startup tests run under Wine.

Not yet verified: sustained video performance, all physical keyboard/mouse paths,
HTTPS web arming and mutual TLS on hardware as separate test cases, or interactive
Linux behavior. Consult the desktop workflow for current build/test status.
The reported Let's Encrypt rejection used a matching DNS hostname. Its exact cause
is not yet reproduced; the new TLS diagnostics distinguish HTTPS from console
certificate errors. Neither endpoint silently bypasses verification.
macOS builds and verification are deferred.
Synthetic decoder tests do not establish correctness for all ASPEED bitstreams.
