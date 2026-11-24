#!/usr/bin/env bash
set -euo pipefail

if (( $# < 3 )); then
    echo "Usage: $0 QT_PREFIX WORK_DIRECTORY LIBRARY_PATH [CMake arguments...]" >&2
    exit 2
fi

qt=$(realpath "$1")
mkdir -p "$2"
work=$(realpath "$2")
library=$3
shift 3
repo=$(cd "$(dirname "$0")/.." && pwd)
version=6.8.3
archive=qtwebsockets-everywhere-src-$version.tar.xz
source="$work/qtwebsockets-everywhere-src-$version"

curl --fail --location --retry 3 --output "$work/$archive" \
    "https://download.qt.io/archive/qt/6.8/$version/submodules/$archive"
printf '%s  %s\n' fc6310c1b12d6e22a31062727aa6574ed69239d036c8a4247b3b089902709b11 "$work/$archive" | sha256sum --check
tar -xJf "$work/$archive" -C "$work"
git -C "$source" apply "$repo/cmake/patches/qtwebsockets/qt-6.8.3-security.patch"

cmake -S "$source" -B "$work/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$work/install" \
    -DCMAKE_PREFIX_PATH="$qt" -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF "$@"
cmake --build "$work/build" --parallel 3
cmake --install "$work/build"

test -f "$qt/$library"
test -f "$work/install/$library"
install -m 755 "$work/install/$library" "$qt/$library"
cmp "$work/install/$library" "$qt/$library"
printf 'Installed patched Qt WebSockets: %s\n' "$library"
