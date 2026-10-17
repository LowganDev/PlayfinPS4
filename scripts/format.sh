#!/usr/bin/env bash
# Formats Playfin's sources (third_party is left untouched). Needs clang-format.
set -euo pipefail
cd "$(dirname "$0")/.."
if ! command -v clang-format >/dev/null; then
    echo "clang-format not found (apt install clang-format)" >&2
    exit 1
fi
find src tests -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i
