#!/usr/bin/env bash
set -euo pipefail
destination="$(realpath -m "$1")"
work="$(realpath -m "$2")"
mkdir -p "$work" "$destination"
revision=27d21e4977c9751d01199c7a226a6faf494c3dd9
if [[ ! -d "$work/osxcross/.git" ]]; then
    git clone https://github.com/tpoechtrager/osxcross.git "$work/osxcross"
fi
git -C "$work/osxcross" checkout --detach "$revision"
sdk="$work/osxcross/tarballs/MacOSX14.5.sdk.tar.xz"
curl --fail --location --retry 3 https://github.com/joseluisq/macosx-sdks/releases/download/14.5/MacOSX14.5.sdk.tar.xz -o "$sdk"
printf '%s  %s\n' 6e146275d19f027faa2e8354da5e0267513abf013b8f16ad65a231653a2b1c5d "$sdk" | sha256sum --check
(
    cd "$work/osxcross"
    UNATTENDED=1 BUILD_FLAVOR=llvm ENABLE_REPLACEMENT_LIPO=0 ENABLE_ARCHS="arm64 x86_64" OSX_VERSION_MIN=12.0 TARGET_DIR="$destination" ./build.sh
)
signer="$work/rcodesign.tar.gz"
curl --fail --location --retry 3 https://github.com/indygreg/apple-platform-rs/releases/download/apple-codesign/0.29.0/apple-codesign-0.29.0-x86_64-unknown-linux-musl.tar.gz -o "$signer"
printf '%s  %s\n' dbe85cedd8ee4217b64e9a0e4c2aef92ab8bcaaa41f20bde99781ff02e600002 "$signer" | sha256sum --check
mkdir -p "$work/signer"
tar -xzf "$signer" -C "$work/signer"
signer_binary="$(find "$work/signer" -type f -name rcodesign -print -quit)"
install -m 755 "$signer_binary" "$destination/bin/rcodesign"
