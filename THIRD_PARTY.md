# Sources and licensing

The application is MIT-licensed. Protocol and Hermon decoding logic are derived
from Thomas Jones's ATENtion, with its copyright/license retained in `LICENSE`.

Reference sources inspected for this initial port:

- `librarian/ATENtion`, commit `e7acb90c1c8cab22a2a82ff22a0ef755f91816dd`.
- Existing ATENtion checkout, commit `a99e97f500c8d7ca98dfb4bb836fc1dc3b86483d`,
  including the later per-session ASPEED context/block-map API and protocol fixes.

`third_party/aspeed/{decoder.c,jpegdec.h,jtable.h}` are copied without algorithmic
changes from the latter checkout. They originate in ASPEED Technology's reference
decoder and retain the MPL-2.0 license in `third_party/aspeed/LICENSE`. The complete
vendored source and license are installed alongside the application. Keeping this
library as C does not require shipping a separate DLL/.so/.dylib: it is statically
linked into the application, independently of the dynamically linked Qt libraries.

`resources/client.pfx` is the **public vendor-distributed** generic Supermicro iKVM
client identity already embedded by ATENtion, from X10 AST2400 firmware 04.07.
It is not a user's BMC credential. It does not pin or verify the server certificate.
A client.pfx beside the executable overrides this identity.
`resources/client-cert.pem` and `resources/client-key.pem` contain the same public
vendor identity, extracted from that PFX for TLS backends without a PKCS#12
importer (including Qt SecureTransport). Custom PFX overrides still require a
backend with PKCS#12 import support.

Qt is dynamically linked. This port uses Qt Core, Gui, Network and Widgets, plus Qt Test for tests. Qt and its bundled dependencies retain
their own licenses; see https://doc.qt.io/qt-6.8/licensing.html . Development
archives include Qt's LGPL/GPL license texts. Exact Qt source releases are at
https://download.qt.io/archive/qt/6.8/6.8.3/single/ . Do not treat the application's
MIT license as replacing Qt's or ASPEED's license.

Windows cross-build packages include OpenSSL 3.5.9 (Apache-2.0), built from the
checksum-verified upstream source using MinGW. Its license is included under
`licenses/OpenSSL.txt`. Source: https://github.com/openssl/openssl/tree/openssl-3.5.9 .
MinGW GCC runtime libraries use the GCC Runtime Library Exception; their copyright
and exception text, the MinGW-w64 notices, and GPLv3 are included under `licenses/`.

macOS cross-build tooling uses osxcross commit
27d21e4977c9751d01199c7a226a6faf494c3dd9 and the macOS 14.5 SDK snapshot from
https://github.com/joseluisq/macosx-sdks/releases/tag/14.5 (SHA256 pinned in the
setup script). Apple SDKs retain Apple's license; the SDK is a build input and
is not included in application ZIPs. rcodesign 0.29.0 creates ad-hoc signatures
on Linux; it is also a build tool and is not included in application ZIPs.
