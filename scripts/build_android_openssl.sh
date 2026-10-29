#!/usr/bin/env bash
set -euo pipefail
prefix=$(realpath -m "$1")
work=$(realpath -m "$2")
export ANDROID_NDK_ROOT="$(realpath "$3")"
export PATH="$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH"
mkdir -p "$work"
cd "$work"
curl --fail --location --retry 3 https://github.com/openssl/openssl/releases/download/openssl-3.5.9/openssl-3.5.9.tar.gz -o openssl.tar.gz
printf '%s  %s\n' 603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a openssl.tar.gz | sha256sum -c -
tar -xzf openssl.tar.gz
cd openssl-3.5.9
# Name the libraries at link time. Post-link patchelf rewriting can break
# Android load-segment mappings and consequently symbol-version strings.
cat > Configurations/99-qatention.conf <<'EOF'
my %targets = (
    "android-qatention-arm64" => {
        inherit_from => [ "android-arm64" ],
        shlib_variant => "_3",
    },
);
EOF
./Configure android-qatention-arm64 shared no-tests no-module -D__ANDROID_API__=28 -Wl,-z,max-page-size=16384
make -j3 build_libs
mkdir -p "$prefix"
cp libcrypto_3.so "$prefix/libcrypto_3.so"
cp libssl_3.so "$prefix/libssl_3.so"
cp LICENSE.txt "$prefix/OpenSSL.txt"
