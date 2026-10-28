#!/usr/bin/env bash
set -euo pipefail
# Build with the same MinGW runtime as the app; no MSVC redistributable required.
prefix=$(realpath -m "$1")
work=$(realpath -m "$2")
mkdir -p "$work"
cd "$work"
curl --fail --location --retry 3 https://github.com/openssl/openssl/releases/download/openssl-3.5.9/openssl-3.5.9.tar.gz -o openssl.tar.gz
printf '%s  %s\n' 603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a openssl.tar.gz | sha256sum -c -
tar -xzf openssl.tar.gz
cd openssl-3.5.9
CC=x86_64-w64-mingw32-gcc-posix AR=x86_64-w64-mingw32-ar RC=x86_64-w64-mingw32-windres RANLIB=x86_64-w64-mingw32-ranlib \
    perl Configure mingw64 shared no-tests --prefix="$prefix" --openssldir="$prefix/ssl"
make -j3 build_generated
make -j3 libcrypto-3-x64.dll libssl-3-x64.dll
mkdir -p "$prefix/bin" "$prefix/licenses"
cp libcrypto-3-x64.dll libssl-3-x64.dll "$prefix/bin/"
cp LICENSE.txt "$prefix/licenses/OpenSSL.txt"
