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
- Qt Quick UI loads with the software renderer; the bundled mutual-TLS client
  identity imports successfully with the local TLS backend.
- Installed application passes the same headless smoke test without the Qt SDK
  in its runtime library path (OS libraries are still required).

Not yet verified: a live BMC, sustained video performance, physical keyboard/mouse
behavior, HTTPS web arming against hardware, full mutual-TLS negotiation, or
Windows/macOS binaries in this initial local verification. The desktop build workflow
now checks those builds; consult its run results for current platform status.
Synthetic decoder tests do not establish correctness for all ASPEED bitstreams.
