#!/usr/bin/env bash
set -euo pipefail
prefix=$(realpath -m "$1")
work=$(realpath -m "$2")
arch="$3"
case "$arch" in arm64|x86_64) ;; *) exit 2 ;; esac
mkdir -p "$work"
cd "$work"
curl --fail --location --retry 3 https://github.com/openssl/openssl/releases/download/openssl-3.5.9/openssl-3.5.9.tar.gz -o openssl.tar.gz
printf '%s  %s\n' 603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a openssl.tar.gz | sha256sum -c -
tar -xzf openssl.tar.gz
cd openssl-3.5.9
export MACOSX_DEPLOYMENT_TARGET=12.0
CC="$arch-apple-darwin23.5-clang" AR="$arch-apple-darwin23.5-ar" RANLIB="$arch-apple-darwin23.5-ranlib" \
    perl Configure "darwin64-$arch-cc" shared no-tests no-module --prefix="$prefix" --openssldir="$prefix/ssl" -mmacosx-version-min=12.0
make -j3 build_generated
make -j3 libcrypto.3.dylib libssl.3.dylib
mkdir -p "$prefix/lib" "$prefix/licenses"
cp libcrypto.3.dylib libssl.3.dylib "$prefix/lib/"
cp LICENSE.txt "$prefix/licenses/OpenSSL.txt"
for name in libcrypto.3.dylib libssl.3.dylib; do
    "$arch-apple-darwin23.5-install_name_tool" -id "@rpath/$name" "$prefix/lib/$name"
done
"$arch-apple-darwin23.5-install_name_tool" -change "$prefix/lib/libcrypto.3.dylib" '@rpath/libcrypto.3.dylib' "$prefix/lib/libssl.3.dylib"
