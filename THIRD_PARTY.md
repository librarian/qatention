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
Firmware expecting a different vendor identity may require a future override UI.

Qt is dynamically linked. This port uses Qt Core, Gui, Network, Qml, Quick and
Quick Controls 2, plus Qt Test for tests. Qt and its bundled dependencies retain
their own licenses; see https://doc.qt.io/qt-6.8/licensing.html . Development
archives include Qt's LGPL/GPL license texts. Exact Qt source releases are at
https://download.qt.io/archive/qt/6.8/6.8.3/single/ . Do not treat the application's
MIT license as replacing Qt's or ASPEED's license.
